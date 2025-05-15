#include <SPI.h>
#include <Wire.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <LCDWIKI_TOUCH.h> 
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

// LNA pin
const byte LNA_PIN = 11;
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

unsigned long last_frequency = 14000000;
unsigned long frequency_step = 100000;

// Rotary encoder
const int EncoderPinCLK = 2; 
const int EncoderPinDT = 12;  
const int EncoderPinSW = 13;  

// Updated by the ISR (Interrupt Service Routine)
unsigned volatile long frequency = 14000000;

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
int realAmplitude = 0;
int amplitude = 0;
int minAmplitude = -154;
int maxAmplitude = 17;
int minAmplitudeSweep = -70;
int maxAmplitudeSweep = 10;
unsigned int attValue = 0;
unsigned int lastAttValue = 0;

// LNA
bool lnaEnabled = false;

// Killswitch :)
bool outputEnabled = false;

//touch related
bool wasPressed = false;

//amplitude step
unsigned int ampStep = 1;

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
              amplitude = amplitude - ampStep;
            }  else {
              amplitude = minAmplitude;
            }
          }
          else {
            if (amplitude < maxAmplitude) {
              amplitude = amplitude + ampStep;
            } else {
              amplitude = maxAmplitude;
            }
          }
          changeATT();
          changeAmplitude();
          break;
        case 3:
//          changeLNA();
          break;
        case 4:
          if (digitalRead(EncoderPinDT) == LOW) {
            showATT();
          } else { showATT(); }
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
            if (amplitude > minAmplitudeSweep) {
              amplitude = amplitude - 10;
            }  else {
              amplitude = minAmplitudeSweep;
            }
          }
          else {
            if (amplitude < maxAmplitudeSweep) {
              amplitude = amplitude + 10;
            } else {
              amplitude = maxAmplitudeSweep;
            }
          }
          changeATT();
          changeAmplitude();
          break;
        case 5:
//          changeLNA();
          break;
        case 6:
          if (digitalRead(EncoderPinDT) == LOW) {
            showATT();
          } else { showATT(); }
          break;
      }
      lastInterruptTime = interruptTime; 
    }
  }
}

void setGain() {
  Serial3.println("P"+String(realAmplitude));
}

void changeAmplitude() {
  mylcd.Fill_Rect(140, 175, 100, 25, BACKGROUND_COLOR);
  mylcd.Set_Text_colour(RED);
  mylcd.Print_String(String(amplitude) + " dbm", FRAME_SPACING + 140, 175);
  mylcd.Set_Text_colour(TEXT_COLOR);
  if (amplitude > 0) {
    lnaEnabled = 0;
    changeLNA();
    realAmplitude = realAmplitude-ampGain();
  } else {
    lnaEnabled = 1;
    changeLNA();
  }
}

int ampGain(){
  //this function is to return amp gain based on frequency (with multiple LNAs)
  //now only for one
  return 18;
}

void show_frequency() {
  Serial3.println("F"+String(frequency));
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Print_String("GENERATOR MODE", 150, FRAME_SPACING);
  mylcd.Print_String("Status:", FRAME_SPACING, 50);
  if (outputEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 50); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 50); }
  mylcd.Print_String("Frequency:", FRAME_SPACING, 100);
  mylcd.Print_String(format_frequency(frequency), FRAME_SPACING + 140, 100);
  mylcd.Print_String("Amplitude: ", FRAME_SPACING, 175);
  mylcd.Print_String(String(amplitude) + " dbm", FRAME_SPACING + 140, 175);
  mylcd.Print_String("LNA: ", FRAME_SPACING, 200);
  if (lnaEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 200); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 200); }
  mylcd.Print_String("ATT: ", FRAME_SPACING, 225);
  mylcd.Print_String(String(attValue) + " db", FRAME_SPACING + 140, 225);
  mylcd.Print_String("Freq. step:   <<<<", FRAME_SPACING, 290);
  mylcd.Print_String(format_frequency(frequency_step), 240 + FRAME_SPACING, 290);
  mylcd.Print_String(">>>>", FRAME_SPACING + 420, 290);
  mylcd.Set_Text_colour(ARROW_COLOR);
  mylcd.Print_String(ARROW, 350, 50);
  mylcd.Set_Text_colour(TEXT_COLOR);
  genMenu = 0;
}

void showATT() {
  mylcd.Fill_Rect(140, 225, 100, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(attValue) + " db", 140, 225);
}

void show_sweep() {
  Serial3.println("S"+String(sweepStartFrequency)+":"+String(sweepStopFrequency)+":"+String(sweepTime));
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Print_String("SWEEP MODE", 170, FRAME_SPACING);
  mylcd.Print_String("Status:", FRAME_SPACING, 50);
  if (outputEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 50); } else { mylcd.Print_String("Disabled", FRAME_SPACING + 140, 50); }
  mylcd.Print_String("Start: ", FRAME_SPACING, 75);
  mylcd.Print_String(format_frequency(sweepStartFrequency), FRAME_SPACING + 140, 75);
  mylcd.Print_String("Stop: ", FRAME_SPACING, 100);
  mylcd.Print_String(format_frequency(sweepStopFrequency), FRAME_SPACING + 140, 100);
  mylcd.Print_String("Time: ", FRAME_SPACING, 125);
  mylcd.Print_String(String(sweepTime), FRAME_SPACING + 140, 125);

  mylcd.Print_String("Amplitude: ", FRAME_SPACING, 175);
  mylcd.Print_String(String(amplitude) + " dbm", FRAME_SPACING + 140, 175);
  mylcd.Print_String("LNA: ", FRAME_SPACING, 200);
  if (lnaEnabled) { mylcd.Print_String("Enabled", FRAME_SPACING + 140, 200); } else {
    mylcd.Print_String("Disabled", FRAME_SPACING + 140, 200);
  }
  mylcd.Print_String("ATT: ", FRAME_SPACING, 225);
  mylcd.Print_String(String(attValue) + " db", FRAME_SPACING + 140, 225);
  mylcd.Print_String("Freq. step:   <<<<", FRAME_SPACING, 290);
  mylcd.Print_String(format_frequency(frequency_step), FRAME_SPACING + 250, 290);
  mylcd.Print_String(">>>>", FRAME_SPACING + 420, 290);

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

void changeTime() {
  mylcd.Fill_Rect(140, 125, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(String(sweepTime), 140, 125);
}

void changeGenFrequency() {
  mylcd.Fill_Rect(140, 100, 200, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(frequency), 140, 100);
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
      mylcd.Print_String(ARROW, 350, 100);
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
  mylcd.Fill_Rect(240, 290, 182, 25, BACKGROUND_COLOR);
  mylcd.Print_String(format_frequency(frequency_step), FRAME_SPACING + 240, 290);
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

void touchProcess() {
  if ((my_touch.TP_Get_State() & TP_PRES_DOWN) && !wasPressed) {
    wasPressed = true;
  }
  else if (!(my_touch.TP_Get_State() & TP_PRES_DOWN) && wasPressed) {
    wasPressed = false;
    //mode
    if ((my_touch.y<=25) && (my_touch.y>=0)) {
      if (!sweepMode) { 
        Serial.println("Sweep mode");
        sweepMode = true;
        show_sweep();
      } else {
        sweepMode = false;
        Serial.println("Generator mode");
        show_frequency();
      }
    }
    if (sweepMode) {
        //status
        if ((my_touch.y<=50) && (my_touch.y>=25)) { 
          sweepMenu=0; 
          sweepChangeMenu();
        }
        //start freq.
        if ((my_touch.y<=75) && (my_touch.y>=50)) { 
          sweepMenu=1; 
          sweepChangeMenu();
        }
        //stop freq.
        if ((my_touch.y<=100) && (my_touch.y>=75)) { 
          sweepMenu=2; 
          sweepChangeMenu();
        }
        //time
        if ((my_touch.y<=125) && (my_touch.y>=100)) { 
          sweepMenu=3; 
          sweepChangeMenu();
        }
        //amplitude
        if ((my_touch.y<=250) && (my_touch.y>=126)) { 
          sweepMenu=4; 
          sweepChangeMenu();
        }
    } else {
        //status
        if ((my_touch.y<=74) && (my_touch.y>=25)) { 
          genMenu=0; 
          genChangeMenu();
        }
        //freq.
        if ((my_touch.y<=124) && (my_touch.y>=76)) { 
          genMenu=1; 
          genChangeMenu();
        }
        //amplitude
        if ((my_touch.y<=250) && (my_touch.y>=126)) { 
          genMenu=2; 
          genChangeMenu();
        }
    }
    //freq. step
    if ((my_touch.y<=300) && (my_touch.y>=251)) {
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
}

void changeATT() {
  if (amplitude > -10) {
    realAmplitude = amplitude;
    attValue = 0;
  } else if ((amplitude <= -10) && (amplitude > -20)) {
    attValue = 10;
    realAmplitude = amplitude + 10;
  } else if ((amplitude <= -20) && (amplitude > -30)) {
    attValue = 20;
    realAmplitude = amplitude + 20;
  } else if ((amplitude <= -30) && (amplitude > -40)) {
    attValue = 30;
    realAmplitude = amplitude + 30;
  } else if ((amplitude <= -40) && (amplitude > -50)) {
    attValue = 40;
    realAmplitude = amplitude + 40;
  } else if ((amplitude <= -50) && (amplitude > -60)) {
    attValue = 50;
    realAmplitude = amplitude + 50;
  } else if ((amplitude <= -60) && (amplitude > -70)) {
    attValue = 60;
    realAmplitude = amplitude + 60;
  } else if (amplitude <= -70) {
    attValue = 70;
    realAmplitude = amplitude + 70;
  }
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
  Serial3.println("F"+String(15000000));
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

void setup() {
  Serial.begin(9600);
  Serial3.begin(115200);
  Serial3.println("P"+String(realAmplitude));
  Serial3.println("D");
  mylcd.Init_LCD();
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Set_Rotation(3); 
  mylcd.Set_Text_Mode(1);
  mylcd.Set_Text_colour(BLACK);
  mylcd.Set_Text_Back_colour(BACKGROUND_COLOR);
  mylcd.Set_Text_Size(4);
  mylcd.Print_String("Signal Generator", 50, 80);
  mylcd.Set_Text_Size(3);
  mylcd.Print_String("---------------------", 50, 110);
  mylcd.Print_String("GRA & AFCH AD9910 +", 50, 150);
  mylcd.Print_String("Agilent ATT 10-70db", 50, 190);
  delay(3000);
  mylcd.Set_Text_colour(TEXT_COLOR);
  mylcd.Fill_Screen(BACKGROUND_COLOR);
  mylcd.Set_Text_Size(2); 

  my_touch.TP_Set_Rotation(3);
  my_touch.TP_Init(mylcd.Get_Rotation(),mylcd.Get_Display_Width(),mylcd.Get_Display_Height()); 

  //digitalWrite(LNA_PIN, lnaEnabled);
  pinMode(ATT1, OUTPUT);
  pinMode(ATT2, OUTPUT);
  pinMode(ATT3, OUTPUT);
  pinMode(ATT4, OUTPUT);
  pinMode(ATT5, OUTPUT);
  pinMode(ATT6, OUTPUT);

  resetATTPins();
  resetATT();

  //checkATT();

  // Rotary pulses are INPUTs
  pinMode(EncoderPinCLK, INPUT);
  pinMode(EncoderPinDT, INPUT);
  pinMode(EncoderPinSW, INPUT_PULLUP);

  Serial3.println("F"+String(frequency));

  // Attach the routine to service the interrupts
  attachInterrupt(digitalPinToInterrupt(EncoderPinCLK), isr, LOW);
  show_frequency();
  Serial.println("Start");
}

void loop() {
  //Encoder pressed
  if (!digitalRead(EncoderPinSW)) {
      changeATTValue();
      setGain();
      mylcd.Fill_Rect(140, 175, 100, 25, BACKGROUND_COLOR);
      mylcd.Print_String(String(amplitude) + " dbm", FRAME_SPACING + 140, 175);
  }
 
  // process generator 
  if (!sweepMode) {
    if (frequency != last_frequency) {
      Serial.print(frequency > last_frequency ? "Up  :" : "Down:");
      Serial.println(format_frequency(frequency));
      changeGenFrequency();
      //set_bpf(frequency);
      Serial3.println("F"+String(frequency));
      last_frequency = frequency;
    }
  } else {
    // process sweep
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
