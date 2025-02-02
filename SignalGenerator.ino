#include <SPI.h>
#include <Wire.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <LCDWIKI_TOUCH.h> 
#include <PE43xx.h>
#include <stdio.h>
#include <string.h>

LCDWIKI_KBV mylcd(ILI9488,40,38,39,43,41);
LCDWIKI_TOUCH my_touch(53,52,50,51,44);

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

#define TEXT_COLOR 0x0000
#define BACKGROUND_COLOR 0x6C64
#define FRAME_SPACING 3
#define ARROW "<==="
#define ARROW_COLOR 0xA145

// Filter pins
const byte BCD1 = 6;
const byte BCD2 = 5;
const byte BCD3 = 4;
const byte BCD4 = 3;

// LNA pin
#define LNA_PIN 11

// attenuator
#define ATTENUATOR_PIN_LE 16
#define ATTENUATOR_PIN_CLK 15
#define ATTENUATOR_PIN_DATA 14
PE43xx attenuator(ATTENUATOR_PIN_LE, ATTENUATOR_PIN_CLK, ATTENUATOR_PIN_DATA, PE4302);

void set_bpf(unsigned volatile long frequency) {
  if ((frequency >= 1800000) && (frequency <= 2000000)) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("BPF 160m"); }
  if ((frequency >= 3500000) && (frequency <= 3800000)) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("BPF 80m"); }
  if ((frequency >= 7000000) && (frequency <= 7100000)) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("BPF 40m"); }
  if ((frequency >= 10100000) && (frequency <= 10150000)) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, LOW); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("BPF 30m"); }
  if ((frequency >= 14000000) && (frequency <= 14350000)) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("BPF 20m"); }
  if ((frequency >= 18068000) && (frequency <= 18168000)) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("BPF 17m"); }
  if ((frequency >= 21000000) && (frequency <= 21450000)) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("BPF 15m"); }
  if ((frequency >= 24890000) && (frequency <= 24990000)) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, HIGH); Serial.println("BPF 12m"); }
  if ((frequency >= 28000000) && (frequency <= 29700000)) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, HIGH); Serial.println("BPF 10m"); }
  updateBCD();
}

void set_lpf(unsigned volatile long frequency) {
  if (frequency <= 2000000) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("LPF 160m"); }
  if (frequency <= 3800000) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("LPF 80m"); }
  if (frequency <= 7100000) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("LPF 40m"); }
  if (frequency <= 10150000) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, LOW); digitalWrite(BCD4, LOW); Serial.println("LPF 30m"); }
  if (frequency <= 14350000) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, LOW); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("LPF 20m"); }
  if (frequency <= 18168000) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("LPF 17m"); }
  if (frequency <= 21450000) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("LPF 15m"); }
  if (frequency <= 24990000) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, HIGH); digitalWrite(BCD3, HIGH); digitalWrite(BCD4, LOW); Serial.println("LPF 12m"); }
  if (frequency <= 29700000) { digitalWrite(BCD1, LOW); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, HIGH); Serial.println("LPF 10m"); }
  if (frequency <= 50000000) { digitalWrite(BCD1, HIGH); digitalWrite(BCD2, LOW); digitalWrite(BCD3, LOW); digitalWrite(BCD4, HIGH); Serial.println("LPF 6m"); }
  updateBCD();
}

const unsigned long max_frequency_step = 100000000; //Max Frequency step
const unsigned int min_frequency_step = 1;
const unsigned long max_frequency = 500000000; //Max Frequency
const int min_frequency=25; // Minimum Frequency

unsigned long last_frequency = 14000000;
unsigned long frequency_step = 100000;

// Rotary encoder
const int EncoderPinCLK = 2; 
const int EncoderPinDT = 12;  
const int EncoderPinSW = 13;  

// Updated by the ISR (Interrupt Service Routine)
unsigned volatile long frequency = 14000000;

// Double click stuff
unsigned long lastPressTime = 0;
const int doubleClickInterval = 300;
bool waitingForSecondClick = false;

// Long click stuff
const int longPressDuration = 500;
const int delayAfterLongClick = 1000;
bool longPressDetected = false;
unsigned long longClickTime = 0;

// Sweep mode
bool sweepMode = false;
unsigned volatile long sweepStartFrequency =  1000000;
unsigned volatile long sweepStopFrequency =  2000000;
unsigned volatile long sweepCurrentFrequency =  1000001;

unsigned volatile long sweepLastStartFrequency = 1000000;
unsigned volatile long sweepLastStopFrequency = 2000000;

unsigned int sweepStep = 1;
int sweepTime = 10;
int sweepLastTime = 10;

unsigned int sweepPoints = 10000;
bool sweepModeStart = true;
bool sweepModeStop = false;
bool sweepModeStep = false;
// sweepMenu is to determine which parameter we are changing in sweep mode
// 0 - Output enabled
// 1 - Start freq.
// 2 - Stop freq.
// 3 - Sweep time
// 4 - Amplitude
// 5 - ATT
// 6 - LNA
short sweepMenu = 1;

// genMenu - same as sweepmenu
// 0 - Output enabled
// 1 - Freq.
// 2 - Amplitude
// 3 - ATT
// 4 - LNA
short genMenu = 1;

// Amplitude
int amplitude = 0;
int minAmplitude = -72;
int maxAmplitude = 17;

// ATT value
float attLevel = 0;
float attStep = 0.5;

// LNA
bool lnaEnabled = false;

// Killswitch :)
bool outputEnabled = false;

void isr ()  {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime > 5) {
    if (!sweepMode) {
      switch (genMenu) {
      case 0:
        if (digitalRead(EncoderPinDT) == LOW)
        {
          outputEnabled = !outputEnabled;
        }
        else {
          outputEnabled = !outputEnabled;
        }
        changeOutput();
        break;
      case 1:
        if (digitalRead(EncoderPinDT) == LOW)
        {
          frequency=frequency-frequency_step;
        }
        else {
          frequency=frequency+frequency_step;
        }
        frequency = min(max_frequency, max(min_frequency, frequency));
        break;
        case 2:
          if (digitalRead(EncoderPinDT) == LOW) {
            if (amplitude > minAmplitude) {
              amplitude = amplitude - 1;
            }  else {
              amplitude = minAmplitude;
            }
          }
          else {
            if (amplitude < maxAmplitude) {
              amplitude = amplitude + 1;
            } else {
              amplitude = maxAmplitude;
            }
          }
          changeAmplitude();
          break;
        case 3:
          changeLNA();
          break;
        case 4:
          if (digitalRead(EncoderPinDT) == LOW) {
            setATT(false);
          } else { setATT(true); }
          break;
      }
      lastInterruptTime = interruptTime;
    } else { 
      switch (sweepMenu) {
        case 0:
          if (digitalRead(EncoderPinDT) == LOW)
          {
            outputEnabled = !outputEnabled;
          }
          else {
            outputEnabled = !outputEnabled;
          }
          changeOutput();
          break;
        case 1:
          if (digitalRead(EncoderPinDT) == LOW) {
            sweepStartFrequency = sweepStartFrequency - frequency_step;
          } else {
            sweepStartFrequency = sweepStartFrequency + frequency_step;
          }
          sweepStartFrequency = min(max_frequency, max(min_frequency, sweepStartFrequency));
          if (sweepStartFrequency > sweepStopFrequency) { sweepStopFrequency = sweepStartFrequency; changeStop(); }
          sweepCurrentFrequency = sweepStartFrequency;
          Serial.println("Sweep start freq.:");
          Serial.println(format_frequency(sweepStartFrequency));
          changeStart();
          break;
        case 2:
          if (digitalRead(EncoderPinDT) == LOW) {
            sweepStopFrequency = sweepStopFrequency - frequency_step;
          } else {
            sweepStopFrequency = sweepStopFrequency + frequency_step;
          }
          sweepStopFrequency = min(max_frequency, max(min_frequency, sweepStopFrequency));
          if (sweepStopFrequency < sweepStartFrequency) { sweepStartFrequency = sweepStopFrequency; changeStart(); }
          sweepCurrentFrequency = sweepStartFrequency;
          Serial.println("Sweep stop freq.:");
          Serial.println(format_frequency(sweepStopFrequency));
          Serial.println(sweepStep);
          changeStop();
          break;
        case 3:
          if (digitalRead(EncoderPinDT) == LOW) {
            if (sweepTime >= 2) sweepTime = sweepTime - 1;
          } else {
            if (sweepTime <= 99) sweepTime = sweepTime + 1;
          }
          changeTime();
          break;
        case 4:
          if (digitalRead(EncoderPinDT) == LOW) {
            if (amplitude > minAmplitude) {
              amplitude = amplitude - 1;
            }  else {
              amplitude = minAmplitude;
            }
          }
          else {
            if (amplitude < maxAmplitude) {
              amplitude = amplitude + 1;
            } else {
              amplitude = maxAmplitude;
            }
          }
          changeAmplitude();
          break;
        case 5:
          changeLNA();
          break;
        case 6:
          if (digitalRead(EncoderPinDT) == LOW) {
            setATT(false);
          } else { setATT(true); }
          changeATT();
          break;
      }
      lastInterruptTime = interruptTime; 
    }
  }
}

void changeATT() {}

void changeAmplitude() {
  mylcd.Fill_Rect(140, 175, 100, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(amplitude) + " db", FRAME_SPACING + 140, 175);
  if (amplitude > 0) {
    lnaEnabled = 0;
    changeLNA();
    Serial3.println("P"+String(amplitude-ampGain()));
  } else {
    lnaEnabled = 1;
    changeLNA();
    Serial3.println("P"+String(amplitude));
  }
}

int ampGain(){
  return 18;
}


void show_frequency() {
  Serial3.println("F"+String(frequency));
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Print_String("Generating", 170, FRAME_SPACING);
  mylcd.Print_String("Status:", FRAME_SPACING, 50);
  if (outputEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 50); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 50); }
  mylcd.Print_String("Frequency:", FRAME_SPACING, 75);
  mylcd.Print_String(format_frequency(frequency), FRAME_SPACING + 140, 75);
  mylcd.Print_String("Amplitude: ", FRAME_SPACING, 175);
  mylcd.Print_String(String(amplitude) + " db", FRAME_SPACING + 140, 175);
  mylcd.Print_String("LNA: ", FRAME_SPACING, 200);
  if (lnaEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 200); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 200); }
  mylcd.Print_String("ATT: ", FRAME_SPACING, 225);
  mylcd.Print_String(String(attLevel) + " db", FRAME_SPACING + 140, 225);
  mylcd.Print_String("Change step:   <<<<", FRAME_SPACING, 275);
  mylcd.Print_String(format_frequency(frequency_step), 240 + FRAME_SPACING, 275);
  mylcd.Print_String(">>>>", FRAME_SPACING + 420, 275);
  mylcd.Print_String("BCD State: ", FRAME_SPACING, 100);
  mylcd.Print_String(String(digitalRead(BCD1)) + " " + String(digitalRead(BCD2)) + " " + String(digitalRead(BCD3)) + " " + String(digitalRead(BCD4)), 140, 100);

  mylcd.Set_Text_colour(ARROW_COLOR);
  mylcd.Print_String(ARROW, 350, 50);
  mylcd.Set_Text_colour(TEXT_COLOR);
  genMenu = 0;

}

void show_sweep() {
  Serial3.println("S"+String(sweepStartFrequency)+":"+String(sweepStopFrequency)+":"+String(sweepTime));
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Print_String("Sweeping", 170, FRAME_SPACING);
  mylcd.Print_String("Status:", FRAME_SPACING, 50);
  if (outputEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 50); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 50); }
  mylcd.Print_String("Start: ", FRAME_SPACING, 75);
  mylcd.Print_String(format_frequency(sweepStartFrequency), FRAME_SPACING + 140, 75);
  mylcd.Print_String("Stop: ", FRAME_SPACING, 100);
  mylcd.Print_String(format_frequency(sweepStopFrequency), FRAME_SPACING + 140, 100);
  mylcd.Print_String("Time: ", FRAME_SPACING, 125);
  mylcd.Print_String(String(sweepTime), FRAME_SPACING + 140, 125);

  mylcd.Print_String("Amplitude: ", FRAME_SPACING, 175);
  mylcd.Print_String(String(amplitude) + " db", FRAME_SPACING + 140, 175);
  mylcd.Print_String("LNA: ", FRAME_SPACING, 200);
  if (lnaEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 200); } else {
    mylcd.Print_String("Disabled", FRAME_SPACING + 140, 200);
  }
  mylcd.Print_String("ATT: ", FRAME_SPACING, 225);
  mylcd.Print_String(String(attLevel) + " db", FRAME_SPACING + 140, 225);
  mylcd.Print_String("Change step:   <<<<", FRAME_SPACING, 275);
  mylcd.Print_String(format_frequency(frequency_step), FRAME_SPACING + 250, 275);
  mylcd.Print_String(">>>>", FRAME_SPACING + 420, 275);

  mylcd.Set_Text_colour(ARROW_COLOR);
  mylcd.Print_String(ARROW, 350, 50);
  mylcd.Set_Text_colour(TEXT_COLOR);

  sweepMenu = 0;
}

void changeStart() {
  mylcd.Fill_Rect(140, 75, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(sweepStartFrequency), 140, 75);
}

void changeStop() {
  mylcd.Fill_Rect(140, 100, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(sweepStopFrequency), 140, 100);
}

void changePoints() {
  mylcd.Fill_Rect(140, 125, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(sweepPoints), 140, 125);
}

void changeTime() {
  mylcd.Fill_Rect(140, 125, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(sweepTime), 140, 125);
}

void changeGenFrequency() {
  mylcd.Fill_Rect(140, 75, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(frequency), 140, 75);
}

void clearArrow() {
  mylcd.Set_Text_colour(ARROW_COLOR);
  mylcd.Fill_Rect(350, 25, 50, 225, BACKGROUND_COLOR);
}

void sweepChangeMenu() {
  switch(sweepMenu) {
    case 0:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 50);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 1:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 75);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 2:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 100);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 3:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 125);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 4:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 175);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 5:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 200);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 6:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 225);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break; 
  }
}

void genChangeMenu() {
  switch(genMenu) {
    case 0:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 50);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 1:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 75);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 2:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 175);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 3:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 200);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
    case 4:
      clearArrow();
      mylcd.Print_String(ARROW, 350, 225);
      mylcd.Set_Text_colour(TEXT_COLOR);
      break;
  }
}

void changeStep() {
  mylcd.Fill_Rect(240, 275, 182, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(frequency_step), FRAME_SPACING + 240, 275);
}

void changeLNA() {
  lnaEnabled = !lnaEnabled;
  mylcd.Fill_Rect(140, 200, 120, 25, BACKGROUND_COLOR);
  mylcd.Print_String("LNA: ", FRAME_SPACING, 200);
  if (lnaEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 200); } else {
    mylcd.Print_String("Disabled", FRAME_SPACING + 140, 200);
  }
  digitalWrite(LNA_PIN, lnaEnabled);
}

void changeOutput() {
  mylcd.Fill_Rect(140, 50, 150, 25, BACKGROUND_COLOR);
  if (outputEnabled) { 
    Serial3.println("E");
    mylcd.Print_String("Enabled", FRAME_SPACING + 140, 50); 
  } else { 
    Serial3.println("D");
    mylcd.Print_String("Disabled", FRAME_SPACING + 140, 50); 
  }
}

void updateBCD() {
  mylcd.Fill_Rect(140, 100, 120, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(digitalRead(BCD1)) + " " + String(digitalRead(BCD2)) + " " + String(digitalRead(BCD3)) + " " + String(digitalRead(BCD4)), 140, 100);
}

void touchProcess() {
  if (my_touch.TP_Get_State()&TP_PRES_DOWN) 
  {
    delay(200);
    if (sweepMode) {
        if ((my_touch.y<=50) && (my_touch.y>=25)) { 
          sweepMenu=0; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=75) && (my_touch.y>=50)) { 
          sweepMenu=1; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=100) && (my_touch.y>=75)) { 
          sweepMenu=2; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=125) && (my_touch.y>=100)) { 
          sweepMenu=3; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=175) && (my_touch.y>=150)) { 
          sweepMenu=4; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=200) && (my_touch.y>=175)) { 
          sweepMenu=5; 
          sweepChangeMenu();
        }
        if ((my_touch.y<=225) && (my_touch.y>=200)) { 
          sweepMenu=6; 
          sweepChangeMenu();
        }
    } else {
        if ((my_touch.y<=50) && (my_touch.y>=25)) { 
          genMenu=0; 
          genChangeMenu();
        }
        if ((my_touch.y<=75) && (my_touch.y>=50)) { 
          genMenu=1; 
          genChangeMenu();
        }
        if ((my_touch.y<=175) && (my_touch.y>=150)) { 
          genMenu=2; 
          genChangeMenu();
        }
        if ((my_touch.y<=200) && (my_touch.y>=175)) { 
          genMenu=3; 
          genChangeMenu();
        }
        if ((my_touch.y<=225) && (my_touch.y>=200)) { 
          genMenu=4; 
          genChangeMenu();
        }
    }
    
    if ((my_touch.y<=300) && (my_touch.y>=275)) {
      if  ((my_touch.x<=250) && (my_touch.x>=100)) {
        if (frequency_step == min_frequency_step) {
          frequency_step = max_frequency_step;
        } else {
          frequency_step /= 10;
        }
        
        Serial.print("Multiplier:");
        Serial.println(frequency_step);
        changeStep();

        } if (my_touch.x>=420) {
          if (frequency_step == max_frequency_step) {
            frequency_step = min_frequency_step;
            } else {
              frequency_step *= 10;
            }
            Serial.print("Multiplier:");
            Serial.println(frequency_step);
            changeStep();
          }
      }
    }
    if ((my_touch.y<=50) && (my_touch.y>=0)) {
      if (sweepMode) {
        sweepMode = false;
        delay(200);
        show_frequency();
      } 
      else {
        sweepMode = true;
        delay(200);
        show_sweep();
      }
  }
}

void setATT(bool isIncrementing) {
  if (isIncrementing) { attLevel = attLevel + attStep; } else { attLevel = attLevel - attStep; } 
  if (attLevel > attenuator.getMax())
  {
    attLevel = attenuator.getMax();
  }
  if (attLevel < 0)
  {
    attLevel = 0;
  }
  attenuator.setLevel(attLevel);
  mylcd.Fill_Rect(140, 225, 100, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(attLevel) + " db", 140, 225);
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

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);

  mylcd.Init_LCD();
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Set_Rotation(3); 
  mylcd.Set_Text_Mode(1);
  mylcd.Set_Text_colour(BLACK);
  mylcd.Set_Text_Back_colour(BACKGROUND_COLOR);
  mylcd.Set_Text_Size(7);
  mylcd.Print_String("SIGEN", 140, 80);
  mylcd.Set_Text_Size(5);
  mylcd.Set_Text_colour(RED);
  mylcd.Print_String("AD9910", 160, 150);
  delay(5000);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Set_Text_Size(2); 

  my_touch.TP_Set_Rotation(3);
  my_touch.TP_Init(mylcd.Get_Rotation(),mylcd.Get_Display_Width(),mylcd.Get_Display_Height()); 

  pinMode(BCD1, OUTPUT);
  pinMode(BCD2, OUTPUT);
  pinMode(BCD3, OUTPUT);
  pinMode(BCD4, OUTPUT);
  pinMode(LNA_PIN, OUTPUT);
  digitalWrite(BCD1, LOW); 
  digitalWrite(BCD2, LOW); 
  digitalWrite(BCD3, LOW); 
  digitalWrite(BCD4, LOW);
  digitalWrite(LNA_PIN, lnaEnabled);

  // Rotary pulses are INPUTs
  pinMode(EncoderPinCLK, INPUT);
  pinMode(EncoderPinDT, INPUT);
  pinMode(EncoderPinSW, INPUT_PULLUP);

  Serial3.println("F"+String(frequency));
  attenuator.begin();

  // Attach the routine to service the interrupts
  attachInterrupt(digitalPinToInterrupt(EncoderPinCLK), isr, LOW);
 
  show_frequency();
  set_bpf(frequency);
  Serial.println("Start");
}

void loop() {
  //Encoder pressed
  if (!digitalRead(EncoderPinSW)) {
    unsigned long currentTime = millis();

    // reset long press
    if (currentTime - longClickTime > delayAfterLongClick) {
      longPressDetected = false;
      Serial.println("Long press = false");
    }
    
    // long press handle
    while (!digitalRead(EncoderPinSW)) {
      if (millis() - currentTime >= longPressDuration) {
        longClickTime = millis();
        longPressDetected = true;
        Serial.println("Long press = true");
        if (sweepMode) {
          if (sweepMenu !=6) sweepMenu++; else sweepMenu = 0;
            Serial.println("Sweep menu item:");
            Serial.println(sweepMenu);
            waitingForSecondClick = false;
            sweepChangeMenu();
            delay(200);
        } else {
          if (genMenu !=4) genMenu++; else genMenu = 0;
          Serial.println("Gen menu item:");
          Serial.println(genMenu);
          waitingForSecondClick = false;
          genChangeMenu();
          delay(200);
        }
      }
    }

    // double click
    if (waitingForSecondClick && (currentTime - lastPressTime <= doubleClickInterval)) {
      if (!sweepMode) { 
        Serial.println("Sweep mode");
        show_sweep();
        sweepMode = true; 
      } else {
        sweepMode = false;
        Serial.println("Generator mode");
        show_frequency();
      }
      waitingForSecondClick = false;
    } else {
      waitingForSecondClick = true;
      lastPressTime = currentTime;
    }
  }

  // single click
  if (waitingForSecondClick && (millis() - lastPressTime > doubleClickInterval) && !longPressDetected) {
    if (frequency_step == max_frequency_step) {
      frequency_step = 1;
    } else {
      frequency_step *= 10;
    }
    Serial.print("Multiplier:");
    Serial.println(frequency_step);
    changeStep();
    waitingForSecondClick = false;
  }
 
  // process generator 
  if (!sweepMode) {
    if (frequency != last_frequency) {
      Serial.print(frequency > last_frequency ? "Up  :" : "Down:");
      Serial.println(format_frequency(frequency));
      changeGenFrequency();
      set_bpf(frequency);
      Serial3.println("F"+String(frequency));
      last_frequency = frequency;
    }
  } else {
    if ((sweepStartFrequency != sweepLastStartFrequency) || (sweepStopFrequency != sweepLastStopFrequency) || (sweepTime != sweepLastTime)) {
      Serial3.println("S"+String(sweepStartFrequency)+":"+String(sweepStopFrequency)+":"+String(sweepTime));
      sweepLastStartFrequency = sweepStartFrequency;
      sweepLastStopFrequency = sweepStopFrequency;
      sweepLastTime = sweepTime;
    }
  }
  // scan touch
  my_touch.TP_Scan(0);
  touchProcess();
}
