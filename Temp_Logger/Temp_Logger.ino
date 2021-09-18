#include <max6675.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define DS1307_ADDRESS 0x68

/************************************************
 * FILE CONSTANTS
 ***********************************************/
// Quantity constants
const byte ZERO = 0x00;

const int TEMP_AVG_ARRAY = 5;

const long TEMP_READ_INTERVAL = 30000;
const long TEMP_COMPARE_INTERVAL = 60000;

// Pin constants
const int BT_PIN_THERMO_DO = 30;
const int BT_PIN_THERMO_CS = 32;
const int BT_PIN_THERMO_CLK = 34;

const int HR_PIN_THERMO_DO = 38;
const int HR_PIN_THERMO_CS = 40;
const int HR_PIN_THERMO_CLK = 42;

const int CP_PIN_THERMO_DO = 44;
const int CP_PIN_THERMO_CS = 46;
const int CP_PIN_THERMO_CLK = 48;


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
float heatReturnAvg = 0.0;

float collectorTempArray[TEMP_AVG_ARRAY];
float collectorTempAvg = 0.0;

// Time interval counters
unsigned long previousMillisRead = 0;
unsigned long previousMillisCompare = 0;


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
    for (int i = 0; i < 3; i++) Serial3.write(0xff);
}


/************************************************
 * MAIN FUNCTIONS
 ***********************************************/
void TempRead() { //Reads temps every minute, creates average, and sends values to Nextion
    tankTempArray[arrayIndex] = (TankTempSensor.readCelsius());
    heatReturnTempArray[arrayIndex] = (HeatReturnTempSensor.readCelsius());
    collectorTempArray[arrayIndex] = (CollectorTempSensor.readCelsius());
    
    for (int i = 0; i < TEMP_AVG_ARRAY; i++) {
        tankTempAvg += tankTempArray[i];
        heatReturnAvg += heatReturnTempArray[i];
        collectorTempAvg += collectorTempArray[i];
    }
    
    tankTempAvg /= TEMP_AVG_ARRAY;
    heatReturnAvg /= TEMP_AVG_ARRAY;
    collectorTempAvg /= TEMP_AVG_ARRAY;

    arrayIndex++;
    if (arrayIndex >= TEMP_AVG_ARRAY) arrayIndex = 0;

    Serial3.print("Status.t8.txt=\"" + String(round(tankTempAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();

    Serial3.print("t9.txt=\"" + String(round(heatReturnAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();

    Serial3.print("t10.txt=\"" + String(round(collectorTempAvg)) + "\xB0" + "C\"");
    Serial3LineEnd();
}

void TempCompare() { //Compares temps every 15mins, updates valve relay, writes, time, temps, valve and flow runtime to SD card
    if (tankTempAvg > heatReturnAvg) {
        digitalWrite(10, LOW); // set pin 10 LOW
        Serial3.print("t11.bco=2016");
        Serial3LineEnd();
    } else {
        digitalWrite(10, HIGH); // set pin 10 HIGH
        Serial3.print("t11.bco=63488");
        Serial3LineEnd();
    }
    PrintDate();
}

void Midnight() {
    //TODO: Update Nextion time, send valve and collector run times to SD card and reset timer
}
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

void PrintDate() {

    // Reset the register pointer
    Wire.beginTransmission(DS1307_ADDRESS);
    Wire.write(ZERO);
    Wire.endTransmission();

    Wire.requestFrom(DS1307_ADDRESS, 7);

    int second = BcdToDec(Wire.read());
    int minute = BcdToDec(Wire.read());
    int hour = BcdToDec(Wire.read() & 0b111111); //24 hour time
    int weekDay = BcdToDec(Wire.read()); //0-6 -> sunday â€“ Saturday
    int monthDay = BcdToDec(Wire.read());
    int month = BcdToDec(Wire.read());
    int year = BcdToDec(Wire.read());

    //print the date EG 3/1/11 23:59:59
    Serial.print(month);
    Serial.print("/");
    Serial.print(monthDay);
    Serial.print("/");
    Serial.print(year);
    Serial.print(" ");
    Serial.print(hour);
    Serial.print(":");
    Serial.print(minute);
    Serial.print(":");
    Serial.println(second);
}


/************************************************
 * SYSTEM FUNCTIONS
 ***********************************************/
void setup() {
    Serial.begin(9600);
    Wire.begin();
    Serial3.begin(9600);
    SetDateTime();
    pinMode(10, OUTPUT); // Pump Relay pin set as output
    Serial.println("Initializing");
    // wait for MAX chip to stabilize
    delay(500);
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
    }
}
