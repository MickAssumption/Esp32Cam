/*********
  Autor do Código: Rui Santos
  Mais informações em: https://randomnerdtutorials.com/
  Modificado por: Klinsman Jorge
  Mais informações em: https://youtube.com/tdcprojetos
  Adição de botão, led e traduções de comentários por: Ébano Assumpção

  IMPORTANT!!!
   - seleciona em ferramentas a placa "ESP32 Wrover Module"
   - Ainda em ferramentas, selecione a Partion Scheme "Huge APP (3MB No OTA)
   - GPIO 0 precisa ser conectado ao GND antes.
   - Depois de conectar o GPIO 0 no GND, pressione o botão reset da placa pra carregar o programa

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/
/*
 * Instruções de ligação do botão, buzzer e led.
 * Um terminal do botão deve ser ligado a um resistor de 10K que leva ao GND e o outro lado a pino 02 da placa
 * O terminal maior do led (anodo) deve ser ligado a ao pino 14 e o catodo (terminal menor) ao GND
 * O buzzer deve ser ligado ao pino 14 e o GND.
 * 
 */
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //biblioteca pra desabilitar o detector de brownout
#include "soc/rtc_cntl_reg.h"  //biblioteca também usada pra desabilitar o detector de brownout
//#include "dl_lib.h"
#include "esp_http_server.h"

#define ACCESS_POINT 0 // ACCESS_POINT 1 o ESP cria uma rede(menor delay), ACCESS_POINT 0, O ESP conecta-se a uma rede(maior delay)

//Mude as váriaveis abaixo conforme sua rede
const char* ssid = "VIVOFIBRA-0414";
const char* password = "7223360414";

IPAddress staticIP(192, 168, 15, 117);
IPAddress gateway(192, 168, 15, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 15, 1);

#define PART_BOUNDARY "123456789000000000000987654321"


#define CAMERA_MODEL_AI_THINKER


// INICIO DAS DEFINIÇÕES DO BOTÃO E LED

#define buttonPin 2
#define led1 14
#define buzzer 15

long lastDebounceTime = 0;  // O primeiro Bounce
long debounceDelay = 50;    //O tempo de debounce
int buttonState; //o valor atual do pino
int lastButtonState = LOW; //o primeiro valor do pino

// FIM DAS DEFINIÇÕES DO BOTÃO E LED

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
#error "Seleciona o modelo da camera, abestado"
#endif

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("captura da camera deu ruim");
      res = ESP_FAIL;
    } else {
      if (fb->width > 400) {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG deu ruim");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  return res;
}

void startCameraServer() {
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

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //desabilita o detector de brownout

  pinMode(buttonPin, INPUT);
  pinMode(led1, OUTPUT);
  pinMode(buzzer, OUTPUT);

  WiFi.config(staticIP, dns, gateway, subnet);
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  pinMode(4, OUTPUT);

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

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Xii, deu ruim 0x%x", err);
    return;
  }

  if (ACCESS_POINT) {
    WiFi.softAP(ssid, password);
  }
  else
  {
    WiFi.begin(ssid, password);
  }
  int led = LOW; 
 while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      digitalWrite(led1, led);
      led = !led;
    }

    Serial.println("");
    Serial.println("WiFi connected");
    digitalWrite(led1, HIGH);



  //Inicia o servidor
  startCameraServer();
  Serial.print("Camera acessível em:  'http://");
  Serial.print(ACCESS_POINT ? WiFi.softAPIP() : WiFi.localIP());
  Serial.println("' Para visualização");
  digitalWrite(4, 1);
  delay(500);
  digitalWrite(4, 0);

}

void loop() {
while (WiFi.status() == WL_DISCONNECTED) {
    ESP.restart();
    digitalWrite(led1, LOW);
    Serial.print("Connection Lost");
}
int reading = digitalRead(buttonPin);
if (reading != lastButtonState) {
lastDebounceTime = millis();
  }//o botão recebe o tempo atual em millisegndos.

 if ((millis() - lastDebounceTime) > debounceDelay)
  {
    // Quando o botão é apertado
    if (reading != buttonState)
    {
      Serial.print("Butao tá como? ");
      Serial.println(HIGH == reading ? "Desligado" : "Ligadão");
      buttonState = reading;
// Dentro desse if, vc coloca o o que vc quer que aconteça quando apertado o botão
      if (buttonState == LOW) {
        Serial.print("Arroooxaa!! ");
        digitalWrite(buzzer, HIGH); //a ação feita do debounce
        delay(3000);
          digitalWrite(buzzer, LOW);
      }
    }
  }
  // save the reading.  Next time through the loop,
  lastButtonState = reading;
}
