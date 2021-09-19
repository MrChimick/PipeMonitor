#include <max6675.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define DS1307_ADDRESS 0x68

/************************************************
 * DATA STRUCTURES
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
 * FILE CONSTANTS
 ***********************************************/
// Quantity constants
const byte ZERO = 0x00;

const byte TEMP_AVG_ARRAY = 5;

const long TEMP_READ_INTERVAL = 30000;
const long TEMP_COMPARE_INTERVAL = 60000;

const byte NUM_SEC = 60;
const byte NUM_MIN = 60;
const byte NUM_HOUR = 24;

const int STARTUP_DELAY = 500;
const int STARTUP_SPEED = 9600;

// Pin constants
const byte BT_PIN_THERMO_DO = 30;
const byte BT_PIN_THERMO_CS = 32;
const byte BT_PIN_THERMO_CLK = 34;

const byte HR_PIN_THERMO_DO = 38;
const byte HR_PIN_THERMO_CS = 40;
const byte HR_PIN_THERMO_CLK = 42;

const byte CP_PIN_THERMO_DO = 44;
const byte CP_PIN_THERMO_CS = 46;
const byte CP_PIN_THERMO_CLK = 48;

const byte SD_PIN_CS = 53;

const byte CP_PIN_FLOW = 7;

const byte RELAY_PIN = 10;

// String constants
const String FILE_TEMPERATURE = "temp_log.csv";
const String FILE_FLOW_TIME = "flow_log.csv";
const String HEAD_TEMPERATURE = "Time,Location,Temperature";
const String HEAD_FLOW_TIME = "Time Started,Time Ended,Duration,Location";

const String BT_LOCATION_STR = "Boiler Tank";
const String HR_LOCATION_STR = "Heat Return Pipe";
const String CP_LOCATION_STR = "Collector Pipe";


/************************************************
 * GLOBAL VARIABLES
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

// Time interval counters
unsigned long previousMillisRead = 0;
unsigned long previousMillisCompare = 0;

// Flow variables
FlowTracker heatReturnFlowTracker;
FlowTracker collectorFlowTracker;


/************************************************
 * HELPER FUNCTIONS
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
    for (byte i = 0; i < 3; i++) Serial3.write(0xff);
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
 * DATE & TIME FUNCTIONS
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
        seconds += (endTime.minute - startTime.minute)* NUM_SEC;
    } else if (endTime.second != startTime.second) { // different seconds
        seconds += endTime.second - startTime.second;
    } else {
        return 0; // literally the same time
    }

    return round(seconds / NUM_SEC);
}


/************************************************
 * MAIN FUNCTIONS
 ***********************************************/
void TempRead() { //Reads temps every minute, creates average, and sends values to Nextion
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

    Serial3.print("Status.t8.txt=\"" + String(round(tankTempAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();

    Serial3.print("t9.txt=\"" + String(round(heatReturnTempAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();

    Serial3.print("t10.txt=\"" + String(round(collectorTempAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();
}

void TempCompare() { //Compares temps every 15mins, updates valve relay, writes, time, temps
    if (tankTempAvg > heatReturnTempAvg) {
        if (heatReturnFlowTracker.flowStatus != true) { // flow just turned on
            digitalWrite(RELAY_PIN, LOW); // set pin 10 LOW
            heatReturnFlowTracker.flowStatus = true;
            heatReturnFlowTracker.startTime = GetCurrentDateTime();
            Serial3.print("t11.bco=2016");
            Serial3LineEnd();
        }
    } else if (heatReturnFlowTracker.flowStatus == true) { // flow just turned off
        digitalWrite(RELAY_PIN, HIGH); // set pin 10 HIGH
        heatReturnFlowTracker.endTime = GetCurrentDateTime();
        SD_LogFlow(HR_LOCATION_STR, heatReturnFlowTracker);
        heatReturnFlowTracker.flowStatus = false;
        Serial3.print("t11.bco=63488");
        Serial3LineEnd();
    }
}

void LogAllTemperatures() {
    SD_LogTemp(BT_LOCATION_STR, tankTempAvg);
    SD_LogTemp(HR_LOCATION_STR, heatReturnTempAvg);
    SD_LogTemp(CP_LOCATION_STR, collectorTempAvg);
}

void FlowDetection() {
    if (digitalRead(CP_PIN_FLOW) == HIGH) {
        if (collectorFlowTracker.flowStatus != true) { // flow just turned on
            collectorFlowTracker.flowStatus = true;
            collectorFlowTracker.startTime = GetCurrentDateTime();
            Serial3.print("t12.bco=2016");
            Serial3LineEnd();
        }
    } else if (collectorFlowTracker.flowStatus == true) { // flow just turned off
        collectorFlowTracker.endTime = GetCurrentDateTime();
        SD_LogFlow(CP_LOCATION_STR, collectorFlowTracker);
        collectorFlowTracker.flowStatus = false;
        Serial3.print("t12.bco=63488");
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


/************************************************
 * SYSTEM FUNCTIONS
 ***********************************************/
void setup() {
    Serial.begin(STARTUP_SPEED);
    Wire.begin();
    Serial3.begin(STARTUP_SPEED);
    SetDateTime();
    pinMode(RELAY_PIN, OUTPUT); // Pump Relay pin set as output
    pinMode(SD_PIN_CS, OUTPUT); // SPI bus
    pinMode(CP_PIN_FLOW, INPUT); // Flow detection
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
    }

    if ((currentMillis - previousMillisCompare) >= TEMP_COMPARE_INTERVAL) {
        previousMillisCompare = currentMillis;
        TempCompare();
        LogAllTemperatures();
    }
}
