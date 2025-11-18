/*
* Monitor de Redes Wi-Fi Seguras em Tempo Real com FreeRTOS
* - ESP32 + Arduino core (WiFi.h) + FreeRTOS (tasks, queue, semaphore)
* - Compatível com Wokwi (board-esp32-devkit-c-v4)
*/
 
#include <Arduino.h>
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
 
// -------------------- Configurações de RTOS --------------------
#define WIFI_MONITOR_TASK_PRIO   3   // maior prioridade
#define ALERT_TASK_PRIO          2   // média
#define LOGGER_TASK_PRIO         1   // menor
 
#define WIFI_EVENT_QUEUE_LEN     10
 
// -------------------- Lista de redes seguras --------------------
#define SAFE_NETWORKS_NUM 5
 
// Coloque aqui as redes "do trabalho" que são permitidas.
// Uma delas pode ser "Wokwi-GUEST" para testar no simulador.
const char *safe_networks[SAFE_NETWORKS_NUM] = {
  "Wokwi-GUEST",     // segura (para teste no Wokwi)
  "REDE_TRABALHO_1",
  "REDE_TRABALHO_2",
  "REDE_GESTOR",
  "REDE_CORPORATIVA"
};
 
// -------------------- Estrutura de eventos --------------------
typedef enum {
  WIFI_EVENT_OK = 0,          // conectado em rede segura
  WIFI_EVENT_UNSAFE,          // conectado em rede NÃO segura
  WIFI_EVENT_DISCONNECTED     // sem rede
} wifi_event_type_t;
 
typedef struct {
  wifi_event_type_t type;
  char ssid[33];              // SSID máx. 32 + '\0'
} wifi_event_msg_t;
 
// -------------------- Objetos RTOS --------------------
QueueHandle_t xWifiEventQueue = NULL;
SemaphoreHandle_t xSafeListMutex = NULL;
 
// -------------------- Funções auxiliares --------------------
 
// Verifica se uma rede está na lista de redes seguras
bool isNetworkSafe(const char *ssid) {
  bool safe = false;
 
  if (ssid == NULL || ssid[0] == '\0') {
    return false;
  }
 
  // Protege a lista com mutex para simular acesso concorrente seguro
  if (xSafeListMutex != NULL &&
      xSemaphoreTake(xSafeListMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
 
    for (int i = 0; i < SAFE_NETWORKS_NUM; i++) {
      if (strcmp(ssid, safe_networks[i]) == 0) {
        safe = true;
        break;
      }
    }
 
    xSemaphoreGive(xSafeListMutex);
  }
 
  return safe;
}
 
// Tenta conectar em uma rede "padrão" (para Wokwi: Wokwi-GUEST)
void connectToDefaultNetwork() {
  Serial.println("[WIFI] Conectando à rede padrão...");
 
  // No Wokwi, a rede padrão é "Wokwi-GUEST" sem senha
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", ""); // ajuste para sua rede se for rodar no hardware real
 
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
 
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Conectado em: ");
    Serial.println(WiFi.SSID());
  } else {
    Serial.println("[WIFI] Falha ao conectar na rede padrão.");
  }
}
 
// -------------------- Tasks --------------------
 
// Tarefa que monitora a rede atual e manda eventos para a fila
void wifiMonitorTask(void *pvParameters) {
  while (1) {
    wifi_event_msg_t msg;
    memset(&msg, 0, sizeof(msg));
 
    if (WiFi.status() == WL_CONNECTED) {
      String ssidStr = WiFi.SSID();
      ssidStr.toCharArray(msg.ssid, sizeof(msg.ssid));
      bool safe = isNetworkSafe(msg.ssid);
      msg.type = safe ? WIFI_EVENT_OK : WIFI_EVENT_UNSAFE;
    } else {
      msg.type = WIFI_EVENT_DISCONNECTED;
      strcpy(msg.ssid, "<SEM_REDE>");
    }
 
    if (xWifiEventQueue != NULL) {
      // Timeout na fila -> robustez (não bloqueia para sempre)
      if (xQueueSend(xWifiEventQueue, &msg, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println("[MONITOR] Fila cheia, evento descartado");
      }
    }
 
    vTaskDelay(pdMS_TO_TICKS(2000)); // verifica a cada 2s
  }
}
 
void alertTask(void *pvParameters) {
  wifi_event_msg_t msg;
 
  // LED interno do ESP32 = GPIO 2
  pinMode(2, OUTPUT);
 
  while (1) {
    if (xQueueReceive(xWifiEventQueue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
 
      switch (msg.type) {
      case WIFI_EVENT_OK:
        Serial.print("[ALERTA] Rede segura conectada: ");
        Serial.println(msg.ssid);
        digitalWrite(2, LOW);  // LED apagado
        break;
 
      case WIFI_EVENT_UNSAFE:
        Serial.print("[ALERTA] *** REDE NAO SEGURA ***: ");
        Serial.println(msg.ssid);
        digitalWrite(2, HIGH); // LED acende indicando perigo
 
        Serial.println("[ALERTA] Desconectando...");
        WiFi.disconnect();
 
        connectToDefaultNetwork();
        break;
 
      case WIFI_EVENT_DISCONNECTED:
        Serial.println("[ALERTA] Sem rede conectada");
        digitalWrite(2, LOW);
        break;
      }
    }
  }
}
 
// Tarefa de log simples (heartbeat do sistema)
void loggerTask(void *pvParameters) {
  while (1) {
    Serial.print("[LOGGER] Status WiFi: ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Conectado em ");
      Serial.println(WiFi.SSID());
    } else {
      Serial.println("Desconectado");
    }
 
    // Aqui você poderia, por exemplo, checar filas, contadores, etc.
    vTaskDelay(pdMS_TO_TICKS(5000)); // a cada 5s
  }
}
 
// -------------------- setup/loop (Arduino) --------------------
 
void setup() {
  Serial.begin(115200);
  delay(1000);
 
  Serial.println();
  Serial.println("==== Monitor de Redes Wi-Fi Seguras com FreeRTOS (ESP32) ====");
 
  // Conecta na rede padrão (no Wokwi: Wokwi-GUEST)
  connectToDefaultNetwork();
 
  // Cria fila e semáforo
  xWifiEventQueue = xQueueCreate(WIFI_EVENT_QUEUE_LEN, sizeof(wifi_event_msg_t));
  xSafeListMutex = xSemaphoreCreateMutex();
 
  if (xWifiEventQueue == NULL || xSafeListMutex == NULL) {
    Serial.println("[ERRO] Falha ao criar fila ou semaforo!");
    while (1) {
      delay(1000);
    }
  }
 
  // Criação das tasks (prioridades diferentes)
  xTaskCreate(
    wifiMonitorTask,
    "wifi_monitor_task",
    4096,
    NULL,
    WIFI_MONITOR_TASK_PRIO,
    NULL
  );
 
  xTaskCreate(
    alertTask,
    "alert_task",
    4096,
    NULL,
    ALERT_TASK_PRIO,
    NULL
  );
 
  xTaskCreate(
    loggerTask,
    "logger_task",
    4096,
    NULL,
    LOGGER_TASK_PRIO,
    NULL
  );
}
 
void loop() {
  // tudo está nas tasks do FreeRTOS
}
