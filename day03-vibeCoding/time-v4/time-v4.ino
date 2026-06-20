#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "time.h"
#include "secrets.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===================== WI-FI & TIME CONFIG =====================
const char* ssid     = SECRET_SSID;     
const char* password = SECRET_PASS; 
char timeString[9] = "00:00:00";

// ===================== PIN CONFIG =====================
#define SDA_PIN 21
#define SCL_PIN 22

#define BTN_MODE   14   // New Dedicated Mode Switch Button
#define BTN_B1     4   // Timer: Start/Pause
#define BTN_B2     18  // Timer: Increase
#define BTN_B3     19  // Timer: Change Field
#define BTN_B4     23  // Timer: Reset / Morse Keyer

// ===================== STATE MACHINES =====================
enum Mode { MODE_WATCH = 0, MODE_AI, MODE_MORSE, MODE_TIMER };
int currentMode = MODE_WATCH;

enum TimerState { TIMER_IDLE, TIMER_RUNNING, TIMER_PAUSED, TIMER_ALARM };
TimerState tState = TIMER_IDLE;

enum TimerField { FIELD_HOURS, FIELD_MINUTES, FIELD_SECONDS };
TimerField selectedField = FIELD_SECONDS;

// ===================== TIMER VARIABLES =====================
long targetSeconds = 0;   // Overall countdown capacity setup
long remainingSeconds = 0;
unsigned long lastTimerUpdate = 0;
bool flashState = false;
unsigned long lastFlashTime = 0;

int editHours = 0;
int editMinutes = 0;
int editSeconds = 0;

// ===================== BUTTON CONFIG =====================
struct Button {
  uint8_t pin;
  bool stableState;      
  bool lastStableState;  
  unsigned long lastDebounceTime;
};
#define DEBOUNCE_DELAY 50

Button btnMode   = {BTN_MODE, HIGH, HIGH, 0};
Button btnB1     = {BTN_B1, HIGH, HIGH, 0};
Button btnB2     = {BTN_B2, HIGH, HIGH, 0};
Button btnB3     = {BTN_B3, HIGH, HIGH, 0};
Button btnB4     = {BTN_B4, HIGH, HIGH, 0};

// ===================== MORSE CONFIG =====================
unsigned long morsePressTime = 0;
bool morseActive = false;
#define DOT_THRESHOLD 250  

// ===================== FUNCTION DECLARATIONS =====================
void updateButton(Button &btn);
bool wasPressed(Button &btn);
void handleInput();
void handleTimerLogic();
void drawWatch();
void drawAI();
void drawMorse();
void drawTimer();

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) while (true);

  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  
  display.clearDisplay();
  display.setCursor(10, 20);
  display.print("Connecting Wi-Fi...");
  display.display();

  WiFi.begin(ssid, password);
  int retryCounter = 0;
  while (WiFi.status() != WL_CONNECTED && retryCounter < 10) {
    delay(500);
    retryCounter++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    configTime(19800, 0, "pool.ntp.org");
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_B1, INPUT_PULLUP);
  pinMode(BTN_B2, INPUT_PULLUP);
  pinMode(BTN_B3, INPUT_PULLUP);
  pinMode(BTN_B4, INPUT_PULLUP);
}

// ===================== MAIN LOOP =====================
void loop() {
  handleInput();
  handleTimerLogic();

  // Handle local time update only when in watch mode
  if (currentMode == MODE_WATCH) {
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  }

  display.clearDisplay();
  switch (currentMode) {
    case MODE_WATCH: drawWatch(); break;
    case MODE_AI:    drawAI(); break;
    case MODE_MORSE: drawMorse(); break;
    case MODE_TIMER: drawTimer(); break;
  }
  display.display();
}

// ===================== INPUT PROCESSING =====================
void handleInput() {
  updateButton(btnMode);
  updateButton(btnB1);
  updateButton(btnB2);
  updateButton(btnB3);
  updateButton(btnB4);

  // Dedicated button 2 handler switches main panel view loop
  if (wasPressed(btnMode)) {
    currentMode = (currentMode + 1) % 4;
    return;
  }

  // Route actions conditionally based on view
  if (currentMode == MODE_TIMER) {
    
    // B1: Start / Pause / Silence Alarm
    if (wasPressed(btnB1)) {
      if (tState == TIMER_ALARM) {
        tState = TIMER_IDLE;
      } else if (tState == TIMER_IDLE && (editHours > 0 || editMinutes > 0 || editSeconds > 0)) {
        remainingSeconds = (editHours * 3600) + (editMinutes * 60) + editSeconds;
        tState = TIMER_RUNNING;
        lastTimerUpdate = millis();
      } else if (tState == TIMER_RUNNING) {
        tState = TIMER_PAUSED;
      } else if (tState == TIMER_PAUSED) {
        tState = TIMER_RUNNING;
        lastTimerUpdate = millis();
      }
    }

    // B2: Increase Value (Only allowed in IDLE configuration mode)
    if (wasPressed(btnB2) && tState == TIMER_IDLE) {
      if (selectedField == FIELD_HOURS)   editHours = (editHours + 1) % 24;
      if (selectedField == FIELD_MINUTES) editMinutes = (editMinutes + 1) % 60;
      if (selectedField == FIELD_SECONDS) editSeconds = (editSeconds + 1) % 60;
    }

    // B3: Change Field Selection
    if (wasPressed(btnB3) && tState == TIMER_IDLE) {
      if (selectedField == FIELD_SECONDS) selectedField = FIELD_HOURS;
      else if (selectedField == FIELD_HOURS) selectedField = FIELD_MINUTES;
      else if (selectedField == FIELD_MINUTES) selectedField = FIELD_SECONDS;
    }

    // B4: Reset
    if (wasPressed(btnB4)) {
      tState = TIMER_IDLE;
      editHours = 0; editMinutes = 0; editSeconds = 0;
      remainingSeconds = 0;
    }
  } 
  
  // Morse handling active only on Screen 2
  else if (currentMode == MODE_MORSE) {
    if (digitalRead(BTN_B4) == LOW && !morseActive) {
      morsePressTime = millis(); morseActive = true;
    }
    if (digitalRead(BTN_B4) == HIGH && morseActive) {
      unsigned long duration = millis() - morsePressTime;
      Serial.println((duration < DOT_THRESHOLD) ? "." : "-");
      morseActive = false;
    }
  }
}

// ===================== TIMER ENGINE CORE =====================
void handleTimerLogic() {
  if (tState == TIMER_RUNNING) {
    if (millis() - lastTimerUpdate >= 1000) {
      if (remainingSeconds > 0) {
        remainingSeconds--;
      }
      if (remainingSeconds == 0) {
        tState = TIMER_ALARM;
        lastFlashTime = millis();
      }
      lastTimerUpdate = millis();
    }
  }
}

// ===================== UI DRAWING MODULES =====================
void drawFrame(const char* title) {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(0, 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(4, 3);
  display.print(title);
}

void drawWatch() { drawFrame("WATCH"); display.setTextSize(2); display.setCursor(16, 25); display.print(timeString); display.setTextSize(1); display.setCursor(34, 50); display.print("IST Sync'd"); }
void drawAI()    { drawFrame("AI ASSIST"); display.setTextSize(1); display.setCursor(10, 25); display.print("Voice Assistant"); display.setCursor(25, 42); display.print("[Ready for I2S]"); }
void drawMorse() { drawFrame("MORSE COMM"); display.setTextSize(1); display.setCursor(10, 20); display.print("Tap  -> Dot (.)"); display.setCursor(10, 32); display.print("Hold -> Dash (-)"); display.setCursor(10, 48); display.print("B4 is connected key"); }

void drawTimer() {
  // CRITICAL STATE: TIMER ALARM FLASHING GRAPHICS
  if (tState == TIMER_ALARM) {
    if (millis() - lastFlashTime >= 300) {
      flashState = !flashState;
      lastFlashTime = millis();
    }
    
    if (flashState) {
      display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    display.setTextSize(2);
    display.setCursor(12, 15);
    display.print("TIME'S UP!");
    display.setTextSize(1);
    display.setCursor(20, 45);
    display.print("Press B1 to Clear");
    display.setTextColor(SSD1306_WHITE); // Reset text default
    return;
  }

  drawFrame("COUNTDOWN TIMER");
  
  int h = (tState == TIMER_IDLE) ? editHours : remainingSeconds / 3600;
  int m = (tState == TIMER_IDLE) ? editMinutes : (remainingSeconds % 3600) / 60;
  int s = (tState == TIMER_IDLE) ? editSeconds : remainingSeconds % 60;

  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);

  display.setTextSize(2);
  display.setCursor(16, 24);
  display.print(buf);

  // Underline current editing field in IDLE state
  if (tState == TIMER_IDLE) {
    display.setTextSize(1);
    display.setCursor(10, 48);
    if (selectedField == FIELD_HOURS)   display.print("Edit: [HOURS]");
    if (selectedField == FIELD_MINUTES) display.print("Edit: [MINUTES]");
    if (selectedField == FIELD_SECONDS) display.print("Edit: [SECONDS]");
  } else if (tState == TIMER_RUNNING) {
    display.setTextSize(1); display.setCursor(24, 48); display.print(">> RUNNING <<");
  } else if (tState == TIMER_PAUSED) {
    display.setTextSize(1); display.setCursor(28, 48); display.print("|| PAUSED ||");
  }
}

// ===================== DEBOUNCE CORE SUBSYSTEM =====================
void updateButton(Button &btn) {
  bool reading = digitalRead(btn.pin);
  if (reading != btn.stableState) {
    if (millis() - btn.lastDebounceTime > DEBOUNCE_DELAY) {
      btn.lastStableState = btn.stableState;
      btn.stableState = reading;
    }
  } else {
    btn.lastDebounceTime = millis();
  }
}

bool wasPressed(Button &btn) {
  if (btn.lastStableState == HIGH && btn.stableState == LOW) {
    btn.lastStableState = LOW; 
    return true;
  }
  return false;
}