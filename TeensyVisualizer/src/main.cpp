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
// float smoothedValue = 0.0;
// float alpha = 0.1; // smoothing factor
#define POT_PIN A0
#define BUTTON_PIN 
#define NUM_READINGS 20

// Current display mode
// 0 = Bar graph
// 1 = Waveform
uint8_t displayMode = 0;

#define BUTTON_PIN 2
#define DEBOUNCE_MS 50
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

int readings[NUM_READINGS];
int readIndex = 0;
int total = 0;
int smoothedValue = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(10); // 10-bit ADC resolution, ranges 0-1023
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCK(OLED_SCK);
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.setFontRefHeightExtendedText();

  for (int i = 0; i < NUM_BARS; i++) bars[i] = 0;
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  display.clearBuffer();
  display.sendBuffer();
}


void loop() {
  while (Serial.available()) {

    uint8_t header = Serial.read();

    // Waveform packet (512-byte payload)
    if (header == 0xAA) {

      while (Serial.available() < 256) {
        // Wait for full packet
      }

      uint8_t peak[128];
      uint8_t trough[128];

      for (int i = 0; i < 128; i++) {
        peak[i] = Serial.read();
        trough[i] = Serial.read();
      }

      if (displayMode == 1) {
        display.clearBuffer();
        for (int x = 0; x < 128; x++) {
          int yPeak   = map(peak[x],   0, 255, SCREEN_HEIGHT - 1, 0);
          int yTrough = map(trough[x], 0, 255, SCREEN_HEIGHT - 1, 0);
          display.drawLine(x, yPeak, x, yTrough);
        }
        display.sendBuffer();
      }
    }

    // Sensitivity packet from potentiometer
    else if (header == 0xFE) {
      while (!Serial.available());
      uint8_t pot = Serial.read();
      // Just ignored here; Python uses it.
    }

    // Display mode switching packet
    else if (header == 0xFD) {
      while (!Serial.available());
      displayMode = Serial.read();
    }

    // Bar-graph packet
    else if (header == 0xAB) {
      while (!Serial.available());
      uint8_t vol = Serial.read();
      if (displayMode == 0) {
        drawBarGraph(vol);
      }
    }
  }


  // Read potentiometer and smooth value
  total -= readings[readIndex];            // subtract the oldest reading
  readings[readIndex] = analogRead(POT_PIN) / 4;  // read new value (0-255)
  total += readings[readIndex];            // add the new reading

  readIndex = (readIndex + 1) % NUM_READINGS; // advance index
  smoothedValue = total / NUM_READINGS;    // compute average

  Serial.write(0xFE);
  Serial.write((int)smoothedValue);          

  // Read button state and send
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading == LOW && lastButtonState == HIGH) {
      // Button pressed
      displayMode = (displayMode + 1) % 2; // Toggle between 0 and 1
      Serial.write(0xFD);
      Serial.write(displayMode);
    }
  }

  lastButtonState = reading;
}

int smooth(int oldValue, int newValue) {
    if (newValue > oldValue) return newValue;  // instant rise
    return (oldValue * 6 + newValue * 5) / 11; 
}

void drawBarGraph(uint8_t volume) {
  int h = map(volume, 0, 255, 0, SCREEN_HEIGHT - 1);
  h = constrain(h, 0, SCREEN_HEIGHT - 1);

  for (int i = 0; i < NUM_BARS - 1; i++) {
    bars[i] = bars[i + 1];
  }

  bars[NUM_BARS - 1] = smooth(bars[NUM_BARS - 1], h);

  display.clearBuffer();

  int barWidth = SCREEN_WIDTH / NUM_BARS;
  for (int i = 0; i < NUM_BARS; i++) {
    int x = i * barWidth;
    int y = SCREEN_HEIGHT - bars[i];
    display.drawBox(x, y, barWidth - 1, bars[i]);
  }

  display.sendBuffer();
}