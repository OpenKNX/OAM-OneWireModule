#ifdef WIREMODULE
#include "WireDevice.h"
#include "knx.h"
#include "KnxHelper.h"
#include "OneWireDS2482.h"

uint8_t WireDevice::sDeviceCount = 0;
uint8_t WireDevice::sDeviceIndex = 0;
WireDevice *WireDevice::sDevice[COUNT_1WIRE_CHANNEL] = {0};

// uint8_t WireDevice::sUnknownDeviceFirst = 0;
uint8_t WireDevice::sUnknownDeviceIndex = 0;
uint8_t WireDevice::sUnknownDeviceLast = 0;
uint32_t WireDevice::sUnknownDeviceDelay = 0;
uint8_t WireDevice::sUnknownDeviceDelaySeconds = 60;

bool WireDevice::sForceSensorRead = false;
uint32_t WireDevice::sKnxLoopCallbackDelay = 0;

WireDevice::WireDevice()
{
}

// deserialize device from ets parameters
WireDevice::WireDevice(uint8_t iDeviceIndex, OneWireDS2482* iBusMaster[])
{
    mDeviceIndex = iDeviceIndex; // device index in application
    sDevice[sDeviceCount++] = this;

    bool lIsNew = false;
    // we have to create an instance of this sensor on 1-wire-level, through a factory based on sensor-id
    mOneWire = OneWire::factory(knx.paramData(calcParamIndex(WIRE_sDeviceId)), &lIsNew);
    setDeviceParameter();
    if (lIsNew) {
        uint8_t lSelectedBusmaster = (knx.paramByte(calcParamIndex(WIRE_sBusMasterSelect1)) & WIRE_sBusMasterSelect1Mask) >> WIRE_sBusMasterSelect1Shift;
        OneWireDS2482* lBusMaster = iBusMaster[lSelectedBusmaster - 1];
        lBusMaster->addSensor(mOneWire);
    }
    if (!isIButton()) mOneWire->setModeConnected(true);
}

WireDevice::~WireDevice()
{
}

// static
void WireDevice::processReadRequests()
{
    // this method is called after startup delay and executes read requests, which should just happen once after startup
    static bool sCalledProcessReadRequests = false;
    if (!sCalledProcessReadRequests)
    {
        // we go through all IO devices defined as outputs and check for initial read requests
        for (uint8_t lDeviceId = 0; lDeviceId < sDeviceCount; lDeviceId++)
        {
            WireDevice *lDevice = sDevice[lDeviceId];
            if (lDevice->processReadRequest()) {
                knx.getGroupObject(lDevice->getIndex() + WIRE_KoOffset).requestObjectRead();
                printDebug("ReadRequest send for KO%d\n", lDevice->getIndex() + WIRE_KoOffset);
            }
        }
        sCalledProcessReadRequests = true;
    }
}

// static
void WireDevice::processKOCallback(GroupObject &iKo)
{
    // check for 1-Wire-KO
    if (iKo.asap() >= WIRE_KoOffset && iKo.asap() < ((knx.paramByte(WIRE_BusMasterCount) & WIRE_BusMasterCountMask) >> WIRE_BusMasterCountShift) * 30 + WIRE_KoOffset)
    {
        uint8_t lDeviceIndex = iKo.asap() - WIRE_KoOffset;
        // has to be an input KO (for an 1W output device)
        WireDevice *lDevice = sDevice[lDeviceIndex];
        // we have to check this (in case someone writes on a KO of a sensor device)
        if (lDevice->isIO())
        {
            // find correct DPT for KO
            if (lDevice->getModelFunction() == ModelFunction_IoByte)
                lDevice->setValue(iKo.value(getDPT(VAL_DPT_5)));
            else
                lDevice->setValue(iKo.value(getDPT(VAL_DPT_1)));
        }
    }
}

// static
void WireDevice::processIButtonGroups()
{
    // group processing would be a loop over max 90 devices
    // times 8 Groups and the according logical operations
    // this would block too long!
    // Here we implement an asynchroneous algorithm
    // as described in doc/iButton-Group-Handling.txt
    // so we iterate per device just through all 8 groups
    // final group result takes at max 90 iterations
    // currently we have about 3000 iterations per second, so it is still fast enough
    static uint8_t sIndex = 0;
    static bool sIButtonExist = true;
    static uint8_t sToProcess = 0xFF;
    static uint8_t sGroupType = knx.paramByte(WIRE_Group1); // this is constant for the whole program runtime
    WireDevice *lDevice;

    // we iterate only if there are iButtons
    // if (sIButtonExist) {
    sIButtonExist = false;
    // create start condition
    if (sIndex == 0)
    {
        sToProcess = 0xFF;
    }
    // search for the next iButton
    do
    {
        lDevice = sDevice[sIndex++];
    } while (sIndex < sDeviceCount && !lDevice->isIButton());
    // process this iButton
    if (sIndex <= sDeviceCount && lDevice->isIButton())
    {
        sIButtonExist = true;
        bool lValue = lDevice->getValue();
        uint8_t lButtonGroups = knx.paramByte((sIndex - 1) * WIRE_ParamBlockSize + WIRE_ParamBlockOffset + WIRE_sGroup1);
        uint8_t lButtonGroupsToProcess = lButtonGroups & sToProcess;
        uint8_t lGroupBit = 0x80;
        for (uint8_t lGroupIndex = 0; lGroupIndex < 7 && lButtonGroupsToProcess; lGroupIndex++)
        {
            if (lButtonGroupsToProcess & lGroupBit)
            {
                //group has to be processed
                bool lGroupType = sGroupType & lGroupBit;
                if (lGroupType != lValue)
                {
                    // this is the case, where
                    // - group type is AND and value is 0 or
                    // - group type is OR and value is 1
                    // we set the group KO to value...
                    GroupObject &lKo = knx.getGroupObject(WIRE_KoGroup1 + lGroupIndex);
                    if ((bool)lKo.value(getDPT(VAL_DPT_1)) != lValue)
                    {
                        printDebug("KO%d sendet Wert: %d\n", WIRE_KoGroup1 + lGroupIndex, lValue);
                        lKo.value(lValue, getDPT(VAL_DPT_1));
                    }
                    // and mark this group as processed
                    sToProcess &= ~lGroupBit;
                }
            }
            lGroupBit >>= 1;
            knxLoopCallback();
        }
    }
    if (sIndex >= sDeviceCount && sIButtonExist)
    {
        // we iterated through all iButtons, let's process remaining groups
        uint8_t lGroupBit = 0x80;
        for (uint8_t lGroupIndex = 0; lGroupIndex < 7; lGroupIndex++)
        {
            if (sToProcess & lGroupBit)
            {
                // group was not processed, we set KO to group type
                GroupObject &lKo = knx.getGroupObject(WIRE_KoGroup1 + lGroupIndex);
                bool lValue = (sGroupType & lGroupBit);
                if ((bool)lKo.value(getDPT(VAL_DPT_1)) != lValue)
                {
                    printDebug("KO%d sendet Wert: %d\n", WIRE_KoGroup1 + lGroupIndex, lValue);
                    lKo.value(lValue, getDPT(VAL_DPT_1));
                }
            }
            lGroupBit >>= 1;
            knxLoopCallback();
        }
    }
    if (sIndex >= sDeviceCount)
        sIndex = 0;
}

// static
void WireDevice::processUnknownDevices()
{
    bool lForce = sUnknownDeviceDelay == 0;

    if (lForce || delayCheck(sUnknownDeviceDelay, sUnknownDeviceDelaySeconds * 1000))
    {
        if (sUnknownDeviceIndex < sDeviceCount)
            sUnknownDeviceIndex = sDeviceCount;
        if (sUnknownDeviceIndex < sUnknownDeviceLast)
        {
            OneWire *lSensor = sDevice[sUnknownDeviceIndex++]->mOneWire;
            if (lSensor->Mode() == OneWire::New)
            {
                // output is 1 new ID in 2 Seconds at max
                printDebug("KO%d sendet Wert: ", WIRE_KoNewId);
                char lBuffer[15];
                lBuffer[14] = 0;
                sprintf(lBuffer, "%02X%02X%02X%02X%02X%02X%02X", lSensor->Id()[0], lSensor->Id()[1], lSensor->Id()[2], lSensor->Id()[3], lSensor->Id()[4], lSensor->Id()[5], lSensor->Id()[6]);
                printDebug("%s\n", lBuffer);
                knx.getGroupObject(WIRE_KoNewId).value(lBuffer, getDPT(VAL_DPT_16));
                sUnknownDeviceDelaySeconds = 2; // check in 2 Seconds for next new ID
            }
        }
        if (sUnknownDeviceIndex >= sUnknownDeviceLast)
        {
            sUnknownDeviceIndex = 0;
            sUnknownDeviceDelaySeconds = 60; // next output of all IDs in a minute
        }
        sUnknownDeviceDelay = millis();
        if (sUnknownDeviceDelay == 0)
            sUnknownDeviceDelay = 1;
    }
}

// static
void WireDevice::processOneWire(bool iForce)
{
    // are there any OW sensors
    if (sDeviceCount > 0)
    {
        if (iForce)
        {
            for (uint8_t i = 0; i < sDeviceCount; i++)
                sDevice[i]->clearSendDelay();
        }
        // we iterate through all OW-Sensors
        sDevice[sDeviceIndex]->processOneWire();
        if (++sDeviceIndex >= sDeviceCount)
            sDeviceIndex = 0;
    }
}

// jeder neue Sensor, der erstmals bei der 1W-Suche erkannt wird,
// wird über diesen Callback der Applikation mitgeteilt.
// Hier wird gecheckt, ob er schon in der Liste der neuen Sensoren ist.
// Wenn nicht, wird er dieser Liste zugefügt. Diese Liste
// wird dann jede Minute ausgegeben.
// static
bool WireDevice::processNewIdCallback(OneWire *iOneWire)
{
    bool lResult = false;
    if (sUnknownDeviceLast < sDeviceCount)
        sUnknownDeviceLast = sDeviceCount;
    for (uint8_t lIndex = sDeviceCount; lIndex < sUnknownDeviceLast; lIndex++)
    {
        OneWire *lSensor = sDevice[lIndex]->mOneWire;
        if (equalId(iOneWire->Id(), lSensor->Id()))
        {
            lResult = true;
            break;
        }
    }
    if (!lResult && sUnknownDeviceLast < COUNT_1WIRE_CHANNEL)
    {
        // new sensor found, we add it to unknown device list
        sDevice[sUnknownDeviceLast] = new WireDevice();
        sDevice[sUnknownDeviceLast++]->mOneWire = iOneWire;
        // triger send new sensor info
        sUnknownDeviceDelay = millis() - 58000; // start output in 2 seconds
    }
    return lResult;
}

// static - this is not perfect, but it works
bool WireDevice::measureOneWire(MeasureType iMeasureType, float &eValue)
{
    eValue = sDevice[sDeviceIndex]->getValue();
    return true;
}

// static
void WireDevice::loop()
{
    knxLoopCallback(); // improve knx responsiveness

    // if (!gIsSetup)
    //     return;

    processOneWire(sForceSensorRead);
    knxLoopCallback();
    processUnknownDevices();
    knxLoopCallback();
    processIButtonGroups();
    // falls Du auch ein KO zum anfordern der Werte anbieten willst, muss in der Routine, die das KO auswertet
    // nur die folgende Variable auf true gesetzt werden, dann werden die Sensorwerte gesendet.
    sForceSensorRead = false;

    // gOneWireBM.loop();
}

// static
void WireDevice::forceSensorRead()
{
    sForceSensorRead = true;
}

// static
void WireDevice::knxLoopCallback()
{
    // this is a generic dispatcher which ensures, that knx.loop() is called as often as 
    // necessary, but not more often than every 1 ms.
    if (delayCheck(sKnxLoopCallbackDelay, 5)) {
        knx.loop();
        sKnxLoopCallbackDelay = millis();
    }
}

uint8_t WireDevice::getIndex()
{
    return mDeviceIndex;
}

uint32_t WireDevice::calcParamIndex(uint16_t iParamIndex)
{
    return mDeviceIndex * WIRE_ParamBlockSize + WIRE_ParamBlockOffset + iParamIndex;
}

uint8_t WireDevice::getModelFunction() {
    return knx.paramByte(calcParamIndex(WIRE_sModelFunction));
}

void WireDevice::setValue(uint8_t iValue)
{
    if (mData.actor.lastOutputValue != iValue)
    {
        if (mOneWire->setValue(iValue, getModelFunction()))
            mData.actor.lastOutputValue = iValue;
    }
}

uint8_t WireDevice::getValue()
{
    uint8_t lResult = 0;
    if (mOneWire != NULL)
    {
        mOneWire->getValue(lResult, getModelFunction());
    }
    return lResult;
}

void WireDevice::clearSendDelay() {
    mData.sensor.sendDelay = 0;
}

bool WireDevice::isIO() {
    if (mOneWire == NULL) {
        return false;
    } else {
        return (mOneWire->Family() == MODEL_DS2413 || mOneWire->Family() == MODEL_DS2408);
    }
}

bool WireDevice::isIButton() {
    if (mOneWire == NULL)
    {
        return false;
    }
    else
    {
        return (mOneWire->Family() == MODEL_DS1990);
    }
}

void WireDevice::setDeviceParameter()
{
    uint8_t lModelFunction = knx.paramByte(calcParamIndex(WIRE_sModelFunction));
    switch (mOneWire->Family())
    {
        case MODEL_DS18B20:
        case MODEL_DS18S20:
            mOneWire->setParameter(OneWire::MeasureResolution, 0x00, lModelFunction);
            break;
        case MODEL_DS1990:
            mData.sensor.lastSentValue = NO_NUM; // NAN was not working here
            break;
        case MODEL_DS2408:
        case MODEL_DS2413:
            mOneWire->setParameter(OneWire::IoMask, knx.paramByte(calcParamIndex(WIRE_sIoBitmask0)), lModelFunction);
            mOneWire->setParameter(OneWire::IoInvertMask, knx.paramByte(calcParamIndex(WIRE_sIoInvertBitmask0)), lModelFunction);
            mData.sensor.lastSentValue = NO_NUM; // NAN was not working here
            // for IO, we disable ReadRequests from its KO as long as no valid initial state is known
            knx.getGroupObject(mDeviceIndex + WIRE_KoOffset).commFlag(Uninitialized);
        default: 
            break;
    }
}

void WireDevice::processOneWire() {

    if (mOneWire != NULL)
    {
        bool lLastSent = false;
        bool lNewState = false;
        bool lIsInitial = (mData.sensor.lastSentValue < -100);
        bool lSendInitial = false;
        if (lIsInitial)
        {
            lSendInitial = (knx.paramByte(calcParamIndex(WIRE_siButtonSendStatus)) & WIRE_siButtonSendStatusMask) >> WIRE_siButtonSendStatusShift;
        }
        uint8_t lValue = 0;
        switch (mOneWire->Family())
        {
            case MODEL_DS18B20:
            case MODEL_DS18S20:
            case MODEL_DS2438:
                processSensor(10.0, calcParamIndex(0), mDeviceIndex + WIRE_KoOffset);
                break;
            case MODEL_DS1990:
                lLastSent = (mData.sensor.lastSentValue != 0);
                lNewState = (mOneWire->Mode() == OneWire::Connected);
                // we just send initially on the bus if requested in ETS appllication
                if (mOneWire->Mode() != OneWire::New)
                {
                    if (lLastSent != lNewState || lIsInitial)
                    {
                        if (!lIsInitial || lSendInitial) {
                            knx.getGroupObject(mDeviceIndex + WIRE_KoOffset).value(lNewState, getDPT(VAL_DPT_1));
                            printDebug("KO%d sendet Wert: %d\n", mDeviceIndex + WIRE_KoOffset, lNewState);
                        }
                        mData.sensor.lastSentValue = lNewState;
                    }
                }
                break;
            case MODEL_DS2408:
            case MODEL_DS2413:
                // handle I/O device, here just the input part (getting data from device)
                // Output is done in KO callback
                // now do input processing
                if (mOneWire->Mode() == OneWire::Connected)
                {
                    lValue = getValue();
                    if (lValue != mData.actor.lastInputValue || lIsInitial)
                    {
                        if (!lIsInitial || lSendInitial)
                        {
                            knx.getGroupObject(mDeviceIndex + WIRE_KoOffset).value(lValue, (getModelFunction() == ModelFunction_IoByte) ? getDPT(VAL_DPT_5) : getDPT(VAL_DPT_1));
                            printDebug("KO%d sendet Wert: %0X\n", mDeviceIndex + WIRE_KoOffset, lValue);
                        }
                        mData.actor.lastInputValue = lValue;
                    }
                }
                break;
            default:
                break;
        }
    }
}

// read request on startup is currently not supported, because the KO (having an L-Flag) answers himself
// the processing itself works, but there is currently no known way to prevent replys on such a read
bool WireDevice::processReadRequest() {
    bool lResult = false;
    if (isIO()) {
        uint8_t lIoMask = knx.paramByte(calcParamIndex(WIRE_sIoBitmask0));
        bool lSendReadRequest = (knx.paramByte(calcParamIndex(WIRE_sIOReadRequest)) & WIRE_sIOReadRequestMask) >> WIRE_sIOReadRequestShift;
        if (mOneWire->Family() == MODEL_DS2413) {
            lIoMask |= 0xFC;
        }
        lResult = lSendReadRequest && (lIoMask < 0xFF);
    }
    return lResult;
}

// void WireDevice::setup(OneWire *iOneWire, uint8_t iModelFunction) {
//     mDevice = iOneWire;
//     mModelFunction = iModelFunction;
// }

// generic sensor processing
void WireDevice::processSensor(float iOffsetFactor, uint16_t iParamIndex, uint16_t iKoNumber) {
    bool lForce = mData.sensor.sendDelay == 0;
    bool lSend = lForce;
    float lValueFactor = 1.0;
    // value factor depends on model funtion
    uint8_t lModelFunction = getModelFunction();
    if (lModelFunction >= ModelFunction_RawVDD && lModelFunction <= ModelFunction_RawVSens) {
        lValueFactor = 1000.0;
    }
    // process send cycle
    uint32_t lCycle =  getDelayPattern(iParamIndex + WIRE_sSensorDelayBase);

    // we waited enough, let's send the value
    if (lCycle && delayCheck(mData.sensor.sendDelay, lCycle))
        lSend = true;

    float lValue = 0;

    // process read cycle
    if (lSend || delayCheck(mData.sensor.readDelay, 1000))
    {
        // we waited enough, let's read the sensor
        int32_t lOffset = knx.paramByte(iParamIndex + WIRE_sSensorOffset);
        bool lValid = mOneWire->getValue(lValue, lModelFunction);
        if (lValid)
        {
            // we have now the internal sensor value, we correct it now
            lValue = lValue * lValueFactor;
            lValue += (lOffset / iOffsetFactor);
            // smoothing (? glätten ?) of the new value
            // Formel: Value = ValueAlt + (ValueNeu - ValueAlt) / p
            if (!lForce && isNum(mData.sensor.lastValue))
            {
                lValue = mData.sensor.lastValue + (lValue - mData.sensor.lastValue) / knx.paramByte(iParamIndex + WIRE_sSensorSmooth);
            }
            // evaluate sending conditions (relative delta / absolute delta)
            if (mData.sensor.lastSentValue > 0.0)
            {
                // currently we assume indoor measurement with values > 0.0
                float lDelta = 100.0 - lValue / mData.sensor.lastSentValue * 100.0;
                uint32_t lPercent = knx.paramByte(iParamIndex + WIRE_sSensorDeltaPercent);
                if (lPercent && (uint32_t)abs(lDelta) >= lPercent)
                    lSend = true;
                // absolute diff we have to calculate in int due to rounding problems
                uint32_t lAbsolute = knx.paramWord(iParamIndex + WIRE_sSensorDeltaAbs);
                if (lAbsolute > 0 && abs(lValue * iOffsetFactor - mData.sensor.lastSentValue * iOffsetFactor) >= lAbsolute)
                    lSend = true;
            }
            // we always store the new value in KO, even it it is not sent (to satisfy potential read request)
            if (isNum(lValue)) { 
                mData.sensor.lastValue = lValue;
                knx.getGroupObject(iKoNumber).valueNoSend(lValue, getDPT(VAL_DPT_9));
            } else {
                lSend = false;
            }
            // wenn in KONNEKTING möglich, sollte der Wert im KO gespeichert werden, ohne dass
            // er gesendet wird, damit ein GroupValueRead zwischendurch auch den korrekten Wert liefert
            // hier lValue ins KO schreiben, KO-Nummer steht in iKoNumber
        }
        else
        {
            lSend = false;
        }
        mData.sensor.readDelay = millis();
    }
    if (lSend)
    {
        // wenn in KONNEKTING möglich, dann den letzten ins KO geschriebenen Wert jetzt senden lassen
        // sonst einfach lValue ueber das KO senden lassen, KO-Nummer steht in iKoNumber
        printDebug("KO%d sendet Wert: %f\n", iKoNumber, lValue);
        knx.getGroupObject(iKoNumber).objectWritten();
        mData.sensor.lastSentValue = (float)knx.getGroupObject(iKoNumber).value(getDPT(VAL_DPT_9));
        mData.sensor.sendDelay = millis();
        if (mData.sensor.sendDelay == 0)
            mData.sensor.sendDelay = 1;
    }
}
#endif
