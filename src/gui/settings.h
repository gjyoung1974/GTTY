// Persistent application settings.
//
// Settings live in named configuration profiles, like WSJT-X "Configurations":
// each profile is a complete GKeyFile at ~/.config/gtty/profiles/<name>.conf
// and ~/.config/gtty/gtty.conf records which profile is active. A legacy
// single-file gtty.conf is migrated into the "Default" profile on first load.
#pragma once
#include <string>
#include <vector>

#include "core/macros.h"
#include "core/modem/rtty_demodulator.h"
#include "core/rig/rig_control.h"
#include "core/rtty_params.h"
#include "core/transmitter.h"

namespace gtty {

struct TonePreset {
  const char* name;
  double mark, space;   // 0/0 = custom
};
const std::vector<TonePreset>& tonePresets();
// Index of the preset matching these tones, or 0 (Custom).
int tonePresetIndex(double mark, double space);

struct BandPreset {
  std::string label;  // "20m"
  double hz;          // typical RTTY dial frequency
};
std::vector<BandPreset> defaultBands();

// Named configuration profiles. "Default" always exists and cannot be renamed
// or deleted. Names are file names: no slashes, not starting with a dot.
struct Profiles {
  static constexpr const char* kDefault = "Default";
  static std::string dir();                              // ~/.config/gtty/profiles (created)
  static std::string path(const std::string& name);      // file for a profile
  static bool validName(const std::string& name);
  static bool exists(const std::string& name);           // Default is always reported as existing
  static std::vector<std::string> list();                // Default first, then case-insensitively sorted
  static std::string current();                          // active profile recorded in gtty.conf
  static void setCurrent(const std::string& name);
  // File operations. All return false and fill err on failure.
  static bool clone(const std::string& from, const std::string& to, std::string& err);
  static bool rename(const std::string& from, const std::string& to, std::string& err);  // updates current()
  static bool remove(const std::string& name, std::string& err);                         // current() falls back to Default
  // Move a pre-profile gtty.conf into profiles/Default.conf. Safe to call repeatedly.
  static void migrateLegacy();
};

// Per-engine decoder settings, indexed like the DecoderBank slots
// (0 A, 1 B: matched filters; 2 Correlator; 3 codec2).
struct DecoderSlotSettings {
  int mode = 0;              // DecoderMode: 0 Normal, 1 Selective, 2 Flutter, 3 Custom (matched filters only)
  double filterBits = 0.6;   // Custom: matched filter length in bit periods
  int window = 1;            // Custom: 0 boxcar, 1 Hann
  double atcTau = 1.0;       // Custom: ATC time constant (s)
  bool enabled = true;       // engine runs
  bool show = true;          // engine has an RX pane
  bool votes = true;         // engine takes part in the merged best guess
};
std::vector<DecoderSlotSettings> defaultDecoderSettings();

struct Settings {
  std::string profile = Profiles::kDefault;  // profile these settings were loaded from and save to

  // audio
  std::string inputDevice;    // PortAudio device name ("" = default)
  std::string outputDevice;
  int inputChannel = -1;      // -1 mix, 0 left, 1 right
  // rig
  RigConfig rig;
  double manualDialHz = 14080000;  // used for band/GridTracker/logging when no rig is connected
  int rigFilterHz = 0;             // receiver filter width to select when the rig connects (0 = leave)
  std::vector<BandPreset> bands = defaultBands();
  // rtty / demod
  RttyParams rtty;
  bool afc = true;
  bool atc = true;
  double afcRangeHz = 60;
  float squelch = 0.0f;
  bool netTx = true;          // TX follows AFC-tracked RX tones
  std::vector<DecoderSlotSettings> decoders = defaultDecoderSettings();
  bool showVotePane = true;   // merged best-guess pane
  bool skimmerEnabled = false;
  double skimmerStepHz = 50;  // channel spacing across the waterfall range
  // tx
  TxConfig tx;
  bool autoRx = false;          // return to receive when the TX buffer has been empty for autoRxDelaySec
  double autoRxDelaySec = 2.0;
  bool clearTxOnRx = true;      // clear the TX pane when returning to receive
  bool txWordMode = false;      // type-ahead sends only completed words
  int autoCqIntervalSec = 5;    // pause between Auto CQ repeats
  int autoCqMacro = 1;          // 1-based macro button used by Auto CQ
  // rx display / logging
  bool rxTimestamps = false;
  bool rxLogToFile = false;
  std::string rxLogDir;
  // fldigi-compatible XML-RPC (CQRLOG, TLF, ...)
  bool rpcEnabled = false;
  std::string rpcBind = "127.0.0.1";
  int rpcPort = 7362;
  // station
  std::string myCall, myName, myQth, myGrid;
  // macros
  std::vector<MacroDef> macros = defaultMacros();
  // logging
  std::string scpPath;
  std::string logDbPath;
  // GridTracker2 / WSJT-X UDP protocol
  bool gtEnabled = false;
  std::string gtHost = "127.0.0.1";
  int gtPort = 2237;
  bool gtSendDecodes = true;    // report decoded callsigns as Decode messages
  bool gtSendLogged = true;     // report logged QSOs
  bool gtAcceptReply = true;    // GridTracker click -> fill the Call field
  // DX cluster / band map
  bool clusterEnabled = false;
  std::string clusterHost;
  int clusterPort = 7300;
  std::string clusterLogin;       // blank = station callsign
  std::string clusterCommands;    // sent after login; ';' or newline separated
  bool clusterRttyOnly = true;
  int clusterMaxAgeMin = 30;
  bool bandMapVisible = true;
  bool bandMapThisBand = false;
  // callbook
  int callbookProvider = 0;       // 0 none, 1 QRZ.com XML (subscription), 2 HamQTH
  std::string callbookUser, callbookPass;
  bool callbookAutoFill = true;   // fill Name / QTH / grid from the lookup
  // display
  double waterfallLoHz = 300, waterfallHiHz = 3000;
  // 0 follow the rig's sideband (high audio on the left in RTTY-L / LSB, like the rig's
  // RF scope), 1 low audio on the left, 2 low audio on the right
  int waterfallDirection = 0;
  int fontSize = 12;
  int theme = 0;   // 0 follow system, 1 light, 2 dark

  static std::string configPath();   // ~/.config/gtty/gtty.conf: the profile index
  static std::string dataDir();
  void setDefaultPaths();            // fill empty logbook / SCP / session paths with their defaults
  void load();                       // load the active profile (Profiles::current())
  bool loadProfile(const std::string& name);  // load a named profile; false (defaults kept) if it has no file
  bool loadFrom(const std::string& path);
  void save() const;                 // save to this profile's file and make it the active profile
  bool saveTo(const std::string& path) const;
};

}  // namespace gtty
