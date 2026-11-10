#include "core/audio/wav_file.h"

#include <sndfile.h>

#include <vector>

namespace gtty {

bool readAudioFile(const std::string& path, std::vector<float>& samples, double& sampleRate,
                   std::string& err) {
  SF_INFO info{};
  SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
  if (!f) { err = sf_strerror(nullptr); return false; }
  sampleRate = info.samplerate;
  std::vector<float> buf(static_cast<size_t>(info.frames) * info.channels);
  sf_count_t got = sf_readf_float(f, buf.data(), info.frames);
  sf_close(f);
  samples.resize(static_cast<size_t>(got));
  for (sf_count_t i = 0; i < got; ++i) {
    float v = 0;
    for (int c = 0; c < info.channels; ++c) v += buf[i * info.channels + c];
    samples[i] = v / info.channels;
  }
  return true;
}

bool readIqFile(const std::string& path, std::vector<float>& iOut, std::vector<float>& qOut,
                double& sampleRate, std::string& err) {
  SF_INFO info{};
  SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
  if (!f) { err = sf_strerror(nullptr); return false; }
  if (info.channels < 2) { sf_close(f); err = "I/Q input needs a two-channel file"; return false; }
  sampleRate = info.samplerate;
  std::vector<float> buf(static_cast<size_t>(info.frames) * info.channels);
  sf_count_t got = sf_readf_float(f, buf.data(), info.frames);
  sf_close(f);
  iOut.resize(static_cast<size_t>(got));
  qOut.resize(static_cast<size_t>(got));
  for (sf_count_t n = 0; n < got; ++n) {
    iOut[n] = buf[n * info.channels];
    qOut[n] = buf[n * info.channels + 1];
  }
  return true;
}

bool writeWavFile(const std::string& path, const std::vector<float>& samples, double sampleRate,
                  std::string& err) {
  SF_INFO info{};
  info.samplerate = static_cast<int>(sampleRate);
  info.channels = 1;
  info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
  if (!f) { err = sf_strerror(nullptr); return false; }
  sf_writef_float(f, samples.data(), static_cast<sf_count_t>(samples.size()));
  sf_close(f);
  return true;
}

}  // namespace gtty
