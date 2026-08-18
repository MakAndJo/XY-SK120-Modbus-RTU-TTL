#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "XY-SKxxx.h"  // Keep this include to fix the compilation error

void setupWebServer(AsyncWebServer* server);
void handleWebSocketMessage(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, 
                           AwsFrameInfo* info, uint8_t* data, size_t len);
void handleDeviceSettingAction(AsyncWebSocketClient* client, const String& action, DynamicJsonDocument& doc);
String getContentType(String filename);
bool handleFileRead(AsyncWebServerRequest *request);

// New unified status functions
void sendCompletePSUStatus(AsyncWebSocketClient* client);
void sendOperatingModeDetails(AsyncWebSocketClient* client);

// Server-side polling: read fresh PSU status and push it to all connected clients
void pollAndBroadcastPSUStatus();

// ESPHome-style keep-alive of the WiFi host registers (0x0030-0x0034).
// Re-writes the block every ~1s while a WiFi host (0x3B3A) is active.
void wifiModuleKeepAlive();

// Force the next poll to broadcast even if the status is unchanged (new client)
void forceStatusBroadcast();

// PSU helper functions
float getPSUVoltage(XY_SKxxx* powerSupply);
float getPSUCurrent(XY_SKxxx* powerSupply);
float getPSUPower(XY_SKxxx* powerSupply);
bool isPSUOutputEnabled(XY_SKxxx* powerSupply);
bool setPSUOutput(XY_SKxxx* powerSupply, bool enable);
String getPSUOperatingMode(XY_SKxxx* powerSupply);
void getPSUOperatingModeDetails(XY_SKxxx* powerSupply, String& modeName, float& setValue);

#endif // WEB_INTERFACE_H
