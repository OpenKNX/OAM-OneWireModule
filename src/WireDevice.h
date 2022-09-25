#pragma once
#ifdef WIREMODULE
#include "knx.h"
#include "HardwareDevices.h"
#include "Sensor.h"
#include <OneWire.h>

class WireDevice
{
  private:
    static uint8_t sDeviceCount; // max. device index to process during runtime
    static uint8_t sDeviceIndex; // iterator for global device processing
    static WireDevice *sDevice[COUNT_1WIRE_CHANNEL]; // list of all used devices accross all BM

    // unknown device processing
    // static uint8_t sUnknownDeviceFirst;
    static uint8_t sUnknownDeviceIndex;
    static uint8_t sUnknownDeviceLast;
    static uint32_t sUnknownDeviceDelay;
    static uint8_t sUnknownDeviceDelaySeconds;

    static bool sForceSensorRead;
    static uint32_t sKnxLoopCallbackDelay;

    OneWire *mOneWire = NULL;
    uData mData;
    uint8_t mDeviceIndex = 0; 

  public:
    WireDevice();
    WireDevice(uint8_t iDeviceIndex, OneWireDS2482* iBusMaster[]);
    ~WireDevice();

    // general processing is static
    static void loop();
    static void processKOCallback(GroupObject &iKo);
    static bool measureOneWire(MeasureType iMeasureType, float &eValue);
    static void processIButtonGroups();
    static void processUnknownDevices();
    static void processOneWire(bool iForce);
    static void processReadRequests();
    static bool processNewIdCallback(OneWire *iOneWireSensor);
    static void knxLoopCallback(); // just to avoid knx reference in common
    static void forceSensorRead();

    uint8_t getIndex();
    uint32_t calcParamIndex(uint16_t iParamIndex);
    uint8_t getModelFunction();

    void setValue(uint8_t iValue);
    uint8_t getValue();
    void clearSendDelay();
    void processOneWire();
    bool processReadRequest();
    bool isIO();
    bool isIButton();
    void setDeviceParameter();
    // void setup(OneWire *iOneWire, uint8_t iModelFunction);
    void processSensor(float iOffsetFactor, uint16_t iParamIndex, uint16_t iKoNumber);
};
#endif
