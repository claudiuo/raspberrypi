all: RFRcvCmplxData

RFRcvCmplxData: RCSwitch.o RFRcvCmplxData.o
    $(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@ -lwiringPi -lcurl -lsqlite3

clean:
    $(RM) *.o RFRcvCmplxData
