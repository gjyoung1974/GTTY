// PortAudio-based audio I/O. Input and output are separate streams so they
// may come from different devices / host APIs. The RX callback pushes mono
// float samples into a lock-free ring; the TX callback pulls from a TxSource.
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "core/ring_buffer.h"

#include <portaudio.h>

namespace gtty {

struct AudioDeviceInfo {
  int index;
  std::string name;
  std::string hostApi;
  int maxInputs;
  int maxOutputs;
  double defaultSampleRate;
};

class TxSource {
 public:
  virtual ~TxSource() = default;
  // Fill n mono samples; must be real-time safe.
  virtual void generate(float* out, size_t n) = 0;
};

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  static std::vector<AudioDeviceInfo> listDevices();
  static int defaultInputDevice();
  static int defaultOutputDevice();

  // inChannel: 0 = left, 1 = right, -1 = mix. sampleRate 0 = device default.
  bool openInput(int device, double sampleRate, int inChannel, std::string& err);
  bool openOutput(int device, double sampleRate, std::string& err);
  void closeInput();
  void closeOutput();
  bool inputOpen() const { return in_ != nullptr; }
  bool outputOpen() const { return out_ != nullptr; }
  double inputRate() const { return inRate_; }
  double outputRate() const { return outRate_; }

  SpscRing<float>& rxRing() { return rx_; }
  float inputPeak() const { return inPeak_.load(); }
  size_t inputOverruns() const { return overruns_.load(); }

  void setTxSource(TxSource* s) { txSource_.store(s); }
  TxSource* txSource() const { return txSource_.load(); }

 private:
  static int inCallback(const void* in, void* out, unsigned long frames,
                        const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags flags,
                        void* user);
  static int outCallback(const void* in, void* out, unsigned long frames,
                         const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags flags,
                         void* user);

  PaStream* in_ = nullptr;
  PaStream* out_ = nullptr;
  double inRate_ = 0, outRate_ = 0;
  int inChannels_ = 1;
  int inChannel_ = -1;
  SpscRing<float> rx_{1 << 17};
  std::atomic<float> inPeak_{0};
  std::atomic<size_t> overruns_{0};
  std::atomic<TxSource*> txSource_{nullptr};
  std::vector<float> scratch_;
};

}  // namespace gtty
