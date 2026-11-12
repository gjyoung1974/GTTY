#include "core/rig/rig_control.h"

#include <hamlib/rig.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace gtty {

namespace {
int listCb(const struct rig_caps* caps, rig_ptr_t data) {
  auto* v = static_cast<std::vector<RigModelInfo>*>(data);
  v->push_back({static_cast<int>(caps->rig_model), caps->mfg_name ? caps->mfg_name : "",
                caps->model_name ? caps->model_name : "", caps->version ? caps->version : "",
                rig_strstatus(caps->status)});
  return 1;  // continue
}
}  // namespace

RigControl::RigControl() { rig_set_debug(RIG_DEBUG_NONE); }
RigControl::~RigControl() { close(); }

std::vector<RigModelInfo> RigControl::listModels() {
  rig_set_debug(RIG_DEBUG_NONE);
  rig_load_all_backends();
  std::vector<RigModelInfo> v;
  rig_list_foreach(listCb, &v);
  std::sort(v.begin(), v.end(), [](const RigModelInfo& a, const RigModelInfo& b) {
    if (a.mfg != b.mfg) return a.mfg < b.mfg;
    return a.name < b.name;
  });
  return v;
}

void RigControl::setError(const std::string& e) {
  // Hamlib messages can carry multi-line debug dumps; keep the first line only.
  std::string one = e.substr(0, e.find('\n'));
  if (one.size() > 120) one = one.substr(0, 117) + "...";
  state_.error = one;
}

bool RigControl::openPttPort(const RigConfig& cfg, std::string& err) {
  std::lock_guard<std::mutex> lk(mu_);
  pttPort_.close();
  pttPortError_.clear();
  ownPttPort_ = cfg.pttOnOwnPort();
  if (!ownPttPort_) return true;
  if (pttPort_.open(cfg.pttDevice, cfg.pttType == "RTS" ? SerialPtt::Line::Rts : SerialPtt::Line::Dtr, err)) return true;
  pttPortError_ = err;
  return false;
}

bool RigControl::open(const RigConfig& cfg, std::string& err) {
  closeRig();
  cfg_ = cfg;
  RIG* rig = rig_init(static_cast<rig_model_t>(cfg.model));
  if (!rig) { err = "rig_init failed: unknown model " + std::to_string(cfg.model); return false; }

  auto setConf = [&](const char* key, const std::string& val) {
    if (val.empty()) return;
    auto tok = rig_token_lookup(rig, key);
    if (tok != RIG_CONF_END) rig_set_conf(rig, tok, val.c_str());
  };
  setConf("rig_pathname", cfg.device);
  if (cfg.baud > 0) setConf("serial_speed", std::to_string(cfg.baud));
  std::string ptt = cfg.pttType;
  for (auto& c : ptt) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  // A PTT line on its own port is keyed by pttPort_, not by Hamlib: keep Hamlib off that port.
  if (ptt == "NONE" || cfg.pttOnOwnPort()) ptt = "None";
  setConf("ptt_type", ptt);
  if (ptt == "DTR" || ptt == "RTS") setConf("ptt_pathname", cfg.device);

  int rc = rig_open(rig);
  if (rc != RIG_OK) {
    err = std::string("rig_open: ") + rigerror2(rc);
    rig_cleanup(rig);
    return false;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    rig_ = rig;
    state_ = RigState{};
    state_.connected = true;
  }
  run_ = true;
  poller_ = std::thread([this] { pollLoop(); });
  return true;
}

void RigControl::close() {
  closeRig();
  std::lock_guard<std::mutex> lk(mu_);
  pttPort_.close();
  ownPttPort_ = false;
}

void RigControl::closeRig() {
  run_ = false;
  if (poller_.joinable()) poller_.join();
  std::lock_guard<std::mutex> lk(mu_);
  if (rig_) {
    rig_close(rig_);
    rig_cleanup(rig_);
    rig_ = nullptr;
  }
  state_ = RigState{};
}

RigState RigControl::state() const {
  std::lock_guard<std::mutex> lk(mu_);
  return state_;
}

void RigControl::pollLoop() {
  while (run_) {
    RigState snapshot;
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (rig_) {
        freq_t f = 0;
        int rc = rig_get_freq(rig_, RIG_VFO_CURR, &f);
        if (rc == RIG_OK) { state_.freqHz = f; state_.error.clear(); }
        else setError(rigerror2(rc));
        rmode_t m = RIG_MODE_NONE;
        pbwidth_t w = 0;
        if (rig_get_mode(rig_, RIG_VFO_CURR, &m, &w) == RIG_OK) {
          state_.mode = rig_strrmode(m);
          state_.widthHz = static_cast<int>(w);
        }
        ptt_t p = RIG_PTT_OFF;
        if (pttPort_.isOpen()) state_.ptt = pttPort_.get();
        else if (rig_get_ptt(rig_, RIG_VFO_CURR, &p) == RIG_OK) state_.ptt = p != RIG_PTT_OFF;
      }
      snapshot = state_;
    }
    if (onState) onState(snapshot);
    for (int i = 0; i < cfg_.pollMs / 10 && run_; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

bool RigControl::setFreq(double hz) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_) return false;
  int rc = rig_set_freq(rig_, RIG_VFO_CURR, hz);
  if (rc == RIG_OK) state_.freqHz = hz; else setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::setMode(const std::string& mode, int widthHz) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_) return false;
  rmode_t m = rig_parse_mode(mode.c_str());
  if (m == RIG_MODE_NONE) return false;
  int rc = rig_set_mode(rig_, RIG_VFO_CURR, m, widthHz < 0 ? RIG_PASSBAND_NOCHANGE : widthHz);
  if (rc == RIG_OK) state_.mode = mode; else setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::setPtt(bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  if (ownPttPort_) {
    // Never fall back to the rig: with NET rigctl that would key through whatever
    // rigctld was started with, which is not what was asked for.
    std::string e = pttPortError_;
    bool ok = pttPort_.isOpen() && pttPort_.set(on, &e);
    if (ok) state_.ptt = on; else setError(e);
    return ok;
  }
  if (!rig_) return false;
  int rc = rig_set_ptt(rig_, RIG_VFO_CURR, on ? RIG_PTT_ON : RIG_PTT_OFF);
  if (rc == RIG_OK) state_.ptt = on; else setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::setWidth(int hz) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_ || hz <= 0) return false;
  rmode_t m = RIG_MODE_NONE;
  pbwidth_t w = 0;
  int rc = rig_get_mode(rig_, RIG_VFO_CURR, &m, &w);
  if (rc != RIG_OK) { setError(rigerror2(rc)); return false; }
  rc = rig_set_mode(rig_, RIG_VFO_CURR, m, hz);
  if (rc == RIG_OK) state_.widthHz = hz; else setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::setIfShift(int hz) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_) return false;
  value_t v;
  v.i = hz;
  int rc = rig_set_level(rig_, RIG_VFO_CURR, RIG_LEVEL_IF, v);
  if (rc != RIG_OK) setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::hasIfShift() const {
  std::lock_guard<std::mutex> lk(mu_);
  return rig_ && rig_has_set_level(rig_, RIG_LEVEL_IF);
}

bool RigControl::sendText(const std::string& text) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_) return false;
  int rc = rig_send_morse(rig_, RIG_VFO_CURR, text.c_str());
  if (rc != RIG_OK) setError(rigerror2(rc));
  return rc == RIG_OK;
}

bool RigControl::stopText() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!rig_) return false;
  return rig_stop_morse(rig_, RIG_VFO_CURR) == RIG_OK;
}

std::string bandForFrequency(double hz) {
  double k = hz / 1000.0;
  struct B { double lo, hi; const char* name; };
  static const B bands[] = {
      {1800, 2000, "160m"},   {3500, 4000, "80m"},    {5250, 5450, "60m"},
      {7000, 7300, "40m"},    {10100, 10150, "30m"},  {14000, 14350, "20m"},
      {18068, 18168, "17m"},  {21000, 21450, "15m"},  {24890, 24990, "12m"},
      {28000, 29700, "10m"},  {50000, 54000, "6m"},   {144000, 148000, "2m"},
      {420000, 450000, "70cm"}};
  for (const auto& b : bands)
    if (k >= b.lo && k <= b.hi) return b.name;
  return "";
}

}  // namespace gtty
