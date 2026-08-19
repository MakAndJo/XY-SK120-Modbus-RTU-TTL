#ifndef WIFI_WEBSOCKET_HANDLER_H
#define WIFI_WEBSOCKET_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Function declarations
void handleAddWifiNetworkCommand(AsyncWebSocketClient* client, DynamicJsonDocument& doc);
void handleRemoveWifiNetworkCommand(AsyncWebSocketClient* client, DynamicJsonDocument& doc);
void handleConnectWifiCommand(AsyncWebSocketClient* client, DynamicJsonDocument& doc);
void sendErrorResponse(AsyncWebSocketClient* client, const String& error);

#endif // WIFI_WEBSOCKET_HANDLER_H
