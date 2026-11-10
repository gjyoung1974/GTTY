// Thread-safe event queue from the DSP / rig / keyer threads to the UI.
#pragma once
#include <mutex>
#include <queue>
#include <string>
#include <variant>
#include <vector>

#include "core/log/callbook.h"
#include "core/modem/rtty_demodulator.h"
#include "core/net/cluster_client.h"
#include "core/rig/rig_control.h"

namespace gtty {

struct RxTextEvent { int slot; std::string text; };
struct TxEchoEvent { std::string text; };
struct SpectrumEvent { std::vector<float> db; double binHz; };
struct ScopeEvent { int slot; std::vector<ScopePoint> points; };
struct DemodStatusEvent { int slot; DemodStatus status; };
struct LevelEvent { float rxPeak; size_t overruns; };
struct RigStateEvent { RigState state; };
struct PttEvent { bool on; };
struct MessageEvent { std::string text; bool error; };
struct ClusterSpotEvent { Spot spot; };
struct ClusterStatusEvent { bool connected; std::string info; };
struct ClusterLineEvent { std::string line; };
struct CallbookEvent { CallbookResult result; };
struct SkimmerTextEvent { double markHz; float quality; std::string text; };   // text decoded by one skimmer channel

using Event = std::variant<RxTextEvent, TxEchoEvent, SpectrumEvent, ScopeEvent, DemodStatusEvent,
                           LevelEvent, RigStateEvent, PttEvent, MessageEvent, ClusterSpotEvent,
                           ClusterStatusEvent, ClusterLineEvent, CallbookEvent, SkimmerTextEvent>;

class EventQueue {
 public:
  void push(Event e) {
    std::lock_guard<std::mutex> lk(mu_);
    if (q_.size() > 10000) q_.pop();
    q_.push(std::move(e));
  }
  bool pop(Event& e) {
    std::lock_guard<std::mutex> lk(mu_);
    if (q_.empty()) return false;
    e = std::move(q_.front());
    q_.pop();
    return true;
  }
  size_t size() const { std::lock_guard<std::mutex> lk(mu_); return q_.size(); }

 private:
  mutable std::mutex mu_;
  std::queue<Event> q_;
};

}  // namespace gtty
