// Thread-safe Hamlib wrapper with a background poller for frequency/mode.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct s_rig;

namespace gtty {

struct RigModelInfo {
  int model;
  std::string mfg, name, version, status;
};

struct RigConfig {
  bool enabled = false;
  int model = 1;                 // Hamlib model number (1 = dummy, 2 = NET rigctl)
  std::string device;            // /dev/ttyUSB0 or host:port for model 2
  int baud = 0;                  // 0 = backend default
  std::string pttType = "RIG";   // RIG, DTR, RTS, NONE
  std::string pttDevice;         // serial device for DTR/RTS PTT (empty = rig device)
  int pollMs = 250;
};

struct RigState {
  bool connected = false;
  double freqHz = 0;
  std::string mode;
  int widthHz = 0;
  bool ptt = false;
  std::string error;
};

class RigControl {
 public:
  RigControl();
  ~RigControl();
  RigControl(const RigControl&) = delete;
  RigControl& operator=(const RigControl&) = delete;

  static std::vector<RigModelInfo> listModels();

  bool open(const RigConfig& cfg, std::string& err);
  void close();
  bool isOpen() const { return rig_ != nullptr; }
  const RigConfig& config() const { return cfg_; }

  RigState state() const;
  bool setFreq(double hz);
  // mode: Hamlib mode name (RTTY, RTTYR, USB, LSB, PKTUSB ...). width -1 = no change.
  bool setMode(const std::string& mode, int widthHz = -1);
  bool setPtt(bool on);
  // Receiver filter: passband width in Hz for the current mode (rig_set_mode).
  bool setWidth(int hz);
  // IF shift in Hz (RIG_LEVEL_IF); hasIfShift() tells whether the backend supports it.
  bool setIfShift(int hz);
  bool hasIfShift() const;
  // Sends text through the rig's own keyer (Hamlib rig_send_morse). In RTTY/FSK
  // mode on Elecraft K3/K4/KX3 and Kenwood TS-890 this keys FSK directly.
  bool sendText(const std::string& text);
  bool stopText();

  std::function<void(const RigState&)> onState;  // called from the poller thread

 private:
  void pollLoop();
  void setError(const std::string& e);

  s_rig* rig_ = nullptr;
  RigConfig cfg_;
  mutable std::mutex mu_;      // guards rig_ calls and state_
  RigState state_;
  std::thread poller_;
  std::atomic<bool> run_{false};
};

std::string bandForFrequency(double hz);

}  // namespace gtty
