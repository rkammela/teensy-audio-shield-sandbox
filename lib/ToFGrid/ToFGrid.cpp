#include "ToFGrid.h"

#include <SparkFun_VL53L5CX_Library.h>

namespace {
  // 8x8 ranging at 10 Hz matches the AURA hand-tracking budget; both
  // sensors share these settings.
  constexpr int      RESOLUTION = 8 * 8;
  constexpr uint16_t RATE_HZ    = 10;
  constexpr int      HAND_THRESHOLD_MM = 800;
  constexpr int      HAND_MIN_VALID_MM = 50;

  SparkFun_VL53L5CX sensor[ToFGrid::MAX_CHANNELS];
  bool              initialized[ToFGrid::MAX_CHANNELS] = { false, false };

  inline bool validChannel(int channel) {
    return channel >= 0 && channel < ToFGrid::MAX_CHANNELS;
  }
}

namespace ToFGrid {

bool begin(int channel, TwoWire& bus, uint8_t addr, uint32_t freq_hz) {
  if (!validChannel(channel)) return false;

  bus.begin();
  bus.setClock(freq_hz);

  if (!sensor[channel].begin(addr, bus)) {
    initialized[channel] = false;
    return false;
  }

  sensor[channel].setResolution(RESOLUTION);
  sensor[channel].setRangingFrequency(RATE_HZ);
  sensor[channel].startRanging();

  initialized[channel] = true;
  return true;
}

bool isInitialized(int channel) {
  return validChannel(channel) && initialized[channel];
}

bool readFrame(int channel, uint16_t outGrid[8][8]) {
  if (!validChannel(channel) || !initialized[channel]) return false;

  if (!sensor[channel].isDataReady()) return false;

  VL53L5CX_ResultsData results;
  if (!sensor[channel].getRangingData(&results)) {
    Serial.print("ERROR: Failed to get ranging data from CH");
    Serial.println(channel);
    return false;
  }

  // The sensor is mounted rotated 180 degrees relative to the LED matrix,
  // so we flip both axes once at the source. Every downstream consumer
  // sees correctly-oriented data without having to flip again.
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int rawIndex = (7 - row) * 8 + (7 - col);
      outGrid[row][col] = results.distance_mm[rawIndex];
    }
  }
  return true;
}

void calculateHandMetrics(uint16_t grid[8][8],
                          float& avgDistance,
                          int& centroidX, int& centroidY,
                          int& activeZones) {
  long sumX = 0, sumY = 0, sumDist = 0;
  int count = 0;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint16_t dist = grid[row][col];
      if (dist < HAND_THRESHOLD_MM && dist > HAND_MIN_VALID_MM) {
        sumX    += col;
        sumY    += row;
        sumDist += dist;
        count++;
      }
    }
  }
  if (count > 0) {
    centroidX   = sumX / count;
    centroidY   = sumY / count;
    avgDistance = (float)sumDist / count;
    activeZones = count;
  } else {
    centroidX   = 4;
    centroidY   = 4;
    avgDistance = 2000.0f;
    activeZones = 0;
  }
}

} // namespace ToFGrid
