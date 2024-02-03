#include <RTClib.h>
#include <NixdorfVFD.h>   // https://github.com/MrTransistorsChannel/NixdorfVFD
#include <SoftwareSerial.h>
#include <Wire.h>

#include <ESP8266WiFiMulti.h>
#include <time.h>
#include <coredecls.h> // optional settimeofday_cb() callback to check on server

// Needed for interrupts
void IRAM_ATTR DCF77_ISR();

// Display related
NixdorfVFD vfd;
SoftwareSerial vfdSerial(D5, D6, true); // SoftwareSerial port for display on pins D5 and D6, inverted logic

// Rtc and time related
RTC_DS1307 rtc;
// RTC_Millis rtc;
time_t now;     // this are the seconds since Epoch (1970) - UTC
tm tm;          // the structure tm holds time information in a more convenient way

// Dcf related
const byte dcfInterruptPin = D3; // Port where Dcf signal is connected. D3 on esp8266 is pulled-up so that no external pull-up is required
const byte dcfStatusLedPin = D0;
volatile unsigned long lastInt = 0;
volatile unsigned long long currentBuf = 0;
volatile byte bufCounter;
bool invertedSignal = true;
static byte pulseStart   = invertedSignal ? LOW : HIGH;

// Button and Mosfet related
const byte buttonPin = D8;
const byte mostfetPin = D7;
int buttonState = 0;

// Wlan related
ESP8266WiFiMulti wifiMulti;
// WiFi connect timeout per AP. Increase when connecting takes longer.
const uint32_t connectTimeoutMs = 5000;

// Configuration of NTP
#define MY_NTP_SERVER "de.pool.ntp.org"
#define MY_TZ "CET-1CEST,M3.5.0/02,M10.5.0/03"

// Helper
int settingTime = 0;
int updateInterval = 100;
//int ntpUpdateIntervalDebug = 15000;
//int ntpUpdateInterval = 86400000;
//unsigned long ntpTimingHelper = 0;


// char timeFormatWithSeconds[] = "hh:mm:ss";

void setup () {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

#ifndef ESP8266
  while (!Serial); // wait for serial port to connect. Needed for native USB
#endif

  configTime(MY_TZ, MY_NTP_SERVER);

  initMosfet();
  initDisplay();
  initRtc();
  initDcf();
  initWlan();
  
  settimeofday_cb(time_is_set);                  // register callback if time was sent
}

void loop() {
  DateTime rtcnow = rtc.now();

  // loopTimeToSerialConsole(rtcnow);
  loopUpdateDisplay(rtcnow);
  loopMosfet();
  
  
  showTime();
  printRTC();

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
  Wire.begin(D2, D1);
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

void initWlan() {
  Serial.print("Init Wlan ... ");

  // Don't save WiFi configuration in flash - optional
  WiFi.persistent(false);
  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // Register multi WiFi networks
  wifiMulti.addAP("WifiNumber1", "PasswordNumber1");
  wifiMulti.addAP("WifiNumber2", "PasswordNumber2");
  wifiMulti.addAP("WifiNumber3", "PasswordNumber3");
  // More is possible

  while (wifiMulti.run(connectTimeoutMs) != WL_CONNECTED) {
    delay(200);
    Serial.print ( "." );
  }
      
  Serial.print("WiFi connected: ");
  Serial.print(WiFi.SSID());
  Serial.print(" ");
  Serial.println(WiFi.localIP());
}

void loopMosfet() {
  // read the state of the pushbutton value
  buttonState = digitalRead(buttonPin);
  // check if the pushbutton is pressed.
  if (buttonState == HIGH) {
    // Recognize button push
    settingTime = 1;
    Serial.println("Setting time ... ");
    
    vfd.clear();
    vfd.setCursor(0, 0);
    vfd.print("Setting time ...");

    delay(1500); // Show message for 1.5 s
  } else if (settingTime == 1) {
    // Keep display off while getting the time
    digitalWrite(mostfetPin, LOW);
  } else {
    // Turn button on again.
    digitalWrite(mostfetPin, HIGH);
  }
}

void time_is_set(bool from_sntp) {
  if (from_sntp)                       // needs Core 3.0.0 or higher!
  {
    Serial.println(F("The internal time is set from SNTP."));
    setRTC();
  }
  else
  {
    Serial.println(F("The internal time is set."));
  }
}

void setRTC() {
  Serial.println(F("setRTC --> from internal time"));
  
  time(&now);                          // read the current time and store to now
  localtime_r(&now, &tm);                 // update the structure tm with the current GMT
  rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
}

void loopTimeToSerialConsole(DateTime rtcnow) {
  char dateFormat[] = "DDD, DD. MMM YYYY";
  char timeFormat[] = "hh:mm:ss";
  Serial.println(rtcnow.toString(dateFormat));
  Serial.println(rtcnow.toString(timeFormat));
  Serial.println();
}

void loopUpdateDisplay(DateTime rtcnow) {
  // char buf1[] = "DDD, MMM DD YYYY";
  // char buf2[] = "hh:mm:ss";
  char dateFormat[] = "DDD, DD. MMM YYYY";
  char timeFormat[] = "hh:mm";

  // int offsetTime = (20 - timeFormat.length())/2;

  vfd.setCursor(0, 0);
  vfd.print(rtcnow.toString(dateFormat));
  vfd.setCursor(0, 1);
  vfd.print(rtcnow.toString(timeFormat));
}

// **************************
// Code for reading DCF77 signal
// **************************
void IRAM_ATTR DCF77_ISR() {
  // Interrupt handler
  unsigned int dur = 0;
  dur = millis() - lastInt;
  byte sensorValue = digitalRead(dcfInterruptPin);
  
  if(sensorValue == pulseStart) {
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
  digitalWrite(dcfStatusLedPin, sensorValue == pulseStart);
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



void printRTC()
{
  DateTime dtrtc = rtc.now();          // get date time from RTC i
  if (!dtrtc.isValid())
  {
    Serial.println(F("E103: RTC not valid"));
  }
  else
  {
    time_t newTime = getTimestamp(dtrtc.year(), dtrtc.month(), dtrtc.day(), dtrtc.hour(), dtrtc.minute(), dtrtc.second());
    Serial.print(F("RTC:"));
    Serial.print(newTime);
    Serial.print(" ");
    Serial.print(dtrtc.year()); Serial.print("-");
    print10(dtrtc.month()); Serial.print("-");
    print10(dtrtc.day()); Serial.print(" ");
    print10(dtrtc.hour()); Serial.print(":");
    print10(dtrtc.minute()); Serial.print(":");
    print10(dtrtc.second());
    Serial.println(F(" UTC"));         // remember: the RTC runs in UTC
  }
}

void showTime() {
  
  time(&now);                          // read the current time and store to now
  localtime_r(&now, &tm);              // update the structure tm with the current time
  char buf[50];
  strftime(buf, sizeof(buf), " %F %T %Z wday=%w", &tm); // https://www.cplusplus.com/reference/ctime/strftime/
  Serial.print("now:"); Serial.print(now); // in UTC!
  Serial.print(buf);
  if (tm.tm_isdst == 1)                // Daylight Saving Time flag
    Serial.print(" DST");
  else
    Serial.print(" standard");
  Serial.println();
}

/*
   prints an one digit integer with a leading 0
*/
void print10(int value)
{
  if (value < 10) Serial.print("0");
  Serial.print(value);
}

/*
   ESP8266 has no timegm, so we need to create our own...

   Take a broken-down time and convert it to calendar time (seconds since the Epoch 1970)
   Expects the input value to be Coordinated Universal Time (UTC)

   Parameters and values:
   - year  [1970..2038]
   - month [1..12]  ! - start with 1 for January
   - mday  [1..31]
   - hour  [0..23]
   - min   [0..59]
   - sec   [0..59]
   Code based on https://de.wikipedia.org/wiki/Unixzeit example "unixzeit"
*/
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
  const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334}; /* Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag */
  int leapyears = ((year - 1) - 1968) / 4
                  - ((year - 1) - 1900) / 100
                  + ((year - 1) - 1600) / 400; /* Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr) */
  int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
  if ( (mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) )
    days_since_1970 += 1; /* +Schalttag, wenn Jahr Schaltjahr ist */
  return sec + 60 * (min + 60 * (hour + 24 * days_since_1970) );
}


/*
   optional: by default, the NTP will be started after 60 secs
   lets start at a random time in 5 seconds
*/
uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000()
{
  randomSeed(A0);
  return random(5000 + millis());
}