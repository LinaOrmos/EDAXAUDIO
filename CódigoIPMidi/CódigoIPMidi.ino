#include <MIDI.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#define CLASSIC    0
#define VELOCITY   1
#define SCROLLING  2
#define AUTOMATIC  3

#define ANALOG_SWITCH_DEBOUNCE_COUNT 100
#define TIMER_MAX     122

void processUDP(void);
void updateBulbs(void);
void readSwitch(void);
void setIntensity(uint8_t bulb, uint8_t val);
void zeroCrossDetect(void);

void HandleNoteOff(uint8_t channel, uint8_t pitch, uint8_t velocity);
void HandleNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity);
void HandleControlChange(uint8_t channel, uint8_t number, uint8_t value);
                              
uint8_t pwmPins[12] = {2,3,5,6,7,8,11,12,13,44,45,46};
uint8_t pwmValues[12];
uint8_t algorithmIntensities[12];

volatile uint8_t bulbStates[12];
volatile unsigned long  bulbStartTime[12];
volatile uint8_t pedalState = 0;
volatile uint8_t pedalBucket[12];
volatile int mode = SCROLLING;
volatile int scrollPos = 0;
volatile int switchStates[3] = {-1,-1,-1};
volatile int switchStatesDebounceCounter[3];
volatile int bank = 0;
volatile int pgm = 0;

uint8_t mac[] = {0x90,0xA2,0xDA,0x00,0x6C,0x48};
IPAddress myIP(192, 168, 0, 101);
IPAddress multicast(225, 0, 0, 37);
unsigned int destPort = 21928;

int packetSize;
uint8_t messageType;
uint8_t data[2];

EthernetUDP UDP;

uint8_t counter;

void setup() {
  
  ICR1 = ICR3 = ICR4 = ICR5 = TIMER_MAX;
  TCCR1A = (1<<COM1A0) | (1<<COM1A1) | (1<<COM1B0) | (1<<COM1B1) | (1<<COM1C0) | (1<<COM1C1) | (1<<WGM11);
  TCCR3A = (1<<COM3A0) | (1<<COM3A1) | (1<<COM3B0) | (1<<COM3B1) | (1<<COM3C0) | (1<<COM3C1) | (1<<WGM31);
  TCCR4A = (1<<COM4A0) | (1<<COM4A1) | (1<<COM4B0) | (1<<COM4B1) | (1<<COM4C0) | (1<<COM4C1) | (1<<WGM41);
  TCCR5A = (1<<COM5A0) | (1<<COM5A1) | (1<<COM5B0) | (1<<COM5B1) | (1<<COM5C0) | (1<<COM5C1) | (1<<WGM51);


  for(int i=0; i<12; i++) {
    pinMode(pwmPins[i],OUTPUT);
    setIntensity(i,0);
  }

  pinMode(18,INPUT_PULLUP);
  attachInterrupt(5,zeroCrossDetect,FALLING);
  
  randomSeed(analogRead(7));
  
  MIDI.begin(MIDI_CHANNEL_OMNI);

  MIDI.setHandleNoteOn(HandleNoteOn);
  MIDI.setHandleNoteOff(HandleNoteOff);
  MIDI.setHandleControlChange(HandleControlChange);

  Ethernet.begin(mac,myIP);
  UDP.beginMulti(multicast,destPort);
  
  for(int i=0; i<12; i++) {
    delay(50);
    setIntensity(i, 64);
  }
  for(int i=0; i<12; i++) {
    setIntensity(i, 0);
    delay(50);
  }
 
}

void loop() {
  
  
  MIDI.read();
  
  processUDP();
  
  readSwitch();

  updateBulbs();
  
}

void updateBulbs() {
  
  static unsigned long scrollFadeTime;
  
  if( mode == SCROLLING ) {
    
    if( millis() - scrollFadeTime > 14 ) {
      scrollFadeTime = millis();
      for(int i=0; i<12; i++) {
        if( pwmValues[i] > 0 )
          setIntensity(i,pwmValues[i]-1);
      }
    }
    
  }
  else if( mode == AUTOMATIC )
    autoAlgorithm();
  
}

void autoAlgorithm() {
  
  static unsigned long autoFadeTime,autoRandTime;
  static unsigned int autoWaitTime = 1000;
  
  uint8_t randomBulb, randomIntensity;
  
  //Determine if it's time to activate a new bulb
  if( (millis() - autoRandTime) > autoWaitTime ) {
        autoRandTime = millis();
        autoWaitTime = random(1,10)*215;
        
        randomBulb = random(12);
        randomIntensity = random(0,100);
        
        if( randomIntensity < 30)
          randomIntensity = 0;
        
        algorithmIntensities[randomBulb] = randomIntensity;
   }
   
   //Fade in or out the current set of bulbs, if it's time to do that.
   if( (millis() - autoFadeTime) > 75 ) {
     
     autoFadeTime = millis();                    //Reset new timer value, so next cycle comes around somewhat evenly
     
     for(int i=0; i<12; i++) {                   //Go through each bulb, if the bulb_state is on, increment towards it's intensity, if not decrease to zero
        if( pwmValues[i] < algorithmIntensities[i] )
          setIntensity(i,pwmValues[i]+1);
        else if( pwmValues[i] > algorithmIntensities[i] )
          setIntensity(i,pwmValues[i]-1);
     }
     
   }
  
}


void processUDP() {

  packetSize = UDP.parsePacket();

  while(UDP.available() > 0) {
    
    //destIP = UDP.remoteIP();
    
    UDP.read(&messageType,1);
    
    switch( (messageType & 0xF0) ) {
     case 0x80:    //Note Off Message
      UDP.read(data,2);
      HandleNoteOff(messageType&0x0F,data[0],data[1]);
      break;
     case 0x90:    //Note On Message
      UDP.read(data,2);
      HandleNoteOn(messageType&0x0F,data[0],data[1]);
      break;
     case 0xA0:    //Polyphonic Key Press
      UDP.read(data,2);
      break;
     case 0xB0:    //Control Change
      UDP.read(data,2);
      HandleControlChange(messageType&0x0F,data[0],data[1]);
      break;
     case 0xC0:    //Program Change
      UDP.read(data,1);
      //Process byte 2 only (packetBuffer[1]) as program change message, change light bulb mode?)
      break;
     case 0xD0:    //Channel Pressure (after-touch)
      UDP.read(data,1);
      break;
     case 0xE0:    //Pitch Wheel
      UDP.read(data,2);
      break;
     case 0xF0:    //System
     
       switch(messageType) {
        case 0xF0:    //Sysex, hopefully this never gets sent...
        break;
        case 0xF1:    //MIDI Time Code Quarter Frame.
          UDP.read(data,1); 
          break;
        case 0xF2:    //Song Position Pointer. 
          UDP.read(data,2);
          break;
        case 0xF3:    //Song Select
          UDP.read(data,1);
          break;
        case 0xF8:    //Timing Clock
                      //Do something here if you want....
          break;
        default:
          break;
        
       }
       break;
     default:
       UDP.read(data,2);
    }
    
  }
}

//Switches share a common resistor divider network of a total of 9 resistors across 5V
//Switch 0 has possible voltages : analog values : 682, 795, 909, 1023
//Switch 1 has possible voltages : analog values : 339, 454, 569, 682 
//Switch 2 has possible voltages : analog values : 0, 110, 225, 339,
void readSwitch() {
  
  static int n = 0;
  static int val;
  static int newSwitchState;
  static int debounceSwitchStates[3];
  
  static uint8_t message[3];
  
  val = analogRead(n);
  
  switch(n) {
    
   case 0:
     
     if( val < 950) {
       if( val < 850 ) {
        if( val < 725 )
          newSwitchState = 3;
        else
          newSwitchState = 2;
       }
       else
         newSwitchState = 1;
      }
      else
        newSwitchState = 0;
     break;
   
   case 1:
     
     if( val < 625) {
       if( val < 500 ) {
        if( val < 400 )
          newSwitchState = 3;
        else
          newSwitchState = 2;
       }
       else
         newSwitchState = 1;
      }
      else
        newSwitchState = 0;
     break;
  
    case 2:
 
     if( val < 275) {
       if( val < 150 ) {
        if( val < 50 )
          newSwitchState = 3;
        else
          newSwitchState = 2;
       }
       else
         newSwitchState = 1;
      }
      else
        newSwitchState = 0;
     break;
  }
  
  if( newSwitchState != switchStates[n] ) {
    if( newSwitchState == debounceSwitchStates[n] ) {
      if( ++switchStatesDebounceCounter[n] == ANALOG_SWITCH_DEBOUNCE_COUNT ) {
        switchStates[n] = newSwitchState;
        switch(n) {
         case 0:
           mode = newSwitchState;
           for(int i=0; i<16; i++)
            setIntensity(i,0);
           
           message[0] = 0xB0;
           message[1] = 0x10;
           message[2] = mode;
    
           UDP.beginPacket(multicast,destPort);
           UDP.write(message,3);
           UDP.endPacket();
           
           setIntensity(mode, 64);
           delay(250);
           setIntensity(mode, 0);
           break;
         case 1:
           bank = newSwitchState;
           
           message[0] = 0xB0;
           message[1] = 0x00;
           message[2] = bank;
          
           UDP.beginPacket(multicast,destPort);
           UDP.write(message,3);
           UDP.endPacket();
          
           setIntensity(4+bank,64);
           delay(250);
           setIntensity(4+bank,0);
           break;
         case 2:
           pgm = newSwitchState;
           
           message[0] = 0xC0;
           message[1] = pgm;
           
           UDP.beginPacket(multicast,destPort);
           UDP.write(message,2);
           UDP.endPacket();

           setIntensity(8+pgm,64);
           delay(250);
           setIntensity(8+pgm,0);
           break;
        }
      }
    }
    else {
      debounceSwitchStates[n] = newSwitchState;
      switchStatesDebounceCounter[n] = 0;
    }
  }
  else { 
    switchStatesDebounceCounter[n] = 0;
  }
  
  if(++n == 3)
    n = 0;
}

void HandleNoteOff(byte channel, byte pitch, byte velocity) {
  
    static uint8_t message[3], note;
  
    message[0] = 0x80 + channel;
    message[1] = pitch;
    message[2] = velocity;
    
    
    UDP.beginPacket(multicast,destPort);
    UDP.write(message,3);
    UDP.endPacket();
    
    if( (mode == CLASSIC) || (mode == VELOCITY) ) {
      
      note = pitch%12;
      
      if( pedalState > 0 )
        pedalBucket[note] = 1;
      else {
        setIntensity(note,0);
        bulbStates[note] = 0;
      }
    }
}

void HandleNoteOn(byte channel, byte pitch, byte velocity) {
  
  static uint8_t message[3],note;

  if (velocity == 0)
    HandleNoteOff(channel, pitch, velocity);
  else {
    
    message[0] = 0x90 + channel;
    message[1] = pitch;
    message[2] = velocity;
    
    UDP.beginPacket(multicast,destPort);
    UDP.write(message,3);
    UDP.endPacket();
    
    switch(mode) {
    
     case CLASSIC:
      note = pitch%12;
      bulbStates[note] = 1;
      bulbStartTime[note] = millis();
      setIntensity(note,TIMER_MAX-1);
      break;
     
     case VELOCITY:
      note = pitch%12;
      bulbStates[note] = 1;
      bulbStartTime[note] = millis();
      setIntensity(note,velocity);
      break;
     
     case SCROLLING:
      bulbStates[scrollPos] = 1;
      bulbStartTime[scrollPos] = millis();
      setIntensity(scrollPos,velocity);
      if(++scrollPos == 12) {
        scrollPos = 0;
      }
      break;
    }
  }
}

void HandleControlChange(byte channel, byte number, byte value) {
 
  static int i=0;
  
  static uint8_t message[3];
  static int newPedalState;
  
  message[0] = 0xB0 + channel;
  message[1] = number;
  message[2] = value;
  
  UDP.beginPacket(multicast,destPort);
  UDP.write(message,3);
  UDP.endPacket();
   
  if(number == 0x40) {
    if( value > 85 )
      newPedalState = 1;
    else if( value < 25 )
      newPedalState = 0;
    switch(mode) {
      case CLASSIC:
      case VELOCITY:
        if( (pedalState == 1) && (newPedalState == 0) ) {    //Pedal Release
          for(i=0; i<12; i++) {
            if(pedalBucket[i] > 0) {
              pedalBucket[i] = 0;
              bulbStates[i] = 0;
              setIntensity(i,0);
            }
          }
        }
        else if( (pedalState == 0) && (newPedalState == 1) ) { //Pedal Pressed
          pedalState = 1;
        }
        break;
      case SCROLLING:
        if( (pedalState == 1) && (newPedalState == 0) ) {
          for(int i=0; i<12; i++) {
            bulbStates[i] = 0;
          }
          for(int i=0; i<16; i++)
            setIntensity(i,0);
        }
        break;
    }
    pedalState = newPedalState;
  }
  
}

void setIntensity(uint8_t bulb, uint8_t val) {
  switch(bulb) {
    case 0:
      OCR3B = 127-val; break;
    case 1:
      OCR3C = 127-val; break;
    case 2:
      OCR3A = 127-val; break;
    case 3:
      OCR4A = 127-val; break;
    case 4:
      OCR4B = 127-val; break;
    case 5:
      OCR4C = 127-val; break;
    case 6:
      OCR1A = 127-val; break;
    case 7:
      OCR1B = 127-val; break;
    case 8:
      OCR1C = 127-val; break;
    case 9:
      OCR5C = 127-val; break;
    case 10:
      OCR5B = 127-val; break;
    case 11:
      OCR5A = 127-val; break;
  }
  pwmValues[bulb] = val;
}

void zeroCrossDetect() {
  
  TCNT1 = 0;
  TCNT3 = 0;
  TCNT4 = 0;
  TCNT5 = 0;
  
  TCCR1B = (1<<WGM12) | (1<<WGM13) | (1<<CS10) | (1<<CS12);
  TCCR3B = (1<<WGM32) | (1<<WGM33) | (1<<CS30) | (1<<CS32);
  TCCR4B = (1<<WGM42) | (1<<WGM43) | (1<<CS40) | (1<<CS42);
  TCCR5B = (1<<WGM52) | (1<<WGM53) | (1<<CS50) | (1<<CS52);
  
}
