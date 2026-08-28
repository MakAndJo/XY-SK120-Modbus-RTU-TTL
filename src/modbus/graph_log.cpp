#include "graph_log.h"
#include "modbus/psu_service.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>

static SemaphoreHandle_t graphMutex = nullptr;
static GraphPoint gPoints[GRAPH_CAPACITY];
static uint32_t gHead = 0;
static uint32_t gCount = 0;

static void append(const GraphPoint& p) {
  gPoints[gHead] = p;
  gHead = (gHead + 1) % GRAPH_CAPACITY;
  if (gCount < GRAPH_CAPACITY) gCount++;
}

void graphLogSample() {
  PSUStatusData data;
  if (!readPSUStatusBatched(data)) return;
  GraphPoint p;
  p.ts = millis() / 1000;
  p.voltage = data.voltage;
  p.current = data.current;
  p.power = data.power;
  p.ampHours = data.ampHours;
  p.wattHours = data.wattHours;
  p.temp = data.internalTemp;

  if (graphMutex) xSemaphoreTake(graphMutex, portMAX_DELAY);
  append(p);
  if (graphMutex) xSemaphoreGive(graphMutex);
}

void graphLogReset() {
  if (graphMutex) xSemaphoreTake(graphMutex, portMAX_DELAY);
  gHead = 0;
  gCount = 0;
  if (graphMutex) xSemaphoreGive(graphMutex);
}

String graphLogJSON() {
  String out;
  out.reserve((size_t)gCount * 64 + 64);
  out += "{\"action\":\"graphData\",\"points\":[";
  if (graphMutex) xSemaphoreTake(graphMutex, portMAX_DELAY);
  for (uint32_t i = 0; i < gCount; i++) {
    if (i) out += ',';
    const GraphPoint& p = gPoints[(gHead - gCount + i + GRAPH_CAPACITY) % GRAPH_CAPACITY];
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"ts\":%lu,\"v\":%.2f,\"i\":%.3f,\"p\":%.2f,\"ah\":%.3f,\"wh\":%.3f,\"tp\":%.1f}",
             (unsigned long)p.ts, p.voltage, p.current, p.power,
             p.ampHours, p.wattHours, p.temp);
    out += buf;
  }
  if (graphMutex) xSemaphoreGive(graphMutex);
  out += "]}";
  return out;
}

static void graphLogTask(void* param) {
  (void)param;
  while (true) {
    graphLogSample();
    vTaskDelay(GRAPH_SAMPLE_MS / portTICK_PERIOD_MS);
  }
}

void graphLogStart() {
  if (graphMutex) return;
  graphMutex = xSemaphoreCreateMutex();
  xTaskCreate(graphLogTask, "graph", 4096, NULL, 1, NULL);
  Serial.println("[GRAPH] history sampler started");
}