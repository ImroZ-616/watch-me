#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool showAnimation = false;
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;   // India = +5:30 → 5.5*3600
const int daylightOffset_sec = 0;
// Button
const int buttonPin = 4;
// bool showAnimation = false;
bool lastButtonState = HIGH;
unsigned long lastUpdate = 0;

void showClock() {
  if (millis() - lastUpdate >= 1000) {

    lastUpdate = millis();

    struct tm timeinfo;

    if(!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(10, 20);

    display.printf("%02d:%02d:%02d",
                   timeinfo.tm_hour,
                   timeinfo.tm_min,
                   timeinfo.tm_sec);

    display.display();
  }
}
int x = 64;
int y = 32;
int h = 2;
int k = 2;
int R = 3;
int r = 1;

void runAnimation() {
  static unsigned long lastFrame = 0;

  if (millis() - lastFrame < 30) return;
  lastFrame = millis();
  
  display.clearDisplay();

  display.fillCircle(x, y, R, WHITE);
  display.display();

  y += k;
  x += h;
  R += r;

  if (x+R > 128 || x-R < 0) {
    h = -h;
    x += h;
  }

  if (y+R > 64 || y-R < 0) {
    k = -k;
    y += k;
  }

  if (R > 10 || R < 2) {
    r = -r;
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);   // 🔥 REQUIRED
  pinMode(buttonPin, INPUT_PULLUP);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display not found");
    for(;;);
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {

  // 🔘 Button toggle logic
  bool currentButtonState = digitalRead(buttonPin);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    showAnimation = !showAnimation;   // toggle mode
    delay(200); // debounce
  }

  lastButtonState = currentButtonState;

  // 🎯 Mode selection
  if (showAnimation) {
    runAnimation();
  } else {
    showClock();
  }
}