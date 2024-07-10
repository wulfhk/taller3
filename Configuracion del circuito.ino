#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_task_wdt.h"
#include <UrlEncode.h>
#include <HTTPClient.h>

TaskHandle_t videoStreamTaskHandle;

//Replace with your network credentials
const char* ssid = "Gean";
const char* password = "vasquez1";
const char* serverName = "http://34.148.243.48/video_feed";
const char* aggressionCheckUrl = "http://34.148.243.48/check_aggression";

#define PART_BOUNDARY "123456789000000000000987654321"


// This project was tested with the AI Thinker Model, M5STACK PSRAM Model and M5STACK WITHOUT PSRAM
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM

// Not tested with this model
//#define CAMERA_MODEL_WROVER_KIT

#if defined(CAMERA_MODEL_AI_THINKER)
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
#else
  #error "Camera model not selected"
#endif

int pulse=14;

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

String celu = "51965644988";
String api = "8648037";

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->width > 400){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  return res;
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  //Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  }
}

void sendMessage(String message){

  // Data to send with HTTP POST
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + celu + "&apikey=" + api + "&text=" + urlEncode(message);    
  HTTPClient http;
  http.begin(url);

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  // Send HTTP POST request
  int httpResponseCode = http.POST(url);
  if (httpResponseCode == 200){
    Serial.print("Message sent successfully");
  }
  else{
    Serial.println("Error sending the message");
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  }

  // Free resources
  http.end();
}

char global_response[100];
int global_response_len = 0;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            Serial.println("HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            Serial.println("HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            Serial.println("HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            Serial.println("HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                Serial.printf("HTTP_EVENT_ON_DATA, len=%d\n", evt->data_len);
                if (evt->data_len) {
                    // Capturar la respuesta en global_response
                    if (global_response_len + evt->data_len < sizeof(global_response) - 1) {
                        memcpy(global_response + global_response_len, evt->data, evt->data_len);
                        global_response_len += evt->data_len;
                        global_response[global_response_len] = 0; // Null-terminate
                    }
                    printf("%.*s\n", evt->data_len, (char*)evt->data);
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            Serial.println("HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            Serial.println("HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
 
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 

  sensor_t * s = esp_camera_sensor_get();
  if (s) {
  s->set_framesize(s, FRAMESIZE_VGA);
  s->set_quality(s, 8);  // Lower quality for faster transmission
  s->set_brightness(s, 1);  // Increase brightness slightly
  s->set_saturation(s, -1);  // Decrease saturation slightly
  }
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  xTaskCreatePinnedToCore(
  videoStreamTask,
  "VideoStreamTask",
  10000,
  NULL,
  1,
  &videoStreamTaskHandle,
  0
  );
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  pinMode(pulse, OUTPUT);
  digitalWrite(pulse,0);

  Serial.println("Camera initialized successfully");
///////////////////////////////
  // pinMode(FLASH_LED_PIN, OUTPUT);
  //digitalWrite(FLASH_LED_PIN, LOW); // Asegúrate de que el LED esté apagado al inicio
  //Serial.println("LED pin initialized");

  // Test de LED
  //Serial.println("Probando LED");
  //digitalWrite(FLASH_LED_PIN, HIGH);
  //delay(1000);  // LED encendido por 1 segundo
  //digitalWrite(FLASH_LED_PIN, LOW);
  //Serial.println("LED test completado");
/////////////////////////////////////////7
  // Wi-Fi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(WiFi.localIP());

  // Start streaming web server
  startCameraServer();
}

// ... (previous includes and setup remain the same)


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Capturing image...");
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      delay(1000);
      return;
    }
    Serial.printf("Captured image. Size: %zu bytes\n", fb -> len);
    esp_http_client_config_t config = {};
    config.url = aggressionCheckUrl;
    config.event_handler = _http_event_handler;
    esp_http_client_handle_t client = esp_http_client_init( & config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
    int status_code = esp_http_client_get_status_code(client);
    Serial.printf("HTTP Status Code: %d\n", status_code);
    if (status_code == 200) {
        if (global_response_len > 0) {
            Serial.printf("Response: %s\n", global_response);
            if (strstr(global_response, "true") != NULL) {
                Serial.println("AGRESION detectada");
                Serial.println("AGRESION detectada");
                sendMessage("AGRESION DETECTADA. ENTRA A: http://34.148.243.48 ");
                digitalWrite(pulse,1);  //Set the pulse pin high on threshold count
                delay(2000);
                digitalWrite(pulse,0);
            } else {
                Serial.println("No se detectó agresión");
                //digitalWrite(FLASH_LED_PIN, LOW);
                Serial.println("LED apagado");
             }
         } else {
            Serial.println("No se recibió contenido en la respuesta");
         }
     } else {
        Serial.println("Código de estado HTTP inesperado");
     }
   } else {
    Serial.printf("HTTP GET request failed: %s\n", esp_err_to_name(err));
   }

   global_response_len = 0;
   global_response[0] = 0;
    esp_http_client_cleanup(client);
    esp_camera_fb_return(fb);
  }
  delay(100);
}


void videoStreamTask(void *pvParameters) {
  for(;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
      WiFi.reconnect();
      while (WiFi.status() != WL_CONNECTED) {
        //delay(0);
        Serial.print(".");
      }
      Serial.println("WiFi reconnected");
    }

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      continue;
    }

    esp_http_client_config_t config = {};
    config.url = serverName;
    config.event_handler = _http_event_handler;
    config.timeout_ms = 500;  // Reduced timeout

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");

    esp_http_client_open(client, fb->len);
    esp_http_client_write(client, (const char *)fb->buf, fb->len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
      Serial.printf("HTTP POST Status = %d, content_length = %lld\n",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
    } else {
      Serial.printf("HTTP POST request failed: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    esp_camera_fb_return(fb);

    //vTaskDelay(100 / portTICK_PERIOD_MS);  // Short delay between frames
  }
}