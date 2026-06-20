#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Wire.begin(21, 22);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display not found");
    for(;;);
  }
}
int x = 64;
int y = 32;
int h = 2;
int k = 2;
int R = 3;
int r = 1;

// display.clearDisplay();
// display.fillCircle(x,y,5,WHITE);
// display.display();

void loop() {
  display.clearDisplay();

  display.fillCircle(x, y, R, WHITE);

  display.display();
  y = y + k;
  x = x + h;
  R = R + r;

  if (x+R > 128 || x-R < 0) {
    h = -h;
    x = x + h;
  }
  if (y+R > 64 || y-R < 0) {
    k = -k;
    y = y + k;
  }
  if (R > 20 || R < 0) {
    r = -r;
  }

  delay(30);
}