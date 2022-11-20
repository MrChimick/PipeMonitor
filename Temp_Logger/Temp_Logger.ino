#include <max6675.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SoftwareSerial.h>

#define DS1307_ADDRESS 0x68


/************************************************
   DATA STRUCTURES
 ***********************************************/
struct DateTime {
  byte year;
  byte month;
  byte monthDay;
  byte weekDay;
  byte hour;
  byte minute;
  byte second;
};

struct FlowTracker {
  bool flowStatus;
  DateTime startTime;
  DateTime endTime;
};


/************************************************
   FILE CONSTANTS
 ***********************************************/
// Quantity constants
const byte ZERO = 0x00;

const byte TEMP_AVG_ARRAY = 5;

const long TEMP_READ_INTERVAL    = 1000;
const long TEMP_COMPARE_INTERVAL = 30000; //CHANGE TO 900000 in for final

const int TEMP_ROOF_FLOW_THRESHOLD  = 5;
const int TEMP_HOUSE_FLOW_THRESHOLD = 5;

const byte NUM_SEC  = 60;
const byte NUM_MIN  = 60;
const byte NUM_HOUR = 24;

const int STARTUP_DELAY = 1000;
const int STARTUP_SPEED = 9600;

const float RES_OHM = 10000; // Resistance of the resistor, in ohms
const float SH_C1 = 1.009249522e-03, SH_C2 = 2.378405444e-04, SH_C3 = 2.019202697e-07; // Steinhart–Hart coefficients

// Pin constants
const byte PIN_TANK_HIGHER = 10;
const byte PIN_TANK_LOWER  = 10;
const byte PIN_ROOF_LOOP   = 10;
const byte PIN_HOUSE_LOOP  = 10;

const byte PIN_MANIFOLD_RX = 10;
const byte PIN_MANIFOLD_TX = 10;

const byte PIN_SD_CS = 15;

const byte PIN_FLOW_ROOF_LOOP  = 66;
const byte PIN_FLOW_HOUSE_LOOP = 65;

// String constants
const String FILE_TEMPERATURE = "temp_log.csv";
const String FILE_FLOW_TIME   = "flow_log.csv";
const String HEAD_TEMPERATURE = "Time,Location,Temperature";
const String HEAD_FLOW_TIME   = "Time Started,Time Ended,Duration,Location";

const String LOC_STR_TANK       = "Tank";
const String LOC_STR_ROOF_LOOP  = "Roof Loop";
const String LOC_STR_HOUSE_LOOP = "House Loop";
const String LOC_STR_MANIFOLD   = "Manifold";


/************************************************
   GLOBAL VARIABLES
 ***********************************************/
// Temperature collection variables
byte arrayIndex = 0;

float tankTempArray[TEMP_AVG_ARRAY];
float tankTempAvg = 0.0;

float roofLoopTempArray[TEMP_AVG_ARRAY];
float roofLoopTempAvg = 0.0;

float houseLoopTempArray[TEMP_AVG_ARRAY];
float houseLoopTempAvg = 0.0;

float manifoldTempArray[TEMP_AVG_ARRAY];
float manifoldTempAvg = 0.0;

// Time interval counters
unsigned long previousMillisRead = 0;
unsigned long previousMillisCompare = 0;

// Flow variables
FlowTracker roofLoopFlowTracker;
FlowTracker houseLoopFlowTracker;

// RTD Serial Connection
SoftwareSerial manifoldSerial(PIN_MANIFOLD_RX, PIN_MANIFOLD_TX);


/************************************************
   HELPER FUNCTIONS
 ***********************************************/
byte DecToBcd(byte val) {
  // Convert normal decimal numbers to binary coded decimal
  return ((val / 10 * 16) + (val % 10));
}

byte BcdToDec(byte val) {
  // Convert binary coded decimal to normal decimal numbers
  return ((val / 16 * 10) + (val % 16));
}

void Serial2LineEnd() {
  for (byte i = 0; i < 3; i++) Serial2.write(0xff);
}

String DateTimeToString(DateTime timePrint) { // return the date EG 21/10/5 23:59:59
  return (
    String(timePrint.year)     + '/' +
    String(timePrint.month)    + '/' +
    String(timePrint.monthDay) + ' ' +
    String(timePrint.hour)     + ':' +
    String(timePrint.minute)   + ':' +
    String(timePrint.second)
  );
}

float ReadThermistor(byte pin) { // returns the reading from a thermistor in Celcius
  int v = analogRead(pin);
  float r = RES_OHM * (1023.0 / (float)v - 1.0); // resistance of the Thermistor
  float logR = log(r);
  float tKelvin = (1.0 / (SH_C1 + SH_C2 * logR + SH_C3 * logR * logR * logR));
  float tCelsius = tKelvin - 273.15;

  return tCelsius;
}

float ReadRTD(SoftwareSerial rtdSerial) {
  // command for a single reading, default units are Celcius
  rtdSerial.print("R\r"); 

  // recieves the response from the sensor
  String sensorstring = "";
  while (rtdSerial.available() > 0) {
    char inchar = (char)rtdSerial.read();
    sensorstring += inchar;
    if (inchar == '\r') break;
  }
  
  // converts response to a number
  float temp = 0.0;
  if (isdigit(sensorstring[0]) || sensorstring[0] == '-') {
    temp = sensorstring.toFloat();
  }
  
  return temp;
}

/************************************************
   DATE & TIME FUNCTIONS
 ***********************************************/
void SetDateTime() {

  byte second   = 45; //0-59
  byte minute   = 40; //0-59
  byte hour     = 0; //0-23
  byte weekDay  = 2; //1-7
  byte monthDay = 1; //1-31
  byte month    = 3; //1-12
  byte year     = 11; //0-99

  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(ZERO);

  Wire.write(DecToBcd(second));
  Wire.write(DecToBcd(minute));
  Wire.write(DecToBcd(hour));
  Wire.write(DecToBcd(weekDay));
  Wire.write(DecToBcd(monthDay));
  Wire.write(DecToBcd(month));
  Wire.write(DecToBcd(year));

  Wire.write(ZERO); //start

  Wire.endTransmission();
}

DateTime GetCurrentDateTime() {
  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(ZERO);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  DateTime currDateTime;
  currDateTime.second   = BcdToDec(Wire.read());
  currDateTime.minute   = BcdToDec(Wire.read());
  currDateTime.hour     = BcdToDec(Wire.read() & 0b111111); //24 hour time
  currDateTime.weekDay  = BcdToDec(Wire.read()); //0-6 -> sunday â€“ Saturday
  currDateTime.monthDay = BcdToDec(Wire.read());
  currDateTime.month    = BcdToDec(Wire.read());
  currDateTime.year     = BcdToDec(Wire.read());

  return currDateTime;
}

String GetCurrentDateTimeStr() {
  return DateTimeToString(GetCurrentDateTime());
}

int GetMinutesBetweenDateTime(DateTime startTime, DateTime endTime) { // Assumes the days are adjacent
  int seconds = 0;

  if (endTime.monthDay != startTime.monthDay) { // different days
    seconds += (NUM_SEC - startTime.second) + endTime.second;
    seconds += ((NUM_MIN - startTime.minute) + endTime.minute) * NUM_SEC;
    seconds += (((NUM_HOUR - startTime.hour) + endTime.hour) * NUM_MIN) * NUM_SEC;
  } else if (endTime.hour != startTime.hour) { // different hours
    seconds += (NUM_SEC - startTime.second) + endTime.second;
    seconds += ((NUM_MIN - startTime.minute) + endTime.minute) * NUM_SEC;
    seconds += ((endTime.hour - startTime.hour) * NUM_MIN) * NUM_SEC;
  } else if (endTime.minute != startTime.minute) { // different minutes
    seconds += (NUM_SEC - startTime.second) + endTime.second;
    seconds += (endTime.minute - startTime.minute) * NUM_SEC;
  } else if (endTime.second != startTime.second) { // different seconds
    seconds += endTime.second - startTime.second;
  } else {
    return 0; // literally the same time
  }

  return round(seconds / NUM_SEC);
}

void PrintCurrentDateTime() {
  DateTime currTime; // declaring a variable of type DateTime
  currTime = GetCurrentDateTime(); // getting the current DateTime using the function

  // accessing all members
  Serial.println("Second: "   + String(currTime.second));
  Serial.println("Minute: "   + String(currTime.minute));
  Serial.println("Hour: "     + String(currTime.hour));
  Serial.println("WeekDay: "  + String(currTime.weekDay));
  Serial.println("MonthDay: " + String(currTime.monthDay));
  Serial.println("Month: "    + String(currTime.month));
  Serial.println("Year: "     + String(currTime.year));

  // I can change the members as well
  currTime.second = 0;
}


/************************************************
   SD FUNCTIONS
 ***********************************************/
void SD_Init() {
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("SD initialization failed");
    return;
  }

  if (!SD.exists(FILE_TEMPERATURE)) {
    File tempLogFile = SD.open(FILE_TEMPERATURE, FILE_WRITE);
    tempLogFile.println(HEAD_TEMPERATURE);
    tempLogFile.close();
    Serial.println(FILE_TEMPERATURE + " created");
  }
  if (!SD.exists(FILE_FLOW_TIME)) {
    File flowLogFile = SD.open(FILE_FLOW_TIME, FILE_WRITE);
    flowLogFile.println(HEAD_FLOW_TIME);
    flowLogFile.close();
    Serial.println(FILE_FLOW_TIME + " created");
  }

  Serial.println("SD initialization complete");
}

void SD_LogTemp(String location, int temp) {
  String currTime = DateTimeToString(GetCurrentDateTime());
  File tempLogFile = SD.open(FILE_TEMPERATURE, FILE_WRITE);
  if (tempLogFile) {
    tempLogFile.println(currTime + "," + location + "," + String(temp));
    tempLogFile.close();
  }
}

void SD_LogFlow(String location, FlowTracker flowEntry) {
  String startTime = DateTimeToString(flowEntry.startTime);
  String endTime = DateTimeToString(flowEntry.endTime);
  int duration = GetMinutesBetweenDateTime(flowEntry.startTime, flowEntry.endTime);

  File flowLogFile = SD.open(FILE_FLOW_TIME, FILE_WRITE);
  if (flowLogFile) {
    flowLogFile.println(startTime + "," + endTime + "," + String(duration) + "," + location);
    flowLogFile.close();
  }
}

void LogAllTemperatures() {
  SD_LogTemp(LOC_STR_HOUSE_LOOP, houseLoopTempAvg);
  SD_LogTemp(LOC_STR_TANK, tankTempAvg);
  SD_LogTemp(LOC_STR_ROOF_LOOP, roofLoopTempAvg);
  SD_LogTemp(LOC_STR_MANIFOLD, manifoldTempAvg);
}

/************************************************
   MAIN FUNCTIONS
 ***********************************************/
void TempRead() { //Reads temps every second, creates average, and sends values to Nextion
  // read in temperatures with the helper functions
  tankTempArray[arrayIndex]      = ((ReadThermistor(PIN_TANK_HIGHER) + ReadThermistor(PIN_TANK_LOWER)) / 2); // average two tank sensors
  roofLoopTempArray[arrayIndex]  = ReadThermistor(PIN_ROOF_LOOP);
  houseLoopTempArray[arrayIndex] = ReadThermistor(PIN_HOUSE_LOOP);
  manifoldTempArray[arrayIndex]  = ReadRTD(manifoldSerial);
  
  // reset averages
  tankTempAvg      = 0.0;
  roofLoopTempAvg  = 0.0;
  houseLoopTempAvg = 0.0;
  manifoldTempAvg  = 0.0;

  // sum for averages
  for (byte i = 0; i < TEMP_AVG_ARRAY; i++) {
    tankTempAvg      += tankTempArray[i];
    roofLoopTempAvg  += roofLoopTempArray[i];
    houseLoopTempAvg += houseLoopTempArray[i];
    manifoldTempAvg  += manifoldTempArray[i];
  }

  // divide for averages
  tankTempAvg      /= TEMP_AVG_ARRAY;
  roofLoopTempAvg  /= TEMP_AVG_ARRAY;
  houseLoopTempAvg /= TEMP_AVG_ARRAY;
  manifoldTempAvg  /= TEMP_AVG_ARRAY;

  // ensure temp arrays only store certain number before overwriting old values
  arrayIndex++;
  if (arrayIndex >= TEMP_AVG_ARRAY) arrayIndex = 0;

  // send values to nextion
  Serial2.print("Status.t8.txt=\"" + String(round(tankTempAvg)) + "\xB0" + "C\"");
  Serial2LineEnd();
  Serial2.print("t9.txt=\"" + String(round(houseLoopTempAvg)) + "\xB0" + "C\"");
  Serial2LineEnd();
  Serial2.print("t10.txt=\"" + String(round(roofLoopTempAvg)) + "\xB0" + "C\"");
  Serial2LineEnd();
}

void TempCompare() { // Compares temps every 15mins, updates valve relay, writes, time, temps
  float houseLoopDiff = tankTempAvg - houseLoopTempAvg; // only flows if tank is hotter than house
  float roofLoopDiff = roofLoopTempAvg - tankTempAvg; // only flows if roof is hotter than tank

  if (houseLoopDiff > TEMP_HOUSE_FLOW_THRESHOLD) { // flow should be on
    if (houseLoopFlowTracker.flowStatus != true) { // flow needs to turn on
      digitalWrite(PIN_FLOW_HOUSE_LOOP, LOW); // opens flow
      houseLoopFlowTracker.flowStatus = true;
      houseLoopFlowTracker.startTime = GetCurrentDateTime();
      Serial2.print("t11.bco=2016");
      Serial2LineEnd();
    }
  } else if (houseLoopFlowTracker.flowStatus == true) { // flow needs to turn off
    digitalWrite(PIN_FLOW_HOUSE_LOOP, HIGH); // closes flow
    houseLoopFlowTracker.endTime = GetCurrentDateTime();
    SD_LogFlow(LOC_STR_HOUSE_LOOP, houseLoopFlowTracker);
    houseLoopFlowTracker.flowStatus = false;
    Serial2.print("t11.bco=63488");
    Serial2LineEnd();
  }

  if (roofLoopDiff > TEMP_ROOF_FLOW_THRESHOLD) { // flow should be on
    if (roofLoopFlowTracker.flowStatus != true) { // flow needs to turn on
      digitalWrite(PIN_FLOW_ROOF_LOOP, LOW); // opens flow
      roofLoopFlowTracker.flowStatus = true;
      roofLoopFlowTracker.startTime = GetCurrentDateTime();
      //Serial2.print("t11.bco=2016"); // TODO: Replace with correct nextion label
      //Serial2LineEnd();
    }
  } else if (roofLoopFlowTracker.flowStatus == true) { // flow needs to turn off
    digitalWrite(PIN_FLOW_ROOF_LOOP, HIGH); // closes flow
    roofLoopFlowTracker.endTime = GetCurrentDateTime();
    SD_LogFlow(LOC_STR_ROOF_LOOP, roofLoopFlowTracker);
    roofLoopFlowTracker.flowStatus = false;
    //Serial2.print("t11.bco=63488"); // TODO: Replace with correct nextion label
    //Serial2LineEnd();
  }
}

void Midnight() {
  //TODO: Update Nextion time
}


/************************************************
   SYSTEM FUNCTIONS
 ***********************************************/
void setup() {
  Serial.begin(STARTUP_SPEED);
  Wire.begin();
  Serial2.begin(STARTUP_SPEED);
  SetDateTime();
  
  pinMode(PIN_FLOW_ROOF_LOOP, OUTPUT);
  pinMode(PIN_FLOW_HOUSE_LOOP, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);
  
  Serial.println("Initializing");
  // wait for MAX chip to stabilize
  delay(STARTUP_DELAY);
  
  SD_Init();
}

void loop() {
  unsigned long currentMillis = millis();

  if ((currentMillis - previousMillisRead) >= TEMP_READ_INTERVAL) {
    previousMillisRead = currentMillis;
    TempRead();
  }

  if ((currentMillis - previousMillisCompare) >= TEMP_COMPARE_INTERVAL) {
    previousMillisCompare = currentMillis;
    TempCompare();
    LogAllTemperatures();
  }
}
