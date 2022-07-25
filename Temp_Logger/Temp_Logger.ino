#include <max6675.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

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

const long TEMP_READ_INTERVAL = 1000;
const long TEMP_COMPARE_INTERVAL = 30000; //CHANGE TO 900000 in for final

const byte NUM_SEC = 60;
const byte NUM_MIN = 60;
const byte NUM_HOUR = 24;

const int STARTUP_DELAY = 1000;
const int STARTUP_SPEED = 9600;

// Pin constants
const byte BT_PIN_THERMO_DO = 39;
const byte BT_PIN_THERMO_CS = 40;
const byte BT_PIN_THERMO_CLK = 41;

const byte HR_PIN_THERMO_DO = 42;
const byte HR_PIN_THERMO_CS = 43;
const byte HR_PIN_THERMO_CLK = 44;

const byte CP_PIN_THERMO_DO = 62;
const byte CP_PIN_THERMO_CS = 63;
const byte CP_PIN_THERMO_CLK = 64;

const byte SD_PIN_CS = 15;

const byte CP_PIN_FLOW = 69;

const byte RELAY_PIN = 65;
const byte CIRC_PIN = 66;

// String constants
const String FILE_TEMPERATURE = "temp_log.csv";
const String FILE_FLOW_TIME = "flow_log.csv";
const String HEAD_TEMPERATURE = "Time,Location,Temperature";
const String HEAD_FLOW_TIME = "Time Started,Time Ended,Duration,Location";

const String BT_LOCATION_STR = "Boiler Tank";
const String HR_LOCATION_STR = "Heat Return Pipe";
const String CP_LOCATION_STR = "Collector Pipe";

String inputstring = "";                              //a string to hold incoming data from the PC
String sensorstring = "";                             //a string to hold the data from the Atlas Scientific product
boolean input_string_complete = false;                //have we received all the data from the PC
boolean sensor_string_complete = false;               //have we received all the data from the Atlas Scientific product
float temperature;                                    //used to hold a floating point number that is the RTD temperature




/************************************************
   GLOBAL VARIABLES
 ***********************************************/
// Temperature sensor objects
MAX6675 TankTempSensor(BT_PIN_THERMO_CLK, BT_PIN_THERMO_CS, BT_PIN_THERMO_DO);
MAX6675 HeatReturnTempSensor(HR_PIN_THERMO_CLK, HR_PIN_THERMO_CS, HR_PIN_THERMO_DO);
MAX6675 CollectorTempSensor(CP_PIN_THERMO_CLK, CP_PIN_THERMO_CS, CP_PIN_THERMO_DO);

// Temperature collection variables
byte arrayIndex = 0;

float tankTempArray[TEMP_AVG_ARRAY];
float tankTempAvg = 0.0;

float heatReturnTempArray[TEMP_AVG_ARRAY];
float heatReturnTempAvg = 0.0;

float collectorTempArray[TEMP_AVG_ARRAY];
float collectorTempAvg = 0.0;

float tempDiff = 10;

// Time interval counters
unsigned long previousMillisRead = 0;
unsigned long previousMillisCompare = 0;

// Flow variables
FlowTracker heatReturnFlowTracker;
FlowTracker collectorFlowTracker;


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

void Serial3LineEnd() {
  for (byte i = 0; i < 3; i++) Serial2.write(0xff);
}

String DateTimeToString(DateTime timePrint) { // return the date EG 21/10/5 23:59:59
  return (
           String(timePrint.year) + '/' +
           String(timePrint.month) + '/' +
           String(timePrint.monthDay) + ' ' +
           String(timePrint.hour) + ':' +
           String(timePrint.minute) + ':' +
           String(timePrint.second)
         );
}


/************************************************
   DATE & TIME FUNCTIONS
 ***********************************************/
void SetDateTime() {

  byte second = 45; //0-59
  byte minute = 40; //0-59
  byte hour = 0; //0-23
  byte weekDay = 2; //1-7
  byte monthDay = 1; //1-31
  byte month = 3; //1-12
  byte year = 11; //0-99

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
  currDateTime.second = BcdToDec(Wire.read());
  currDateTime.minute = BcdToDec(Wire.read());
  currDateTime.hour = BcdToDec(Wire.read() & 0b111111); //24 hour time
  currDateTime.weekDay = BcdToDec(Wire.read()); //0-6 -> sunday â€“ Saturday
  currDateTime.monthDay = BcdToDec(Wire.read());
  currDateTime.month = BcdToDec(Wire.read());
  currDateTime.year = BcdToDec(Wire.read());

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


/************************************************
   MAIN FUNCTIONS
 ***********************************************/
void TempRead() { //Reads temps every second, creates average, and sends values to Nextion
  tankTempArray[arrayIndex] = (TankTempSensor.readCelsius());
  heatReturnTempArray[arrayIndex] = (HeatReturnTempSensor.readCelsius());
  collectorTempArray[arrayIndex] = (CollectorTempSensor.readCelsius());
  tankTempAvg = 0.0;
  heatReturnTempAvg = 0.0;
  collectorTempAvg = 0.0;

  for (byte i = 0; i < TEMP_AVG_ARRAY; i++) {
    tankTempAvg += tankTempArray[i];
    heatReturnTempAvg += heatReturnTempArray[i];
    collectorTempAvg += collectorTempArray[i];
  }

  tankTempAvg /= TEMP_AVG_ARRAY;
  heatReturnTempAvg /= TEMP_AVG_ARRAY;
  collectorTempAvg /= TEMP_AVG_ARRAY;

  arrayIndex++;
  if (arrayIndex >= TEMP_AVG_ARRAY) arrayIndex = 0;

  Serial2.print("Status.t8.txt=\"" + String(round(tankTempAvg)) + "\xB0" + "C\"");
  Serial3LineEnd();

  Serial2.print("t9.txt=\"" + String(round(heatReturnTempAvg)) + "\xB0" + "C\"");
  Serial3LineEnd();

  Serial2.print("t10.txt=\"" + String(round(collectorTempAvg)) + "\xB0" + "C\"");
  Serial3LineEnd();

  Serial.println(collectorTempAvg);

  Serial.println(tankTempAvg);

}

void TempCompare() { //Compares temps every 15mins, updates valve relay, writes, time, temps
  if (tankTempAvg > heatReturnTempAvg) {
    if (heatReturnFlowTracker.flowStatus != true) { // flow just turned on
      digitalWrite(RELAY_PIN, LOW); // set pin 10 LOW
      heatReturnFlowTracker.flowStatus = true;
      heatReturnFlowTracker.startTime = GetCurrentDateTime();
      Serial2.print("t11.bco=2016");
      Serial3LineEnd();
    }
  } else if (heatReturnFlowTracker.flowStatus == true) { // flow just turned off
    digitalWrite(RELAY_PIN, HIGH); // set pin 10 HIGH
    heatReturnFlowTracker.endTime = GetCurrentDateTime();
    SD_LogFlow(HR_LOCATION_STR, heatReturnFlowTracker);
    heatReturnFlowTracker.flowStatus = false;
    Serial2.print("t11.bco=63488");
    Serial3LineEnd();
  }
}

void LogAllTemperatures() {
  SD_LogTemp(BT_LOCATION_STR, tankTempAvg);
  SD_LogTemp(HR_LOCATION_STR, heatReturnTempAvg);
  SD_LogTemp(CP_LOCATION_STR, collectorTempAvg);
}

void FlowDetection() {
  if (digitalRead(CP_PIN_FLOW) == LOW) {
    if (collectorFlowTracker.flowStatus != true) { // flow just turned on
      collectorFlowTracker.flowStatus = true;
      collectorFlowTracker.startTime = GetCurrentDateTime();
      Serial2.print("t12.bco=2016");
      Serial3LineEnd();
    }
  } else if (collectorFlowTracker.flowStatus == true) { // flow just turned off
    collectorFlowTracker.endTime = GetCurrentDateTime();
    SD_LogFlow(CP_LOCATION_STR, collectorFlowTracker);
    collectorFlowTracker.flowStatus = false;
    Serial2.print("t12.bco=63488");
    Serial3LineEnd();
  }
}

void Midnight() {
  //TODO: Update Nextion time
}

void SD_Init() {
  if (!SD.begin(SD_PIN_CS)) {
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

void PrintCurrentDateTime() {
  DateTime currTime; // declaring a variable of type DateTime
  currTime = GetCurrentDateTime(); // getting the current DateTime using the function

  // accessing all members
  Serial.println("Second: " + String(currTime.second));
  Serial.println("Minute: " + String(currTime.minute));
  Serial.println("Hour: " + String(currTime.hour));
  Serial.println("WeekDay: " + String(currTime.weekDay));
  Serial.println("MonthDay: " + String(currTime.monthDay));
  Serial.println("Month: " + String(currTime.month));
  Serial.println("Year: " + String(currTime.year));

  // I can change the members as well
  currTime.second = 0;
}

void serialEvent3() {                                 //if the hardware serial port_3 receives a char
  sensorstring = Serial3.readStringUntil(13);         //read the string until we see a <CR>
  sensor_string_complete = true;                      //set the flag used to tell if we have received a completed string from the PC
}

void collectorTemp() {
  Serial3.print('R');
  serialEvent3();
  if (sensor_string_complete == true) {                          //if a string from the Atlas Scientific product has been received in its entirety
    Serial.println(sensorstring);                                //send that string to the PC's serial monitor
    //uncomment this section to see how to convert the reading from a string to a float
    if (isdigit(sensorstring[0]) || sensorstring[0] == '-') {    //if the first character in the string is a digit or a "-" sign
      temperature = sensorstring.toFloat();                      //convert the string to a floating point number so it can be evaluated by the Arduino
      if ((temperature + (tempDiff)) >= (tankTempAvg)) {                               //if the RTD temperature is greater than or equal to 25 C
        digitalWrite(CIRC_PIN, LOW);                                  //print "high" this is demonstrating that the Arduino is evaluating the RTD temperature as a number and not as a string
      }
      else  {                              //if the RTD temperature is less than or equal to 24.999 C
        digitalWrite(CIRC_PIN, HIGH);                                   //print "low" this is demonstrating that the Arduino is evaluating the RTD temperature as a number and not as a string
      }
    }

    sensorstring = "";                                             //clear the string:
    sensor_string_complete = false;                                //reset the flag used to tell if we have received a completed string from the Atlas Scientific product
  }
  Serial2.print("Status.t10.txt=\"" + String(round(temperature)) + "\xB0" + "C\"");
  Serial3LineEnd();
}
/************************************************
   SYSTEM FUNCTIONS
 ***********************************************/
void setup() {
  Serial.begin(STARTUP_SPEED);
  Wire.begin();
  Serial2.begin(STARTUP_SPEED);
  SetDateTime();
  Serial3.begin(STARTUP_SPEED);
  inputstring.reserve(10);                            //set aside some bytes for receiving data from the PC
  sensorstring.reserve(30);                           //set aside some bytes for receiving data from Atlas Scientific product
  pinMode(RELAY_PIN, OUTPUT); // Pump Relay pin set as output
  pinMode(SD_PIN_CS, OUTPUT); // SPI bus
  pinMode(CP_PIN_FLOW, INPUT_PULLUP); // Flow detection
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
    FlowDetection();
    collectorTemp();
  }

  if ((currentMillis - previousMillisCompare) >= TEMP_COMPARE_INTERVAL) {
    previousMillisCompare = currentMillis;
    TempCompare();
    LogAllTemperatures();
  }
}
