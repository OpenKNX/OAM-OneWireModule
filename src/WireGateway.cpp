#ifdef WIREGATEWAY
#include "Helper.h"

#include "Sensor.h"
#include "OneWire.h"
#include "WireDevice.h"
#include <OneWireDS2482.h>

#include "IncludeManager.h"

#include "Logic.h"
#include "KnxHelper.h"

const uint8_t cFirmwareMajor = 3;    // 0-31
const uint8_t cFirmwareMinor = 8;    // 0-31
const uint8_t cFirmwareRevision = 0; // 0-63

// Achtung: Bitfelder in der ETS haben eine gewöhnungswürdige
// Semantik: ein 1 Bit-Feld mit einem BitOffset=0 wird in Bit 7(!) geschrieben
uint32_t gStartupDelay;
uint32_t gHeartbeatDelay;
bool gIsRunning = false;
OneWireDS2482* gBusMaster[COUNT_1WIRE_BUSMASTER];
WireDevice gDevice[COUNT_1WIRE_CHANNEL];
uint16_t gCountSaveInterrupt = 0;
uint32_t gSaveInterruptTimestamp = 0;
bool gForceSensorRead = true;
Logic gLogic;

typedef bool (*getSensorValue)(MeasureType, float&);

// uint16_t getError() {
//     return (uint16_t)knx.getGroupObject(LOG_KoError).value(getDPT(VAL_DPT_7));
// }

// void setError(uint16_t iValue) {
//     knx.getGroupObject(LOG_KoError).valueNoSend(iValue, getDPT(VAL_DPT_7));
// }

// void sendError() {
//     knx.getGroupObject(LOG_KoError).objectWritten();
// }

void ProcessHeartbeat()
{
    // the first heartbeat is send directly after startup delay of the device
    if (gHeartbeatDelay == 0 || delayCheck(gHeartbeatDelay, getDelayPattern(LOG_HeartbeatDelayBase)))
    {
        // we waited enough, let's send a heartbeat signal
        knx.getGroupObject(LOG_KoHeartbeat).value(true, getDPT(VAL_DPT_1));
        // if there is an error, we send it with heartbeat, too
        // if (knx.paramByte(LOG_Error) & LOG_ErrorMask) {
        //     if (getError()) sendError();
        // }
        gHeartbeatDelay = millis();
        // debug-helper for logic module
        // print("ParDewpoint: ");
        // println(knx.paramByte(LOG_Dewpoint));
        gLogic.debug();
    }
}

void ProcessReadRequests() {
    // this method is called after startup delay and executes read requests, which should just happen once after startup
    static bool sCalledProcessReadRequests = false;
    if (!sCalledProcessReadRequests)
    {
        // we go through all IO devices defined as outputs and check for initial read requests
        WireDevice::processReadRequests();
        gLogic.processReadRequests();
        sCalledProcessReadRequests = true;
    }
}

// true solgange der Start des gesamten Moduls verzögert werden soll
bool startupDelay()
{
    return !delayCheck(gStartupDelay, getDelayPattern(LOG_StartupDelayBase));
}

bool processDiagnoseCommand()
{
    char *lBuffer = gLogic.getDiagnoseBuffer();
    bool lOutput = false;
    if (lBuffer[0] == 'v')
    {
        // Command v: retrun fimware version, do not forward this to logic,
        // because it means firmware version of the outermost module
        uint16_t lFirmwareVersion = knx.bau().deviceObject().version();
        sprintf(lBuffer, "VER [%d] %d.%d", lFirmwareVersion >> 11, (lFirmwareVersion >> 6) & 0x1F, lFirmwareVersion & 0x3F);
        lOutput = true;
    }
    else
    {
        // let's check other modules for this command
        lOutput = gLogic.processDiagnoseCommand();
    }
    return lOutput;
}

void ProcessDiagnoseCommand(GroupObject &iKo)
{
    // this method is called as soon as iKo is changed
    // an external change is expected
    // because this iKo also is changed within this method,
    // the method is called again. This might result in
    // an endless loop. This is prevented by the isCalled pattern.
    static bool sIsCalled = false;
    if (!sIsCalled)
    {
        sIsCalled = true;
        //diagnose is interactive and reacts on commands
        gLogic.initDiagnose(iKo);
        if (processDiagnoseCommand())
            gLogic.outputDiagnose(iKo);
        sIsCalled = false;
    }
};

void ProcessKoCallback(GroupObject &iKo)
{
    // check if we evaluate own KO
    if (iKo.asap() == LOG_KoDiagnose) {
        ProcessDiagnoseCommand(iKo);
    }
    else
    {
        WireDevice::processKOCallback(iKo);
        // else dispatch to logicmodule
        gLogic.processInputKo(iKo);
    }
}

void appLoop()
{
    if (!knx.configured())
        return;

    // handle KNX stuff
    if (startupDelay())
        return;

    gIsRunning = true;

    // at this point startup-delay is done
    // we process heartbeat
    ProcessHeartbeat();
    ProcessReadRequests();
    gLogic.loop();
    WireDevice::loop();
    uint8_t lNumBusmaster = (knx.paramByte(WIRE_BusMasterCount) & WIRE_BusMasterCountMask) >> WIRE_BusMasterCountShift;
    for (uint8_t lBusmasterIndex = 0; lBusmasterIndex < lNumBusmaster; lBusmasterIndex++)
    {
        gBusMaster[lBusmasterIndex]->loop();
        knx.loop();
    }

    // at Startup, we want to send all values immediately
    // ProcessSensors(gRuntimeData.forceSensorRead);
    gForceSensorRead = false;
}

void appSetup(bool iSaveSupported)
{
    // try to get rid of occasional I2C lock...
    savePower();
    ledProg(true);
    ledInfo(true);
    delay(500);
    restorePower();
    // check hardware availability
    boardCheck();
    ledInfo(false);
    ledProg(false);

    if (knx.configured())
    {
        // 5 bit major, 5 bit minor, 6 bit revision
        // knx.bau().deviceObject().version(cFirmwareMajor << 11 | cFirmwareMinor << 6 | cFirmwareRevision);
        gStartupDelay = millis();
        gHeartbeatDelay = 0;
        gCountSaveInterrupt = 0;
        // GroupObject &lKoRequestValues = knx.getGroupObject(LOG_KoRequestValues);
        if (GroupObject::classCallback() == 0) GroupObject::classCallback(ProcessKoCallback);
        gLogic.setup(iSaveSupported);
        // should we search for new devices?
        bool lSearchNewDevices = knx.paramByte(WIRE_IdSearch) & WIRE_IdSearchMask;
        // are there iButtons?
        // uint8_t lIsIButton = 0;


        Wire.setClock(400000);

        gBusMaster[0] = new OneWireDS2482(WireDevice::processNewIdCallback, WireDevice::knxLoopCallback);
        gBusMaster[0]->setup(0, 1, lSearchNewDevices); 
        uint8_t lNumBusmaster = (knx.paramByte(WIRE_BusMasterCount) & WIRE_BusMasterCountMask) >> WIRE_BusMasterCountShift;
#if COUNT_1WIRE_BUSMASTER > 1
        if (lNumBusmaster > 1) {
            gBusMaster[1] = new OneWireDS2482(WireDevice::processNewIdCallback, WireDevice::knxLoopCallback);
            gBusMaster[1]->setup(1, 3, lSearchNewDevices);
#if COUNT_1WIRE_BUSMASTER > 2
            if (lNumBusmaster > 2)
            {
                gBusMaster[2] = new OneWireDS2482(WireDevice::processNewIdCallback, WireDevice::knxLoopCallback);
                gBusMaster[2]->setup(2, 2, lSearchNewDevices); 
            }
        }
#endif
#endif
        // initialize all known 1-Wire-sensors from application data
        for (uint8_t lDeviceIndex = 0; lDeviceIndex < COUNT_1WIRE_CHANNEL; lDeviceIndex++)
        {
            // check for family information
            uint8_t lFamily = knx.paramByte(lDeviceIndex * WIRE_ParamBlockSize + WIRE_ParamBlockOffset + WIRE_sFamilyCode);
            if (lFamily > 0) {
                // WireDevice *lDevice = new WireDevice(lDeviceIndex, gBusMaster);
                new WireDevice(lDeviceIndex, gBusMaster);
            }
        }
    }
}
#endif
