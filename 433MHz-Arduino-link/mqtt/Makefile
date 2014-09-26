all: RFMqttRcvCmplxData

RFMqttRcvCmplxData: ../RCSwitch.o RFMqttRcvCmplxData.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@ -lwiringPi -lcurl -lsqlite3 -lmosquitto
	

clean:
	$(RM) *.o RFMqttRcvCmplxData
