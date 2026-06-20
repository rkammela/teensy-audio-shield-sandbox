/*
 * ToFGrid - thin wrapper around SparkFun's VL53L5CX driver for projects
 * that want a multi-channel (e.g. left/right hand) 8x8 time-of-flight
 * grid. The library owns the sensor instances and ranging configuration;
 * callers supply the destination buffer for each frame.
 *
 *   ToFGrid::begin(0, Wire);              // left  sensor on Wire
 *   ToFGrid::begin(1, Wire1);             // right sensor on Wire1
 *   if (ToFGrid::readFrame(0, leftGrid))  { ... new frame ... }
 *
 * The frame written into outGrid is already rotated 180 degrees relative
 * to the raw sensor output, so grid[0][0] is the top-left zone from the
 * player's point of view.
 */

#ifndef AURA_TOF_GRID_H
#define AURA_TOF_GRID_H

#include <Arduino.h>
#include <Wire.h>

namespace ToFGrid {

  // Default I2C address and bus frequency used by SparkFun's VL53L5CX
  // breakout board (matches the chip's power-on defaults).
  constexpr uint8_t  DEFAULT_ADDR = 0x29;
  constexpr uint32_t DEFAULT_FREQ = 400000;
  constexpr int      MAX_CHANNELS = 2;

  // Initialize one sensor channel. Brings the I2C bus up at the given
  // frequency, runs sensor.begin(), switches to 8x8 mode and starts
  // ranging at 10 Hz. Returns true on success, false if the sensor did
  // not respond (caller can keep going without that hand).
  bool begin(int channel, TwoWire& bus,
             uint8_t addr     = DEFAULT_ADDR,
             uint32_t freq_hz = DEFAULT_FREQ);

  // True once begin() has succeeded for that channel.
  bool isInitialized(int channel);

  // Poll one channel. If the sensor has new data, the 8x8 frame is
  // copied (and rotated 180 degrees) into outGrid and the function
  // returns true. Otherwise outGrid is left untouched and it returns
  // false. Safe to call every loop().
  bool readFrame(int channel, uint16_t outGrid[8][8]);

  // Compute simple "hand" statistics from an 8x8 grid: number of cells
  // closer than the hand threshold, the average distance of those cells,
  // and the centroid (in matrix coordinates, 0..7). When no cells are
  // active the centroid defaults to the middle and avgDistance to 2000.
  void calculateHandMetrics(uint16_t grid[8][8],
                            float& avgDistance,
                            int& centroidX, int& centroidY,
                            int& activeZones);

} // namespace ToFGrid

#endif // AURA_TOF_GRID_H
