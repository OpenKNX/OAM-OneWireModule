# Update procedure for a new firmware or application version

Only tested on Windows 10!

This description is just valid, if you successfuly built and installed a firmware and application the first time according to the [dev setup](https://github.com/mumpf/knx-wire/blob/release/doc/knx-dev-setup.md) instructions.

Open Visual Studio Code. It opens with the last project you used.

In case the opened project is not the WireGateway project, open "WireGateway (Workplace)", you will find it in File->Open Recent menu.

Press Ctrl-Shift-G (Opens Source Control).

Below "SOURCE CONTROL PROVIDERS" you will find all projects necessary for the firmware:

* knx-wire
* knx-logic
* knx-common
* knx

Do for each of them the following:

Click on the project name (i.e. knx-wire).

There is an additional area called "knx-wire Git", having a text box below with a text "Message". At the end of the Git-Area you see 3 dots (...) indicating a menu. Click on these 3 dots.

In the upcomming menu click on the topmost entry "Pull".

As said, do this for each project.

As soon as all 4 pulls are finished, continue with the build steps form the initial documentation:

Press Ctrl-Shift-B, select the "**Build PlatformIO** knx-wire" build task and press enter.

Now the compiler starts, this may take a while, there will be many yellow warnings, they can be ignored.

At the end, there should be a message like

    Linking .pio\build\build\firmware.elf
    Checking size .pio\build\build\firmware.elf
    Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
    RAM:   [==        ]  23.7% (used 7780 bytes from 32768 bytes)
    Flash: [=====     ]  49.3% (used 129364 bytes from 262144 bytes)
    Building .pio\build\build\firmware.bin
    ============================ [SUCCESS] Took 34.60 seconds ======

Now you successfully build the updated Firmware for the Sensormodule.

## How to upload the Firmware to your Hardware

Connect your device via USB to your PC

Press Ctrl-Shift-B, select "**Upload USB** knx-wire" build task and press enter.

Wait until file is uploaded.

Afterwards you have to reprogram physical address (PA) and Application from ETS.

## How to build a knxprod for this firmware

Here you have to do exactly the same steps as desribed in the according chapter in the knx-dev-beta-setup.pdf document.
