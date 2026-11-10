// Second-order IIR sections (RBJ cookbook). Used for manual notch filters.
#pragma once
#include <cmath>

namespace gtty {

class Biquad {
 public:
  Biquad() = default;
  // Notch at f0 with -3 dB bandwidth bw (Hz).
  static Biquad notch(double f0, double bw, double fs) {
    Biquad q;
    double w0 = 2.0 * M_PI * f0 / fs;
    double Q = f0 / bw;
    double alpha = std::sin(w0) / (2.0 * Q);
    double a0 = 1.0 + alpha;
    q.b0_ = 1.0 / a0;
    q.b1_ = -2.0 * std::cos(w0) / a0;
    q.b2_ = 1.0 / a0;
    q.a1_ = -2.0 * std::cos(w0) / a0;
    q.a2_ = (1.0 - alpha) / a0;
    return q;
  }
  inline float process(float x) {
    // transposed direct form II
    double y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return static_cast<float>(y);
  }
  void reset() { z1_ = z2_ = 0; }

 private:
  double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  double z1_ = 0, z2_ = 0;
};

}  // namespace gtty
