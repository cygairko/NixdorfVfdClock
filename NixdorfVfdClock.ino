#include <RTClib.h>
#include <NixdorfVFD.h>   // https://github.com/MrTransistorsChannel/NixdorfVFD
#include <SoftwareSerial.h>

// Needed for interrupts
void IRAM_ATTR DCF77_ISR();

// Display related
NixdorfVFD vfd;
SoftwareSerial vfdSerial(D5, D6, true); // SoftwareSerial port for display on pins D5 and D6, inverted logic

// Rtc related
RTC_DS1307 rtc;
// RTC_Millis rtc;

// Dcf related
const byte dcfInterruptPin = D3; // Port where Dcf signal is connected. D3 on esp8266 is pulled-up so that no external pull-up is required
const byte dcfStatusLedPin = D0;
volatile unsigned long lastInt = 0;
volatile unsigned long long currentBuf = 0;
volatile byte bufCounter;
bool invertedSignal = true;

// Button and Mosfet related
const byte buttonPin = D8;
const byte mostfetPin = D7;
int buttonState = 0;

// Helper
int settingTime = 0;
int updateInterval = 100;


// char timeFormatWithSeconds[] = "hh:mm:ss";

void setup () {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

#ifndef ESP8266
  while (!Serial); // wait for serial port to connect. Needed for native USB
#endif

  initMosfet();
  initDisplay();
  initRtc();
  initDcf();
}

void loop() {
  // // put your main code here, to run repeatedly:
  // vfd.setCursor(4, 0);          // Moving cursor to 5 column in the first row
  // // VFD.print("Hello world!");
  // DateTime now = rtc.now();
  // // DS1307_RTC.
  // vfd.clear();
  // vfd.print(now.year());
  // // vfd.print(Week_days[now.dayOfTheWeek()]);
  // Serial.println(__TIME__);
  // delay(200);

  DateTime now = rtc.now();

  // loopTimeToSerialConsole(now);
  loopUpdateDisplay(now);
  loopMosfet();

  // One loop every half second
  delay(updateInterval);
}

void initMosfet() {
  Serial.print("Init Mosfet ... ");
  pinMode(buttonPin, INPUT);
  pinMode(mostfetPin, OUTPUT);
  Serial.println("OK");
}

void initDisplay() {
  Serial.print("Init display ... ");
  vfdSerial.begin(9600);              // Starting serial communication at 9600bod
  vfd.begin(vfdSerial);               // Initialising the display
  vfd.clear();
  vfd.home();
  // vfd.print(".");                     // Print a dot as to test
  Serial.println("OK");
}

void initRtc() {
  Serial.print("Init RTC ... ");
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  // When time needs to be re-set on a previously configured device, the
  // following line sets the RTC to the date & time this sketch was compiled
  // rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));      // Start in year 2000
  Serial.println("OK");
}

void initDcf() {
  Serial.print("Init DCF ... ");
  // Attaching the interrupt listener
  pinMode(dcfInterruptPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(dcfInterruptPin), DCF77_ISR, CHANGE);
  initDcfLed();
  Serial.println("OK");
}

void initDcfLed() {
  pinMode(dcfStatusLedPin, OUTPUT);
}

void loopMosfet() {
  // read the state of the pushbutton value
  buttonState = digitalRead(buttonPin);
  // check if the pushbutton is pressed.
  // if it is, the buttonState is HIGH
  if (buttonState == HIGH || settingTime == 1) {
    
    // turn LED on
    settingTime = 1;
    digitalWrite(mostfetPin, LOW);
  } else {
    // turn LED off
    digitalWrite(mostfetPin, HIGH);
  }
}

void loopTimeToSerialConsole(DateTime now) {
  char dateFormat[] = "DDD, DD. MMM YYYY";
  char timeFormat[] = "hh:mm:ss";
  Serial.println(now.toString(dateFormat));
  Serial.println(now.toString(timeFormat));
  Serial.println();
  }

void loopUpdateDisplay(DateTime now) {
  // char buf1[] = "DDD, MMM DD YYYY";
  // char buf2[] = "hh:mm:ss";
  char dateFormat[] = "DDD, DD. MMM YYYY";
  char timeFormat[] = "hh:mm";

  // int offsetTime = (20 - timeFormat.length())/2;

  vfd.setCursor(0, 0);
  vfd.print(now.toString(dateFormat));
  vfd.setCursor(0, 1);
  vfd.print(now.toString(timeFormat));
}

// **************************
// Code for reading DCF77 signal
// **************************
void IRAM_ATTR DCF77_ISR() {
  unsigned int dur = 0;
  dur = millis() - lastInt;
  
  // Output for debugging
  // Serial.print(!digitalRead(interruptPin));

  int readDcf;
  if (invertedSignal) {
    readDcf = !digitalRead(dcfInterruptPin);
  } else {
    readDcf = digitalRead(dcfInterruptPin);
  }
  
  if(readDcf) {
    if(dur>1500){
      if(bufCounter==59){
        evaluateSequence();
      }
      bufCounter = 0;
      currentBuf = 0;
    }
  }
  else{
    if(dur>150){
      currentBuf |= ((unsigned long long)1<<bufCounter);
    }
    bufCounter++;
  }
  lastInt = millis();
  digitalWrite(dcfStatusLedPin, readDcf);
}
void evaluateSequence(){
  byte dcf77Year = (currentBuf>>50) & 0xFF;    // year = bit 50-57
  byte dcf77Month = (currentBuf>>45) & 0x1F;       // month = bit 45-49
  byte dcf77DayOfWeek = (currentBuf>>42) & 0x07;   // day of the week = bit 42-44
  byte dcf77DayOfMonth = (currentBuf>>36) & 0x3F;  // day of the month = bit 36-41
  byte dcf77Hour = (currentBuf>>29) & 0x3F;       // hour = bit 29-34
  byte dcf77Minute = (currentBuf>>21) & 0x7F;     // minute = 21-27 
  bool parityBitMinute = (currentBuf>>28) & 1;
  bool parityBitHour = (currentBuf>>35) & 1;
  bool parityBitDate = (currentBuf>>58) & 1;
  if((parity_even_bit(dcf77Minute)) == parityBitMinute){
    if((parity_even_bit(dcf77Hour)) == parityBitHour){
      if(((parity_even_bit(dcf77DayOfMonth) + parity_even_bit(dcf77DayOfWeek) 
           + parity_even_bit(dcf77Month) + parity_even_bit(dcf77Year))%2) == parityBitDate){
        rtc.adjust(DateTime(rawByteToInt(dcf77Year) + 2000, rawByteToInt(dcf77Month), 
            rawByteToInt(dcf77DayOfMonth), rawByteToInt(dcf77Hour), rawByteToInt(dcf77Minute), 0));
        
        Serial.println("**********************");
        Serial.println("Time adjusted from Dcf");
        Serial.println("**********************");
        Serial.println();
        settingTime = 0;
       }
    }
  }
}
unsigned int rawByteToInt(byte raw){
  return ((raw>>4)*10 + (raw & 0x0F));
}
// uncomment the following lines if you don't use an AVR MCU
bool parity_even_bit(byte val){
 val ^= val >> 4;
 val ^= val >> 2;
 val ^= val >> 1;
 val &= 0x01;
 return val;
}
// **************************
// Code for reading DCF77 signal
// **************************