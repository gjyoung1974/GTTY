#pragma once
#include <cmath>

#include "core/baudot.h"

namespace gtty {

// RTTY signaling parameters shared by modulator, demodulator and FSK keyer.
struct RttyParams {
  double baud = 45.45;         // 45.45, 50, 75, 100 ...
  double shift = 170.0;        // Hz
  double markFreq = 2125.0;    // audio mark tone (Hz)
  // Default audio/AFSK tones: mark 2125 Hz, space 2295 Hz (170 Hz shift, space
  // above mark). Set reverse=true for space below mark (2125/1955).
  bool reverse = false;        // false: space = mark + shift; true: space = mark - shift

  // Set both tones directly; derives shift and polarity.
  void setTones(double mark, double space) {
    markFreq = mark;
    shift = std::fabs(space - mark);
    reverse = space < mark;
  }
  double stopBits = 1.5;       // 1.5 or 2 (TX); RX tolerates either
  BaudotVariant variant = BaudotVariant::ITA2;
  bool unshiftOnSpace = true;

  double spaceFreq() const { return reverse ? markFreq - shift : markFreq + shift; }
  double centerFreq() const { return (markFreq + spaceFreq()) / 2.0; }
  double bitPeriod() const { return 1.0 / baud; }
  // Start bit + 5 data + stop bits.
  double charPeriod() const { return (1.0 + 5.0 + stopBits) / baud; }
};

}  // namespace gtty
