#pragma once
#include <string>
#include <vector>

namespace gtty {

// Reads any libsndfile-supported file as mono float (channels averaged).
bool readAudioFile(const std::string& path, std::vector<float>& samples, double& sampleRate,
                   std::string& err);
// Reads a two-channel file as I/Q (left = I, right = Q).
bool readIqFile(const std::string& path, std::vector<float>& i, std::vector<float>& q, double& sampleRate,
                std::string& err);
// Writes 16-bit PCM mono WAV.
bool writeWavFile(const std::string& path, const std::vector<float>& samples, double sampleRate,
                  std::string& err);

}  // namespace gtty
