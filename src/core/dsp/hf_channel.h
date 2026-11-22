// HF channel simulator after Watterson: two equal-power paths, each fading as an
// independent complex Gaussian process with a Gaussian Doppler spectrum (Rayleigh
// fading), the second delayed against the first. This is the model behind CCIR 520
// and ITU-R F.1487, and what the hardware and software channel simulators used to
// compare HF modems implement. With white noise added afterwards it gives a repeatable
// stand-in for a fading band, so a decoder change can be measured instead of judged
// by ear on whatever the band was doing that day.
#pragma once
#include <string>
#include <vector>

namespace gtty {

struct HfChannelProfile {
  const char* key;       // for the command line
  const char* name;
  double delayMs;        // differential delay between the two paths
  double dopplerHz;      // Doppler spread (2 sigma of the Gaussian spectrum) of each path
};

// "awgn" (no fading: one steady path) and the ITU-R F.1487 conditions.
const std::vector<HfChannelProfile>& hfChannelProfiles();
const HfChannelProfile* findHfChannelProfile(const std::string& key);

// Passes the real signal x (sampled at fs) through the channel. The mean power is
// kept, so a signal-to-noise ratio set afterwards is the mean over the fades. The
// output is as long as the input and aligned with it (the first path is not delayed).
std::vector<float> applyHfChannel(const std::vector<float>& x, double fs, const HfChannelProfile& p,
                                  unsigned seed);

// Adds white Gaussian noise for a signal-to-noise ratio in a 3 kHz bandwidth, the
// convention for HF modem figures. signalPower is the mean power of the signal while
// it is on (0.5 for a tone of unit amplitude).
void addWhiteNoise(std::vector<float>& x, double fs, double snrDb3k, unsigned seed, double signalPower = 0.5);

}  // namespace gtty
