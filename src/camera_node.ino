#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <PubSubClient.h>
#include <base64.h> 
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================= MACROS DE DEPURACIÓN =================
#define DEBUG_STRING "["+String(pcTaskGetName(NULL))+" - "+String(__FUNCTION__)+"():"+String(__LINE__)+"]   "

// ================= CONFIGURACIÓN TEMPORIZADOR =================
// Tiempo para foto automática: 1 Hora = 3600000 ms
// (Para pruebas puedes bajarlo a 60000 ms)
#define AUTO_PHOTO_INTERVAL_MS  3600000 

// ================= PINES (AI THINKER) =================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_PIN           4

// ================= CREDENCIALES (Igual que placa sensores) =================
const String ssid = "DIGIFIBRA-TySA";
const String password = "T7zKKUR4b5kS";

const String mqtt_server = "iot.ac.uma.es";
const int    mqtt_port   = 1883;
const String mqtt_user   = "II11";
const String mqtt_pass   = "zSWgWId8";

// Topic donde escucharemos la orden (Grupo II11)
const String mqtt_topic_sub = "II11/camera/orden";

// URL para SUBIR la foto (HTTPS)
const char* POST_URL = "https://nr11.iot-uma.es/camera";

// ================= OBJETOS GLOBALES =================
WiFiClient wClient;              // Cliente para MQTT
PubSubClient mqtt_client(wClient); 
NetworkClientSecure https_client; // Cliente para subir fotos

String ID_PLACA; // Se generará automáticamente

// ================= HANDLES FREERTOS =================
SemaphoreHandle_t semPhotoTrigger = NULL; // Semáforo para disparar foto
TimerHandle_t xAutoTimer = NULL;          // Temporizador automático

// ================= PROTOTIPOS =================
void taskMQTTService(void *pvParameters);
void taskPhotoConsumer(void *pvParameters);
void autoTimerCallback(TimerHandle_t xTimer);
void procesa_mensaje(char* topic, byte* payload, unsigned int length);
void conecta_wifi();
void conecta_mqtt();

// -------------------------------------------------------------------------
// SETUP
// -------------------------------------------------------------------------
void setup() {
  // 1. Desactivar Brownout (Vital para ESP32-CAM)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.println();
  
  // 2. CONFIGURACIÓN CÁMARA
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = 15; 
  config.fb_count     = 1;

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Error cámara. Reiniciando...");
    ESP.restart();
  }
  
  // Ajustes de sensor
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0); 
  }

  // 3. INICIALIZAR FREERTOS
  semPhotoTrigger = xSemaphoreCreateBinary();

  // Tarea 1: Servicio MQTT (Mantiene la conexión y escucha)
  xTaskCreate(taskMQTTService, "MQTT Service", 4096, NULL, 2, NULL);

  // Tarea 2: Consumidor de Fotos (Hace la foto y la sube)
  // Necesita mucha pila (stack) para HTTPS y Base64
  xTaskCreate(taskPhotoConsumer, "Photo Worker", 10240, NULL, 1, NULL);

  // Timer: Disparo automático cada hora
  xAutoTimer = xTimerCreate("TimerHora", pdMS_TO_TICKS(AUTO_PHOTO_INTERVAL_MS), pdTRUE, (void *)0, autoTimerCallback);
  if (xAutoTimer != NULL) xTimerStart(xAutoTimer, 0);

  // Configurar cliente HTTPS inseguro para la subida
  https_client.setInsecure();

  Serial.println(DEBUG_STRING+"Setup completado. Tareas iniciadas.");
  vTaskDelete(NULL); // Eliminar tarea setup
}

void loop() {
}

// -------------------------------------------------------------------------
// CALLBACK MQTT: PRODUCTOR 1 (Manual)
// -------------------------------------------------------------------------
void procesa_mensaje(char* topic, byte* payload, unsigned int length) { 
  String mensaje = "";
  for(int i=0; i<length; i++) mensaje += (char)payload[i];
  
  Serial.println(DEBUG_STRING+"Mensaje recibido ["+ String(topic) +"]: " + mensaje);

  // Si recibimos mensaje en el topic de orden, liberamos semáforo
  if (String(topic) == mqtt_topic_sub) {
    Serial.println(DEBUG_STRING+"Orden de foto recibida. Activando semáforo.");
    xSemaphoreGive(semPhotoTrigger);
  }
}

// -------------------------------------------------------------------------
// CALLBACK TIMER: PRODUCTOR 2 (Automático)
// -------------------------------------------------------------------------
void autoTimerCallback(TimerHandle_t xTimer) {
  Serial.println(DEBUG_STRING+"Hora cumplida. Activando semáforo automático.");
  xSemaphoreGive(semPhotoTrigger);
}

// -------------------------------------------------------------------------
// TAREA 1: SERVICIO MQTT (Conexión y Loop)
// -------------------------------------------------------------------------
void taskMQTTService(void *pvParameters) {
  conecta_wifi();
  ID_PLACA = "ESP32-CAM-" + String(WiFi.getHostname());
  
  mqtt_client.setServer(mqtt_server.c_str(), mqtt_port);
  mqtt_client.setBufferSize(512); // Buffer para mensajes
  mqtt_client.setCallback(procesa_mensaje);
  
  conecta_mqtt();

  while(true) {
    if (!mqtt_client.connected()) conecta_mqtt();
    mqtt_client.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// -------------------------------------------------------------------------
// TAREA 2: CONSUMIDOR (Hacer y Subir Foto)
// -------------------------------------------------------------------------
void taskPhotoConsumer(void *pvParameters) {
  while(true) {
    // Esperamos bloqueados hasta que alguien active el semáforo
    if (xSemaphoreTake(semPhotoTrigger, portMAX_DELAY) == pdTRUE) {
       
      Serial.println(DEBUG_STRING+"Iniciando captura...");
      
      // Flash ON
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(150 / portTICK_PERIOD_MS);
      
      camera_fb_t * fb = esp_camera_fb_get();
      
      vTaskDelay(150 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW); // Flash OFF

      if (!fb) {
        Serial.println(DEBUG_STRING+"Fallo captura de cámara");
      } else {
        Serial.println(DEBUG_STRING+"Foto OK: " + String(fb->len) + " bytes. Subiendo...");
        
        // Convertir a Base64 y JSON
        String imageFile = base64::encode(fb->buf, fb->len);
        String jsonPayload = "{\"image\":\"" + imageFile + "\"}";

        HTTPClient http;
        http.setTimeout(20000); 

        // Usamos el cliente HTTPS seguro configurado globalmente
        if (http.begin(https_client, POST_URL)) {
          http.addHeader("Content-Type", "application/json");
          
          int httpCode = http.POST(jsonPayload);

          if (httpCode > 0) {
             Serial.println(DEBUG_STRING+"Subida ÉXITO. HTTP: " + String(httpCode));
          } else {
             Serial.println(DEBUG_STRING+"Error HTTP: " + http.errorToString(httpCode));
          }
          http.end();
        } else {
          Serial.println(DEBUG_STRING+"Error conexión con servidor HTTPS");
        }
        
        esp_camera_fb_return(fb); // Liberar memoria IMPORTANTE
      }
    }
  }
}

// -------------------------------------------------------------------------
// FUNCIONES AUXILIARES
// -------------------------------------------------------------------------
void conecta_wifi() {
  Serial.println(DEBUG_STRING+"Conectando WiFi: " + ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.println("\n"+DEBUG_STRING+"WiFi OK. IP: " + WiFi.localIP().toString());
}

void conecta_mqtt() {
  while (!mqtt_client.connected()) {
    Serial.println(DEBUG_STRING+"Intentando conexión MQTT a " + mqtt_server + "...");
    
    // Conexión con Usuario y Contraseña
    if (mqtt_client.connect(ID_PLACA.c_str(), mqtt_user.c_str(), mqtt_pass.c_str())) {
      Serial.println(DEBUG_STRING+"Conectado al Broker!");
      // Suscribirse al canal de órdenes
      mqtt_client.subscribe(mqtt_topic_sub.c_str());
      Serial.println(DEBUG_STRING+"Suscrito a: " + mqtt_topic_sub);
    } else {
      Serial.println(DEBUG_STRING+"Fallo MQTT: "+ String(mqtt_client.state()) +" reintento en 5s");
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}