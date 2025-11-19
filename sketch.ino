/*
 * SISTEMA DE MONITORAMENTO DE SEGURANÇA DE REDES (Simulação)
 * Alunos:
   Enzo Moraes Chang - RM 87424
   Matheus Martins de Sena - RM 89129 

 * * Descrição:
 * Implementação de sistema multitarefa para validar SSIDs de redes Wi-Fi
 * utilizando Filas (Queues) e Mutex para proteção de dados.
 *
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <vector>

// --- CONFIGURAÇÕES DE HARDWARE ---
#define PINO_LED_ALERTA  2  // LED Azul (Built-in no ESP32 DevKit V1)

// --- CONFIGURAÇÕES DO SISTEMA ---
#define TAMANHO_FILA     10
#define TEMPO_HEARTBEAT  10000 // 10 segundos conforme PDF (item 5.3)

// --- SIMULAÇÃO DE DADOS (MOCKING) ---
// Lista de redes que o "driver" simulado pode encontrar
const char* redesSimuladas[] = {
  "REDE_SEGURA_1",    // Conhecida
  "WIFI_DA_PRACA",    // Perigosa
  "Corporate_WiFi",   // Conhecida
  "Free_Airport_Wi",  // Perigosa
  "Home_Office_Net"   // Conhecida
};

// --- GLOBAIS E OBJETOS DO FREERTOS ---
QueueHandle_t xFilaSSID;              // Fila para transportar os nomes das redes
SemaphoreHandle_t xMutexListaSegura;  // Mutex para proteger a leitura da lista
std::vector<String> listaRedesConfiaveis; // "Safe List"

// Variável auxiliar para manter o estado do LED entre as piscadas do Heartbeat
volatile bool estadoAlertaAtivo = false; 

// ============================================================
//  TAREFA 1: vTaskInjector (Prioridade ALTA - 2)
//  Objetivo: Simular o driver Wi-Fi e injetar dados na fila
// ============================================================
void vTaskInjector(void *pvParameters) {
  char ssidBuffer[32]; // Buffer temporário

  for (;;) {
    // 1. Simula a chegada de um sinal Wi-Fi aleatório
    int indiceSorteado = random(0, 5);
    strcpy(ssidBuffer, redesSimuladas[indiceSorteado]);

    // 2. Envia para a Fila (sem processar, apenas injeta)
    // portMAX_DELAY faz a tarefa esperar se a fila estiver cheia
    if (xQueueSend(xFilaSSID, &ssidBuffer, portMAX_DELAY) == pdPASS) {
      Serial.print("[INJETOR] SSID enviado para analise: ");
      Serial.println(ssidBuffer);
    }

    // Aguarda um tempo aleatório entre envios para simular tráfego real
    vTaskDelay(pdMS_TO_TICKS(random(2000, 5000)));
  }
}

// ============================================================
//  TAREFA 2: vTaskVerifyNetwork (Prioridade MÉDIA - 1)
//  Objetivo: Consumir fila, verificar segurança e acionar LED
// ============================================================
void vTaskVerifyNetwork(void *pvParameters) {
  char ssidRecebido[32];
  bool redeEhSegura = false;

  for (;;) {
    // 1. Aguarda chegar algo na fila
    if (xQueueReceive(xFilaSSID, &ssidRecebido, portMAX_DELAY) == pdTRUE) {
      
      redeEhSegura = false; // Reset do status
      String strSSID = String(ssidRecebido);

      // 2. Solicita o Mutex antes de ler a memória compartilhada (Lista)
      if (xSemaphoreTake(xMutexListaSegura, portMAX_DELAY) == pdTRUE) {
        
        // Busca na lista de redes confiáveis
        for (const auto& rede : listaRedesConfiaveis) {
          if (rede == strSSID) {
            redeEhSegura = true;
            break;
          }
        }
        
        // Libera o Mutex imediatamente após a leitura
        xSemaphoreGive(xMutexListaSegura);
      }

      // 3. Lógica de Alerta Visual (Item 5.1 e 5.2 do PDF)
      if (redeEhSegura) {
        Serial.println("[VERIFY] Rede CONFIAVEL detectada. LED OFF.");
        estadoAlertaAtivo = false;
        digitalWrite(PINO_LED_ALERTA, LOW);
      } else {
        Serial.print("[VERIFY] ALERTA! Rede DESCONHECIDA: ");
        Serial.println(strSSID);
        estadoAlertaAtivo = true;
        digitalWrite(PINO_LED_ALERTA, HIGH);
      }
    }
  }
}

// ============================================================
//  TAREFA 3: vTaskHeartbeat (Prioridade BAIXA - 0)
//  Objetivo: Monitor de saúde, pisca LED periodicamente
// ============================================================
void vTaskHeartbeat(void *pvParameters) {
  for (;;) {
    // Aguarda 10 segundos (conforme item 5.3)
    vTaskDelay(pdMS_TO_TICKS(TEMPO_HEARTBEAT));

    Serial.println("[HEARTBEAT] Verificacao de sistema ativo...");

    // Pisca rapidamente 3 vezes
    for (int i = 0; i < 3; i++) {
      digitalWrite(PINO_LED_ALERTA, !digitalRead(PINO_LED_ALERTA)); // Inverte estado
      vTaskDelay(pdMS_TO_TICKS(100)); 
    }

    // Restaura o estado correto do LED de acordo com o alerta atual
    // Isso garante que o Heartbeat não "apague" um alerta real acidentalmente
    digitalWrite(PINO_LED_ALERTA, estadoAlertaAtivo ? HIGH : LOW);
  }
}

// ============================================================
//  SETUP (Inicialização)
// ============================================================
void setup() {
  Serial.begin(115200);
  
  // Configuração do GPIO
  pinMode(PINO_LED_ALERTA, OUTPUT);
  digitalWrite(PINO_LED_ALERTA, LOW);

  // Criação dos objetos do Kernel
  xFilaSSID = xQueueCreate(TAMANHO_FILA, sizeof(char[32]));
  xMutexListaSegura = xSemaphoreCreateMutex();

  // Populando a "Safe List" (Simulando carregamento de memória flash)
  listaRedesConfiaveis.push_back("REDE_SEGURA_1");
  listaRedesConfiaveis.push_back("Corporate_WiFi");
  listaRedesConfiaveis.push_back("Home_Office_Net");

  Serial.println("--- Sistema Iniciado ---");

  // Criação das Tarefas com as prioridades especificadas no PDF
  // vTaskInjector: Prioridade 2 (Alta)
  xTaskCreate(vTaskInjector, "Injector", 2048, NULL, 2, NULL);

  // vTaskVerifyNetwork: Prioridade 1 (Média)
  xTaskCreate(vTaskVerifyNetwork, "Verify", 2048, NULL, 1, NULL);

  // vTaskHeartbeat: Prioridade 0 (Baixa)
  xTaskCreate(vTaskHeartbeat, "Heartbeat", 2048, NULL, 0, NULL);
}

void loop() {
  // No FreeRTOS com Arduino, o loop() é mantido vazio ou usado para tarefas de fundo.
  // Tudo acontece nas Tasks acima.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
