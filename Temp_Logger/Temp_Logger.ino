#include "max6675.h"

#include <Wire.h>

#define DS1307_ADDRESS 0x68
byte zero = 0x00;

int thermoDO = 30;
int thermoCS = 32;
int thermoCLK = 34;

int thermoDO2 = 38;
int thermoCS2 = 40;
int thermoCLK2 = 42;

byte decToBcd(byte val) {
  // Convert normal decimal numbers to binary coded decimal
  return ((val / 10 * 16) + (val % 10));
}

byte bcdToDec(byte val) {
  // Convert binary coded decimal to normal decimal numbers
  return ((val / 16 * 10) + (val % 16));
}

MAX6675 TankTemp(thermoCLK, thermoCS, thermoDO);
MAX6675 HeatReturn(thermoCLK2, thermoCS2, thermoDO2);

unsigned long previousMillis = 0;
unsigned long previousMillis1 = 0;
const long DisplayInterval = 30000;
const long TempCheckInterval = 60000;

float TankTempArray[5];
byte arrayIndex = 0;
float TankTempAvg = 0.0;

float HeatReturnArray[5];
float HeatReturnAvg = 0.0;

void TempUpdate() //Reads temps every minute, creates average, and sends values to Nextion
{
  TankTempArray[arrayIndex] = (TankTemp.readCelsius());
  for (int i = 0; i < 4; i++) {
    TankTempAvg = TankTempAvg + TankTempArray[i];
  }
  TankTempAvg = TankTempAvg / 5;
  HeatReturnArray[arrayIndex] = (HeatReturn.readCelsius());
  for (int i = 0; i < 4; i++) {
    HeatReturnAvg = HeatReturnAvg + HeatReturnArray[i];
  }
  HeatReturnAvg = HeatReturnAvg / 5;
  arrayIndex++;
  if (arrayIndex > 4) arrayIndex = 0;

  Serial.print("Tank Temp = "); //Comment out in final
  Serial.println(TankTemp.readCelsius()); //Comment out in final
  Serial.print("Tank Temp Avg = "); //Comment out in final
  Serial.println(TankTempAvg); //Comment out in final

  Serial.print("Heat Return = "); //Comment out in final
  Serial.println(HeatReturn.readCelsius()); //Comment out in final
  Serial.print("Heat Return Avg = "); //Comment out in final
  Serial.println(HeatReturnAvg); //Comment out in final
  //TODO: Send Avg Temps to Nextion
}

void TempCheck() //Compares temps every 15mins, updates valve relay, writes, time, temps, valve and flow runtime to SD card
{
  if (TankTemp.readCelsius() > HeatReturn.readCelsius()) {
    digitalWrite(10, LOW); // set pin 10 LOW
  } else {
    digitalWrite(10, HIGH); // set pin 10 HIGH
    //TODO:Write HeatReturnAvg, TankTempAvg, Time to SD card
    //TODO: Start chrono when relay=LOW and stop when RELAY=HIGH. Write daily value to SD card at midnight and reset.
    //      Change color of valve status indicator on Nextion (t11) relay LOW = bco2016, relay HIGH = bco63488
  }
  printDate();
}

void setDateTime() {

  byte second = 45; //0-59
  byte minute = 40; //0-59
  byte hour = 0; //0-23
  byte weekDay = 2; //1-7
  byte monthDay = 1; //1-31
  byte month = 3; //1-12
  byte year = 11; //0-99

  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(zero);

  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(weekDay));
  Wire.write(decToBcd(monthDay));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year));

  Wire.write(zero); //start

  Wire.endTransmission();

}

void printDate() {

  // Reset the register pointer
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(zero);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  int second = bcdToDec(Wire.read());
  int minute = bcdToDec(Wire.read());
  int hour = bcdToDec(Wire.read() & 0b111111); //24 hour time
  int weekDay = bcdToDec(Wire.read()); //0-6 -> sunday â€“ Saturday
  int monthDay = bcdToDec(Wire.read());
  int month = bcdToDec(Wire.read());
  int year = bcdToDec(Wire.read());

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

void setup() {
  Serial.begin(9600);
  Wire.begin();
  setDateTime();
  pinMode(10, OUTPUT); // Pump Relay pin set as output

  Serial.println("Initializing");
  // wait for MAX chip to stabilize
  delay(500);
}

void loop() {
  //TODO:Look for bluetooth request, dump data folder to bluetooth when requested
  //TODO:Add Hall Sensor. Start Chrono when pulses/second > 1 and stop when <1. Write daily runtime to SD card at midnight and reset. 
  //     Change Color of Heating Indicator on Nextion pps>1 = bco63488, pps<1 = bco2016
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= DisplayInterval) {
    previousMillis = currentMillis;
    TempUpdate();

  }

  if (currentMillis - previousMillis1 >= TempCheckInterval) {
    previousMillis1 = currentMillis;
    TempCheck();
  }
}  
