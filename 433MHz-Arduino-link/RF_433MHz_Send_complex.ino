/*
  Based on code from http://code.google.com/p/rc-switch/
  
  Instead of sending one value at a time, send multiple bytes combined in a long.
  For these values (batt, temp, humidity) but we could just send byte value w/o
  decimal digits but for better precision send a calculated value that will need
  to be decoded on the receiver side, like:
  - sender code - each station has a unique code, 0, 1, 2...15 (4 bit)
  - battery level - get mV value [0-12500], /1000 to get V [0-12.5], *20 to get some decimals [0-250]; overall /50 (8 bit)
    -- constrain final value between [0-255] to make sure we don't exceed a byte = 8 bits
    -- receiver will have to multiply by 50
  - temperature - get F value: 0-102.3; x10: 0-1023 (10 bits)
    -- constrain final value between [0-1023] to make sure we don't exceed 10 bits
    -- receiver will have to divide by 10
  - humidity - get % value: 0-100; x10: 0-1000 (10 bits)
    -- constrain final value between [0-1023] to make sure we don't exceed 10 bits
    -- receiver will have to divide by 10
  - motion sensor - 0=off/1=on.

  There are 2 kinds of transmissions:
  - sender code + batt level + temp + humidity: once every 3 minutes using TimedAction (instead of delay or interrupts);
  - sender code + motion sensor: triggered on RISING interrupt on pin 2.

  DHT:
  - Connect pin 1 (on the left) of the sensor to +5V
  - Connect pin 2 of the sensor to whatever your DHTPIN is
  - Connect pin 4 (on the right) of the sensor to GROUND
  - Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor.

  TX:
  - Connect pin 1 (on the left) of the sensor to whatever your TXPIN is
  - Connect pin 2 of the sensor to +5V
  - Connect pin 3 (on the right) of the sensor to GROUND.

  Motion detection with PIR sensor using interrupts. PIR sensor OUT pin is set HIGH when motion is detected.
  The output pin stays HIGH for a few seconds so we will use 2 interrupt handlers:
  - RISING to detect motion: in the handler code, instead of using delay to wait
    for the OUT pin to get back to LOW, we set a new interrupt handler;
  - FALLING to detect sensor OUT pin reset to LOW; in this interrupt handler
    we also restore the original RISING interrupt handler so the cycle repeats.

  PIR:
  - Connect pin 1 (on the left) of the sensor to GROUND
  - Connect pin 2 of the sensor to pin 2 (on Leonard, this is interrupt 1;
            on Uno, it is interrupt 0 as per http://arduino.cc/en/Reference/attachInterrupt)
  - Connect pin 3 (on the right) of the sensor to +5V.
 */

#include <RCSwitch.h>
#include "DHT.h"
#include "Arduino.h"
#include <TimedAction.h>

#define DHTPIN 3     // what pin is the DHT connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302)
#define TXPIN 7     // what pin is the transmitter connected to

RCSwitch mySwitch = RCSwitch();
DHT dht(DHTPIN, DHTTYPE);
//this initializes a TimedAction class that will read DHT sensor every 3 minutes
TimedAction timedAction = TimedAction(180000, txSensorReadings);

// use LED on pin 13 as simple indicator when transmitting
int txLed = 13;
// TODO change pin: use LED on pin 13 as simple indicator when motion sensor is ON
int pirLed = 13;

byte code = 1;   // first station (allowed values 0-15)
volatile byte batt = 0;	// 8-bit unsigned
volatile word temp, humid = 0;  // 16-bit unsigned but we are really going to use only 10 bits 0-1023
unsigned long combined = 0;
volatile int pirState = LOW;

void setup() {
  pinMode(txLed, OUTPUT);
  pinMode(pirLed, OUTPUT);
  attachInterrupt(1, blinkMotionStart, RISING); // pin 2 on Leonardo

  Serial.begin(9600);

  // transmitter is connected to Arduino Pin #7 
  mySwitch.enableTransmit(TXPIN);

  // optional set number of transmission repetitions - default is 10;
  // set to a higher value since in practice I noticed loss with distance
  mySwitch.setRepeatTransmit(15);

  dht.begin();
}

void loop() {
  timedAction.check();

  delay(3000);
}

void transmitSensorData() {
  digitalWrite(txLed, HIGH);   // turn the LED on (HIGH is the voltage level)

  // combined them in one long
  combined = code;
  combined = combined << 10 | temp;
  combined = combined << 10 | humid;
  combined = combined << 8 | batt;
  
  Serial.print("DHT: ");
  Serial.println(combined);
//  Serial.println(combined, BIN);

  // send using decimal code
  mySwitch.send(combined, 32);
  delay(1000);
  digitalWrite(txLed, LOW);    // turn the LED off by making the voltage LOW
}

void txSensorReadings() {
  getSensorReadings();
  getBattLevel();
  transmitSensorData();
}

void getBattLevel() {
  long battMV = readVcc();
  batt = battMV / 50.0;
  constrain(batt, 0, 255);

  Serial.print("batt: ");
  Serial.println(batt);
}

void getSensorReadings() {
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  // use temp in F for larger range of values
  byte tf = (byte)dht.convertCtoF(t);

  // check if returns are valid, if they are NaN (not a number)
  // then something went wrong so we won't transmit anything
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT");
  } else {
  	temp = tf * 10;
  	constrain(temp, 0, 1023);
  	humid = h * 10;
  	constrain(humid, 0, 1023);

    Serial.println("");
    Serial.print("Humidity: "); 
    Serial.print(h);
    Serial.print(" %\t");
//    Serial.print("Temperature: "); 
//    Serial.print(t);
//    Serial.println(" *C");
    Serial.print("Temperature: "); 
    Serial.print(tf);
    Serial.println(" *F");
  }
}

void transmitMotionData(byte motion) {
  digitalWrite(txLed, HIGH);   // turn the LED on (HIGH is the voltage level)

  // combined them in one long
  combined = code;
  combined = combined << 10 | 0;
  combined = combined << 10 | 0;
  combined = combined << 8 | motion;

  Serial.print("PIR: ");
  Serial.println(combined);
//  Serial.println(combined, BIN);

  // send using decimal code
  mySwitch.send(combined, 32);
  delay(1000);
  digitalWrite(txLed, LOW);    // turn the LED off by making the voltage LOW
}

void blinkMotionStart()
{
  // replace interrupt handler with another one using FALLING
  // so we know when the PIR sensor has been reset
  attachInterrupt(1, blinkMotionEnd, FALLING); // pin 2 on Leonardo
  pirState = !pirState;
  digitalWrite(pirLed, pirState);
  transmitMotionData(1);
}

void blinkMotionEnd()
{
  // replace with the original RISING interrupt handler
  attachInterrupt(1, blinkMotionStart, RISING); // pin 2 on Leonardo
  pirState = !pirState;
  digitalWrite(pirLed, pirState);
  transmitMotionData(0);
}

// http://www.instructables.com/id/Secret-Arduino-Voltmeter/
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
     ADMUX = _BV(MUX5) | _BV(MUX0) ;
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  
 
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring
 
  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both
 
  long result = (high<<8) | low;
 
  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}