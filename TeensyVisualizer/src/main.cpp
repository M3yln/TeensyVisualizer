#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TEXT_OFFSET 10
#define TITLE_X_OFFSET 38

// SPI pins
#define OLED1_CS   10
#define OLED1_DC    9
#define OLED1_RST   8
#define OLED_SCK  13
#define OLED_MOSI 11

#define NUM_BARS 64
uint8_t bars[NUM_BARS];

#define AUDIO_SAMPLE_MAX 255

U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI display(U8G2_R0, OLED1_CS, OLED1_DC, OLED1_RST);
void updateBarGraph(int sample);
int smooth(int oldValue, int newValue);

void setup() {
  Serial.begin(115200);
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCK(OLED_SCK);
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.setFontRefHeightExtendedText();

  for (int i = 0; i < NUM_BARS; i++) bars[i] = 0;

  display.clearBuffer();
  display.sendBuffer();
}


void loop() {
    while (Serial.available()) {
        int volume = Serial.read();  // 0-255
        updateBarGraph(volume);
    }

  display.clearBuffer();
  display.drawStr(TITLE_X_OFFSET, TEXT_OFFSET, "Audio IN:");
  int barWidth = SCREEN_WIDTH / NUM_BARS;

  for (int i = 0; i < NUM_BARS; i++) {
    int h = bars[i];
    int x = i * barWidth;
    int y = SCREEN_HEIGHT - h;
    display.drawBox(x, y, barWidth - 1, h);
  }
  display.sendBuffer();
  // delay(5);
}

int smooth(int oldValue, int newValue) {
    if (newValue > oldValue) return newValue;  // instant rise
    return (oldValue * 4 + newValue * 4) / 8; 
}

void updateBarGraph(int sample) {
  // turn 0–255 audio sample into 0–63 height
  int h = map(sample, 0, AUDIO_SAMPLE_MAX, 0, SCREEN_HEIGHT - 1);
  h = constrain(h, 0, SCREEN_HEIGHT - 1);

  // shift bars left 
  for (int i = 0; i < NUM_BARS - 1; i++) {
    bars[i] = bars[i + 1];
  }

  // new bar goes at the end with smoothing
  bars[NUM_BARS - 1] = smooth(bars[NUM_BARS - 1], h);
}