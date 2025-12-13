// TeensyVisualizer.ino
#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// SPI pins (if you need software SPI change constructor accordingly)
#define OLED1_CS   10
#define OLED1_DC    9
#define OLED1_RST   8
#define OLED_SCK  13
#define OLED_MOSI 11

#define NUM_BARS 64
uint8_t bars[NUM_BARS];

U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI display(U8G2_R0, OLED1_CS, OLED1_DC, OLED1_RST);

// smoothing
int smooth(int oldValue, int newValue) {
    if (newValue > oldValue) return newValue;  // instant rise
    return (oldValue * 6 + newValue * 5) / 11;
}

// bargraph update
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

void drawFFT(uint8_t *bins, int binCount) {
    display.clearBuffer();

    int pixelsPerBin = SCREEN_WIDTH / binCount;  // 128 / 64 = 2 px per bin

    // safety fallback:
    if (pixelsPerBin < 1) pixelsPerBin = 1;

    for (int i = 0; i < binCount; i++) {
        uint8_t mag = bins[i];  // 0–255 magnitude from Python

        // scale 0–255 → 0–SCREEN_HEIGHT
        int scaledHeight = (mag * SCREEN_HEIGHT) / 255;

        // bottom-aligned bar
        int x = i * pixelsPerBin;
        int y = SCREEN_HEIGHT - scaledHeight;
        // draw vertical bar
        display.drawBox(x, y, pixelsPerBin, scaledHeight);
    }

    display.sendBuffer();
}


#define POT_PIN A0
#define BUTTON_PIN A17
#define NUM_READINGS 20
#define TAG_SIZE 4
#define FFT_WIDTH 32
uint8_t fft_bins[FFT_WIDTH];

uint8_t displayMode = 1; // 0=bargraph, 1=waveform, 2=FFT

int readings[NUM_READINGS];
int readIndex = 0;
int total = 0;
int smoothedValue = 0;

// --- Tag-based parser state machine ---
// The parser reads 4-byte tags followed by payloads of known length, allowing for
// extensible commands from the PC side with data packets to be sent from Python 
// to Teensy and vice versa.
enum ParserState { READ_TAG, READ_PAYLOAD };
ParserState parserState = READ_TAG;
char tagBuf[TAG_SIZE];
uint8_t tagIndex = 0;

const int MAX_PAYLOAD = 256;
uint8_t payloadBuf[MAX_PAYLOAD];
int payloadLenExpected = 0;
int payloadIndex = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(10); // 0..1023
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCK(OLED_SCK);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.setFontRefHeightExtendedText();

  for (int i = 0; i < NUM_BARS; i++) bars[i] = 0;
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  display.clearBuffer();
  display.sendBuffer();
}

// helper to send tags back to Python
void send_tag_with_byte(const char* tag, uint8_t value) {
  Serial.write((const uint8_t*)tag, TAG_SIZE);
  Serial.write(value);
}

void process_full_packet(const char* tag, uint8_t* buf, int len) {
  if (memcmp(tag, "WAVE", TAG_SIZE) == 0) 
  {
    if (len < 256) return; 
    if (displayMode == 1) {
      display.clearBuffer();
      for (int x = 0; x < 128; x++) {
        int yPeak   = map(buf[2*x],   0, 255, SCREEN_HEIGHT - 1, 0);
        int yTrough = map(buf[2*x+1], 0, 255, SCREEN_HEIGHT - 1, 0);
        display.drawLine(x, yPeak, x, yTrough);
      }
      display.sendBuffer();
    }
  } 
  else if (memcmp(tag, "BAR ", TAG_SIZE) == 0) 
  {
    if (len < 1) return;
    uint8_t vol = buf[0];
    if (displayMode == 0) {
      drawBarGraph(vol);
    }
  } 
  else if (memcmp(tag, "POT ", TAG_SIZE) == 0) 
  {
    // Teensy doesn't need to act on POT packets (Python uses them). Ignore.
  } 
  else if (memcmp(tag, "MODE", TAG_SIZE) == 0) 
  {
    if (len < 1) return;
    displayMode = buf[0];
  } 
  else if (memcmp(tag, "FFT ", TAG_SIZE) == 0) 
  {
    if (len < FFT_WIDTH) return;
    for (int i = 0; i < FFT_WIDTH; i++) {
        fft_bins[i] = smooth(fft_bins[i], buf[i]);
    }
    if (displayMode == 2) {
        drawFFT(fft_bins, FFT_WIDTH);
    }
  } 
  else
  {
    // unknown tag: ignore
  }
}

unsigned long lastPotSendMs = 0;
const unsigned long POT_SEND_INTERVAL_MS = 80; // how often Teensy sends POT to Python

void loop() {
  // --- 1) Serial parser (non-blocking) ---
  while (Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();

    if (parserState == READ_TAG) {
      tagBuf[tagIndex++] = (char)b;
      if (tagIndex >= TAG_SIZE) {
        // tag complete, decide expected payload length
        if (memcmp(tagBuf, "WAVE", TAG_SIZE) == 0) payloadLenExpected = 256;
        else if (memcmp(tagBuf, "BAR ", TAG_SIZE) == 0) payloadLenExpected = 1;
        else if (memcmp(tagBuf, "POT ", TAG_SIZE) == 0) payloadLenExpected = 1;
        else if (memcmp(tagBuf, "MODE", TAG_SIZE) == 0) payloadLenExpected = 1;
        else if (memcmp(tagBuf, "FFT ", TAG_SIZE) == 0) payloadLenExpected = FFT_WIDTH;
        else {
          // unknown tag: reset and continue
          tagIndex = 0;
          parserState = READ_TAG;
          continue;
        }
        // move to payload read state
        payloadIndex = 0;
        parserState = READ_PAYLOAD;
      }
    } else if (parserState == READ_PAYLOAD) {
      // store payload bytes
      if (payloadIndex < MAX_PAYLOAD) {
        payloadBuf[payloadIndex++] = b;
      } else {
        // overflow (shouldn't happen if payloadLenExpected <= MAX_PAYLOAD)
        payloadIndex++;
      }

      // if payload complete, process it
      if (payloadIndex >= payloadLenExpected) {
        process_full_packet(tagBuf, payloadBuf, payloadIndex);
        // reset parser for next tag
        tagIndex = 0;
        parserState = READ_TAG;
        payloadIndex = 0;
      }
    }
  }

  // send smoothed pot to Python periodically
  // smoothing running average
  total -= readings[readIndex];
  readings[readIndex] = analogRead(POT_PIN) / 4; // 0..255
  total += readings[readIndex];
  readIndex = (readIndex + 1) % NUM_READINGS;
  smoothedValue = total / NUM_READINGS;

  unsigned long now = millis();
  if (now - lastPotSendMs >= POT_SEND_INTERVAL_MS) {
    lastPotSendMs = now;
    send_tag_with_byte("POT ", (uint8_t)smoothedValue);
  }

  // handle button press (debounced with simple delay)
  static unsigned long lastButtonToggleMs = 0;
  const unsigned long BUTTON_DEBOUNCE_MS = 200;

  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long t = millis();
    if (t - lastButtonToggleMs > BUTTON_DEBOUNCE_MS) {
      // toggle
      displayMode = (displayMode + 1) % 3;
      // inform Python
      send_tag_with_byte("MODE", displayMode);
      lastButtonToggleMs = t;
      // small wait while button released (avoid bounce loops)
      delay(20);
    }
  }
  delay(1);
}
