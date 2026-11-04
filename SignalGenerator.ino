#include <SPI.h>
#include <Wire.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <LCDWIKI_TOUCH.h> 
#include <stdio.h>
#include <string.h>

const int TouchIrqPin = 44;

LCDWIKI_KBV mylcd(ILI9488,40,38,39,43,41);
LCDWIKI_TOUCH my_touch(53,52,50,51,TouchIrqPin);

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

// --- Dark instrument theme (RGB565) ---
#define BACKGROUND_COLOR 0x0883  // graphite screen background
#define PANEL_COLOR      0x10E5  // card / panel fill
#define PANEL_SEL_COLOR  0x1928  // active tab tint
#define TEXT_COLOR       0xE77E  // primary (near-white) text
#define TEXT_DIM_COLOR   0x7C54  // dim field labels
#define ACCENT_COLOR     0x3699  // cyan accent: frequency, units, cursor, tabs
#define AMBER_COLOR      0xF524  // ATT value
#define LED_ON_COLOR     0x36AF  // output enabled
#define LED_OFF_COLOR    0x530D  // output disabled
#define FRAME_SPACING 3

// Layout: screen is 480x320 (Set_Rotation(3)). Panels span MX..MX+CW.
#define MX 8    // left/right margin
#define CW 464  // content width

// Frequency-step footer geometry (shared by Generator and Sweep screens).
#define STEP_PANEL_Y       284
#define STEP_BUTTON_Y      288
#define STEP_BUTTON_W      36
#define STEP_BUTTON_H      24
#define STEP_LEFT_BUTTON_X 158
#define STEP_RIGHT_BUTTON_X 428
#define STEP_VALUE_X       202
#define STEP_VALUE_W       220

//10db
#define ATT1 8
#define ATT2 7
//40db
#define ATT3 6
#define ATT4 5
//20db
#define ATT5 4
#define ATT6 3

const unsigned long max_frequency_step = 100000000; //Max Frequency step
const unsigned int min_frequency_step = 1;
const unsigned long max_frequency = 500000000; //Max Frequency
const int min_frequency=25; // Minimum Frequency
// The DDS module accepts sweep only from 100 kHz and with start < stop.
const unsigned long min_sweep_frequency = 100000;

unsigned long last_frequency = 14000000;
volatile unsigned long frequency_step = 100000;

// Rotary encoder
const int EncoderPinCLK = 2; 
const int EncoderPinDT = 12;  
const int EncoderPinSW = 13;  
const unsigned long ENCODER_MIN_PULSE_US = 1000;
const unsigned long ENCODER_BUTTON_DEBOUNCE_MS = 30;
const unsigned long AMPLITUDE_BLINK_INTERVAL_MS = 400;
const unsigned long AMPLITUDE_EDIT_IDLE_MS = 700;

// Updated by the ISR (Interrupt Service Routine)
unsigned volatile long frequency = 14000000;

// Sweep mode
volatile bool sweepMode = false;
unsigned volatile long sweepStartFrequency =  1000000;
unsigned volatile long sweepStopFrequency =  2000000;
unsigned volatile long sweepCurrentFrequency =  1000001;

unsigned volatile long sweepLastStartFrequency = 1000000;
unsigned volatile long sweepLastStopFrequency = 2000000;

unsigned int sweepStep = 1;
volatile int sweepTime = 10;
int sweepLastTime = 10;

bool sweepModeStart = true;
bool sweepModeStop = false;
bool sweepModeStep = false;
// sweepMenu is to determine which parameter we are changing in sweep mode
// 0 - Output enabled
// 1 - Start freq.
// 2 - Stop freq.
// 3 - Sweep time
// 4 - Amplitude
volatile short sweepMenu = 1;

// genMenu - same as sweepmenu
// 0 - Output enabled
// 1 - Freq.
// 2 - Amplitude
volatile short genMenu = 1;

// Amplitude
int realAmplitude = 0;
volatile int amplitude = 0;
int appliedAmplitude = 0;
// The DDS module accepts P-72..P0 (DAC current Normal); the step attenuator
// adds up to 70 dB below that.
const int minModuleGain = -72;
const int maxModuleGain = 0;
int minAmplitude = minModuleGain - 70;
int maxAmplitude = maxModuleGain;
int minAmplitudeSweep = minAmplitude;
int maxAmplitudeSweep = maxModuleGain;
unsigned int attValue = 0;
unsigned int lastAttValue = 0;

// Amplitude calibration maps the gain sent to the DDS module to a corrected
// value. Keep the points ordered by requestedGainDbm. Values between two
// measured points are linearly interpolated; outside the measured range the
// uncalibrated value is used.
//
// The table is intentionally empty until the generator can be measured.
// Replace the empty row and add as many measured points as required, e.g.:
//   { -20, -19 },  // requesting -20 dBm needs P-19
//   {   0,   1 },  // requesting   0 dBm needs P1
struct AmplitudeCalibrationPoint {
  int16_t requestedGainDbm;
  int16_t calibratedGainDbm;
};

const int16_t EMPTY_CALIBRATION_VALUE = -32768;
const AmplitudeCalibrationPoint amplitudeCalibration[] = {
  { EMPTY_CALIBRATION_VALUE, EMPTY_CALIBRATION_VALUE }
};

// Killswitch :)
volatile bool outputEnabled = false;

//touch related
bool wasPressed = false;
const unsigned long TOUCH_MIN_PRESS_MS = 25;
unsigned long touchPressStartedAt = 0;
uint16_t touchDownX = 0;
uint16_t touchDownY = 0;
uint16_t touchLastX = 0;
uint16_t touchLastY = 0;

//amplitude step
volatile unsigned int ampStep = 1;

// UI-refresh flags: the ISR only mutates state and raises a flag; loop()
// does the LCD drawing and Serial output so no slow/blocking work runs
// inside the interrupt.
volatile bool uiOutputChanged = false;
volatile bool uiAmplitudeChanged = false;
volatile bool uiSweepStartChanged = false;
volatile bool uiSweepStopChanged = false;
volatile bool uiSweepTimeChanged = false;

// Cached last-drawn text per field, so the change* helpers repaint only the
// character cells that actually changed instead of the whole field. Reset to
// "" whenever the screen is fully cleared (show_frequency / show_sweep).
char freqCache[24]  = "";
char ampCache[16]   = "";
char attCache[16]   = "";
char startCache[24] = "";
char stopCache[24]  = "";
char timeCache[8]   = "";

// A changed amplitude is only applied to the DDS and step attenuator when the
// encoder button is pressed. Blink the requested value until that commit.
bool amplitudeTextVisible = true;
unsigned long amplitudeBlinkLastToggle = 0;
unsigned long amplitudeLastEditTime = 0;

// Rect of the currently selected field's accent border, so selectField()
// erases only the previous border instead of the whole screen. selW==0 means
// "no border drawn yet" (reset on every full repaint).
int16_t selX = 0, selY = 0, selW = 0, selH = 0;

// Underline the frequency digit controlled by the encoder. The selected
// decimal place is represented by frequency_step (1 Hz .. 100 MHz), so the
// same cursor works for generator frequency and sweep start/stop fields.
int16_t digitCursorX = 0, digitCursorY = 0, digitCursorW = 0;

void applyEncoderStep(bool directionLow) {
    if (!sweepMode) {
      switch (genMenu) {
      case 0:
        outputEnabled = !outputEnabled;
        uiOutputChanged = true;
        break;
      case 1:
        if (directionLow) {
          // clamp at minimum instead of underflowing (unsigned) and wrapping to max
          frequency = (frequency > (unsigned long)min_frequency + frequency_step) ? frequency - frequency_step : (unsigned long)min_frequency;
        } else {
          frequency = (frequency < max_frequency - frequency_step) ? frequency + frequency_step : max_frequency;
        }
        break;
        case 2:
          if (directionLow) {
            amplitude = (amplitude - (int)ampStep > minAmplitude) ? amplitude - (int)ampStep : minAmplitude;
          }
          else {
            amplitude = (amplitude + (int)ampStep < maxAmplitude) ? amplitude + (int)ampStep : maxAmplitude;
          }
          uiAmplitudeChanged = true;
          break;
      }
    } else {
      switch (sweepMenu) {
        case 0:
          outputEnabled = !outputEnabled;
          uiOutputChanged = true;
          break;
        case 1:
          if (directionLow) {
            sweepStartFrequency = (sweepStartFrequency > min_sweep_frequency + frequency_step) ? sweepStartFrequency - frequency_step : min_sweep_frequency;
          } else {
            sweepStartFrequency = (sweepStartFrequency < max_frequency - 1 - frequency_step) ? sweepStartFrequency + frequency_step : max_frequency - 1;
          }
          if (sweepStartFrequency >= sweepStopFrequency) { sweepStopFrequency = sweepStartFrequency + 1; uiSweepStopChanged = true; }
          sweepCurrentFrequency = sweepStartFrequency;
          uiSweepStartChanged = true;
          break;
        case 2:
          if (directionLow) {
            sweepStopFrequency = (sweepStopFrequency > min_sweep_frequency + 1 + frequency_step) ? sweepStopFrequency - frequency_step : min_sweep_frequency + 1;
          } else {
            sweepStopFrequency = (sweepStopFrequency < max_frequency - frequency_step) ? sweepStopFrequency + frequency_step : max_frequency;
          }
          if (sweepStopFrequency <= sweepStartFrequency) { sweepStartFrequency = sweepStopFrequency - 1; uiSweepStartChanged = true; }
          sweepCurrentFrequency = sweepStartFrequency;
          uiSweepStopChanged = true;
          break;
        case 3:
          if (directionLow) {
            if (sweepTime >= 2) sweepTime = sweepTime - 1;
          } else {
            if (sweepTime <= 99) sweepTime = sweepTime + 1;
          }
          uiSweepTimeChanged = true;
          break;
        case 4:
          if (directionLow) {
            amplitude = (amplitude - 10 > minAmplitudeSweep) ? amplitude - 10 : minAmplitudeSweep;
          }
          else {
            amplitude = (amplitude + 10 < maxAmplitudeSweep) ? amplitude + 10 : maxAmplitudeSweep;
          }
          uiAmplitudeChanged = true;
          break;
      }
    }
}

void isr() {
  static unsigned long lowSince = 0;
  static bool pendingLowPulse = false;
  static bool pendingDirectionLow = false;

  unsigned long now = micros();
  bool clockLow = digitalRead(EncoderPinCLK) == LOW;

  if (clockLow) {
    // Start measuring a candidate LOW pulse. A bounce pulse normally returns
    // HIGH very quickly; a real encoder phase stays LOW much longer.
    if (!pendingLowPulse) {
      lowSince = now;
      pendingDirectionLow = digitalRead(EncoderPinDT) == LOW;
      pendingLowPulse = true;
    }
  } else if (pendingLowPulse) {
    if (now - lowSince >= ENCODER_MIN_PULSE_US) {
      applyEncoderStep(pendingDirectionLow);
    }
    pendingLowPulse = false;
  }
}

int calibratedGainFor(int requestedGainDbm) {
  bool haveLower = false;
  bool haveUpper = false;
  AmplitudeCalibrationPoint lower;
  AmplitudeCalibrationPoint upper;

  const size_t pointCount = sizeof(amplitudeCalibration) /
                            sizeof(amplitudeCalibration[0]);
  for (size_t i = 0; i < pointCount; i++) {
    const AmplitudeCalibrationPoint point = amplitudeCalibration[i];
    if (point.requestedGainDbm == EMPTY_CALIBRATION_VALUE ||
        point.calibratedGainDbm == EMPTY_CALIBRATION_VALUE) {
      continue;
    }

    if (point.requestedGainDbm == requestedGainDbm) {
      return point.calibratedGainDbm;
    }
    if (point.requestedGainDbm < requestedGainDbm &&
        (!haveLower || point.requestedGainDbm > lower.requestedGainDbm)) {
      lower = point;
      haveLower = true;
    }
    if (point.requestedGainDbm > requestedGainDbm &&
        (!haveUpper || point.requestedGainDbm < upper.requestedGainDbm)) {
      upper = point;
      haveUpper = true;
    }
  }

  if (!haveLower || !haveUpper) return requestedGainDbm;

  const long inputSpan = upper.requestedGainDbm - lower.requestedGainDbm;
  const long outputSpan = upper.calibratedGainDbm - lower.calibratedGainDbm;
  return lower.calibratedGainDbm +
         ((long)(requestedGainDbm - lower.requestedGainDbm) * outputSpan) /
         inputSpan;
}

void setGain() {
  char buf[16];
  snprintf(buf, sizeof(buf), "P%d", calibratedGainFor(realAmplitude));
  Serial3.println(buf);
}

// Redraw only the character cells that differ from the cached previous value.
// Monospace glyphs are 6*sz wide, 8*sz tall. `bg` is the fill behind the field
// (the panel colour, so cleared cells match the card, not the screen).
void drawTextDiff(int16_t x, int16_t y, const char *text, char *cache, uint16_t bg, uint8_t sz) {
  const int16_t charW = 6 * sz;
  const int16_t charH = 8 * sz;
  mylcd.Set_Text_Size(sz);
  int16_t tlen = strlen(text);
  int16_t clen = strlen(cache);
  for (int16_t i = 0; i < tlen; i++) {
    if (i >= clen || cache[i] != text[i]) {
      char cbuf[2] = { text[i], '\0' };
      mylcd.Fill_Rect(x + i * charW, y, charW, charH, bg);
      mylcd.Print_String(cbuf, x + i * charW, y);
    }
  }
  // erase trailing cells left over from a previously longer value
  for (int16_t i = tlen; i < clen; i++) {
    mylcd.Fill_Rect(x + i * charW, y, charW, charH, bg);
  }
  strcpy(cache, text);
}

// Filled rounded card at PANEL_COLOR.
void panel(int16_t x, int16_t y, int16_t w, int16_t h) {
  mylcd.Set_Draw_color(PANEL_COLOR);
  mylcd.Fill_Round_Rectangle(x, y, x + w, y + h, 6);
}

// Dim uppercase field label (size 2).
void labelDim(const char *t, int16_t x, int16_t y) {
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(TEXT_DIM_COLOR);
  mylcd.Print_String(t, x, y);
}

// LED dot + ON/OFF text for the OUTPUT field, vertically centred on cy.
void drawOutputState(int16_t cy) {
  mylcd.Set_Text_Size(2);
  if (outputEnabled) {
    mylcd.Set_Draw_color(LED_ON_COLOR);
    mylcd.Fill_Circle(150, cy, 7);
    mylcd.Set_Text_colour(TEXT_COLOR);
    mylcd.Print_String("ON", 172, cy - 8);
  } else {
    mylcd.Set_Draw_color(LED_OFF_COLOR);
    mylcd.Fill_Circle(150, cy, 7);
    mylcd.Set_Text_colour(TEXT_DIM_COLOR);
    mylcd.Print_String("OFF", 172, cy - 8);
  }
}

// Decorative sawtooth in the sweep "SWEEP" panel (drawn only on full repaint).
void drawRamp() {
  mylcd.Set_Draw_color(ACCENT_COLOR);
  const int16_t x0 = 150, x1 = 460, base = 214, peak = 190;
  const int16_t teeth = 3;
  int16_t tw = (x1 - x0) / teeth;
  for (int16_t i = 0; i < teeth; i++) {
    int16_t sx = x0 + i * tw;
    mylcd.Draw_Line(sx, base, sx + tw, peak);   // rising ramp
    mylcd.Draw_Line(sx + tw, peak, sx + tw, base); // fast retrace
  }
}

// Two mode tabs across the top; the active one gets the accent tint + text.
void drawTabs(bool sweep) {
  mylcd.Set_Draw_color(sweep ? PANEL_COLOR : PANEL_SEL_COLOR);
  mylcd.Fill_Round_Rectangle(MX, 4, MX + 229, 34, 4);
  mylcd.Set_Draw_color(sweep ? PANEL_SEL_COLOR : PANEL_COLOR);
  mylcd.Fill_Round_Rectangle(MX + 235, 4, MX + CW, 34, 4);
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(sweep ? TEXT_DIM_COLOR : ACCENT_COLOR);
  mylcd.Print_String("GENERATOR", MX + 58, 12);
  mylcd.Set_Text_colour(sweep ? ACCENT_COLOR : TEXT_DIM_COLOR);
  mylcd.Print_String("SWEEP", MX + 327, 12);
}

// Rounded frequency-step button with a two-stroke chevron. Drawing the icon
// with lines (instead of a text glyph) keeps both arrows visually balanced.
void drawStepButton(int16_t x, bool pointsRight) {
  mylcd.Set_Draw_color(PANEL_SEL_COLOR);
  mylcd.Fill_Round_Rectangle(x, STEP_BUTTON_Y,
                             x + STEP_BUTTON_W, STEP_BUTTON_Y + STEP_BUTTON_H, 5);
  mylcd.Set_Draw_color(ACCENT_COLOR);
  mylcd.Draw_Round_Rectangle(x, STEP_BUTTON_Y,
                             x + STEP_BUTTON_W, STEP_BUTTON_Y + STEP_BUTTON_H, 5);

  int16_t cx = x + STEP_BUTTON_W / 2;
  int16_t cy = STEP_BUTTON_Y + STEP_BUTTON_H / 2;
  int16_t direction = pointsRight ? 1 : -1;
  for (int16_t stroke = 0; stroke < 2; stroke++) {
    mylcd.Draw_Line(cx - direction * 3 + stroke, cy - 6,
                    cx + direction * 3 + stroke, cy);
    mylcd.Draw_Line(cx + direction * 3 + stroke, cy,
                    cx - direction * 3 + stroke, cy + 6);
  }
}

// Repaint the step value centred between its arrow buttons.
void drawStepValue() {
  const char *value = format_frequency(frequency_step);
  const int16_t textWidth = strlen(value) * 12; // size-2 glyph width
  const int16_t valueX = STEP_VALUE_X + (STEP_VALUE_W - textWidth) / 2;
  mylcd.Fill_Rect(STEP_VALUE_X, 292, STEP_VALUE_W, 16, PANEL_COLOR);
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(value, valueX, 292);
}

// Card-style footer used on both screens.
void drawFrequencyStep() {
  panel(MX, STEP_PANEL_Y, CW, 32);
  labelDim("Freq. step:", 18, 292);
  drawStepButton(STEP_LEFT_BUTTON_X, false);
  drawStepButton(STEP_RIGHT_BUTTON_X, true);
  drawStepValue();
}

unsigned long decimalStepForDigit(const char *text, int16_t digitIndex) {
  unsigned long step = 1;
  for (int16_t i = digitIndex + 1; text[i] && text[i] != ' '; i++) {
    if (text[i] >= '0' && text[i] <= '9') {
      if (step <= max_frequency_step / 10) step *= 10;
    }
  }
  return step;
}

// Select the nearest displayed digit to a touch and translate its decimal
// place into the encoder step. Separators and the unit text snap to the
// nearest digit instead of becoming dead touch zones.
void selectFrequencyDigit(unsigned long value, int16_t textX, uint8_t textSize,
                          int16_t touchX) {
  const char *text = format_frequency(value);
  const int16_t len = strlen(text);
  const int16_t charW = 6 * textSize;
  int16_t index = (touchX - textX) / charW;
  if (index < 0) index = 0;
  if (index >= len) index = len - 1;

  int16_t digitIndex = -1;
  for (int16_t distance = 0; distance < len && digitIndex < 0; distance++) {
    int16_t left = index - distance;
    int16_t right = index + distance;
    if (left >= 0 && text[left] >= '0' && text[left] <= '9') digitIndex = left;
    else if (right < len && text[right] >= '0' && text[right] <= '9') digitIndex = right;
  }
  if (digitIndex < 0) return;

  noInterrupts();
  frequency_step = decimalStepForDigit(text, digitIndex);
  interrupts();
  changeStep();
}

void eraseFrequencyDigitCursor() {
  if (digitCursorW) {
    mylcd.Fill_Rect(digitCursorX, digitCursorY, digitCursorW, 3, PANEL_COLOR);
    digitCursorW = 0;
  }
}

void drawFrequencyDigitCursor(unsigned long value, int16_t textX,
                              int16_t textY, uint8_t textSize) {
  eraseFrequencyDigitCursor();
  const char *text = format_frequency(value);
  int16_t selectedIndex = -1;
  for (int16_t i = 0; text[i] && text[i] != ' '; i++) {
    if (text[i] >= '0' && text[i] <= '9' &&
        decimalStepForDigit(text, i) == frequency_step) {
      selectedIndex = i;
      break;
    }
  }
  if (selectedIndex < 0) return;

  const int16_t charW = 6 * textSize;
  digitCursorX = textX + selectedIndex * charW + 1;
  digitCursorY = textY + 8 * textSize + 2;
  digitCursorW = charW - 2;
  mylcd.Set_Draw_color(ACCENT_COLOR);
  mylcd.Fill_Rect(digitCursorX, digitCursorY, digitCursorW, 3, ACCENT_COLOR);
}

void drawSelectedFrequencyDigit() {
  if (!sweepMode && genMenu == 1) {
    drawFrequencyDigitCursor(frequency, 18, 74, 3);
  } else if (sweepMode && sweepMenu == 1) {
    drawFrequencyDigitCursor(sweepStartFrequency, 150, 84, 2);
  } else if (sweepMode && sweepMenu == 2) {
    drawFrequencyDigitCursor(sweepStopFrequency, 150, 120, 2);
  } else {
    eraseFrequencyDigitCursor();
  }
}

void cycleFrequencyDigit() {
  unsigned long editedValue = frequency;
  if (sweepMode && sweepMenu == 1) editedValue = sweepStartFrequency;
  else if (sweepMode && sweepMenu == 2) editedValue = sweepStopFrequency;
  const char *text = format_frequency(editedValue);
  int16_t firstDigit = 0;
  while (text[firstDigit] && !(text[firstDigit] >= '0' && text[firstDigit] <= '9')) {
    firstDigit++;
  }
  unsigned long highestDisplayedStep = decimalStepForDigit(text, firstDigit);

  noInterrupts();
  frequency_step = (frequency_step == min_frequency_step)
                     ? highestDisplayedStep
                     : frequency_step / 10;
  interrupts();
  changeStep();
}

// 2px rectangle outline used as the selection cursor.
void drawBorder(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  mylcd.Set_Draw_color(c);
  mylcd.Draw_Rectangle(x, y, x + w - 1, y + h - 1);
  mylcd.Draw_Rectangle(x + 1, y + 1, x + w - 2, y + h - 2);
}

// Move the accent cursor: erase the previous field's border (repaint it in the
// panel colour, inside the card) and draw the new one in ACCENT_COLOR.
void selectField(int16_t x, int16_t y, int16_t w, int16_t h) {
  // inset the border so it sits inside the rounded card, off the corner cut
  x += 2; y += 2; w -= 4; h -= 4;
  if (selW) { drawBorder(selX, selY, selW, selH, PANEL_COLOR); }
  selX = x; selY = y; selW = w; selH = h;
  drawBorder(x, y, w, h, ACCENT_COLOR);
}

void changeAmplitude() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d dbm", amplitude);
  mylcd.Set_Text_colour(TEXT_COLOR);
  drawTextDiff(18, sweepMode ? 250 : 198, buf, ampCache, PANEL_COLOR, 3);
  amplitudeTextVisible = true;
  amplitudeBlinkLastToggle = millis();
}

void serviceAmplitudeBlink(unsigned long now) {
  if (amplitude == appliedAmplitude) {
    if (!amplitudeTextVisible) changeAmplitude();
    return;
  }

  // Keep the value continuously visible while the encoder is being turned.
  if (now - amplitudeLastEditTime < AMPLITUDE_EDIT_IDLE_MS) return;

  if (now - amplitudeBlinkLastToggle < AMPLITUDE_BLINK_INTERVAL_MS) return;

  amplitudeBlinkLastToggle = now;
  amplitudeTextVisible = !amplitudeTextVisible;
  if (amplitudeTextVisible) {
    changeAmplitude();
  } else {
    mylcd.Set_Text_colour(TEXT_COLOR);
    drawTextDiff(18, sweepMode ? 250 : 198, "", ampCache, PANEL_COLOR, 3);
  }
}

void show_frequency() {
  char buf[24];
  snprintf(buf, sizeof(buf), "F%lu", (unsigned long)frequency);
  Serial3.println(buf);
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  drawTabs(false);
  selW = 0;  // forget the previous screen's selection border
  digitCursorW = 0;

  // Frequency hero
  panel(MX, 42, CW, 74);
  labelDim("FREQUENCY", 18, 48);
  strcpy(freqCache, format_frequency(frequency));
  mylcd.Set_Text_Size(3);
  mylcd.Set_Text_colour(ACCENT_COLOR);
  mylcd.Print_String(freqCache, 18, 74);

  // Output status
  panel(MX, 124, CW, 40);
  labelDim("OUTPUT", 18, 136);
  drawOutputState(144);

  // Amplitude tile
  panel(MX, 172, 229, 54);
  labelDim("AMPLITUDE", 18, 178);
  snprintf(buf, sizeof(buf), "%d dbm", amplitude);
  strcpy(ampCache, buf);
  mylcd.Set_Text_Size(3);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(buf, 18, 198);
  amplitudeTextVisible = true;
  amplitudeBlinkLastToggle = millis();

  // ATT tile
  panel(243, 172, 229, 54);
  labelDim("ATT STATE", 253, 178);
  snprintf(buf, sizeof(buf), "%u db", attValue);
  strcpy(attCache, buf);
  mylcd.Set_Text_Size(3);
  mylcd.Set_Text_colour(AMBER_COLOR);
  mylcd.Print_String(buf, 253, 198);

  drawFrequencyStep();

  genMenu = 0;
  genChangeMenu();  // draw the selection border
}

void showATT() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u db", attValue);
  mylcd.Set_Text_colour(AMBER_COLOR);
  drawTextDiff(253, sweepMode ? 250 : 198, buf, attCache, PANEL_COLOR, 3);
}

void show_sweep() {
  char buf[32];
  snprintf(buf, sizeof(buf), "S%lu:%lu:%d", (unsigned long)sweepStartFrequency, (unsigned long)sweepStopFrequency, sweepTime);
  Serial3.println(buf);
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  drawTabs(true);
  selW = 0;
  digitCursorW = 0;

  // Output status
  panel(MX, 40, CW, 32);
  labelDim("OUTPUT", 18, 48);
  drawOutputState(56);

  // Start / Stop / Time — full-width rows (frequency strings are too long for
  // side-by-side cells)
  panel(MX, 76, CW, 32);
  labelDim("START", 18, 84);
  strcpy(startCache, format_frequency(sweepStartFrequency));
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(startCache, 150, 84);

  panel(MX, 112, CW, 32);
  labelDim("STOP", 18, 120);
  strcpy(stopCache, format_frequency(sweepStopFrequency));
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(stopCache, 150, 120);

  panel(MX, 148, CW, 32);
  labelDim("TIME", 18, 156);
  snprintf(buf, sizeof(buf), "%d ms", sweepTime);
  strcpy(timeCache, buf);
  mylcd.Set_Text_Size(2);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(buf, 150, 156);

  // Sweep ramp icon
  panel(MX, 186, CW, 34);
  labelDim("SWEEP", 18, 194);
  drawRamp();

  // Amplitude tile
  panel(MX, 226, 229, 52);
  labelDim("AMPLITUDE", 18, 232);
  snprintf(buf, sizeof(buf), "%d dbm", amplitude);
  strcpy(ampCache, buf);
  mylcd.Set_Text_Size(3);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Print_String(buf, 18, 250);
  amplitudeTextVisible = true;
  amplitudeBlinkLastToggle = millis();

  // ATT tile
  panel(243, 226, 229, 52);
  labelDim("ATT STATE", 253, 232);
  snprintf(buf, sizeof(buf), "%u db", attValue);
  strcpy(attCache, buf);
  mylcd.Set_Text_Size(3);
  mylcd.Set_Text_colour(AMBER_COLOR);
  mylcd.Print_String(buf, 253, 250);

  drawFrequencyStep();

  sweepMenu = 0;
  sweepChangeMenu();
}

void changeStart() {
  mylcd.Set_Text_colour(TEXT_COLOR);
  drawTextDiff(150, 84, format_frequency(sweepStartFrequency), startCache, PANEL_COLOR, 2);
  drawSelectedFrequencyDigit();
}

void changeStop() {
  mylcd.Set_Text_colour(TEXT_COLOR);
  drawTextDiff(150, 120, format_frequency(sweepStopFrequency), stopCache, PANEL_COLOR, 2);
  drawSelectedFrequencyDigit();
}

void changeTime() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d ms", sweepTime);
  mylcd.Set_Text_colour(TEXT_COLOR);
  drawTextDiff(150, 156, buf, timeCache, PANEL_COLOR, 2);
}

void changeGenFrequency() {
  mylcd.Set_Text_colour(ACCENT_COLOR);
  drawTextDiff(18, 74, format_frequency(frequency), freqCache, PANEL_COLOR, 3);
  drawSelectedFrequencyDigit();
}

// Cursor rects mirror the editable panels drawn in show_sweep().
void sweepChangeMenu() {
  switch(sweepMenu) {
    case 0: selectField(MX, 40, CW, 32);    break; // output
    case 1: selectField(MX, 76, CW, 32);    break; // start
    case 2: selectField(MX, 112, CW, 32);   break; // stop
    case 3: selectField(MX, 148, CW, 32);   break; // time
    case 4: selectField(MX, 226, 229, 52);  break; // amplitude
  }
  drawSelectedFrequencyDigit();
}

// Cursor rects mirror the editable panels drawn in show_frequency().
void genChangeMenu() {
  switch(genMenu) {
    case 0: selectField(MX, 124, CW, 40);   break; // output
    case 1: selectField(MX, 42, CW, 74);    break; // frequency
    case 2: selectField(MX, 172, 229, 54);  break; // amplitude
  }
  drawSelectedFrequencyDigit();
}

void changeStep() {
  drawStepValue();
  drawSelectedFrequencyDigit();
}

// Repaint just the LED + ON/OFF inside the OUTPUT card (keeps the selection
// border and the "OUTPUT" label intact) and push E/D to the module.
void changeOutput() {
  int16_t py = sweepMode ? 40 : 124;
  int16_t ph = sweepMode ? 32 : 40;
  // clear just the LED+text strip, staying clear of the selection border edges
  mylcd.Fill_Rect(138, py + 5, 320, ph - 10, PANEL_COLOR);
  Serial3.println(outputEnabled ? "E" : "D");
  drawOutputState(py + ph / 2);
}

void touchProcess() {
  const bool pressed = my_touch.TP_Get_State() & TP_PRES_DOWN;
  const bool coordinateValid = my_touch.x < 480 && my_touch.y < 320;

  if (pressed) {
    // TP_Scan marks the panel as pressed from TIRQ alone, even when its ADC
    // samples disagree. Only start/continue a gesture with screen coordinates.
    if (!coordinateValid) return;

    if (!wasPressed) {
      wasPressed = true;
      touchPressStartedAt = millis();
      touchDownX = my_touch.x;
      touchDownY = my_touch.y;
    }
    touchLastX = my_touch.x;
    touchLastY = my_touch.y;
    return;
  }

  if (wasPressed) {
    wasPressed = false;
    if (millis() - touchPressStartedAt < TOUCH_MIN_PRESS_MS) return;

    const uint16_t ty = touchLastY;
    const uint16_t tx = touchLastX;

    // A tab activates only when both ends of the gesture are on that same tab.
    // This prevents a noisy release or a drag across the screen from changing
    // operating mode.
    const int8_t downTab = touchDownY <= 36 ? (touchDownX < 240 ? 0 : 1) : -1;
    const int8_t upTab = ty <= 36 ? (tx < 240 ? 0 : 1) : -1;
    if (downTab >= 0 || upTab >= 0) {
      if (downTab != upTab) return;
      if (upTab == 0) {
        if (sweepMode) { sweepMode = false; Serial.println("Generator mode"); show_frequency(); }
      } else {
        if (!sweepMode) { sweepMode = true; Serial.println("Sweep mode"); show_sweep(); }
      }
      return;
    }

    if (sweepMode) {
      if      (ty >= 40  && ty <= 72)  { sweepMenu = 0; sweepChangeMenu(); }
      else if (ty >= 76  && ty <= 108) {
        sweepMenu = 1;
        sweepChangeMenu();
        selectFrequencyDigit(sweepStartFrequency, 150, 2, tx);
      }
      else if (ty >= 112 && ty <= 144) {
        sweepMenu = 2;
        sweepChangeMenu();
        selectFrequencyDigit(sweepStopFrequency, 150, 2, tx);
      }
      else if (ty >= 148 && ty <= 180) { sweepMenu = 3; sweepChangeMenu(); }
      else if (ty >= 226 && ty <= 278 && tx < 240) { sweepMenu = 4; sweepChangeMenu(); }
    } else {
      if      (ty >= 124 && ty <= 164) { genMenu = 0; genChangeMenu(); }
      else if (ty >= 42  && ty <= 116) {
        genMenu = 1;
        genChangeMenu();
        selectFrequencyDigit(frequency, 18, 3, tx);
      }
      else if (ty >= 172 && ty <= 226) {
        if (tx < 240) {
          // second tap on the already-selected amplitude toggles the step size
          if (genMenu != 2) { genMenu = 2; genChangeMenu(); }
          else { ampStep = (ampStep == 1) ? 10 : 1; }
        }
      }
    }

    // Frequency-step footer: only the visible arrow buttons are actionable.
    if (ty >= STEP_BUTTON_Y && ty <= STEP_BUTTON_Y + STEP_BUTTON_H) {
      if (tx >= STEP_LEFT_BUTTON_X && tx <= STEP_LEFT_BUTTON_X + STEP_BUTTON_W) {
        unsigned long nextStep = (frequency_step == min_frequency_step) ? max_frequency_step : frequency_step / 10;
        noInterrupts();
        frequency_step = nextStep;
        interrupts();
        Serial.print("Multiplier:"); Serial.println(frequency_step);
        changeStep();
      } else if (tx >= STEP_RIGHT_BUTTON_X && tx <= STEP_RIGHT_BUTTON_X + STEP_BUTTON_W) {
        unsigned long nextStep = (frequency_step == max_frequency_step) ? min_frequency_step : frequency_step * 10;
        noInterrupts();
        frequency_step = nextStep;
        interrupts();
        Serial.print("Multiplier:"); Serial.println(frequency_step);
        changeStep();
      }
    }
  }
}

void updateAmplitudeSettings(int requestedAmplitude) {
  if (requestedAmplitude > -10) {
    realAmplitude = requestedAmplitude;
    attValue = 0;
  } else if ((requestedAmplitude <= -10) && (requestedAmplitude > -20)) {
    attValue = 10;
    realAmplitude = requestedAmplitude + 10;
  } else if ((requestedAmplitude <= -20) && (requestedAmplitude > -30)) {
    attValue = 20;
    realAmplitude = requestedAmplitude + 20;
  } else if ((requestedAmplitude <= -30) && (requestedAmplitude > -40)) {
    attValue = 30;
    realAmplitude = requestedAmplitude + 30;
  } else if ((requestedAmplitude <= -40) && (requestedAmplitude > -50)) {
    attValue = 40;
    realAmplitude = requestedAmplitude + 40;
  } else if ((requestedAmplitude <= -50) && (requestedAmplitude > -60)) {
    attValue = 50;
    realAmplitude = requestedAmplitude + 50;
  } else if ((requestedAmplitude <= -60) && (requestedAmplitude > -70)) {
    attValue = 60;
    realAmplitude = requestedAmplitude + 60;
  } else if (requestedAmplitude <= -70) {
    attValue = 70;
    realAmplitude = requestedAmplitude + 70;
  }
}

void changeATT() {
  updateAmplitudeSettings(amplitude);
  Serial.println(realAmplitude);
}

void changeATTValue() {
  if (lastAttValue != attValue) {
    switch(attValue) {
      case 0:
        resetATT();
        delay(100);
        resetATT();
        showATT();
        break;
      case 10:
        att10db();
        delay(100);
        att10db();
        showATT();
        break;
      case 20:
        att20db();
        delay(100);
        att20db();
        showATT();
        break;
      case 30:
        att30db();
        delay(100);
        att30db();
        showATT();
        break;
      case 40:
        att40db();
        delay(100);
        att40db();
        showATT();
        break;
      case 50:
        att50db();
        delay(100);
        att50db();
        showATT();
        break;
      case 60:
        att60db();
        delay(100);
        att60db();
        showATT();
        break;
      case 70:
        att70db();
        delay(100);
        att70db();
        showATT();
    }
    lastAttValue = attValue;
  }
  Serial.println(realAmplitude);
}

char* format_frequency(unsigned volatile long frequency) {
    static char output[50];

    if (frequency >= 1000000) {
        unsigned long mhz = frequency / 1000000;
        unsigned long khz = (frequency % 1000000) / 1000;
        unsigned long hz = frequency % 1000;
        sprintf(output, "%lu.%03lu.%03lu MHz", mhz, khz, hz);
    } else if (frequency >= 1000) {
        unsigned long khz = frequency / 1000;
        unsigned long hz = frequency % 1000;
        sprintf(output, "%lu.%03lu kHz", khz, hz);
    } else {
        sprintf(output, "%lu Hz", frequency);
    }

    return output;
}

void resetATTPins() {
  digitalWrite(ATT1, LOW);
  digitalWrite(ATT2, LOW);
  digitalWrite(ATT3, LOW);
  digitalWrite(ATT4, LOW);
  digitalWrite(ATT5, LOW);
  digitalWrite(ATT6, LOW);
}

void att10db() {
  digitalWrite(ATT1, HIGH);
  digitalWrite(ATT4, HIGH);
  digitalWrite(ATT6, HIGH);
  delay(50);
  digitalWrite(ATT1, LOW);
  digitalWrite(ATT4, LOW);
  digitalWrite(ATT6, LOW);
}

void att20db() {
  digitalWrite(ATT2, HIGH);
  digitalWrite(ATT4, HIGH);
  digitalWrite(ATT5, HIGH);
  delay(50);
  digitalWrite(ATT2, LOW);
  digitalWrite(ATT4, LOW);
  digitalWrite(ATT5, LOW);
}

void att30db() {
  digitalWrite(ATT1, HIGH);
  digitalWrite(ATT4, HIGH);
  digitalWrite(ATT5, HIGH);
  delay(50);
  digitalWrite(ATT1, LOW);
  digitalWrite(ATT4, LOW);
  digitalWrite(ATT5, LOW);
}

void att40db() {
  digitalWrite(ATT2, HIGH);
  digitalWrite(ATT3, HIGH);
  digitalWrite(ATT6, HIGH);
  delay(50);
  digitalWrite(ATT2, LOW);
  digitalWrite(ATT3, LOW);
  digitalWrite(ATT6, LOW);
}

void att50db() {
  digitalWrite(ATT1, HIGH);
  digitalWrite(ATT3, HIGH);
  digitalWrite(ATT6, HIGH);
  delay(50);
  digitalWrite(ATT1, LOW);
  digitalWrite(ATT3, LOW);
  digitalWrite(ATT6, LOW);
}

void att60db() {
  digitalWrite(ATT2, HIGH);
  digitalWrite(ATT3, HIGH);
  digitalWrite(ATT5, HIGH);
  delay(10);
  digitalWrite(ATT2, LOW);
  digitalWrite(ATT3, LOW);
  digitalWrite(ATT5, LOW);
}

void att70db() {
  digitalWrite(ATT1, HIGH);
  digitalWrite(ATT3, HIGH);
  digitalWrite(ATT5, HIGH);
  delay(50);
  digitalWrite(ATT1, LOW);
  digitalWrite(ATT3, LOW);
  digitalWrite(ATT5, LOW);
}

void resetATT() {
  digitalWrite(ATT2, HIGH);
  digitalWrite(ATT4, HIGH);
  digitalWrite(ATT6, HIGH);
  delay(50);
  digitalWrite(ATT2, LOW);
  digitalWrite(ATT4, LOW);
  digitalWrite(ATT6, LOW);
}

void changeAmpStep () {}

void checkATT() {
  Serial3.println("F15000000");
  Serial3.println("E");
  mylcd.Print_String("ATT: ", FRAME_SPACING, 225);
  att10db();
  delay(2000);
  att20db();
  delay(2000);
  att30db();
  delay(2000);
  att40db();
  delay(2000);
  att50db();
  delay(2000);
  att60db();
  delay(2000);
  att70db();
  delay(2000);
  resetATT();
  Serial3.println("D");
}

// The DDS module opens its serial port only after its own splash screen, so
// commands sent right after power-up are lost. Poll it with "V" until it
// answers (or give up after a timeout) before sending the initial state.
const unsigned long MODULE_BOOT_TIMEOUT_MS = 10000;
const unsigned long MODULE_POLL_INTERVAL_MS = 300;

void waitForModule() {
  const unsigned long started = millis();
  while (millis() - started < MODULE_BOOT_TIMEOUT_MS) {
    Serial3.println("V");
    const unsigned long sent = millis();
    while (millis() - sent < MODULE_POLL_INTERVAL_MS) {
      if (Serial3.available()) {
        delay(50);
        while (Serial3.available()) Serial3.read();
        Serial.println("DDS module ready");
        return;
      }
    }
  }
  Serial.println("DDS module did not answer");
}

void showSplashScreen() {
  const int16_t cardX = 28;
  const int16_t cardY = 42;
  const int16_t cardW = 424;
  const int16_t cardH = 236;

  mylcd.Fill_Screen(BACKGROUND_COLOR);

  // A quiet, instrument-style title card that matches the main UI.
  mylcd.Set_Draw_color(PANEL_COLOR);
  mylcd.Fill_Round_Rectangle(cardX, cardY,
                            cardX + cardW, cardY + cardH, 10);
  mylcd.Set_Draw_color(ACCENT_COLOR);
  mylcd.Draw_Round_Rectangle(cardX, cardY,
                            cardX + cardW, cardY + cardH, 10);
  mylcd.Draw_Round_Rectangle(cardX + 3, cardY + 3,
                            cardX + cardW - 3, cardY + cardH - 3, 8);

  // Small decorative rules frame the subtitle without adding a bitmap.
  mylcd.Draw_Line(70, 104, 220, 104);
  mylcd.Draw_Line(260, 104, 410, 104);
  mylcd.Fill_Circle(240, 104, 3);

  mylcd.Set_Text_Mode(1);
  mylcd.Set_Text_Back_colour(PANEL_COLOR);
  mylcd.Set_Text_colour(TEXT_DIM_COLOR);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("DIRECT DIGITAL SYNTHESIS", 96, 72);

  mylcd.Set_Text_colour(ACCENT_COLOR);
  mylcd.Set_Text_Size(4);
  mylcd.Print_String("AD9910", 168, 124);

  mylcd.Set_Draw_color(TEXT_DIM_COLOR);
  mylcd.Draw_Line(96, 170, 384, 170);

  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Set_Text_Size(3);
  mylcd.Print_String("SIGNAL GENERATOR", 96, 194);
}

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  setGain();
  Serial3.println("D");
  mylcd.Init_LCD();
  mylcd.Set_Rotation(3); 
  showSplashScreen();
  delay(3000);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Set_Text_Size(2); 

  my_touch.TP_Set_Rotation(3);
  my_touch.TP_Init(mylcd.Get_Rotation(),mylcd.Get_Display_Width(),mylcd.Get_Display_Height()); 
  // The controller's interrupt output is active-low. Keep it at a defined
  // level when the line is idle, even if the display board's pull-up is weak.
  pinMode(TouchIrqPin, INPUT_PULLUP);

  pinMode(ATT1, OUTPUT);
  pinMode(ATT2, OUTPUT);
  pinMode(ATT3, OUTPUT);
  pinMode(ATT4, OUTPUT);
  pinMode(ATT5, OUTPUT);
  pinMode(ATT6, OUTPUT);

  resetATTPins();
  resetATT();

  //checkATT();

  // Keep bare mechanical encoder contacts at a defined level when open.
  pinMode(EncoderPinCLK, INPUT_PULLUP);
  pinMode(EncoderPinDT, INPUT_PULLUP);
  pinMode(EncoderPinSW, INPUT_PULLUP);

  waitForModule();
  Serial3.println("D");
  setGain();
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "F%lu", (unsigned long)frequency);
    Serial3.println(buf);
  }

  // Attach the routine to service the interrupts
  attachInterrupt(digitalPinToInterrupt(EncoderPinCLK), isr, CHANGE);
  show_frequency();
  Serial.println("Start");
}

void loop() {
  // Debounce the encoder switch and act once on a stable press edge.
  static bool swStable = HIGH;
  static bool swLastReading = HIGH;
  static unsigned long swLastChangeTime = 0;
  unsigned long now = millis();
  bool swReading = digitalRead(EncoderPinSW);

  if (swReading != swLastReading) {
    swLastReading = swReading;
    swLastChangeTime = now;
  }

  if (swReading != swStable &&
      now - swLastChangeTime >= ENCODER_BUTTON_DEBOUNCE_MS) {
    swStable = swReading;
    if (swStable == LOW) {
      if ((!sweepMode && genMenu == 1) ||
          (sweepMode && (sweepMenu == 1 || sweepMenu == 2))) {
        cycleFrequencyDigit();
        return;
      }
      noInterrupts();
      int amplitudeBeingApplied = amplitude;
      interrupts();
      updateAmplitudeSettings(amplitudeBeingApplied);
      changeATTValue();  // commits the attenuator and repaints the ATT tile
      setGain();
      appliedAmplitude = amplitudeBeingApplied;
      changeAmplitude(); // refresh the amplitude tile
    }
  }

  // handle UI refreshes requested by the ISR (LCD/Serial done here, not in the ISR)
  if (uiOutputChanged)     { uiOutputChanged = false;     changeOutput(); }
  if (uiAmplitudeChanged)  {
    uiAmplitudeChanged = false;
    amplitudeLastEditTime = now;
    changeATT();
    changeAmplitude();
  }
  if (uiSweepStartChanged) { uiSweepStartChanged = false; changeStart(); }
  if (uiSweepStopChanged)  { uiSweepStopChanged = false;  changeStop(); }
  if (uiSweepTimeChanged)  { uiSweepTimeChanged = false;  changeTime(); }

  serviceAmplitudeBlink(now);

  // process generator
  if (!sweepMode) {
    if (frequency != last_frequency) {
      Serial.print(frequency > last_frequency ? "Up  :" : "Down:");
      Serial.println(format_frequency(frequency));
      changeGenFrequency();
      //set_bpf(frequency);
      char buf[24];
      snprintf(buf, sizeof(buf), "F%lu", (unsigned long)frequency);
      Serial3.println(buf);
      last_frequency = frequency;
    }
  } else {
    // process sweep
    if ((sweepStartFrequency != sweepLastStartFrequency) || (sweepStopFrequency != sweepLastStopFrequency) || (sweepTime != sweepLastTime)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "S%lu:%lu:%d", (unsigned long)sweepStartFrequency, (unsigned long)sweepStopFrequency, sweepTime);
      Serial3.println(buf);
      sweepLastStartFrequency = sweepStartFrequency;
      sweepLastStopFrequency = sweepStopFrequency;
      sweepLastTime = sweepTime;
    }
  }
  // scan touch
  // TP_Scan leaves the previous coordinates intact when ADC samples disagree.
  // Poison them before every scan so touchProcess can reject such a sample.
  my_touch.x = 0xffff;
  my_touch.y = 0xffff;
  my_touch.TP_Scan(0);
  touchProcess();
}
