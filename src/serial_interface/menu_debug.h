#pragma once

#include <Arduino.h>
#include "XY-SKxxx.h"

// Main debug menu functions
void displayDebugMenu();
void handleDebugMenu(const String& input, XY_SKxxx* ps);

// Basic read/write commands
void handleDebugReadWrite(const String& input, XY_SKxxx* ps);
bool handleDebugRead(const String& input, XY_SKxxx* ps);
bool handleDebugWrite(const String& input, XY_SKxxx* ps);
bool handleDebugMultiWrite(const String& input, XY_SKxxx* ps);
bool handleDebugBlockWrite(const String& input, XY_SKxxx* ps);
bool handleDebugRaw(const String& input, XY_SKxxx* ps);

// Input register (FC04) read commands
bool handleDebugReadInput(const String& input, XY_SKxxx* ps);
bool handleDebugScanInput(const String& input, XY_SKxxx* ps);

// Bus sniffing command
bool handleDebugSniff(const String& input, XY_SKxxx* ps);
void printSniffFrame(uint8_t* data, int len, int index);

// Background bus sniffing state (non-blocking, runs alongside keep-alive)
extern bool sniffActive;
extern unsigned long sniffUntilMs;
extern uint8_t sniffBuffer[256];
extern int sniffLen;
extern int sniffFrameCount;
extern unsigned long sniffLastByteMs;
void sniffTick(XY_SKxxx* ps);
void sniffStop();

// Scan and compare commands
bool handleDebugScan(const String& input, XY_SKxxx* ps);
bool handleDebugCompare(const String& input, XY_SKxxx* ps);

// Write trial command
bool handleDebugWriteTrial(const String& input, XY_SKxxx* ps);

// Write range command
bool handleDebugWriteRange(const String& input, XY_SKxxx* ps);

// Manual weather block (RTC/weather, 0x0200-0x0214) write command
void handleDebugWeather(const String& input, XY_SKxxx* ps);

// Auto-scan weather icon codes: every second bumps the icon +1 and shows the
// code index in the "current temp" field, so you can film the PSU screensaver.
void handleDebugWeatherScan(const String& input, XY_SKxxx* ps);
