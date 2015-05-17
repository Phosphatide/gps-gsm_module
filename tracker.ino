/**
This sketch makes a basic GPS/GSM module that can communicate
with a phone while it's on the go and away from the user. It
uses the Adafruit Ultimate GPS to obtain coordinates and the
Adafruit FONA to send SMS updates of the module's location.
The FONA can also recieve receive commands through SMS to
execute other functions. A piezo buzzer is also connected to
the Arduino as a way to send feedback to the user without
having to use SMS or a serial monitor.

This particular sketch is specifically writted for use with
a high-altitude balloon payload. From the moment the Arduino
is turned on, coordinates are continuously read from the GPS.
Those coordinates are then periodically sent via SMS to a
specified phone number, and the time interval between text
messages are set in this code before being uploaded to the
Arduino. Commands can be sent via SMS back to the module for
one of two actions: the current GPS coordinates, or activating
the buzzer to play a loud alarm to aid in the search for the
payload after the landing.

Sketch by:  Phosphatide

Parts of code are taken from the parsing example sketch for
the Adafruit Ultimate GPS and the FONAtest for the Adafruit
FONA. All due credit goes to the people at Adafruit.
**/

#include <Adafruit_GPS.h>
#include <Adafruit_FONA.h>
#include <SoftwareSerial.h>

#define GPS_RX 2
#define GPS_TX 3
#define FONA_RX 4
#define FONA_TX 5
#define FONA_RST 6
#define BUZZ_PIN 7
#define GPSECHO  true

char replybuffer[161];
bool beeping = false;
bool fonaSetup = false;
int8_t startSMS;

// Default phone number to send texts to:
char sendto[21] = "+###########";

SoftwareSerial gpsSS(GPS_TX, GPS_RX);
Adafruit_GPS GPS(&gpsSS);

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

// this keeps track of whether we're using the interrupt
// off by default!
boolean usingInterrupt = false;

void useInterrupt(boolean v)
{ 
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
    usingInterrupt = true;
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
    usingInterrupt = false;
  }
}

void setup()  
{
  Serial.begin(115200);
  
  /********
  GPS Setup
  ********/
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(9600);
  
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time
  
  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  // GPS.sendCommand(PGCMD_ANTENNA);

  // the nice thing about this code is you can have a timer0 interrupt go off
  // every 1 millisecond, and read data from the GPS for you. that makes the
  // loop code a heck of a lot easier!
  useInterrupt(true);
  
  
  /********
  Buzzer Setup
  ********/
   // initialize the buzzer pin as an output:
  pinMode(BUZZ_PIN, OUTPUT);
}

void setupFONA()
{
  /********
  FONA Setup
  ********/
  fonaSS.begin(9600);
  
  // if FONA search fails, play an error buzzer
  if (!fona.begin(fonaSS)){
    for(int i = 0; i < 4; i++){
      tone(BUZZ_PIN, 100);
      delay(500);
      noTone(BUZZ_PIN);
      delay(500);
    }
    while(1);
  }
  startSMS = fona.getNumSMS();
  fonaSetup = true;
  
  // if main setup goes well, play a jingle
  for(int i = 0; i < 4; i++){
    tone(BUZZ_PIN, 150+(i*150));
    delay(100);
    noTone(BUZZ_PIN);
  }
}


// Interrupt is called once a millisecond, looks for any new GPS data, and stores it
SIGNAL(TIMER0_COMPA_vect) {
  char c = GPS.read();
  // if you want to debug, this is a good time to do it!
#ifdef UDR0
  if (GPSECHO)
    if (c) UDR0 = c;  
    // writing direct to UDR0 is much much faster than Serial.print 
    // but only one character can be written at a time. 
#endif
}

uint32_t gpsTimer = millis();
uint32_t fonaTimer = gpsTimer;

void loop() {
  gpsSS.listen();
  // in case you are not using the interrupt above, you'll
  // need to 'hand query' the GPS, not suggested :(
  if (! usingInterrupt) {
    // read data from the GPS in the 'main loop'
    char c = GPS.read();
    // if you want to debug, this is a good time to do it!
    if (GPSECHO)
      if (c) Serial.print(c);
  }
  
  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    // a tricky thing here is if we print the NMEA sentence, or data
    // we end up not listening and catching other sentences! 
    // so be very wary if using OUTPUT_ALLDATA and trytng to print out data
    //Serial.println(GPS.lastNMEA());   // this also sets the newNMEAreceived() flag to false
  
    if (!GPS.parse(GPS.lastNMEA()))   // this also sets the newNMEAreceived() flag to false
      return;  // we can fail to parse a sentence in which case we should just wait for another
  }

  // if millis() or a timer wraps around, we'll just reset it
  if (gpsTimer > millis())  gpsTimer = millis();
  if (fonaTimer > millis()) fonaTimer = millis();
  
  Serial.print(millis() - gpsTimer);
  Serial.println("/300000");
  
  char coordinates[30];
  Serial.println((int)GPS.fix);
  bool fix = GPS.fix;
  
  // record the current coordinate location
  if (fix) {
    // if this is the first time a fix is found, set up the FONA
    // will not execute again afterward
    if (!fonaSetup) setupFONA();
    
    String data;
    float latitude = GPS.latitudeDegrees, longitude = GPS.longitudeDegrees, height = GPS.altitude;
    char degree[12];
    dtostrf(latitude, 2, 4, degree);
    data += degree;
    data += ", ";
    dtostrf(longitude, 3, 4, degree);
    data += degree;
    data += ", ";
    dtostrf(height, 2, 2, degree);
    data += degree;
    
    data.toCharArray(coordinates, data.length() + 1);
    Serial.println(coordinates);
  }
  
  // if the FONA is not ready yet, do not do any of the following
  // must wait until a GPS fix is found before switching serial devices
  if (fonaSetup){
    // approximately every 2 seconds, check for new messages
    if (millis() - fonaTimer > 2000){
      fonaTimer = millis();
      fonaSS.listen();
      // if a new text message is received, analyze it for a command
      int8_t smsnum = fona.getNumSMS();
      Serial.println(smsnum);
      if (smsnum > startSMS){
        startSMS = fona.getNumSMS();
        uint16_t smslen;
        if (fona.readSMS(smsnum, replybuffer, 250, &smslen)){
          char phoneNum[21];
          if (fona.getSMSSender(smsnum, phoneNum, 21)){
            String analyze = replybuffer;
            analyze.toLowerCase();
            Serial.println(analyze);
            
            // replies with coordinates and altitude
            if (analyze == "gps")
            {
              if (fix) {
                if (fona.sendSMS(phoneNum, coordinates)) {Serial.println("SMS sent");}
                else {Serial.println("SMS failed");}
              }
              else{
                char message[] = "ERROR: NO GPS FIX";
                if(fona.sendSMS(phoneNum, message)) {Serial.println("SMS sent");}
                else {Serial.println("SMS failed");}
              }
            }
            
            // toggles the alerting siren
            else if(analyze == "beep")
            {
              if (beeping){
                beeping = false;
                char message[] = "BEEPING OFF";
                if(fona.sendSMS(phoneNum, message)) {Serial.println("SMS sent");}
                else {Serial.println("SMS failed");}
              }
              else{
                beeping = true;
                char message[] = "BEEPING ON";
                if(fona.sendSMS(phoneNum, message)) {Serial.println("SMS sent");}
                else {Serial.println("SMS failed");}
              }
            }
            
            // informs sender that a wrong command was sent
            else
            {
              char message[] = "INVALID COMMAND";
              if(fona.sendSMS(phoneNum, message)) {Serial.println("SMS sent");}
              else {Serial.println("SMS failed");}
            }
          }
        }
      }
    }
    
    // approximately every 5 minutes or so, send out the current location
    if (millis() - gpsTimer > 300000) { 
      gpsTimer = millis(); // reset the timer
      fonaSS.listen();
      if (fix) {
        if (fona.sendSMS(sendto, coordinates)) {Serial.println("Update sent");}
        else {Serial.println("Update failed");}
      }
      else{
        char message[] = "WARNING: NO GPS FIX";
        if (fona.sendSMS(sendto, message)) {Serial.println("Update sent");}
        else {Serial.println("Update failed");}
      }
    }
  }
  
  // play an alarm to help track down the module
  if (beeping){
    delay(1750);
    for(int i = 0; i < 4; i++){
      tone(BUZZ_PIN, 4200);
      if (i != 3) delay(100);
      else delay(250);
      noTone(BUZZ_PIN);
      delay(10);
    }
  }
}
