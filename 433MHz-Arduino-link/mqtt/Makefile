all: RFMqttRcvCmplxData

RFMqttRcvCmplxData: ../RCSwitch.o RFMqttRcvCmplxData.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@ -lwiringPi -lsqlite3 -lmosquitto

clean:
	$(RM) *.o RFMqttRcvCmplxData
