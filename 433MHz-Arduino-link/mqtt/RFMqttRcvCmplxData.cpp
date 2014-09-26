/*
  RFRcvCmplxData: based on RFSniffer: receive 4 simple values
  concatenated in an unsigned int (32 bit) value:
  - 4 bit: station code 0-15
  - 10 bit: temperature in F *10 (to get one decimal) -> need to divide by 10
  - 10 bit: humidity in % *10 (to get one decimal) -> need to divide by 10
  - 8 bit: battery voltage in mV /50 (to get some decimals) -> need to multiply by 50

  Because the data is sent repeatedly, we need to make sure we don't get duplicates.
  So we store the last value received and if we get it again in the next 30s, we
  consider it a duplicate. The temp, humidity and voltage values are sent every few minutes
  that's why we limit the check to 30s: if we receive the same value after 30s, we
  consider it a new value - this is possible if none of tem, humidity or voltage changed.

  For the motion sensor, things are different: we don't check the sensor periodically,
  the Arduino code reacts using interrupts so there is no interval to use to check
  for duplicates. But there is no need to: when the motion sensor is triggered, we get
  a message with motion=1; then the sensor is not triggered again until it resets (a few
  seconds); when it resets, we get a message with motion=0 so the value will be different.
  Then if the sensor is triggered again, we get motion=1 - a new value. And so on.

  This version of the code removes all the code posting to IoT services - that code is for
  now only in RFRcvCmplxData.cpp.

  The main purpose of this code is to post data to a localhost mosquitto broker so node-red
  can be used to get the data from mosquitto and do the rest of the work. In addition, I will
  save to a local db, with a posted flag = 1 (if publish was successful), 0 otherwise.
*/

#include "../RCSwitch.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include <mosquitto.h>

#define MAXBUF 512

void postData();
void postDb(int posted);
// mosquitto related methods
static void die(const char *msg);
static bool set_callbacks(struct mosquitto *m);
int postMosquitto(struct mosquitto *m);
static struct mosquitto *init();
void mqttPublish();

struct Data
{
    unsigned char stationCode, motion;
    float temp, humid, batt;
};

RCSwitch mySwitch;

unsigned char t1, t4, stationCode, motion = 0;
unsigned short t2, t3 = 0;
float temp, humid, batt = 0;

bool isMotion = false;

struct Data data;

// MQTT related
#define BROKER_HOSTNAME "localhost"
#define BROKER_PORT 1883
#define BROKER_KEEPALIVE 60
#define TOPIC_STATIONS "stations"
#define TOPIC_MOTION "pir"
#define TOPIC_SENSORS "dht"

//time_t rawtime;
//struct tm * timeinfo;
//char timeBuffer[80];

sqlite3 *dbConn;
bool dbConnectionOK = true;
sqlite3_stmt *dbRes;
int dbError;
char dbBuffer[MAXBUF];
char sqlInsertDHT[] = "INSERT INTO dht (station, temp, humidity, voltage, posted) values(%u, %.1f, %.1f, %.1f, %u)";
char sqlInsertPIR[] = "INSERT INTO pir (station, motion, posted) values(%u, %u, %u)";

// test data
unsigned int value = 4026531841;   //15-0-0-1

char mosqId[30];
struct mosquitto *mosq = NULL;

int main(int argc, char *argv[]) {

    float startTime = clock();

    mosq = init();
    // this code needs to post to mosquitto, if can't connect stop right away
    if(!mosq) { die("can't connect\n"); }

    if (!set_callbacks(mosq)) { die("set_callbacks() failure\n"); }

    if(mosquitto_connect(mosq, BROKER_HOSTNAME, BROKER_PORT, BROKER_KEEPALIVE)){
    	fprintf(stderr, "Unable to connect.\n");
		return 1;
	}

    dbError = sqlite3_open("sensors.db", &dbConn);
    if (dbError) {
        puts("Can not open database");
        dbConnectionOK = false;
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

     if(wiringPiSetup() == -1)
       return 0;

     mySwitch = RCSwitch();
     mySwitch.enableReceive(PIN);

     while(1) {

      if (mySwitch.available()) {

        value = mySwitch.getReceivedValue();

        float crtTime = clock();

        if (value == 0) {
          printf("Unknown encoding");
        } else {
          if(value == lastValue && (crtTime-startTime)/CLOCKS_PER_SEC < 30) {
              // nothing to do, it's a duplicate that came in less than 30s
              // printf("Duplicate value\n");
          } else {
              //printf("%f\n", (crtTime - startTime)/CLOCKS_PER_SEC);
              // reset the startTime
              startTime = clock();

              printf("\nReceived %u\n", value);
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
              printf("raw values: %u-%i-%i-%i\n", t1, t2, t3, t4);

              data.stationCode = 0;
              data.motion = 0;
              data.temp = 0.0;
              data.humid = 0.0;
              data.batt = 0.0;

              // if t2 and t3 are 0, it's motion sensor data; otherwise is temp/humid/batt
              if(t2 == 0 && t3 == 0) {
                  isMotion = true;
                  // real values
                  stationCode = t1;    // numeric code 0-15
                  motion = t4;         // 0=off,1=on
                  printf("single values: %u-%u\n", stationCode, motion);
                  data.stationCode = stationCode;
                  data.motion = motion;
                  postData();
              } else {
                  isMotion = false;
                  // real values
                  stationCode = t1;    // numeric code 0-15
                  temp = t2 / 10.0;    // F
                  humid = t3 / 10.0;   // %
                  batt = t4 * 50.0;    // mV
                  printf("single values: %u-%.1f-%.1f-%.1f\n", stationCode, temp, humid, batt);
                  data.stationCode = stationCode;
                  data.temp = temp;
                  data.humid = humid;
                  data.batt = batt;
                  postData();
              }

              // store the new value as lastValue
              lastValue = value;
          }
        }

        mySwitch.resetAvailable();
      }
    }

    sqlite3_close(dbConn);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    exit(0);
}

void postData()
{
    // mosquitto publish
    printf("publish to mosquitto and db\n");
    mqttPublish();
}

void mqttPublish() {
    int res = postMosquitto(mosq);
    unsigned short publishSuccess = 1;
    if (res != MOSQ_ERR_SUCCESS) {
        publishSuccess = 0;
    }
    // post to db with the posted flag according to mqtt publish success
    if(dbConnectionOK) {
        // TODO save to db later
        postDb(publishSuccess);
    }
}

void postDb(int posted) {
    dbBuffer[0] = '\0';
    if(isMotion) {
        sprintf(dbBuffer, sqlInsertPIR, data.stationCode, motion, posted);
    } else {
        sprintf(dbBuffer, sqlInsertDHT, data.stationCode, data.temp, data.humid, data.batt, posted);
    }
    puts(dbBuffer);
    dbError = sqlite3_exec(dbConn, dbBuffer, 0, 0, 0);
    if (dbError) {
        puts("Can not insert into database");
    }
}

// mosquitto related methods
/* Initialize a mosquitto client. */
static struct mosquitto *init() {
    mosquitto_lib_init();
    snprintf(mosqId, 30, "client_%d", 1);
    mosq = mosquitto_new(mosqId, true, NULL);
    return mosq;
}

/* Fail with an error message. */
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
    if(message->payloadlen){
    	printf("%s %s\n", message->topic, message->payload);
	}else{
		printf("%s (null)\n", message->topic);
	}
	fflush(stdout);
}

void on_connect(struct mosquitto *mosq, void *userdata, int result)
{
	int i;
	if(!result){
		/* Subscribe to broker information topics on successful connect. */
		mosquitto_subscribe(mosq, NULL, "$SYS/#", 2);
	}else{
		fprintf(stderr, "Connect failed\n");
	}
}

void on_subscribe(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
	int i;

	printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++){
		printf(", %d", granted_qos[i]);
	}
	printf("\n");
}

void on_log(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
	/* Pring all log messages regardless of level. */
	printf("%s\n", str);
}

/* A message was successfully published. */
static void on_publish(struct mosquitto *m, void *udata, int m_id) {
    printf("published successfully\n");
}

static void die(const char *msg) {
    fprintf(stderr, "%s", msg);
    exit(1);
}

/* Register the callbacks that the mosquitto connection will use. */
static bool set_callbacks(struct mosquitto *m) {
    // Set the logging callback.  This should be used if you want event logging information from the client library.
    mosquitto_log_callback_set(m, on_log);
    // Set the connect callback.  This is called when the broker sends a CONNACK message in response to a connection.
    mosquitto_connect_callback_set(m, on_connect);
    // Set the message callback.  This is called when a message is received from the broker.
	mosquitto_message_callback_set(m, on_message);
    // Set the subscribe callback.  This is called when the broker responds to a subscription request.
    // remove this since I don't subscribe to any topic
//	mosquitto_subscribe_callback_set(m, on_subscribe);
    // Set the publish callback.  This is called when a message initiated with mosquitto_publish has been sent to the broker successfully.
    mosquitto_publish_callback_set(m, on_publish);
    return true;
}

// TODO apparently, I should use mosquitto_loop or mosquitto_loop_start to make sure all messages are published
// let's get it to work without it at first and look at this later

int postMosquitto(struct mosquitto *m) {

    // value is 32 bit, allow room for the terminating null byte ('\0'))
    size_t payload_sz = 33;
    char payload[payload_sz];
    size_t payloadlen = 0;
    // the message is the entire value received
    payloadlen = snprintf(payload, payload_sz, "%u", value);
//    if (payload_sz < payloadlen) { die("snprintf payload\n"); }

    // topic name is stations/<station_code>/pir or station/<station_code>/dht
    // maxlen = 8(stations)+1(slash)+2(station_code)+1(slash)+3(pir or dht)+1(terminating null byte)=16
    size_t topic_sz = 16;
    char topic[topic_sz];
    size_t topiclen = 0;
    if(isMotion) {
        topiclen = snprintf(topic, topic_sz, "%s/%u/%s",  TOPIC_STATIONS, data.stationCode, TOPIC_MOTION);
    } else {
        topiclen = snprintf(topic, topic_sz, "%s/%u/%s",  TOPIC_STATIONS, data.stationCode, TOPIC_SENSORS);
    }
//    if (topic_sz < topiclen) { die("snprintf topic\n"); }


//  int mosquitto_publish(    struct 	mosquitto 	*	mosq,
//                              int 	*	mid,
//                              const 	char 	*	topic,
//                              int 		payloadlen,
//                              const 	void 	*	payload,
//                              int 		qos,
//                              bool 		retain	)
    int res = mosquitto_publish(m, NULL, topic, payloadlen, payload, 0, false);
    if (res != MOSQ_ERR_SUCCESS) {
        printf("message not published - error code:%i\n", res);
    }
    return res;
}