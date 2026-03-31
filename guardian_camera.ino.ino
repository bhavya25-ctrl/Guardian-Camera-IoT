#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"

// --- Configuration ---
const char* ssid     = "HOTSPOT NAME";
const char* password = "HOTSPOT PASSWORD";
String token = "TELEGRAM TOKEN";
String chat_id = "TELEGRAM ID";

int gpioPIR = 13;
int ledPin = 4;

// CAMERA_MODEL_AI_THINKER Pins
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

String alerts2Telegram(String token, String chat_id);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  pinMode(gpioPIR, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);

  // --- WiFi Connection with Retry ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  int maxAttempts = 30; // 30 x 500ms = 15 seconds
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi Connection Failed.");
    for (int i = 0; i < 2; i++) {
      digitalWrite(ledPin, HIGH); delay(200);
      digitalWrite(ledPin, LOW); delay(200);
    }
    Serial.println("Retrying in 10 seconds...");
    delay(10000);
    setup(); // Retry setup without restarting
    return;
  }

  Serial.println("\nConnected. IP:");
  Serial.println(WiFi.localIP());
  for (int i = 0; i < 5; i++) {
    digitalWrite(ledPin, HIGH); delay(200);
    digitalWrite(ledPin, LOW); delay(200);
  }

  // --- Camera Initialization ---
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
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }
}

void loop() {
  if (digitalRead(gpioPIR) == HIGH) {
    Serial.println("Motion detected!");
    alerts2Telegram(token, chat_id);
    delay(10000); // Anti-spam delay
  }
  delay(500);
}

String alerts2Telegram(String token, String chat_id) {
  const char* host = "api.telegram.org";
  String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
  String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + chat_id +
                "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }

  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect(host, 443)) {
    Serial.println("Connection failed");
    esp_camera_fb_return(fb);
    return "Connection failed";
  }

  client.println("POST /bot" + token + "/sendPhoto HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(head.length() + fb->len + tail.length()));
  client.println();
  client.print(head);

  size_t chunkSize = 1024;
  for (size_t i = 0; i < fb->len; i += chunkSize) {
    size_t len = min(chunkSize, fb->len - i);
    client.write(fb->buf + i, len);
  }

  client.print(tail);
  esp_camera_fb_return(fb);

  String response = "";
  while (client.connected()) {
    while (client.available()) {
      response += client.readStringUntil('\n');
    }
  }
  client.stop();
  Serial.println("Telegram response:");
  Serial.println(response);
  return response;
}
