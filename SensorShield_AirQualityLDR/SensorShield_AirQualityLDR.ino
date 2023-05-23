#include <RTClib.h>

/*
  SD card datalogger for adafruit shield with Particulate Sensor, Temperature and LDR

  Creative Computing Institute, University Arts London
  Matthew Jarvis 2023

  We are going to use this dust sensor:
  https://wiki.seeedstudio.com/Grove-Dust_Sensor/ which has the PPD42
  and this RTC: PCF8523


  TODO:
  √ Detect if Serial is connected, if so, turn on debug mode
  √ Write date to filename
  √ Write to seperate file for each day?
  √ Not sure the time recieved back from RTC - code was set to reset to last puloaded time when plugged in
  √ Add light sensor
  √ Add TMP36

  When first loading to arduino, to set the clock, uncomment the line in setup()
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  then comment it again and re-upload and check the time is set correctly.
  (if you leave it uncommented, it will always think the time is whenever the code was compiled)

  Data is written with the date, ie 20230420.csv and the columns are:

    timestamp, lowpulseoccupancy, ratio, concentration, ldr, temp, recordcount
    2023-04-20 17:17:21,334364,2.90,1503.28,678,21.29,375466,353


*/

#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_SleepyDog.h>

#define aref_voltage 5

RTC_PCF8523 rtc;
char dateTimeBuffer[24];
char dateBuffer[12];

// set chipselect for SD card
const int chipSelect = 10;

int errors = 0;
int errorLEDPin = 2;
int okLEDPin = 3;
int dataWriteLEDPin = 13;
int ldrPin = A0;
int tempPin = A1;
int dustSensorPin = 8;

float temperatureC;
float tempReading;
int ldrReading;
float voltage;

unsigned long duration;
unsigned long starttime;
unsigned long lowpulseoccupancy = 0;
float ratio = 0;
float concentration = 0;
unsigned long savetime;


// make an empty string for assembling the data to log per line:
String dataString = "";
// make an empty string turning the time into a string:
String timeString = "";

String dataFileNameWithTime;
String dataFileExtention = ".csv";
int recordCount;

// Change things here

int interval_to_save = 60; // seconds inbetween saving the data
bool debugMode = true; // set to 'true' to see data from Serial Port or 'false' for use in the wild
unsigned long dust_sampletime_ms = 30000;//sample 30s ;
int ignoreFirstReadings = 5;


void setup() {
  delay(2000);

  // set up pins
  pinMode(okLEDPin, OUTPUT);
  pinMode(errorLEDPin, OUTPUT);
  pinMode(dataWriteLEDPin, OUTPUT);
  pinMode(tempPin, INPUT);
  pinMode(ldrPin, INPUT);

  digitalWrite(okLEDPin, HIGH);
  digitalWrite(errorLEDPin, LOW);


  Serial.begin(9600);
  if (Serial) {
    debugMode = true;
  } else {
    debugMode = false;
  }

  if (debugMode) {
    Serial.print("Initializing SD card...");
  }

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    if (debugMode) {
      Serial.println("Card failed, or not present");
    }
    // turn on the error light
    digitalWrite(errorLEDPin, HIGH);
    delay(10000);
    while (1); // stop the program
  }
  if (debugMode) {
    Serial.println("card initialized.");
  }

  if (! rtc.begin()) {
    if (debugMode) {
      Serial.println("Couldn't find RTC");
    }
    digitalWrite(errorLEDPin, HIGH);
    delay(10000);
    while (1); // stop the program
  }


  // set the RTC from the program complilation time:
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // If you want to set the RTC with an explicit date & time, for example to set
  // July 6, 2023 at 3.30pm you would call:
  // rtc.adjust(DateTime(2023, 7, 6, 15, 30, 0));


  // When the RTC was stopped and stays connected to the battery, it has
  // to be restarted by clearing the STOP bit. Let's do this to ensure
  // the RTC is running.
  rtc.start();

  starttime = millis(); // used to get interval readings for dust sensor;
  savetime = millis(); // used to get interval about when to save

  analogReference(EXTERNAL);

  // set 'working' pin high
  digitalWrite(okLEDPin, HIGH);
  digitalWrite(errorLEDPin, LOW);
  delay(1000);
}

void loop() {

  DateTime now = rtc.now();

  // Build the time into a format we can use IE 1970-01-01 00:00:00
  // create time String with leading zeros
  sprintf(dateTimeBuffer, "%04u-%02u-%02u %02u:%02u:%02u", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  timeString = dateTimeBuffer;


  // sample the dust sensor as when required
  duration = pulseIn(dustSensorPin, LOW);
  lowpulseoccupancy = lowpulseoccupancy + duration;

  if ((millis() - starttime) > dust_sampletime_ms) //if the sample time has passed, take new readings
  {
    ratio = lowpulseoccupancy / (dust_sampletime_ms * 10.0); // Integer percentage 0=>100
    concentration = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62; // using spec sheet curve
    if (debugMode) {
      Serial.print("lowpulseoccupancy:");
      Serial.print(lowpulseoccupancy);
      Serial.print(",");
      Serial.print("ratio:");
      Serial.print(ratio);
      Serial.print(",");
      Serial.print("concentration");
      Serial.println(concentration);
    }
    lowpulseoccupancy = 0;
    starttime = millis();
  }

  // clear the dataString for assembling the data:
  dataString = "";

  // add the time to the data
  dataString += String(timeString);
  dataString += ",";
  dataString += String(lowpulseoccupancy);
  dataString += ",";
  dataString += String(ratio);
  dataString += ",";
  dataString += String(concentration);
  dataString += ",";

  // read the sensors and append to the string:
  ldrReading = analogRead(ldrPin);
  tempReading = analogRead(tempPin);

  // converting that reading to voltage, which is based off the reference voltage
  float voltage = tempReading * aref_voltage / 1024;

  float temperatureC = (voltage - 0.5) * 100 ;  //converting from 10 mv per degree wit 500 mV offset
  //to degrees ((volatge - 500mV) times 100)

  if (debugMode) {
    Serial.print("LDR reading = ");
    Serial.print(ldrReading);     // the raw analog reading
    Serial.print(" Temp reading = ");
    Serial.print(tempReading);     // the raw analog reading

    // print out the voltage
    Serial.print(" - ");
    Serial.print(voltage); Serial.print(" volts. Which makes it ");
    Serial.print(temperatureC); Serial.print(" degrees C");
    Serial.print(" reading:"); Serial.println(recordCount);
  }

  dataString += String(ldrReading);
  dataString += ",";
  dataString += String(temperatureC);
  dataString += ",";
  dataString += String(recordCount);
  recordCount ++;



  if ((millis() - savetime) > (interval_to_save * 1000)) {
    if (recordCount > ignoreFirstReadings) {
      digitalWrite(dataWriteLEDPin, HIGH);
      // open the file. note that only one file can be open at a time,
      // so you have to close this one before opening another.

      sprintf(dateBuffer, "%04u%02u%02u", now.year(), now.month(), now.day());

      dataFileNameWithTime = dateBuffer + dataFileExtention; // + now.year() + now.month() + now.day()

      // set date time callback function
      SdFile::dateTimeCallback(dateTime);
      File dataFile = SD.open(dataFileNameWithTime, FILE_WRITE);

      // if the file is available, write to it:
      if (dataFile) {
        dataFile.println(dataString);
        dataFile.close();
        // if in debug, print to the serial port too:
        if (debugMode) {
          Serial.print(dataString);
          Serial.print (" written to ");
          Serial.println(dataFileNameWithTime);
        }
      }
      // if the file isn't open, pop up an error:
      else {
        if (debugMode) {
          Serial.print("error opening ");
          Serial.println(dataFileNameWithTime);
        }
        digitalWrite(dataWriteLEDPin, HIGH);
        while (1); // stop the program
      }
      digitalWrite(dataWriteLEDPin, LOW);
      savetime = millis();
      // put arduino in low power mode
      int sleepMS = Watchdog.sleep(interval_to_save * 1000);
    }
  }
}




//------------------------------------------------------------------------------
// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = rtc.now();
  sprintf(dateTimeBuffer, "%02d:%02d:%02d %2d/%2d/%2d \n", now.hour(), now.minute(), now.second(), now.month(), now.day(), now.year() - 2000);

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
//------------------------------------------------------------------------------
