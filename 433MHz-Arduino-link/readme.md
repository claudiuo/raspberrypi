Project idea: multiple Arduinos with TX radios, each equipped with DHT and PIR sensors; they all send the data to the Raspberry Pi which posts it to the web and also writes it to a local database. In addition to the values from sensors, I am also sending the voltage level.

The comments in the code for both the sender (Arduino) and the receiver (Raspberry Pi) pretty much explain everything (how it works, how to connect the sensors and radios, and so on). More details can be found [on my blog](http://ivyco.blogspot.com/2014/09/arduino-sensors-to-raspberry-pi-using.html).

You will need to build RCSwitch (which I got from [ninjablocks's repo](https://github.com/ninjablocks/433Utils/tree/master/RPi_utils)) and also install wiringPi, curl and sqlite3 and the related dev libraries: libcurl4-openssl-dev and libsqlite3-dev. If you want to use the database, create it and populate it with 2 tables:

```
sqlite3 sensors.db
sqlite> BEGIN;
sqlite> CREATE TABLE dht (id INTEGER PRIMARY KEY, station INTEGER, temp NUMERIC, humidity NUMERIC, voltage NUMERIC, created_date DEFAULT CURRENT_TIMESTAMP, posted BOOLEAN);
sqlite> COMMIT;
sqlite> BEGIN;
sqlite> CREATE TABLE pir (id INTEGER PRIMARY KEY, station INTEGER, motion INTEGER, created_date DEFAULT CURRENT_TIMESTAMP, posted BOOLEAN);
sqlite> COMMIT;
```

<img src="Ard_DHT_PIR_433-radio_bb.png" width="50%" height="auto"/><img src="RPi_433-radio_bb.png" width="40%" height="auto"/>