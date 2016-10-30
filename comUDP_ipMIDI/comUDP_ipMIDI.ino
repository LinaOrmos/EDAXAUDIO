#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>

#include <MIDI.h>

MIDI_CREATE_DEFAULT_INSTANCE();


uint8_t mac[] = {0x14,0x04,0x0F,0x00,0x0F,0xD2};// Esta es la dirección física de la TIVA. Se encuentra al reverso.
//IPAddress myIP(192, 168, 1, 66); Sólo se utiliza si no se cuenta con DHCP. Es la dirección IP que utilizará la TIVA.
IPAddress multicast(225, 0, 0, 37); //Dirección multicast que utiliza IP midi. En esta dirección cualquier aplicación obtiene el mensaje.
unsigned int destPort = 21928; //Port 1: 21928, Port 2: 21929, y así para los 20 puertos que permite ipMIDI.
EthernetUDP UDP;
static uint8_t message[3]; // array of 4 byte ints. Mensaje MIDI.

uint8_t counter;
int packetSize;
uint8_t messageType;
uint8_t data[2];
int countE = 0; //Contador para renovar el lease de la dirección IP asignada vía DHCP

void processUDP(void);
void HandleNoteOff(uint8_t channel, uint8_t pitch, uint8_t velocity);
void HandleNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity);
void HandleControlChange(uint8_t channel, uint8_t number, uint8_t value);
                              

void setup() {
  // put your setup code here, to run once:

  MIDI.begin(MIDI_CHANNEL_OMNI);
  Ethernet.begin(mac);
  UDP.begin(multicast);

  MIDI.setHandleNoteOn(HandleNoteOn);
  MIDI.setHandleNoteOff(HandleNoteOff);
  MIDI.setHandleControlChange(HandleControlChange);

}

void loop() {
 

  //MIDI.read();
  //processUDP();


  message[0] = 0x91;  //Byte 1: Note On = 9 Byte 2: 1, indica canal 2
  message[1] = 0x50; //7 Bits para Pitch val
  message[2] = 0x6F; // 7 Bits para Velocity
           
  UDP.beginPacket(multicast,destPort);
  UDP.write(message,3);
  UDP.endPacket();

  delay(100);

  counter++;

  if(counter/10 >= 3600){ //Revisa si la dirección IP expira cada hora, si es así la renueva.
    Ethernet.maintain();
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

void HandleNoteOff(byte channel, byte pitch, byte velocity) {
  
    static uint8_t message[3], note;
  
    message[0] = 0x80 + channel;
    message[1] = pitch;
    message[2] = velocity;
    
    
    UDP.beginPacket(multicast,destPort);
    UDP.write(message,3);
    UDP.endPacket();
    
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
   
  
}




