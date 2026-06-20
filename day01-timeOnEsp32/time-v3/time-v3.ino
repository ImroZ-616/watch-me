#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "secrets.h"

// --- Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;   // IST = +5:30
const int daylightOffset_sec = 0;

enum Mode {
  MODE_CLOCK,
  MODE_ZEN,
  MODE_SPIDER
};

Mode currentMode = MODE_CLOCK;

// Button Setup
const int buttonPin = 4;
bool showAnimation = false;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50;

// Display Object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Helper: Get Text Width for Centering ---
void getTextCenter(const char* str, int16_t* x, int16_t* y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  *x = (SCREEN_WIDTH - w) / 2;
  *y = *y; // Y is passed by value, we keep the input Y
}

// --- Mode 1: Beautiful Clock ---
void showClock() {
  static unsigned long lastUpdate = 0;
  
  // Update every second
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    struct tm timeinfo;

    if(!getLocalTime(&timeinfo)) {
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Time Sync Error");
      display.display();
      return;
    }

    display.clearDisplay();

    // 1. Draw Fancy Border (Rounded Rect)
    display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4, WHITE);

    // 2. Draw Time (Centered)
    display.setTextSize(2);
    display.setTextColor(WHITE);
    
    char timeStr[16];
    sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    int16_t tx, ty;
    getTextCenter(timeStr, &tx, &ty);
    display.setCursor(tx, 20); // Position slightly higher up
    display.print(timeStr);

    // 3. Draw Date (Fixed Month bug + Right Aligned)
    // Note: tm_mon is 0-11, so we add 1
    char dateStr[20];
    sprintf(dateStr, "%02d|%02d|%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    
    display.setTextSize(1);
    getTextCenter(dateStr, &tx, &ty);
    display.setCursor(tx, 45); 
    display.print(dateStr);

    // 4. Draw Seconds Progress Bar (Bottom)
    int barWidth = map(timeinfo.tm_sec, 0, 59, 4, SCREEN_WIDTH - 4);
    display.fillRect(4, 58, barWidth, 2, WHITE);

    display.display();
  }
}

// --- Mode 2: Particle Animation ---
struct Particle {
  int x, y;
  int vx, vy;
  int size;
};

Particle particles[5]; // 5 bouncing balls

void initAnimation() {
  for(int i=0; i<5; i++) {
    particles[i].x = random(10, SCREEN_WIDTH-10);
    particles[i].y = random(10, SCREEN_HEIGHT-10);
    particles[i].vx = random(-2, 3);
    particles[i].vy = random(-2, 3);
    particles[i].size = random(2, 5);
  }
}

void runAnimation() {
  static unsigned long lastFrame = 0;
  // Run at ~40 FPS
  if (millis() - lastFrame < 25) return;
  lastFrame = millis();
  
  display.clearDisplay();
  
  // Draw all particles
  for(int i=0; i<5; i++) {
    // Draw
    display.fillCircle(particles[i].x, particles[i].y, particles[i].size, WHITE);
    
    // Update Physics
    particles[i].x += particles[i].vx;
    particles[i].y += particles[i].vy;

    // Bounce X
    if (particles[i].x + particles[i].size > SCREEN_WIDTH || particles[i].x - particles[i].size < 0) {
      particles[i].vx = -particles[i].vx;
    }

    // Bounce Y
    if (particles[i].y + particles[i].size > SCREEN_HEIGHT || particles[i].y - particles[i].size < 0) {
      particles[i].vy = -particles[i].vy;
    }
  }
  
  // Add some text for context
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("ZEN MODE");

  display.display();
}
// ==========================================
// MODE 3: 3D SPIDER ANIMATION (Inverted Body with Drop-In Entrance)
// ==========================================
float spiderY;
bool entranceDone;
float angle;
void initSpiderAnimation() {
  // reset all static-like behaviour
  entranceDone = false;
  spiderY = -20.0;
  angle = 0;
}
void runSpiderAnimation() {
  // State variables
  // static float angle = 0;
  static unsigned long lastFrame = 0;
  
  // Entrance state variables
  // static float spiderY = -20.0; // Start off-screen (above the top)
  const float targetY = 27.0;    // Final target position for the abdomen center
  // static bool entranceDone = false;

  // Frame rate control (~30 FPS)
  if (millis() - lastFrame < 30) return;
  lastFrame = millis();
  
  display.clearDisplay();

  // 1. Update Position / Drop-down Logic
  if (!entranceDone) {
    // Smoothly interpolate (ease-in-out style) toward the target position
    spiderY += (targetY - spiderY) * 0.15;
    
    // Stop dropping once we are close enough to the target
    if (targetY - spiderY < 0.1) {
      spiderY = targetY;
      entranceDone = true;
    }
  }

  // Calculate dynamic offsets based on current spiderY
  int abdomenY = (int)spiderY;
  int headY = abdomenY + 10;      // Head is 10 pixels below abdomen
  int thoraxY = abdomenY + 4;     // Thorax sits between them

  // 2. Draw Thread (Attaches to the Abdomen from the top of the screen)
  if (abdomenY > 0) {
    display.drawLine(64, 0, 64, abdomenY - 4, WHITE);
  }

  // 3. Draw Inverted Vertical Body
  // Abdomen (Top, LARGER)
  display.fillCircle(64, abdomenY, 7, WHITE);
  // Head (Bottom, smaller)
  display.fillCircle(64, headY, 3, WHITE); 
  // Connector (Thorax)
  display.fillRect(63, thoraxY, 2, 4, WHITE);

  // 4. Draw 8 Legs
  float baseAngles[8] = {
    0.0,   0.78,  1.57,  2.35,   
    3.14,  3.92,  4.71,  5.50    
  };

  // Vertical attachment points shift dynamically with spiderY
  int attachY[8] = {
    thoraxY,     thoraxY + 2, thoraxY + 4, thoraxY + 6, 
    thoraxY + 6, thoraxY + 4, thoraxY + 2, thoraxY
  };

  for(int i=0; i<8; i++) {
    // Only spin if the entrance animation is complete
    float theta = baseAngles[i] - (entranceDone ? angle : 0.0);
    
    // 1. Horizontal Projection
    float x_proj = sin(theta) * 26; 
    
    // 2. Depth (Z)
    float z_proj = cos(theta);
    
    // 3. Scale/Thickness of the foot
    int thickness = (int)(1.5 + (z_proj * 1.0)); 
    if(thickness < 1) thickness = 1;

    // --- COORD CALCULATION ---
    int x_start = 64;
    int y_start = attachY[i];
    
    // Joint/Elbow
    int x_joint = 64 + (int)(x_proj * 0.6); 
    int y_joint = y_start - 7; 
    
    // Tip/Foot
    int x_end = 64 + (int)x_proj;
    int y_end = y_start + 9; 

    // Draw Leg Segments
    display.drawLine(x_start, y_start, x_joint, y_joint, WHITE);
    display.drawLine(x_joint, y_joint, x_end, y_end, WHITE);
    
    // Draw Foot
    display.fillCircle(x_end, y_end, thickness, WHITE);
  }

  // 5. Update Rotation (Only advances after dropping down)
  if (entranceDone) {
    angle += 0.07; 
    if (angle > 6.28) angle -= 6.28;
  }

  // 6. Label
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(2, 2);
  display.print("SPIDER");

  display.display();
}
// --- Button Handling ---
void handleButton() {
  static bool lastStableState = HIGH;
  static bool lastReading = HIGH;
  static unsigned long lastDebounceTime = 0;

  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastStableState) {
      lastStableState = reading;
      if (currentMode == MODE_ZEN) initAnimation();

      if (lastStableState == LOW) {  // button pressed
        currentMode = (Mode)((currentMode + 1) % 3);
        if (currentMode == MODE_SPIDER) {
  initSpiderAnimation();
}
        Serial.println("BUTTON PRESSED");
        // long currenttime = millis();
        delay(100);
        if (showAnimation) initAnimation();
      }
    }
  }

  lastReading = reading;
}

// --- Setup & Loop ---
void setup() {
  Serial.begin(115200);
  
  // Initialize I2C for OLED (ESP32 default SDA=21, SCL=22)
  Wire.begin(21, 22); 
  
  pinMode(buttonPin, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Display not found");
    for(;;);
  }

  // Display Boot Message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("Connecting...");
  display.display();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Init Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Initialize animation once
  initAnimation();
}

void loop() {
  handleButton();

  switch (currentMode) {
  case MODE_CLOCK:
    showClock();
    break;

  case MODE_ZEN:
    runAnimation();
    break;

  case MODE_SPIDER:
    runSpiderAnimation();
    break;
}
}