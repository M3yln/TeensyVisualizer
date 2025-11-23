#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TEXT_OFFSET 10

// SPI pins
#define OLED1_CS   10
#define OLED1_DC    9
#define OLED1_RST   8
#define OLED_SCK  13
#define OLED_MOSI 11

U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI display(U8G2_R0, OLED1_CS, OLED1_DC, OLED1_RST);

void setup() {
  Serial.begin(115200);
  display.begin();
  display.setFont(u8g2_font_ncenB08_tf);
  display.setFontRefHeightExtendedText();
}


void loop() {
  display.clearBuffer();
  int16_t y = (SCREEN_HEIGHT + display.getAscent() - display.getDescent()) / 2;
  display.drawStr(0, y, "Hello");
  display.drawStr(0, y + TEXT_OFFSET, "Hello");
  display.sendBuffer();
  Serial.println("looping...");
  delay(500);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}