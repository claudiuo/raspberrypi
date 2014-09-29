Same project as in the parent folder, except this time the Raspberry Pi code publishes to MQTT topics (one for DHT sensor and another for motion sensor). A node-red flow subscribes to these topics and posts sensor data to dweet.io and publishes motion data to another topic; a python script listens to it and plays a sound file - I will use this as a Halloween project to play a sound file when motion sensor is triggered.

More details can be found [on my blog](http://ivyco.blogspot.com/2014/09/project-follow-up-raspberry-pi-with-433.html).

You will need to build RCSwitch (which I got from [ninjablocks's repo](https://github.com/ninjablocks/433Utils/tree/master/RPi_utils)) in the parent folder and also install wiringPi, mosquitto and the related dev library: libmosquitto-dev.