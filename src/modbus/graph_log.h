#ifndef GRAPH_LOG_H
#define GRAPH_LOG_H

#include <Arduino.h>

// Ring-buffer graph history for the local panel. Sampled every GRAPH_SAMPLE_MS
// (5 s) into RAM; reset manually from the panel. GRAPH_CAPACITY points at 5 s
// = 720 points ≈ 1 hour of history (~20 KB RAM).
#define GRAPH_SAMPLE_MS   5000UL
#define GRAPH_CAPACITY    720

struct GraphPoint {
  uint32_t ts;              // uptime seconds
  float voltage;            // V
  float current;            // A
  float power;              // W
  float ampHours;           // Ah
  float wattHours;          // Wh
  float temp;               // °C internal
};

// Init the store + spawn the sampling task. Idempotent.
void graphLogStart();

// Read a fresh PSU status and append a sample (no-op if the PSU is offline).
void graphLogSample();

// Clear all history (manual reset from the panel).
void graphLogReset();

// Serialize the full history as {"action":"graphData","points":[...]}.
String graphLogJSON();

#endif // GRAPH_LOG_H