#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <String.h>
#include <SPI.h>
 
 
// Various constants.
const String ATE = "ATE"; // Echo off/on
const String ATI = "ATI"; // Version id
const String ATZ = "ATZ"; // Reset
const String ATSP = "ATSP"; // Set protocol X
const String ATSH = "ATSH"; // Set headers
const String ATH = "ATH"; // Headers off / on
const String ATL = "ATL"; // Linefeeds off/on
const String ATM = "ATM"; // Memory off/on
const String PROMPT = ">";
const String CANBUS = "6"; // canbus 500k 11 bit protocol id for elm.
const String ATDPN = "ATDPN";
const String ATDESC = "AT@1";
const String ATAT = "ATAT";
const String LF = "\n";
const String CR = "\r";
const String VERSION = "ELM327 v1.3";
const String VERSION_DESC = "openVespa";
const String OKdokie = "OK";
const String ANALOG = "a";
const String DIGITAL = "d";
const String IS_INPUT = "i";
const String IS_OUTPUT = "o";
const String PIDS_0100 = "0100";  //PIDS supported query
const String PIDS_0120 = "0120";  //Extended PIDS
const String PIDS_010C = "010C";  //RPM
const String PIDS_013C = "013C";  //EGT, but really Catalyst Temperature: Bank 1, Sensor 1
const String PIDS_013D = "013D";  //CHT, but really Catalyst Temperature: Bank 2, Sensor 1
const String HPIDS_4100 = "686AF141000010000116"; // 410000100001
const String HPIDS_4120 = "686AF14120000000185E"; // 412000000018
const String HPIDS_410C = "686AF1410C123456";  //Tester RPM value 410C1234
const String HPIDS_413C = "686AF1413C000022";  //Tester EGT -40   413C0000
const String HPIDS_413D = "686AF1413D000022";  //Tester CHT -40   413D0000

const String PIDS_4100 = "410000100001";
const String PIDS_4120 = "412000000018";
const String PIDS_410C = "410C1234";
const String PIDS_413C = "413C0000";  //EGT
const String PIDS_413D = "413D0000";  //CHT
const String ATRV = "ATRV"; //Battery voltage query
const String BATTERY = "12.6V"; //Fake battery voltage
 
String fromTorque = "";
String OBD_header = "686AF1";
 

// Hardcode WiFi parameters as this isn't going to be moving around.
const char* ssid = "openVespa";
const char* password = "openVespa"; 

#define D0 16 //nodeMCU LED
#define CS 4  // aka D2 
#define VR 5  // aka D1
WiFiServer server(35000); 
WiFiClient client;
// NETWORK: Static IP details...
IPAddress ip(192, 168, 0, 10); 
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
int status = WL_IDLE_STATUS;
boolean alreadyConnected = false; // whether or not the client was connected previously
boolean ECHO_sw = true;
boolean HEADER_sw = false;
boolean LF_sw = true;
volatile float RPMcount;                 //number of triggers on the RPM sensor
volatile float RPMValue = 2453;          //calculated RPM from interrupt #2. Declared volatile so it is loaded from RAM
float OldRPMValue = 0;                   //keep track of what the previous rpm value was, for time rollover purposes
float Toothcount = 3;                    //number of triggers per one flywheel revolution
unsigned long timeold;                   //the last time RPM was calculated
volatile unsigned int OBD_EGT;
volatile unsigned int OBD_RPM;
Ticker EGT_timer;
boolean EGT_read_flag = false;

void EGT_read()
{
  EGT_read_flag = true;
}

 
void setup() {
  // Init the pins 
  OBD_EGT = 0;
  pinMode(D0, OUTPUT);
  pinMode(VR, INPUT_PULLUP);
  digitalWrite(D0, HIGH);  //LED OFF
  delay(1000);
  digitalWrite(D0, LOW);  //LED ON
  Serial.begin(9600);    // baud rate 
  WiFi.setPhyMode(WIFI_PHY_MODE_11B);
  WiFi.setAutoConnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid);
  // Static IP Setup Info Here...
  WiFi.softAPConfig(ip,ip,subnet);//(ip, gateway, subnet);
  digitalWrite(D0, HIGH);  //LED OFF
  delay(500);
  digitalWrite(D0, LOW);  //LED ON
  Serial.println("\nSTARTING SERVER");
  // Start the TCP server
  server.begin(); 
  delay(500);
  WiFi.printDiag(Serial);
  Serial.print("Chip ID: ");
  Serial.println(ESP.getChipId());
  Serial.print("Flash ID: ");
  Serial.println(ESP.getFlashChipId()); 
  Serial.print("Flash Speed: ");
  Serial.println(ESP.getFlashChipSpeed());
  Serial.print("Flash Size: ");
  Serial.println(ESP.getFlashChipSize());
  //start and configure hardware SPI
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  //SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.setFrequency(4000000); 
  pinMode(CS,OUTPUT);
  digitalWrite(CS, HIGH);
  attachInterrupt(VR, calcRPM, RISING);
  // flip the pin every 0.3s
  EGT_timer.attach(0.3, EGT_read);
 }

void loop() {

  if(!client){
    client = server.available();
  }

  else
   {

    if (client.status() == CLOSED) {
      client.stop();
      Serial.println("Connection Closed !");
    } 

    if (!alreadyConnected) // when the client sends the first byte, say hello:
    {
      // clead out the input buffer:
      client.flush();
      Serial.println("We have a new client");
      alreadyConnected = true;
      Serial.printf("Connection status: %d\n", WiFi.status());
      WiFi.printDiag(Serial);
    }
    
    if(client.available() > 0)  {
        char c = client.read();  
        //Serial.print(c);
        if(ECHO_sw) {client.print(c);}
         if ((c == '\n' || c == '\r') && fromTorque.length() > 0) 
         {
            fromTorque.toUpperCase();
            processCommand(fromTorque);
            fromTorque = "";
         } 
         else if (c != ' ' && c != '\n' && c !='\r') 
         {
            // Ignore spaces.
            fromTorque += c; 
         }
    }
  }  

 if (EGT_read_flag)
  {  
   //Serial.print("Int. Temp = ");
   //Serial.println(readInternal());
   digitalWrite(D0, !digitalRead(D0));  
   //double c = readCelsius();
   double c = readInternal();
   float f = c;
   f *= 9.0;
   f /= 5.0;
   f += 32;
 /*
   if (isnan(c)) 
   {
     Serial.println("T/C Problem");
   } 
   
   else 
   {
     Serial.print("Thermocouple Temp = *");
     Serial.print(c);
     Serial.print("  *");
     Serial.println(f);
   }

   Serial.print("Analog = ");
   Serial.println(analogRead(A0));
   */
   EGT_read_flag = false;
}
 yield();
}
 
/**
 * Parse the commands sent from Torque
 */
void processCommand(String command) {
    digitalWrite(D0, HIGH);
   // Debug - see what torque is sending on your serial monitor
   //Serial.print(" COMMAND:  ");
   //Serial.println(command);
 
   // Simple command processing from the app to the arduino..
   if (command.equals(ATZ)) {
       //initSensors(); // reset the pins
       client.println(VERSION);
       client.print(LF);
       //client.print(OKdokie);
   } else if (command.startsWith(ATE)) {
       if(command.charAt(3) == '0'){
        ECHO_sw = false;
        Serial.println("ECHO OFF");
       }
       else{ ECHO_sw = true;
       Serial.println("ECHO ON");
       }
       client.println(OKdokie); 
   } else if(command.startsWith(ATI)) {
       client.println(VERSION);
       client.print(LF);
       client.println(OKdokie);
   } else if (command.startsWith(ATDESC)) {
       client.println(VERSION_DESC); 
       client.print(LF);
       client.println(OKdokie);
   } else if (command.startsWith(ATL)) {
       if(command.charAt(3) == '0'){
        LF_sw = false;
        Serial.println("LF OFF");
       }
       else{ LF_sw = true;
       Serial.println("LF ON");
       }
       client.println(OKdokie);
   } else if (command.startsWith(ATAT)) {
       client.println(OKdokie);
   } else if (command.startsWith(ATH)) {
       if(command.charAt(3) == '0'){
        HEADER_sw = false;
        Serial.println("HEADERS OFF");
       }
       else{ HEADER_sw = true;
       Serial.println("HEADERS ON");
       }
       client.println(OKdokie);
   } else if (command.startsWith(ATM)) {
       client.println(OKdokie);
   } else if (command.startsWith(ATSP)) {
       // Set protocol
       //Serial.println("PROTOCOL");
       client.println(OKdokie);
   } else if (command.startsWith(ATSH)) {
       // Set headers
       Serial.print("HEADER:");
       Serial.println(command.substring(4,10));
       OBD_header = command.substring(4,10);
       client.println(OKdokie);
   } else if (command.startsWith(ATDPN)) {
       client.println("3");//CANBUS);
   } else if (command.startsWith(PIDS_0100)) {
    Serial.println("PIDS_0100");
      if(HEADER_sw){
        client.println(HPIDS_4100);
      }
      else{
       client.println(PIDS_4100);
      }
       client.print(LF);
   } else if (command.startsWith(PIDS_0120)) {
      if(HEADER_sw){
        client.println(HPIDS_4120);
      }
      else{
       client.println(PIDS_4120);
      }
   } else if (command.startsWith(PIDS_010C)) {  // RPM
      if(HEADER_sw){
        //client.println(HPIDS_410C);
        //client.println(OBD_header+PIDS_410C+"22");
        client.print(OBD_header+"410C");
        client.printf("%04X",OBD_RPM);
        client.println("22");
      }
      else{
       //client.println(PIDS_410C);
        client.print("410C");
        client.printf("%04X",OBD_RPM);
      }
   } else if (command.startsWith(PIDS_013C)) {  // EGT
      if(HEADER_sw){
        //client.println(HPIDS_413C);
        client.print(OBD_header+"413C");
        client.printf("%04X",OBD_EGT);
        client.println("22");
      }
      else{
       //client.println(PIDS_413C);
       client.print("413C");
       client.printf("%04X",OBD_EGT);
      }
   } else if (command.startsWith(PIDS_013D)) {
      if(HEADER_sw){
        client.println(HPIDS_413D);
      }
      else{
       client.println(PIDS_413D);
      }
   } else if (command.startsWith(ATRV)) {
       client.println(BATTERY);
   }

   else{
    Serial.println("CATCH ALL");
    client.println(OKdokie);
   }
 
   client.print(LF);
   client.print(PROMPT);
  
}


double readInternal(void) {
  uint32_t v;

  v = hspiread32();

  // ignore bottom 4 bits - they're just thermocouple data
  v >>= 4;

  // pull the bottom 11 bits off
  float internal = v & 0x7FF;
  // check sign bit!
  if (v & 0x800) {
    // Convert to negative value by extending sign and casting to signed type.
    int16_t tmp = 0xF800 | (v & 0x7FF);
    internal = tmp;
  }
  internal *= 0.0625; // LSB = 0.0625 degrees
  //Serial.print("\tInternal Temp: "); Serial.println(internal);

  OBD_EGT = (internal*10) + 400;
  return internal;
}

double readCelsius(void) {

  int32_t v;

  v = hspiread32();

  //Serial.print("0x"); Serial.println(v, HEX);

  /*
  float internal = (v >> 4) & 0x7FF;
  internal *= 0.0625;
  if ((v >> 4) & 0x800) 
    internal *= -1;
  Serial.print("\tInternal Temp: "); Serial.println(internal);
  */

  if (v & 0x7) {
    // uh oh, a serious problem!
    return NAN; 
  }

  if (v & 0x80000000) {
    // Negative value, drop the lower 18 bits and explicitly extend sign bits.
    v = 0xFFFFC000 | ((v >> 18) & 0x00003FFFF);
  }
  else {
    // Positive value, just drop the lower 18 bits.
    v >>= 18;
  }
  //Serial.println(v, HEX);
  
  double centigrade = v;

  // LSB = 0.25 degrees C
  centigrade *= 0.25;
  v = (centigrade*10) +400;
  OBD_EGT = v;
  //Serial.print("\nHEX:");
  //Serial.printf("%04X",OBD_EGT);
  //Serial.print("\n");
  return centigrade;
}

uint8_t readError() {
  return hspiread32() & 0x7;
}

uint32_t hspiread32(void) {
  int i;
  // easy conversion of four uint8_ts to uint32_t
  union bytes_to_uint32 {
    uint8_t bytes[4];
    uint32_t integer;
  } buffer;
  
  digitalWrite(CS, LOW);
  delay(1);
  
  for (i=3;i>=0;i--) {
    buffer.bytes[i] = SPI.transfer(0x00);
  }
  
  digitalWrite(CS, HIGH);
  
  return buffer.integer;
  
}

void calcRPM()
{
     RPMcount = RPMcount + 1;              //Increment RPMcount every time the sensor triggers. Return the new value
     Serial.println("VR INTERRUPT!!!");
     if (RPMcount >= Toothcount) 
      {
        if ((micros() - timeold) < 0) 
        {
          RPMValue = OldRPMValue; 
        }         //this handles the rollover of the microsecond clock
        else 
        {
          RPMValue = (60000000/(micros() - timeold)*RPMcount)/Toothcount; 
        }
      timeold = micros();                   //store the time that the RPM sensor was triggered
      RPMcount = 0;                         //reset the RPMcount to zero
      OldRPMValue = RPMValue;      
      OBD_RPM = RPMValue * 4;               
     }
     
}


