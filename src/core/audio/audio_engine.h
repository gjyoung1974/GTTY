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
  // What the device connects to, where the system says (Linux ALSA hardware devices only),
  // e.g. "Mic", "Line", "Speaker", "S/PDIF"; empty when unknown.
  std::string inputKind;
  std::string outputKind;
  std::string hwId;  // the ALSA hardware address, e.g. "hw:1,0"; empty for other devices
  std::string card;  // a USB sound card's full name when it says more than `name`, e.g. "C-Media Electronics Inc. USB Audio Device"
  // What the sound server (PipeWire or PulseAudio) says about this hardware device, for any
  // card on any bus; empty when there is no server or it doesn't know the device.
  std::string serverInputName;   // the names pavucontrol shows, including ones set in WirePlumber
  std::string serverOutputName;
  std::string serverInputPort;   // the port in use, e.g. "Microphone", "Speakers", "Line In"
  std::string serverOutputPort;
  std::string serverVendor;      // who made the card, e.g. "C-Media Electronics, Inc."
  // Set on the sound server's own outputs/inputs (name = kServerPrefix + serverNode), which are
  // opened through PortAudio's "pipewire" or "pulse" device (index) pointed at serverNode.
  std::string serverNode;        // the server's id for it, e.g. "alsa_output.pci-0000_00_1f.3.analog-stereo"
  std::string serverCardName;    // ALSA's name for the card behind it, if any
  int serverPcm = -1;            // and the ALSA PCM device number
  bool serverUnplugged = false;  // the server says nothing is plugged into its port (a jack)

  // The name to show for this device as an input or an output. With a sound server:
  // "<name> — <port> (<vendor>, hw:C,D)", each part left out when unknown or already said.
  // Without one: a USB card's own description and kind (see inputKind), else PortAudio's name.
  std::string label(bool input) const;
};

// Saved names of the sound server's devices: this prefix and the server's id for the device.
inline constexpr const char* kServerPrefix = "server:";

// The device a saved name refers to, or nullptr. An exact name wins; failing that, for an ALSA
// hardware device ("Card: PCM (hw:C,D)"), the same card under another card number (ALSA numbers
// cards in the order the kernel finds them, which can change on a reboot), and then the sound
// server's device for that card and PCM (PortAudio can't list a hw: device the server has open).
const AudioDeviceInfo* findAudioDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& name, bool input);

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
  // serverNode: for the sound server's devices (AudioDeviceInfo::serverNode), with device
  // their PortAudio "pipewire"/"pulse" index.
  bool openInput(int device, double sampleRate, int inChannel, std::string& err, const std::string& serverNode = {});
  bool openOutput(int device, double sampleRate, std::string& err, const std::string& serverNode = {});
  void closeInput();
  void closeOutput();
  bool inputOpen() const { return in_ != nullptr; }
  bool outputOpen() const { return out_ != nullptr; }
  double inputRate() const { return inRate_; }
  double outputRate() const { return outRate_; }

  SpscRing<float>& rxRing() { return rx_; }
  float inputPeak() const { return inPeak_.load(); }
  // Times since the stream was opened that samples were lost: the host reported an
  // overflow, or the receiver did not empty the ring in time. A steadily rising count
  // means the decoders are being fed audio with holes in it.
  size_t inputOverruns() const { return overruns_.load(); }
  size_t outputUnderruns() const { return underruns_.load(); }

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
  std::atomic<size_t> underruns_{0};
  std::atomic<TxSource*> txSource_{nullptr};
  std::vector<float> scratch_;
};

}  // namespace gtty
