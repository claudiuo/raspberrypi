/*
  RFRcvCmplxData: based on RFSniffer: receive 4 simple values
  concatenated in an unsigned int (32 bit) value:
  - 4 bit: station code 0-15
  - 10 bit: temperature in F *10 (to get one decimal) -> need to divide by 10
  - 10 bit: humidity in % *10 (to get one decimal) -> need to divide by 10
  - 8 bit: battery voltage in mV /50 (to get some decimals) -> need to multiply by 50
*/

#include "RCSwitch.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <curl/curl.h>

#define MAXBUF  1024

RCSwitch mySwitch;

unsigned char t1, t4, stationCode, motion = 0;
unsigned short t2, t3 = 0;
float temp, humid, batt = 0;

// sparkfun phantIo constants
char serverUrl[] = "http://data.sparkfun.com";
char publicKey[] = "pwwWW5lZlgCdJQEqzOrO";
char privateKey[] = "<private_key>";
char stationKey[] = "station";
char motionKey[] = "motion";
char humidityKey[] = "humidity";
char tempKey[] = "temp";
char voltageKey[] = "voltage";

char buffer[MAXBUF];

CURL *curl;
CURLcode res;
boolean readyToSendToServer = false;

// TODO remove test data
unsigned int value = 4294961097;   //15-1023-999-201
//unsigned int value = 4294967295;   //15-1023-1023-255
//unsigned int value = 4026531841;   //15-0-0-1

int main(int argc, char *argv[]) {

    float startTime = clock();

    curl = curl_easy_init();
    if(curl) {
        readyToSendToServer = true;
        // in case it is redirected, tell libcurl to follow redirection
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    }

    // we get lots of duplicates in the same transmission so we'll remember
    // the last value received so we don't react twice to the same value
    // however, if more than 30s passed more than likely this is a new
    // transmission so treat it as a new value so it gets posted (see if below)
    unsigned int lastValue = 0;

     // This pin is not the first pin on the RPi GPIO header!
     // Consult https://projects.drogon.net/raspberry-pi/wiringpi/pins/
     // for more information.
     int PIN = 4;

     //if(wiringPiSetup() == -1)
     //  return 0;

     //mySwitch = RCSwitch();
     //mySwitch.enableReceive(PIN);

     while(1) {

  		// TODO reenable to get data from the radio
  //    if (mySwitch.available()) {

  //      unsigned int value = mySwitch.getReceivedValue();

        float crtTime = clock();

        if (value == 0) {
          printf("Unknown value");
        } else {
          if(value == lastValue && (crtTime-startTime)/CLOCKS_PER_SEC < 30) {
              // nothing to do, it's a duplicate that came in less than 30s
              // printf("Duplicate value\n");
          } else {
              //printf("%f\n", (crtTime - startTime)/CLOCKS_PER_SEC);
              // reset the startTime
              startTime = clock();

              printf("Received %u\n", ++value);
              // display each simple value combined in the 32 bit uint
              // first value takes only 4 bits so shift by 28
              t1 = value >> 28;
              // second value uses 10 bits: if we shift by 18 (4 first value, 10 this value),
              // we'll end up with 14 bits but we are interested only in last 10 so 0 the others
              t2 = value >> 18 & 0x3FF;   // 3FF = 00001111111111
              // third value uses 10 bits: if we shift by 8 (4 first value, 10 second value, 10 this value),
              // we'll end up with 14 bits but we are interested only in last 10 so 0 the others
              t3 = value >> 8 & 0x3FF;   // 3FF = 00001111111111
              // last value is the last byte, forcing a convertion from int to byte will get the value
              t4 = value;
              printf("single values: %u-%i-%i-%i\n", t1, t2, t3, t4);

              // if t2 and t3 are 0, it's motion sensor data; otherwise is temp/humid/batt
              if(t2 == 0 && t3 ==0) {
                  // real values
                  stationCode = t1;    // numeric code 0-15
                  motion = t4;         // 0=off,1=on
                  printf("single values: %u-%u\n", stationCode, motion);
                  // send temp/humid/batt sensor data: http://data.sparkfun.com/input/[publicKey]?private_key=[privateKey]&station=[value]&humidity=[value]&temp=[value]&voltage=[value]&motion=[value]
                  sprintf(buffer,
                        "%s/input/%s?private_key=%s&%s=%u&%s=%u&%s=%.1f&%s=%.1f&%s=%.1f", serverUrl, publicKey, privateKey,
                        stationKey, stationCode, motionKey, motion, humidityKey, 0.0, tempKey, 0.0, voltageKey, 0.0);
              } else {
                  // real values
                  stationCode = t1;    // numeric code 0-15
                  temp = t2 / 10.0;    // F
                  humid = t3 / 10.0;   // %
                  batt = t4 * 50.0;    // mV
                  printf("single values: %u-%.1f-%.1f-%.1f\n", stationCode, temp, humid, batt);
                  // send temp/humid/batt sensor data: http://data.sparkfun.com/input/[publicKey]?private_key=[privateKey]&station=[value]&humidity=[value]&temp=[value]&voltage=[value]&motion=[value]
                  sprintf(buffer,
                        "%s/input/%s?private_key=%s&%s=%u&%s=%u&%s=%.1f&%s=%.1f&%s=%.1f", serverUrl, publicKey, privateKey,
                        stationKey, stationCode, motionKey, 0, humidityKey, humid, tempKey, temp, voltageKey, batt);
              }
              printf(buffer);
              if(readyToSendToServer) {
                curl_easy_setopt(curl, CURLOPT_URL, buffer);

                /* Perform the request, res will get the return code */
                res = curl_easy_perform(curl);
                /* Check for errors */
                if(res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                }
              }

              // store the new value as lastValue
              lastValue = value;
          }
        }

        mySwitch.resetAvailable();
//      }
  }

    if(readyToSendToServer) {
        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    exit(0);
}
