#include "settings.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gtty {

namespace {
std::string str(GKeyFile* kf, const char* g, const char* k, const std::string& def) {
  g_autofree gchar* v = g_key_file_get_string(kf, g, k, nullptr);
  return v ? std::string(v) : def;
}
int integer(GKeyFile* kf, const char* g, const char* k, int def) {
  GError* e = nullptr;
  int v = g_key_file_get_integer(kf, g, k, &e);
  if (e) { g_error_free(e); return def; }
  return v;
}
double dbl(GKeyFile* kf, const char* g, const char* k, double def) {
  GError* e = nullptr;
  double v = g_key_file_get_double(kf, g, k, &e);
  if (e) { g_error_free(e); return def; }
  return v;
}
bool boolean(GKeyFile* kf, const char* g, const char* k, bool def) {
  GError* e = nullptr;
  gboolean v = g_key_file_get_boolean(kf, g, k, &e);
  if (e) { g_error_free(e); return def; }
  return v;
}
}  // namespace

const std::vector<TonePreset>& tonePresets() {
  static const std::vector<TonePreset> v = {
      {"Custom", 0, 0},
      {"2125 / 2295 (standard)", 2125, 2295},
      {"2125 / 1955 (reversed)", 2125, 1955},
      {"1275 / 1445 (low, standard)", 1275, 1445},
      {"1275 / 1105 (low, reversed)", 1275, 1105},
      {"2125 / 2975 (850 Hz shift)", 2125, 2975},
  };
  return v;
}

int tonePresetIndex(double mark, double space) {
  const auto& v = tonePresets();
  for (size_t i = 1; i < v.size(); ++i)
    if (v[i].mark == mark && v[i].space == space) return static_cast<int>(i);
  return 0;
}

std::vector<BandPreset> defaultBands() {
  // Centers of the customary RTTY segments (dial frequency, LSB/RTTY mode).
  return {{"160m", 1838000},  {"80m", 3580000},   {"40m", 7040000},   {"30m", 10142000},
          {"20m", 14080000},  {"17m", 18100000},  {"15m", 21080000},  {"12m", 24920000},
          {"10m", 28080000},  {"6m", 50300000}};
}

std::vector<DecoderSlotSettings> defaultDecoderSettings() {
  std::vector<DecoderSlotSettings> v(4);
  v[0].mode = 0;                                   // A: Normal
  v[1].mode = 2; v[1].filterBits = 0.5; v[1].window = 1; v[1].atcTau = 0.3;    // B: Flutter
  v[2].mode = 3; v[2].show = false;                // Correlator (no presets): runs and votes, pane off by default
  v[3].mode = 3; v[3].enabled = false; v[3].show = false; v[3].votes = false;  // codec2: opt in
  return v;
}

std::string Settings::configPath() {
  g_autofree gchar* dir = g_build_filename(g_get_user_config_dir(), "gtty", nullptr);
  g_mkdir_with_parents(dir, 0755);
  g_autofree gchar* p = g_build_filename(dir, "gtty.conf", nullptr);
  return p;
}

std::string Settings::dataDir() {
  g_autofree gchar* dir = g_build_filename(g_get_user_data_dir(), "gtty", nullptr);
  g_mkdir_with_parents(dir, 0755);
  return dir;
}

// ------------------------------------------------------------------ profiles

namespace {
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool copyFile(const std::string& from, const std::string& to, std::string& err) {
  gchar* data = nullptr;
  gsize len = 0;
  GError* e = nullptr;
  if (!g_file_get_contents(from.c_str(), &data, &len, &e)) {
    err = e ? e->message : "read failed";
    if (e) g_error_free(e);
    return false;
  }
  bool ok = g_file_set_contents(to.c_str(), data, static_cast<gssize>(len), &e);
  g_free(data);
  if (!ok) {
    err = e ? e->message : "write failed";
    if (e) g_error_free(e);
  }
  return ok;
}
}  // namespace

std::string Profiles::dir() {
  g_autofree gchar* d = g_build_filename(g_get_user_config_dir(), "gtty", "profiles", nullptr);
  g_mkdir_with_parents(d, 0755);
  return d;
}

std::string Profiles::path(const std::string& name) {
  g_autofree gchar* p = g_build_filename(dir().c_str(), (name + ".conf").c_str(), nullptr);
  return p;
}

bool Profiles::validName(const std::string& name) {
  if (name.empty() || name.size() > 64 || name.front() == '.') return false;
  if (name.front() == ' ' || name.back() == ' ') return false;
  for (unsigned char c : name)
    if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false;
  return true;
}

bool Profiles::exists(const std::string& name) {
  if (name == kDefault) return true;
  return validName(name) && g_file_test(path(name).c_str(), G_FILE_TEST_IS_REGULAR);
}

std::vector<std::string> Profiles::list() {
  std::vector<std::string> names;
  if (GDir* d = g_dir_open(dir().c_str(), 0, nullptr)) {
    while (const char* f = g_dir_read_name(d)) {
      std::string s = f;
      if (s.size() <= 5 || s.compare(s.size() - 5, 5, ".conf") != 0) continue;
      s.resize(s.size() - 5);
      if (s != kDefault && validName(s)) names.push_back(s);
    }
    g_dir_close(d);
  }
  std::sort(names.begin(), names.end(),
            [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
  names.insert(names.begin(), kDefault);
  return names;
}

std::string Profiles::current() {
  GKeyFile* kf = g_key_file_new();
  std::string name = kDefault;
  if (g_key_file_load_from_file(kf, Settings::configPath().c_str(), G_KEY_FILE_NONE, nullptr)) {
    g_autofree gchar* v = g_key_file_get_string(kf, "profiles", "current", nullptr);
    if (v && exists(v)) name = v;
  }
  g_key_file_free(kf);
  return name;
}

void Profiles::setCurrent(const std::string& name) {
  GKeyFile* kf = g_key_file_new();
  g_key_file_set_string(kf, "profiles", "current", name.c_str());
  GError* err = nullptr;
  if (!g_key_file_save_to_file(kf, Settings::configPath().c_str(), &err)) {
    std::fprintf(stderr, "profiles: %s\n", err ? err->message : "save failed");
    if (err) g_error_free(err);
  }
  g_key_file_free(kf);
}

bool Profiles::clone(const std::string& from, const std::string& to, std::string& err) {
  if (!validName(to)) { err = "Invalid configuration name"; return false; }
  if (exists(to)) { err = "A configuration named \"" + to + "\" already exists"; return false; }
  if (!exists(from)) { err = "No configuration named \"" + from + "\""; return false; }
  if (!g_file_test(path(from).c_str(), G_FILE_TEST_IS_REGULAR)) {
    // Default without a file yet: clone the built-in defaults.
    Settings d;
    d.setDefaultPaths();
    if (!d.saveTo(path(to))) { err = "Could not write " + path(to); return false; }
    return true;
  }
  return copyFile(path(from), path(to), err);
}

bool Profiles::rename(const std::string& from, const std::string& to, std::string& err) {
  if (from == kDefault) { err = "The Default configuration cannot be renamed"; return false; }
  if (!validName(to)) { err = "Invalid configuration name"; return false; }
  if (!exists(from)) { err = "No configuration named \"" + from + "\""; return false; }
  if (exists(to)) { err = "A configuration named \"" + to + "\" already exists"; return false; }
  bool wasCurrent = current() == from;
  if (::g_rename(path(from).c_str(), path(to).c_str()) != 0) { err = "Could not rename " + path(from); return false; }
  if (wasCurrent) setCurrent(to);
  return true;
}

bool Profiles::remove(const std::string& name, std::string& err) {
  if (name == kDefault) { err = "The Default configuration cannot be deleted"; return false; }
  if (!exists(name)) { err = "No configuration named \"" + name + "\""; return false; }
  bool wasCurrent = current() == name;
  if (g_unlink(path(name).c_str()) != 0) { err = "Could not delete " + path(name); return false; }
  if (wasCurrent) setCurrent(kDefault);
  return true;
}

void Profiles::migrateLegacy() {
  // Before profiles existed gtty.conf held the settings themselves. If it still
  // does and no Default profile has been written, it becomes the Default profile.
  std::string legacy = Settings::configPath();
  if (g_file_test(path(kDefault).c_str(), G_FILE_TEST_IS_REGULAR)) return;
  GKeyFile* kf = g_key_file_new();
  bool isLegacy = false;
  if (g_key_file_load_from_file(kf, legacy.c_str(), G_KEY_FILE_NONE, nullptr))
    isLegacy = !g_key_file_has_group(kf, "profiles") && g_key_file_has_group(kf, "rtty");
  g_key_file_free(kf);
  if (!isLegacy) return;
  std::string err;
  if (copyFile(legacy, path(kDefault), err)) setCurrent(kDefault);
  else std::fprintf(stderr, "profiles: migration failed: %s\n", err.c_str());
}

// ------------------------------------------------------------------ settings

void Settings::setDefaultPaths() {
  if (logDbPath.empty()) logDbPath = dataDir() + "/logbook.sqlite";
  if (scpPath.empty()) scpPath = dataDir() + "/MASTER.SCP";
  if (rxLogDir.empty()) rxLogDir = dataDir() + "/sessions";
}

void Settings::load() {
  Profiles::migrateLegacy();
  loadProfile(Profiles::current());
}

bool Settings::loadProfile(const std::string& name) {
  profile = name;
  return loadFrom(Profiles::path(name));
}

bool Settings::loadFrom(const std::string& path) {
  setDefaultPaths();
  GKeyFile* kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
    g_key_file_free(kf);
    return false;
  }
  inputDevice = str(kf, "audio", "input", inputDevice);
  outputDevice = str(kf, "audio", "output", outputDevice);
  inputChannel = integer(kf, "audio", "channel", inputChannel);

  rig.enabled = boolean(kf, "rig", "enabled", rig.enabled);
  rig.model = integer(kf, "rig", "model", rig.model);
  rig.device = str(kf, "rig", "device", rig.device);
  rig.baud = integer(kf, "rig", "baud", rig.baud);
  rig.pttType = str(kf, "rig", "ptt_type", rig.pttType);
  rig.pttDevice = str(kf, "rig", "ptt_device", rig.pttDevice);
  rig.pollMs = integer(kf, "rig", "poll_ms", rig.pollMs);
  manualDialHz = dbl(kf, "rig", "manual_dial_hz", manualDialHz);
  rigFilterHz = integer(kf, "rig", "filter_hz", rigFilterHz);
  for (auto& b : bands) {
    b.hz = dbl(kf, "bands", b.label.c_str(), b.hz);
    b.lastHz = dbl(kf, "band_memory", b.label.c_str(), 0);
  }

  rtty.baud = dbl(kf, "rtty", "baud", rtty.baud);
  rtty.shift = dbl(kf, "rtty", "shift", rtty.shift);
  rtty.markFreq = dbl(kf, "rtty", "mark", rtty.markFreq);
  rtty.reverse = boolean(kf, "rtty", "reverse", rtty.reverse);
  rtty.stopBits = dbl(kf, "rtty", "stop_bits", rtty.stopBits);
  rtty.variant = str(kf, "rtty", "variant", "ITA2") == "USTTY" ? BaudotVariant::USTTY : BaudotVariant::ITA2;
  rtty.unshiftOnSpace = boolean(kf, "rtty", "uos", rtty.unshiftOnSpace);
  afc = boolean(kf, "rtty", "afc", afc);
  atc = boolean(kf, "rtty", "atc", atc);
  afcRangeHz = dbl(kf, "rtty", "afc_range", afcRangeHz);
  squelch = static_cast<float>(dbl(kf, "rtty", "squelch", squelch));
  netTx = boolean(kf, "rtty", "net_tx", netTx);
  for (size_t i = 0; i < decoders.size(); ++i) {
    std::string k = std::to_string(i);
    auto& d = decoders[i];
    d.mode = integer(kf, "decoders", ("mode_" + k).c_str(), d.mode);
    d.filterBits = dbl(kf, "decoders", ("filter_bits_" + k).c_str(), d.filterBits);
    d.window = integer(kf, "decoders", ("window_" + k).c_str(), d.window);
    d.atcTau = dbl(kf, "decoders", ("atc_tau_" + k).c_str(), d.atcTau);
    d.enabled = boolean(kf, "decoders", ("enabled_" + k).c_str(), d.enabled);
    d.show = boolean(kf, "decoders", ("show_" + k).c_str(), d.show);
    d.votes = boolean(kf, "decoders", ("votes_" + k).c_str(), d.votes);
  }
  showVotePane = boolean(kf, "decoders", "show_vote", showVotePane);
  skimmerEnabled = boolean(kf, "decoders", "skimmer", skimmerEnabled);
  skimmerStepHz = dbl(kf, "decoders", "skimmer_step", skimmerStepHz);

  tx.method = static_cast<TxConfig::Method>(integer(kf, "tx", "method", static_cast<int>(tx.method)));
  tx.fskDevice = str(kf, "tx", "fsk_device", tx.fskDevice);
  tx.fskInvert = boolean(kf, "tx", "fsk_invert", tx.fskInvert);
  tx.diddle = boolean(kf, "tx", "diddle", tx.diddle);
  tx.afskLevel = static_cast<float>(dbl(kf, "tx", "afsk_level", tx.afskLevel));
  tx.pttViaRig = boolean(kf, "tx", "ptt_via_rig", tx.pttViaRig);
  tx.txMode = str(kf, "tx", "tx_mode", tx.txMode);
  tx.ltrsPreamble = integer(kf, "tx", "preamble", tx.ltrsPreamble);
  tx.pttLeadMs = integer(kf, "tx", "ptt_lead_ms", tx.pttLeadMs);
  autoRx = boolean(kf, "tx", "auto_rx", autoRx);
  autoRxDelaySec = dbl(kf, "tx", "auto_rx_delay", autoRxDelaySec);
  clearTxOnRx = boolean(kf, "tx", "clear_tx_on_rx", clearTxOnRx);
  txWordMode = boolean(kf, "tx", "word_mode", txWordMode);
  autoCqIntervalSec = integer(kf, "tx", "auto_cq_interval", autoCqIntervalSec);
  autoCqMacro = integer(kf, "tx", "auto_cq_macro", autoCqMacro);
  tx.monitorLevel = static_cast<float>(dbl(kf, "tx", "monitor_level", tx.monitorLevel));
  tx.cwIdAtEnd = boolean(kf, "tx", "cwid_at_end", tx.cwIdAtEnd);
  tx.cwIdText = str(kf, "tx", "cwid_text", tx.cwIdText);
  tx.cwIdWpm = dbl(kf, "tx", "cwid_wpm", tx.cwIdWpm);
  tx.cwIdToneHz = dbl(kf, "tx", "cwid_tone", tx.cwIdToneHz);
  tx.tuneTimeoutSec = integer(kf, "tx", "tune_timeout", tx.tuneTimeoutSec);
  clusterEnabled = boolean(kf, "cluster", "enabled", clusterEnabled);
  clusterHost = str(kf, "cluster", "host", clusterHost);
  clusterPort = integer(kf, "cluster", "port", clusterPort);
  clusterLogin = str(kf, "cluster", "login", clusterLogin);
  clusterCommands = str(kf, "cluster", "commands", clusterCommands);
  clusterRttyOnly = boolean(kf, "cluster", "rtty_only", clusterRttyOnly);
  clusterMaxAgeMin = integer(kf, "cluster", "max_age_min", clusterMaxAgeMin);
  bandMapVisible = boolean(kf, "cluster", "bandmap_visible", bandMapVisible);
  bandMapThisBand = boolean(kf, "cluster", "bandmap_this_band", bandMapThisBand);
  modemOnly = boolean(kf, "ui", "modem_only", modemOnly);
  callbookProvider = integer(kf, "callbook", "provider", callbookProvider);
  callbookUser = str(kf, "callbook", "user", callbookUser);
  callbookPass = str(kf, "callbook", "password", callbookPass);
  callbookAutoFill = boolean(kf, "callbook", "auto_fill", callbookAutoFill);
  rxTimestamps = boolean(kf, "rx", "timestamps", rxTimestamps);
  rxLogToFile = boolean(kf, "rx", "log_to_file", rxLogToFile);
  rxLogDir = str(kf, "rx", "log_dir", rxLogDir);
  rpcEnabled = boolean(kf, "xmlrpc", "enabled", rpcEnabled);
  rpcBind = str(kf, "xmlrpc", "bind", rpcBind);
  rpcPort = integer(kf, "xmlrpc", "port", rpcPort);

  myCall = str(kf, "station", "call", myCall);
  myName = str(kf, "station", "name", myName);
  myQth = str(kf, "station", "qth", myQth);
  myGrid = str(kf, "station", "grid", myGrid);

  for (size_t i = 0; i < macros.size(); ++i) {
    std::string kl = "label_" + std::to_string(i + 1), kt = "text_" + std::to_string(i + 1);
    macros[i].label = str(kf, "macros", kl.c_str(), macros[i].label);
    macros[i].text = str(kf, "macros", kt.c_str(), macros[i].text);
  }
  scpPath = str(kf, "log", "scp_path", scpPath);
  logDbPath = str(kf, "log", "db_path", logDbPath);
  gtEnabled = boolean(kf, "gridtracker", "enabled", gtEnabled);
  gtHost = str(kf, "gridtracker", "host", gtHost);
  gtPort = integer(kf, "gridtracker", "port", gtPort);
  gtSendDecodes = boolean(kf, "gridtracker", "send_decodes", gtSendDecodes);
  gtSendLogged = boolean(kf, "gridtracker", "send_logged", gtSendLogged);
  gtAcceptReply = boolean(kf, "gridtracker", "accept_reply", gtAcceptReply);
  waterfallLoHz = dbl(kf, "display", "wf_lo", waterfallLoHz);
  waterfallHiHz = dbl(kf, "display", "wf_hi", waterfallHiHz);
  waterfallDirection = integer(kf, "display", "wf_direction", waterfallDirection);
  fontSize = integer(kf, "display", "font_size", fontSize);
  theme = integer(kf, "display", "theme", theme);
  g_key_file_free(kf);
  return true;
}

void Settings::save() const {
  if (saveTo(Profiles::path(profile))) Profiles::setCurrent(profile);
}

bool Settings::saveTo(const std::string& path) const {
  GKeyFile* kf = g_key_file_new();
  g_key_file_set_string(kf, "audio", "input", inputDevice.c_str());
  g_key_file_set_string(kf, "audio", "output", outputDevice.c_str());
  g_key_file_set_integer(kf, "audio", "channel", inputChannel);

  g_key_file_set_boolean(kf, "rig", "enabled", rig.enabled);
  g_key_file_set_integer(kf, "rig", "model", rig.model);
  g_key_file_set_string(kf, "rig", "device", rig.device.c_str());
  g_key_file_set_integer(kf, "rig", "baud", rig.baud);
  g_key_file_set_string(kf, "rig", "ptt_type", rig.pttType.c_str());
  g_key_file_set_string(kf, "rig", "ptt_device", rig.pttDevice.c_str());
  g_key_file_set_integer(kf, "rig", "poll_ms", rig.pollMs);
  g_key_file_set_double(kf, "rig", "manual_dial_hz", manualDialHz);
  g_key_file_set_integer(kf, "rig", "filter_hz", rigFilterHz);
  for (const auto& b : bands) {
    g_key_file_set_double(kf, "bands", b.label.c_str(), b.hz);
    if (b.lastHz > 0) g_key_file_set_double(kf, "band_memory", b.label.c_str(), b.lastHz);
  }

  g_key_file_set_double(kf, "rtty", "baud", rtty.baud);
  g_key_file_set_double(kf, "rtty", "shift", rtty.shift);
  g_key_file_set_double(kf, "rtty", "mark", rtty.markFreq);
  g_key_file_set_boolean(kf, "rtty", "reverse", rtty.reverse);
  g_key_file_set_double(kf, "rtty", "stop_bits", rtty.stopBits);
  g_key_file_set_string(kf, "rtty", "variant", rtty.variant == BaudotVariant::USTTY ? "USTTY" : "ITA2");
  g_key_file_set_boolean(kf, "rtty", "uos", rtty.unshiftOnSpace);
  g_key_file_set_boolean(kf, "rtty", "afc", afc);
  g_key_file_set_boolean(kf, "rtty", "atc", atc);
  g_key_file_set_double(kf, "rtty", "afc_range", afcRangeHz);
  g_key_file_set_double(kf, "rtty", "squelch", squelch);
  g_key_file_set_boolean(kf, "rtty", "net_tx", netTx);
  for (size_t i = 0; i < decoders.size(); ++i) {
    std::string k = std::to_string(i);
    const auto& d = decoders[i];
    g_key_file_set_integer(kf, "decoders", ("mode_" + k).c_str(), d.mode);
    g_key_file_set_double(kf, "decoders", ("filter_bits_" + k).c_str(), d.filterBits);
    g_key_file_set_integer(kf, "decoders", ("window_" + k).c_str(), d.window);
    g_key_file_set_double(kf, "decoders", ("atc_tau_" + k).c_str(), d.atcTau);
    g_key_file_set_boolean(kf, "decoders", ("enabled_" + k).c_str(), d.enabled);
    g_key_file_set_boolean(kf, "decoders", ("show_" + k).c_str(), d.show);
    g_key_file_set_boolean(kf, "decoders", ("votes_" + k).c_str(), d.votes);
  }
  g_key_file_set_boolean(kf, "decoders", "show_vote", showVotePane);
  g_key_file_set_boolean(kf, "decoders", "skimmer", skimmerEnabled);
  g_key_file_set_double(kf, "decoders", "skimmer_step", skimmerStepHz);

  g_key_file_set_integer(kf, "tx", "method", static_cast<int>(tx.method));
  g_key_file_set_string(kf, "tx", "fsk_device", tx.fskDevice.c_str());
  g_key_file_set_boolean(kf, "tx", "fsk_invert", tx.fskInvert);
  g_key_file_set_boolean(kf, "tx", "diddle", tx.diddle);
  g_key_file_set_double(kf, "tx", "afsk_level", tx.afskLevel);
  g_key_file_set_boolean(kf, "tx", "ptt_via_rig", tx.pttViaRig);
  g_key_file_set_string(kf, "tx", "tx_mode", tx.txMode.c_str());
  g_key_file_set_integer(kf, "tx", "preamble", tx.ltrsPreamble);
  g_key_file_set_integer(kf, "tx", "ptt_lead_ms", tx.pttLeadMs);
  g_key_file_set_boolean(kf, "tx", "auto_rx", autoRx);
  g_key_file_set_double(kf, "tx", "auto_rx_delay", autoRxDelaySec);
  g_key_file_set_boolean(kf, "tx", "clear_tx_on_rx", clearTxOnRx);
  g_key_file_set_boolean(kf, "tx", "word_mode", txWordMode);
  g_key_file_set_integer(kf, "tx", "auto_cq_interval", autoCqIntervalSec);
  g_key_file_set_integer(kf, "tx", "auto_cq_macro", autoCqMacro);
  g_key_file_set_double(kf, "tx", "monitor_level", tx.monitorLevel);
  g_key_file_set_boolean(kf, "tx", "cwid_at_end", tx.cwIdAtEnd);
  g_key_file_set_string(kf, "tx", "cwid_text", tx.cwIdText.c_str());
  g_key_file_set_double(kf, "tx", "cwid_wpm", tx.cwIdWpm);
  g_key_file_set_double(kf, "tx", "cwid_tone", tx.cwIdToneHz);
  g_key_file_set_integer(kf, "tx", "tune_timeout", tx.tuneTimeoutSec);
  g_key_file_set_boolean(kf, "cluster", "enabled", clusterEnabled);
  g_key_file_set_string(kf, "cluster", "host", clusterHost.c_str());
  g_key_file_set_integer(kf, "cluster", "port", clusterPort);
  g_key_file_set_string(kf, "cluster", "login", clusterLogin.c_str());
  g_key_file_set_string(kf, "cluster", "commands", clusterCommands.c_str());
  g_key_file_set_boolean(kf, "cluster", "rtty_only", clusterRttyOnly);
  g_key_file_set_integer(kf, "cluster", "max_age_min", clusterMaxAgeMin);
  g_key_file_set_boolean(kf, "cluster", "bandmap_visible", bandMapVisible);
  g_key_file_set_boolean(kf, "cluster", "bandmap_this_band", bandMapThisBand);
  g_key_file_set_boolean(kf, "ui", "modem_only", modemOnly);
  g_key_file_set_integer(kf, "callbook", "provider", callbookProvider);
  g_key_file_set_string(kf, "callbook", "user", callbookUser.c_str());
  g_key_file_set_string(kf, "callbook", "password", callbookPass.c_str());
  g_key_file_set_boolean(kf, "callbook", "auto_fill", callbookAutoFill);
  g_key_file_set_boolean(kf, "rx", "timestamps", rxTimestamps);
  g_key_file_set_boolean(kf, "rx", "log_to_file", rxLogToFile);
  g_key_file_set_string(kf, "rx", "log_dir", rxLogDir.c_str());
  g_key_file_set_boolean(kf, "xmlrpc", "enabled", rpcEnabled);
  g_key_file_set_string(kf, "xmlrpc", "bind", rpcBind.c_str());
  g_key_file_set_integer(kf, "xmlrpc", "port", rpcPort);

  g_key_file_set_string(kf, "station", "call", myCall.c_str());
  g_key_file_set_string(kf, "station", "name", myName.c_str());
  g_key_file_set_string(kf, "station", "qth", myQth.c_str());
  g_key_file_set_string(kf, "station", "grid", myGrid.c_str());

  for (size_t i = 0; i < macros.size(); ++i) {
    std::string kl = "label_" + std::to_string(i + 1), kt = "text_" + std::to_string(i + 1);
    g_key_file_set_string(kf, "macros", kl.c_str(), macros[i].label.c_str());
    g_key_file_set_string(kf, "macros", kt.c_str(), macros[i].text.c_str());
  }
  g_key_file_set_string(kf, "log", "scp_path", scpPath.c_str());
  g_key_file_set_string(kf, "log", "db_path", logDbPath.c_str());
  g_key_file_set_boolean(kf, "gridtracker", "enabled", gtEnabled);
  g_key_file_set_string(kf, "gridtracker", "host", gtHost.c_str());
  g_key_file_set_integer(kf, "gridtracker", "port", gtPort);
  g_key_file_set_boolean(kf, "gridtracker", "send_decodes", gtSendDecodes);
  g_key_file_set_boolean(kf, "gridtracker", "send_logged", gtSendLogged);
  g_key_file_set_boolean(kf, "gridtracker", "accept_reply", gtAcceptReply);
  g_key_file_set_double(kf, "display", "wf_lo", waterfallLoHz);
  g_key_file_set_double(kf, "display", "wf_hi", waterfallHiHz);
  g_key_file_set_integer(kf, "display", "wf_direction", waterfallDirection);
  g_key_file_set_integer(kf, "display", "font_size", fontSize);
  g_key_file_set_integer(kf, "display", "theme", theme);

  GError* err = nullptr;
  bool ok = g_key_file_save_to_file(kf, path.c_str(), &err);
  if (!ok) {
    std::fprintf(stderr, "settings: %s\n", err ? err->message : "save failed");
    if (err) g_error_free(err);
  }
  g_key_file_free(kf);
  return ok;
}

}  // namespace gtty
