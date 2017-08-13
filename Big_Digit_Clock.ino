#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <TimerOne.h>
#include <EEPROM.h>
#include <MenuBackend.h>
#include <Adafruit_SleepyDog.h>

const uint8_t BL_OFF = 0x0;
const uint8_t BL_RED = 0x1;
const uint8_t BL_YELLOW = 0x3;
const uint8_t BL_GREEN = 0x2;
const uint8_t BL_TEAL = 0x6;
const uint8_t BL_BLUE = 0x4;
const uint8_t BL_VIOLET = 0x5;
const uint8_t BL_WHITE = 0x7;

#define APP_NAME ("BIG CLOCK")
const uint8_t APP_MAJOR = 1;
const uint8_t APP_MINOR = 0;
const uint8_t APP_REV = 0;

const uint8_t BUZZER_PIN = 9;
const uint8_t DATA_PIN  = 5;
const uint8_t CLOCK_PIN = 6;
const uint8_t LATCH_PIN = 7;

RTC_PCF8523 rtc;
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

DateTime last;
DateTime dtDSTStart;
DateTime dtDSTEnd;
DateTime dtTimerEnd;

//====================================================================================================
// Config Variables
//====================================================================================================

struct ConfigSettings_t {
  char ConfigApp[10];
  uint8_t ConfigMajorVersion;
  uint8_t ConfigMinorVersion;
  uint8_t ConfigRevision;
  
  int8_t DSTStartMonth;
  int8_t DSTStartSunday;
  int8_t DSTOffset;

  int8_t DSTEndMonth;
  int8_t DSTEndSunday;
  int8_t StandardOffset;

  TimeSpan tsTimer;
  DateTime dtDisplayOff;
  DateTime dtDisplayOn;
};
//ConfigSettings_t configDefaults = {
ConfigSettings_t configSettings = {
  APP_NAME,
  APP_MAJOR,
  APP_MINOR,
  APP_REV,
  3,
  2,
  -4,
  11,
  1,
  -5,
  TimeSpan(0,0,15,0),
  DateTime(2017,1,1,1,0,0),
  DateTime(2017,1,1,6,0,0)
};

//====================================================================================================
// Menu Variables
//====================================================================================================
void menuUseEvent(MenuUseEvent used);
void menuChangeEvent(MenuChangeEvent changed);

MenuBackend menu = MenuBackend(menuUseEvent,menuChangeEvent);
//                                           "123456789 123456"
  MenuItem timerStartStop   = MenuItem(menu, "Start/Stop Timer", 1);
  MenuItem timerMenu        = MenuItem(menu, "Timer", 1);
    MenuItem timerHour      = MenuItem(menu, "Hours", 2);
    MenuItem timerMinute    = MenuItem(menu, "Minutes", 2);
    MenuItem timerSecond    = MenuItem(menu, "Seconds", 2);
    MenuItem timerSave      = MenuItem(menu, "Save Timer", 3);
    MenuItem timerCancel    = MenuItem(menu, "Discard Changes", 3);
  MenuItem configTime       = MenuItem(menu, "Time and Date", 1);
    MenuItem setHour        = MenuItem(menu, "Hour", 2);
    MenuItem setMinute      = MenuItem(menu, "Minute", 2);
    MenuItem setSecond      = MenuItem(menu, "Second", 2);
    MenuItem setYear        = MenuItem(menu, "Year", 2);
    MenuItem setMonth       = MenuItem(menu, "Month", 2);
    MenuItem setDay         = MenuItem(menu, "Day", 2);
    MenuItem timeSave       = MenuItem(menu, "Save Date/Time", 3);
    MenuItem timeCancel     = MenuItem(menu, "Discard Changes", 3);
  MenuItem configuration    = MenuItem(menu, "Config", 1);
    MenuItem cfgDispOffHour = MenuItem(menu, "Disp Off Hour", 2);
    MenuItem cfgDispOffMin  = MenuItem(menu, "Disp Off Min", 2);
    MenuItem cfgDispOnHour  = MenuItem(menu, "Disp On Hour", 2);
    MenuItem cfgDispOnMin   = MenuItem(menu, "Disp On Min", 2);
    MenuItem dstStartMonth  = MenuItem(menu, "DST Start Mon", 2);
    MenuItem dstStartSunday = MenuItem(menu, "DST Start Sun", 2);
    MenuItem dstOffset      = MenuItem(menu, "DST Offset", 2);
    MenuItem dstEndMonth    = MenuItem(menu, "DST End Month", 2);
    MenuItem dstEndSunday   = MenuItem(menu, "DST End Sun", 2);
    MenuItem nonDSTOffset   = MenuItem(menu, "Std Offset", 2);
    MenuItem dstSave        = MenuItem(menu, "Save Config", 3);
    MenuItem dstCancel      = MenuItem(menu, "Discard Changes", 3);

//====================================================================================================
// Alarm Variables
//====================================================================================================
uint32_t uiAlarmStepStartTime = 0;
uint8_t uiAlarmStep = 0;
struct AlarmSound_t {
  uint16_t frequency;
  uint16_t duration;
};
const AlarmSound_t asAlarmArray[6] = {{880, 100}, {1109, 100}, {1319, 100}, {1109, 100}, {880, 100}, {0, 500}};
const uint8_t uiAlarmArraySize = 6;

//====================================================================================================
// Timer States
//====================================================================================================
enum TimerState { Off, On, Expired };

//====================================================================================================
// Setup
//====================================================================================================
void setup() {
  setupMenu();
  setupSerial();
  setupLCD();
  setupRTC();
  setupBigDigitPins();
  readEEPROMConfig();
  initTimeVariables();

  updateLCDMenu();

  Watchdog.enable(2000);

  Serial.print(F("Free RAM: "));
  Serial.println(free_ram());
//  Serial.print(F("Size of Config: "));
//  Serial.println(sizeof(configSettings));
//  Serial.print(F("Size of DateTime: "));
//  Serial.println(sizeof(last));
//  Serial.print(F("Const: "));
//  Serial.println(sizeof(APP_MAJOR));
//  Serial.print(F("Size of MenuItem: "));
//  Serial.println(sizeof(timerMenu));
//  Serial.print(F("Size of MenuBackend: "));
//  Serial.println(sizeof(menu));

//  playAlarm();
}

//====================================================================================================
// Loop
//====================================================================================================
void loop() {
  uint8_t uiButtons = buttonsReleased();

  if (uiButtons & BUTTON_UP) {
    menu.moveUp();                                                                      // If UP was pressed, move the menu up.
  }
  if (uiButtons & BUTTON_DOWN) {
    menu.moveDown();                                                                    // If DOWN was pressed, move the menu down.
  }
  if (uiButtons & BUTTON_LEFT) {
    if (menu.getCurrent().getLevel() == 2) {
      menu.getCurrent().decrement();                                                    // If LEFT was pressed, and we're in menu level 2, decrement the value.
    }
  }
  if (uiButtons & BUTTON_RIGHT) {
    if (menu.getCurrent().getLevel() == 2) {
      menu.getCurrent().increment();                                                    // If RIGHT was pressed, and we're in menu level 2, increment the value.
    }
  }
  if (uiButtons & BUTTON_SELECT) {
    menu.use();                                                                         // If SELECT was pressed, call the use function.
    if (menu.getCurrent().getLevel() == 1) {
      menu.moveRight();                                                                 // If we're in menu level 1, move right (into the submenu).
    } else if(menu.getCurrent().getLevel() == 3) {
      menu.moveLeft();                                                                  // If we're in menu level 3, move left (out of the submenu).
    }
  }
  if (uiButtons) {
    updateLCDMenu();                                                                    // If any button was pressed, update the LCD menu line.
  }

  if (uiButtons | rtc.now().unixtime() != last.unixtime()) {                            // If a button was pressed, or the time has changed...
    last = rtc.now();                                                                   // Update the last variable

    lcd.setCursor(0,0);                                                                 // Set the cursor to 0,0
    TimeToLCD(LocalTime(last), isTimerRunning());                                       // Write the time to the LCD
    if (isTimerOff()) {
      TimeToBigDigits(LocalTime(last));
    } else if (isTimerOn()) {
      TimeToBigDigits(dtTimerEnd - last);
    } else if (isTimerExpired()) {
      TimeToBigDigits(last - dtTimerEnd);
    }

    if (last.minute() == 30 && last.second() <= 1) {                                    // If it is in the first two seconds of the half hour (e.g. XX:30:00 - XX:30:01)...
        ComputeDSTDates(last);                                                          // Recalculate the DST dates.
        Serial.print(F("Free RAM: "));
        Serial.println(free_ram());
    }
  }

  if (isTimerOn() && last.unixtime() >= dtTimerEnd.unixtime()) {                        // If the timer is enabled, and the current time is later than the timer end time...
    TimerExpire();                                                                      // set the timer to expired...
    startAlarm();                                                                       // start the alarm.
  }
  playAlarm();                                                                          // Play the alarm.
  Watchdog.reset();                                                                     // Reset the watchdog
}


//====================================================================================================
//====================================================================================================
//
// Setup and Initialization routines
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Serial Setup
//====================================================================================================
void setupSerial() {
  while (!Serial); // for Leonardo/Micro/Zero
  Serial.begin(230400);
  Serial.println(F("=================================================="));
  Serial.println(F("     Big Clock"));
  Serial.println(F("=================================================="));
//  Serial.print(F("Free RAM 1: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// LCD Setup
//====================================================================================================
void setupLCD() {
  lcd.begin(16, 2);
  lcd.setBacklight(BL_RED);
//  lcd.cursor();
//  lcd.blink();
//  Serial.print(F("Free RAM 2: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// RTC Setup
//====================================================================================================
void setupRTC() {
  if (! rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    while (1);
  }

  if (! rtc.initialized()) {
    Serial.println(F("RTC is NOT running!"));
  }
//  rtc.adjust((DateTime(F(__DATE__), F(__TIME__)) - TimeSpan(0,-5,0,0)) + TimeSpan(0,0,0,5));
//  rtc.adjust(DateTime(2017,1,1,4,59,55));

//  Serial.print(F("Free RAM 3: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Big Digit Display Setup
//====================================================================================================
void setupBigDigitPins() {
// Setup pins for seven segment display
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  digitalWrite(DATA_PIN, LOW);
  digitalWrite(CLOCK_PIN, LOW);
  digitalWrite(LATCH_PIN, LOW);  

//  Serial.print(F("Free RAM 4: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Read configuration from EEPROM
//====================================================================================================
void readEEPROMConfig() {
  ConfigSettings_t readConfigSettings;
  EEPROM.get(0, readConfigSettings);
  if (strcmp(configSettings.ConfigApp, readConfigSettings.ConfigApp) != 0 ||
      configSettings.ConfigMajorVersion != readConfigSettings.ConfigMajorVersion ||
      configSettings.ConfigMinorVersion != readConfigSettings.ConfigMinorVersion ||
      configSettings.ConfigRevision != readConfigSettings.ConfigRevision) {
    EEPROM.put(0, configSettings);
    Serial.println(F("Saved Config Defaults to EEPROM"));
  } else {
    Serial.println(F("Read Config from EEPROM"));
    configSettings = readConfigSettings;
  }

  Serial.print(F("Major Version: "));
  Serial.println(configSettings.ConfigMajorVersion);
  Serial.print(F("Minor Version: "));
  Serial.println(configSettings.ConfigMinorVersion);
  Serial.print(F("Revision: "));
  Serial.println(configSettings.ConfigRevision);
  Serial.print(F("DST Start Month: "));
  Serial.println(configSettings.DSTStartMonth);
  Serial.print(F("DST Start Sunday: "));
  Serial.println(configSettings.DSTStartSunday);
  Serial.print(F("DST Offset: "));
  Serial.println(configSettings.DSTOffset);
  Serial.print(F("DST End Month: "));
  Serial.println(configSettings.DSTEndMonth);
  Serial.print(F("DST End Sunday: "));
  Serial.println(configSettings.DSTEndSunday);
  Serial.print(F("Standard Offset: "));
  Serial.println(configSettings.StandardOffset);
  Serial.print(F("Timer Days: "));
  Serial.println(configSettings.tsTimer.days());
  Serial.print(F("Timer Hours: "));
  Serial.println(configSettings.tsTimer.hours());
  Serial.print(F("Timer Minutes: "));
  Serial.println(configSettings.tsTimer.minutes());
  Serial.print(F("Timer Seconds: "));
  Serial.println(configSettings.tsTimer.seconds());
  Serial.print(F("Display Off Hour: "));
  Serial.println(configSettings.dtDisplayOff.hour());
  Serial.print(F("Display Off Minute: "));
  Serial.println(configSettings.dtDisplayOff.minute());
  Serial.print(F("Display On Hour: "));
  Serial.println(configSettings.dtDisplayOn.hour());
  Serial.print(F("Display On Minute: "));
  Serial.println(configSettings.dtDisplayOn.minute());

//  Serial.print(F("Free RAM 5: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Write configuration into EEPROM
//====================================================================================================
void writeEEPROMConfig() {
  EEPROM.put(0, configSettings);
}

//====================================================================================================
// Initialize last and DST variables
//====================================================================================================
void initTimeVariables() {
  last = rtc.now();
//  ComputeDSTDates(LocalTime(last));
  ComputeDSTDates(last);

//  Serial.print(F("Free RAM 6: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Setup Menu
//====================================================================================================
void setupMenu() {
  menu.getRoot().add(timerStartStop);
  timerStartStop.addBefore(timerStartStop);
  timerStartStop.addAfter(timerMenu);
    timerHour.addAfter(timerMinute);
    timerMinute.addAfter(timerSecond);
    timerSecond.addAfter(timerSave);
    timerSave.addAfter(timerCancel);
    timerSave.addLeft(timerMenu);
    timerCancel.addLeft(timerMenu);
    timerHour.addLeft(timerMenu);
//  timerMenu.addBefore(timerMenu);
  timerMenu.addAfter(configTime);
    setHour.addAfter(setMinute);
    setMinute.addAfter(setSecond);
    setSecond.addAfter(setYear);
    setYear.addAfter(setMonth);
    setMonth.addAfter(setDay);
    setDay.addAfter(timeSave);
    timeSave.addAfter(timeCancel);
    timeSave.addLeft(configTime);
    timeCancel.addLeft(configTime);
    setHour.addLeft(configTime);
  configTime.addAfter(configuration);
    cfgDispOffHour.addAfter(cfgDispOffMin);
    cfgDispOffMin.addAfter(cfgDispOnHour);
    cfgDispOnHour.addAfter(cfgDispOnMin);
    cfgDispOnMin.addAfter(dstStartMonth);
    dstStartMonth.addAfter(dstStartSunday);
    dstStartSunday.addAfter(dstOffset);
    dstOffset.addAfter(dstEndMonth);
    dstEndMonth.addAfter(dstEndSunday);
    dstEndSunday.addAfter(nonDSTOffset);
    nonDSTOffset.addAfter(dstSave);
    dstSave.addAfter(dstCancel);
    dstSave.addLeft(configuration);
    dstCancel.addLeft(configuration);
    cfgDispOffHour.addLeft(configuration);
//    dstStartMonth.addLeft(configuration);

    menu.select(timerStartStop);

//  Serial.print(F("Free RAM 7: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Returns the amount of free memory
//====================================================================================================
int free_ram () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}


//====================================================================================================
//====================================================================================================
//
// Big Digit Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Writes the time to the big digit display
//====================================================================================================
void TimeToBigDigits(DateTime dtTime) {
  boolean bDisplayOn = false;
  boolean bPM = false;
  uint8_t uiHour;
  DateTime dtDisplayOffToday = DateTime(dtTime.year(), dtTime.month(), dtTime.day(), configSettings.dtDisplayOff.hour(), configSettings.dtDisplayOff.minute(), configSettings.dtDisplayOff.second());
  DateTime dtDisplayOnToday  = DateTime(dtTime.year(), dtTime.month(), dtTime.day(), configSettings.dtDisplayOn.hour(), configSettings.dtDisplayOn.minute(), configSettings.dtDisplayOn.second());

  if (dtTime.hour() == 0) {
    uiHour = 12;
    bPM = false;
  } else if (dtTime.hour() < 12) {
    uiHour = dtTime.hour();
    bPM = false;
  } else if (dtTime.hour() == 12) {
    uiHour = dtTime.hour();
    bPM = true;
  } else {
    uiHour = dtTime.hour() - 12;
    bPM = true;
  }

  if (configSettings.dtDisplayOff.unixtime() < configSettings.dtDisplayOn.unixtime()) {
    if (dtTime.unixtime() >= dtDisplayOffToday.unixtime() && dtTime.unixtime() <= dtDisplayOnToday.unixtime()) {
      bDisplayOn = false;
    } else {
      bDisplayOn = true;
    }
  } else {
    if (dtTime.unixtime() >= dtDisplayOffToday.unixtime() || dtTime.unixtime() <= dtDisplayOnToday.unixtime()) {
      bDisplayOn = false;
    } else {
      bDisplayOn = true;
    }
  }
  if (bDisplayOn) {
    numberToBigDigits((uiHour * 100) + dtTime.minute(), bPM);
  } else {
    numberToBigDigits(0,false);
  }
}

//====================================================================================================
// Writes a time span to the big digit display
//====================================================================================================
void TimeToBigDigits(TimeSpan tsTimeSpan) {
  if (tsTimeSpan.hours() == 0) {
    numberToBigDigits((tsTimeSpan.minutes() * 100) + tsTimeSpan.seconds(), false);  
  } else {
    numberToBigDigits((tsTimeSpan.hours() * 100) + tsTimeSpan.minutes(), false);      
  }
}

//====================================================================================================
// Displays a four digit number (positive only)
//====================================================================================================
void numberToBigDigits(float value, boolean bPM) {
  long number = abs(value); //Remove negative signs and any decimals

  //Serial.print("Number: ");
  //Serial.println(number);

  for (byte x = 0 ; x < 4 ; x++)
  {
    byte remainder = number % 10;

    if (number == 0) {
      remainder = ' ';
    }

    if (x == 2 || (x == 0 && bPM)) {
      postNumber(remainder, true);
    } else {
      postNumber(remainder, false);
    }

    number /= 10;
  }

  toggleLatch();

//  Serial.print(F("Free RAM 8: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Shifts one character out to the large digit display
//====================================================================================================
void postNumber(byte number, boolean decimal) {
  //    -  A
  //   / / F/B
  //    -  G
  //   / / E/C
  //    -. D/DP

  const uint8_t  a = 1<<0;
  const uint8_t  b = 1<<6;
  const uint8_t  c = 1<<5;
  const uint8_t  d = 1<<4;
  const uint8_t  e = 1<<3;
  const uint8_t  f = 1<<1;
  const uint8_t  g = 1<<2;
  const uint8_t dp = 1<<7;

  byte segments;

  switch (number)
  {
    case 1: segments = b | c; break;
    case 2: segments = a | b | d | e | g; break;
    case 3: segments = a | b | c | d | g; break;
    case 4: segments = f | g | b | c; break;
    case 5: segments = a | f | g | c | d; break;
    case 6: segments = a | f | g | e | c | d; break;
    case 7: segments = a | b | c; break;
    case 8: segments = a | b | c | d | e | f | g; break;
    case 9: segments = a | b | c | d | f | g; break;
    case 0: segments = a | b | c | d | e | f; break;
    case ' ': segments = 0; break;
    case '-': segments = g; break;

    case 'a': case 'A': segments = a | b | c | e | f | g; break;
    case 'b': case 'B': segments = c | d | e | f | g; break;
    case 'c': segments = d | e | g; break;
    case 'C': segments = a | d | e | f; break;
    case 'd': case 'D': segments = b | c | d | e | g; break;
    case 'e': case 'E': segments = a | d | e | f | g; break;
    case 'f': case 'F': segments = a | e | f | g; break;
    case 'g': case 'G': segments = a | c | d | e | f; break;
    case 'h': case 'H': case 'k': case 'K': case 'x': case 'X': segments = b | c | e | f | g; break;
    case 'i': case 'I': segments = e | f; break;
    case 'j': case 'J': segments = b | c | d | e; break;
    case 'l': case 'L': segments = d | e | f; break;
    case 'm': case 'M': segments = a | c | e; break;
    case 'n': case 'N': segments = c | e | g; break;
    case 'o': segments = c | d | e | g; break;
    case 'O': segments = a | b | c | d | e | f; break;
    case 'p': case 'P': segments = a | b | e | f | g; break;
    case 'r': case 'R': segments = e | g; break;
    case 's': case 'S': segments = a | c | d | f | g; break;
    case 't': case 'T': segments = d | e | f | g; break;
    case 'u': case 'v': segments = c | d | e; break;
    case 'U': case 'V': segments = b | c | d | e | f; break;
    case 'w': case 'W': segments = b | d | f; break;
    case 'y': case 'Y': segments = b | c | d | f | g; break;
    case 'z': case 'Z': segments = a | b | d | e | g; break;
  }

  if (decimal) segments |= dp;

  //Clock these bits out to the drivers
  for (byte x = 0 ; x < 8 ; x++)
  {
    digitalWrite(CLOCK_PIN, LOW);
    digitalWrite(DATA_PIN, segments & 1 << (7 - x));
    digitalWrite(CLOCK_PIN, HIGH); //Data transfers to the register on the rising edge of SRCK
  }

//  Serial.print(F("Free RAM 9: "));
//  Serial.println(free_ram());
}

//====================================================================================================
// Writes four characters to the display
//====================================================================================================
void writeDisplay(byte* value, boolean dpZero, boolean dpOne, boolean dpTwo, boolean dpThree) {
  postNumber(value[3], dpThree);
  postNumber(value[2], dpTwo);
  postNumber(value[1], dpOne);
  postNumber(value[0], dpZero);
  toggleLatch();
}

//====================================================================================================
// Toggles the latch pin so the shifted in data is displayed
//====================================================================================================
void toggleLatch() {
  //Latch the current segment data
  digitalWrite(LATCH_PIN, LOW);
  digitalWrite(LATCH_PIN, HIGH); //Register moves storage register on the rising edge of RCK
}

//====================================================================================================
//====================================================================================================
//
// Date and time functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Computes start and end dates for DST
//====================================================================================================
void ComputeDSTDates (DateTime dtCurrentDate) {
  uint8_t uiDate = 1 + ((configSettings.DSTStartSunday - 1) * 7);                                                                           // First guess for start date
  uiDate += (7 - (DateTime(dtCurrentDate.year(), configSettings.DSTStartMonth, uiDate, 2, 0, 0)).dayOfTheWeek()) % 7;                       // Adjust by finding the day of the week, and adding the distance to Sunday
  dtDSTStart = DateTime(dtCurrentDate.year(), configSettings.DSTStartMonth, uiDate, 2, 0, 0) - TimeSpan(0,configSettings.StandardOffset,0,0);   // DateTime object on that given date, with the appropriate timezone offset

  uiDate = 1 + ((configSettings.DSTEndSunday - 1) * 7);
  uiDate += (7 - (DateTime(dtCurrentDate.year(), configSettings.DSTEndMonth, uiDate, 2, 0, 0)).dayOfTheWeek()) % 7;
  dtDSTEnd = DateTime(dtCurrentDate.year(), configSettings.DSTEndMonth, uiDate, 2, 0, 0) - TimeSpan(0,configSettings.DSTOffset,0,0);

  Serial.print(F("DST Starts: "));
  Serial.print(dtDSTStart.year());
  Serial.print(F("-"));
  Serial.print(dtDSTStart.month());
  Serial.print(F("-"));
  Serial.print(dtDSTStart.day());
  Serial.print(F(" "));
  Serial.print(dtDSTStart.hour());
  Serial.print(F(":"));
  Serial.print(dtDSTStart.minute());
  Serial.print(F(":"));
  Serial.print(dtDSTStart.second());
  Serial.println(F(" UTC"));

  Serial.print(F("DST Ends: "));
  Serial.print(dtDSTEnd.year());
  Serial.print(F("-"));
  Serial.print(dtDSTEnd.month());
  Serial.print(F("-"));
  Serial.print(dtDSTEnd.day());
  Serial.print(F(" "));
  Serial.print(dtDSTEnd.hour());
  Serial.print(F(":"));
  Serial.print(dtDSTEnd.minute());
  Serial.print(F(":"));
  Serial.print(dtDSTEnd.second());
  Serial.println(F(" UTC"));
}

//====================================================================================================
// Converts time from UTC to local time
//====================================================================================================
DateTime LocalTime (DateTime dtUTC) {
  DateTime dtLocal;
  TimeSpan tsTimeAfterDSTStart = dtUTC - dtDSTStart;
  TimeSpan tsTimeBeforeDSTEnd = dtDSTEnd - dtUTC;

  dtLocal = dtUTC + TimeSpan(0,configSettings.StandardOffset,0,0);
  if (tsTimeAfterDSTStart.totalseconds() >= 0 && tsTimeBeforeDSTEnd.totalseconds() > 0 ) {
    dtLocal = dtUTC + TimeSpan(0,configSettings.DSTOffset,0,0);
  }

  return dtLocal;
}

//====================================================================================================
// Converts time from local time to UTC
//====================================================================================================
DateTime UTCTime (DateTime dtLocal) {
  DateTime dtUTC;
  TimeSpan tsTimeAfterDSTStart = dtLocal - LocalTime(dtDSTStart);
  TimeSpan tsTimeBeforeDSTEnd = LocalTime(dtDSTEnd) - dtLocal;

  dtUTC = dtLocal - TimeSpan(0,configSettings.StandardOffset,0,0);
  if (tsTimeAfterDSTStart.totalseconds() >= 0 && tsTimeBeforeDSTEnd.totalseconds() > 0 ) {
    dtUTC = dtLocal - TimeSpan(0,configSettings.DSTOffset,0,0);
  }

  return dtUTC;
}


////====================================================================================================
//// Returns whether or not a UTC date and time is DST
////====================================================================================================
//bool isDST (DateTime dtUTC) {
//  bool bIsDST = false;
//  TimeSpan tsTimeAfterDSTStart = dtUTC - dtDSTStart;
//  TimeSpan tsTimeBeforeDSTEnd = dtDSTEnd - dtUTC;
//
//  if (tsTimeAfterDSTStart.totalseconds() >= 0 && tsTimeBeforeDSTEnd.totalseconds() > 0 ) {
//    bIsDST = true;
//  }
//
//  return bIsDST;
//}

//====================================================================================================
//====================================================================================================
//
// Button Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Returns whether or not a button was released since the last time the function was called
//====================================================================================================
uint8_t buttonsReleased() {
  static uint8_t suiButtonsDepressed = 0;
  uint8_t uiButtonsPressed = lcd.readButtons();
  uint8_t uiButtonsReleased = 0;

  if (uiButtonsPressed & BUTTON_UP) {
    suiButtonsDepressed = suiButtonsDepressed | BUTTON_UP;
  } else if (suiButtonsDepressed & BUTTON_UP) {
    suiButtonsDepressed = suiButtonsDepressed & !(BUTTON_UP);
    uiButtonsReleased = uiButtonsReleased | BUTTON_UP;
  }
  if (uiButtonsPressed & BUTTON_DOWN) {
    suiButtonsDepressed = suiButtonsDepressed | BUTTON_DOWN;
  } else if (suiButtonsDepressed & BUTTON_DOWN) {
    suiButtonsDepressed = suiButtonsDepressed & !(BUTTON_DOWN);
    uiButtonsReleased = uiButtonsReleased | BUTTON_DOWN;
  }
  if (uiButtonsPressed & BUTTON_LEFT) {
    suiButtonsDepressed = suiButtonsDepressed | BUTTON_LEFT;
  } else if (suiButtonsDepressed & BUTTON_LEFT) {
    suiButtonsDepressed = suiButtonsDepressed & !(BUTTON_LEFT);
    uiButtonsReleased = uiButtonsReleased | BUTTON_LEFT;
  }
  if (uiButtonsPressed & BUTTON_RIGHT) {
    suiButtonsDepressed = suiButtonsDepressed | BUTTON_RIGHT;
  } else if (suiButtonsDepressed & BUTTON_RIGHT) {
    suiButtonsDepressed = suiButtonsDepressed & !(BUTTON_RIGHT);
    uiButtonsReleased = uiButtonsReleased | BUTTON_RIGHT;
  }
  if (uiButtonsPressed & BUTTON_SELECT) {
    suiButtonsDepressed = suiButtonsDepressed | BUTTON_SELECT;
  } else if (suiButtonsDepressed & BUTTON_SELECT) {
    suiButtonsDepressed = suiButtonsDepressed & !(BUTTON_SELECT);
    uiButtonsReleased = uiButtonsReleased | BUTTON_SELECT;
  }

  return uiButtonsReleased;
}

//====================================================================================================
//====================================================================================================
//
// LCD Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Prints a number to the LCD
//====================================================================================================
void LCDPrintNumber(long lNumber, int iLength) {
  int iOrderOfMagnitude = 1;
  Serial.print(F("Order Of Magnitude: "));
  while (pow(10, iOrderOfMagnitude) + 0.5 < lNumber) {
//  while (lNumber / (10^iOrderOfMagnitude) >= 1) {
    iOrderOfMagnitude++;
  }
  for (int i = 0; i < iLength - iOrderOfMagnitude - 1; i++) {
    lcd.print(F("0"));
  }
  lcd.print(lNumber);
  lcd.print(F("   "));
  Serial.println(iOrderOfMagnitude);
  Serial.println();
  Serial.println(pow(10,0));
  Serial.println(pow(10,1));
  Serial.println(pow(10,2));
  Serial.println(pow(10,3));
  Serial.println(pow(10,4));
}

//====================================================================================================
// Writes a date out to the LCD
//====================================================================================================
void DateToLCD(DateTime dtDate) {
  lcd.print(dtDate.year(), DEC);
  lcd.print(F("/"));
  if (dtDate.month() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(dtDate.month(), DEC);
  lcd.print(F("/"));
  if (dtDate.day() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(dtDate.day(), DEC);
}

//====================================================================================================
// Writes the time out to the LCD
//====================================================================================================
void TimeToLCD(DateTime dtTime, boolean bTimer) {
  uint8_t uiHour = 0;
  boolean bPM = false;
  if (dtTime.hour() == 0) {                                 // Hour is 12AM (Midnight)
    lcd.print(F("12"));
    uiHour = 12;
  } else if (dtTime.hour() < 10) {                          // Hour is before 10AM (i.e. 1AM - 9AM)
    lcd.print(F(" "));
    lcd.print(dtTime.hour(), DEC);
    uiHour = dtTime.hour();
  } else if (dtTime.hour() > 12 && dtTime.hour() < 22) {    // Hour is after 12PM (noon) and before 10PM (i.e. 1PM - 9PM)
    lcd.print(F(" "));
    lcd.print(dtTime.hour() - 12, DEC);
    uiHour = dtTime.hour() - 12;
  } else if (dtTime.hour() > 12) {                          // Hour is 10-12PM
    lcd.print(dtTime.hour() - 12, DEC);
    uiHour = dtTime.hour() - 12;
  } else {                                                  // Hour is 10-12AM
    lcd.print(dtTime.hour(), DEC);
    uiHour = dtTime.hour();
  }
  lcd.print(F(":"));
  if (dtTime.minute() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(dtTime.minute(), DEC);
  lcd.print(F(":"));
  if (dtTime.second() < 10) {
    lcd.print(F("0"));
  }
  lcd.print(dtTime.second(), DEC);
  if (dtTime.hour() < 12) {
    lcd.print(F(" AM"));
    bPM = false;
  } else {
    lcd.print(F(" PM"));
    bPM = true;
  }

//  if (timerStartStop.getValue() != 0) {
  if (bTimer) {
    lcd.print(" T");
    Serial.print(dtTime.hour());
    Serial.print(F(":"));
    Serial.print(dtTime.minute());
    Serial.print(F(":"));
    Serial.print(dtTime.second());
    Serial.print(F(" "));
    Serial.print(dtTimerEnd.hour());
    Serial.print(F(":"));
    Serial.print(dtTimerEnd.minute());
    Serial.print(F(":"));
    Serial.println(dtTimerEnd.second());

//    if (dtTime.unixtime() >= dtTimerEnd.unixtime()) {
//      showTime(((dtTime - dtTimerEnd).minutes() * 100) + (dtTime - dtTimerEnd).seconds(), false);
//    } else {
//      showTime(((dtTimerEnd - dtTime).minutes() * 100) + (dtTimerEnd - dtTime).seconds(), false);      
//    }
  } // else {
//    boolean bDisplayOn = false;
//    DateTime dtDisplayOffToday = DateTime(dtTime.year(), dtTime.month(), dtTime.day(), configSettings.dtDisplayOff.hour(), configSettings.dtDisplayOff.minute(), configSettings.dtDisplayOff.second());
//    DateTime dtDisplayOnToday  = DateTime(dtTime.year(), dtTime.month(), dtTime.day(), configSettings.dtDisplayOn.hour(), configSettings.dtDisplayOn.minute(), configSettings.dtDisplayOn.second());
//    if (configSettings.dtDisplayOff.unixtime() < configSettings.dtDisplayOn.unixtime()) {
//      if (dtTime.unixtime() >= dtDisplayOffToday.unixtime() && dtTime.unixtime() <= dtDisplayOnToday.unixtime()) {
//        bDisplayOn = false;
//      } else {
//        bDisplayOn = true;
//      }
//    } else {
//      if (dtTime.unixtime() >= dtDisplayOffToday.unixtime() || dtTime.unixtime() <= dtDisplayOnToday.unixtime()) {
//        bDisplayOn = false;
//      } else {
//        bDisplayOn = true;
//      }
//    }
//    if (bDisplayOn) {
//      showTime((uiHour * 100) + dtTime.minute(), bPM);
//    } else {
//      showTime(0,false);
//    }
//  }

  lcd.print(F("    "));

}

//====================================================================================================
// Writes the menu out to the LCD
//====================================================================================================
void updateLCDMenu() {
  lcd.setCursor(0,1);
  lcd.print(menu.getCurrent().getName());
  if (menu.getCurrent().getLevel() == 2) {
    lcd.print(F(" "));
    lcd.print(menu.getCurrent().getValue());
  }
  lcd.print(F("                "));
}

//====================================================================================================
//====================================================================================================
//
// Menu Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Event for "Use"
//====================================================================================================
void menuUseEvent(MenuUseEvent used) {
  Serial.print(F("Menu use "));
  Serial.println(used.item.getName());
//  startAlarm();

  if (used.item.isEqual(timerStartStop)) {
    if (alarmPlaying() || used.item.getValue() != 0) {
      stopAlarm();
      used.item.setValue(0);
      stopAlarm();
    } else if (used.item.getValue() == 0) {
      used.item.setValue(1);

      dtTimerEnd = rtc.now() + configSettings.tsTimer;

      Serial.print(F("Timer End Time: "));
      Serial.print(dtTimerEnd.hour());
      Serial.print(F(":"));
      Serial.print(dtTimerEnd.minute());
      Serial.print(F(":"));
      Serial.println(dtTimerEnd.second());
    } //else {
//      used.item.setValue(0);
//      stopAlarm();
//    }
  }

  if (used.item.isEqual(timerMenu)) {
    timerHour.setValue(configSettings.tsTimer.hours());
    timerMinute.setValue(configSettings.tsTimer.minutes());
    timerSecond.setValue(configSettings.tsTimer.seconds());
  }
  if (used.item.isEqual(timerSave)) {
    configSettings.tsTimer = TimeSpan(0, timerHour.getValue(), timerMinute.getValue(), timerSecond.getValue());
    writeEEPROMConfig();
  }

  if (used.item.isEqual(configTime)) {
    setHour.setValue(LocalTime(last).hour());
    setMinute.setValue(LocalTime(last).minute());
    setSecond.setValue(LocalTime(last).second());
    setYear.setValue(LocalTime(last).year());
    setMonth.setValue(LocalTime(last).month());
    setDay.setValue(LocalTime(last).day());
  }
  if (used.item.isEqual(timeSave)) {
    rtc.adjust(UTCTime(DateTime(setYear.getValue(), setMonth.getValue(), setDay.getValue(),
                                          setHour.getValue(), setMinute.getValue(), setSecond.getValue())));
  }

  if (used.item.isEqual(configuration)) {
    cfgDispOffHour.setValue(configSettings.dtDisplayOff.hour());
    cfgDispOffMin.setValue(configSettings.dtDisplayOff.minute());
    cfgDispOnHour.setValue(configSettings.dtDisplayOn.hour());
    cfgDispOnMin.setValue(configSettings.dtDisplayOn.minute());
    dstStartMonth.setValue(configSettings.DSTStartMonth);
    dstStartSunday.setValue(configSettings.DSTStartSunday);
    dstOffset.setValue(configSettings.DSTOffset);
    dstEndMonth.setValue(configSettings.DSTEndMonth);
    dstEndSunday.setValue(configSettings.DSTEndSunday);
    nonDSTOffset.setValue(configSettings.StandardOffset);
  }
  if (used.item.isEqual(dstSave)) {
    configSettings.dtDisplayOff = DateTime(configSettings.dtDisplayOff.year(), configSettings.dtDisplayOff.month(), configSettings.dtDisplayOff.day(), cfgDispOffHour.getValue(), cfgDispOffMin.getValue(), configSettings.dtDisplayOff.second());
    configSettings.dtDisplayOn = DateTime(configSettings.dtDisplayOn.year(), configSettings.dtDisplayOn.month(), configSettings.dtDisplayOn.day(), cfgDispOnHour.getValue(), cfgDispOnMin.getValue(), configSettings.dtDisplayOn.second());
    configSettings.DSTStartMonth = dstStartMonth.getValue();
    configSettings.DSTStartSunday = dstStartSunday.getValue();
    configSettings.DSTOffset = dstOffset.getValue();
    configSettings.DSTEndMonth = dstEndMonth.getValue();
    configSettings.DSTEndSunday = dstEndSunday.getValue();
    configSettings.StandardOffset = nonDSTOffset.getValue();
    writeEEPROMConfig();
  }
}

//====================================================================================================
// Event for "Change"
//====================================================================================================
void menuChangeEvent(MenuChangeEvent changed) {
}

//====================================================================================================
//====================================================================================================
//
// Sound Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Play alarm
//====================================================================================================


void playAlarm() {
  if (uiAlarmStepStartTime != 0) {
    if (millis() - uiAlarmStepStartTime >= asAlarmArray[uiAlarmStep].duration) {
//      Serial.print(F("P: "));
//      Serial.println(uiAlarmStepStartTime);
      uiAlarmStep++;
      uiAlarmStepStartTime = millis();
      if (uiAlarmStep >= uiAlarmArraySize) {
        uiAlarmStep = 0;
      }
      if (asAlarmArray[uiAlarmStep].frequency == 0) {
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, HIGH);
      } else {
        tone(BUZZER_PIN, asAlarmArray[uiAlarmStep].frequency);
      }
    }
  }
}

//====================================================================================================
// Start Alarm
//====================================================================================================
void startAlarm() {
  uiAlarmStepStartTime = millis();
  uiAlarmStep = 0;
  playAlarm();
}

//====================================================================================================
// Stop Alarm
//====================================================================================================
void stopAlarm() {
  uiAlarmStep = 0;
  uiAlarmStepStartTime = 0;
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, HIGH);
}

//====================================================================================================
// Is the alarm playing?
//====================================================================================================
uint8_t alarmPlaying() {
  if (uiAlarmStepStartTime != 0) {
    return 1;
  }
  return 0;
}


//====================================================================================================
//====================================================================================================
//
// Timer Functions
//
//====================================================================================================
//====================================================================================================

//====================================================================================================
// Return the state of the timer
//====================================================================================================
TimerState getTimerState() {
  return (TimerState)timerStartStop.getValue();
}

//====================================================================================================
// Is the Timer On?
//====================================================================================================
boolean isTimerOn() {
  return getTimerState() == TimerState::On;
}

//====================================================================================================
// Is the Timer Off?
//====================================================================================================
boolean isTimerOff() {
  return getTimerState() == TimerState::Off;
}

//====================================================================================================
// Is the Timer Expired?
//====================================================================================================
boolean isTimerExpired() {
  return getTimerState() == TimerState::Expired;
}

//====================================================================================================
// Is the Timer Expired?
//====================================================================================================
boolean isTimerRunning() {
  return getTimerState() != TimerState::Off;
}

//====================================================================================================
// Turn Timer On
//====================================================================================================
void TimerOn() {
  timerStartStop.setValue(TimerState::On);
}

//====================================================================================================
// Turn Timer Off
//====================================================================================================
void TimerOff() {
  timerStartStop.setValue(TimerState::Off);
}

//====================================================================================================
// Is the Timer Expired?
//====================================================================================================
void TimerExpire() {
  timerStartStop.setValue(TimerState::Expired);
}


