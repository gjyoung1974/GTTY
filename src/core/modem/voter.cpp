#include "core/modem/voter.h"

#include <algorithm>
#include <cmath>

namespace gtty {

void Voter::configure(int nSlots, double charSamples, uint64_t holdSamples, BaudotVariant v, bool uos) {
  nSlots_ = nSlots;
  votes_.assign(nSlots, true);
  charSamples_ = charSamples;
  hold_ = holdSamples;
  baudot_.setVariant(v);
  baudot_.setUnshiftOnSpace(uos);
  groups_.clear();
}

void Voter::setVoting(int slot, bool votes) {
  if (slot >= 0 && slot < nSlots_) votes_[slot] = votes;
}

void Voter::reset() {
  pending_.clear();
  inSignal_ = false;
  groups_.clear();
  baudot_.reset();
}

void Voter::add(int slot, const CodeEvent& e) {
  if (slot < 0 || slot >= nSlots_ || !votes_[slot] || !e.valid) return;
  // Join the group whose end time is within half a character and that has no
  // vote from this engine yet; otherwise open a new group.
  const double half = charSamples_ * 0.5;
  for (auto& g : groups_) {
    double dt = static_cast<double>(e.sample) - static_cast<double>(g.t0);
    if (std::fabs(dt) < half && g.code[slot] < 0) {
      g.code[slot] = static_cast<int8_t>(e.code);
      g.conf[slot] = e.confidence;
      if (e.sample < g.t0) g.t0 = e.sample;
      return;
    }
  }
  Group g;
  g.t0 = e.sample;
  g.code.assign(nSlots_, -1);
  g.conf.assign(nSlots_, 0.f);
  g.code[slot] = static_cast<int8_t>(e.code);
  g.conf[slot] = e.confidence;
  // Keep groups ordered by time.
  auto it = std::upper_bound(groups_.begin(), groups_.end(), g, [](const Group& a, const Group& b) { return a.t0 < b.t0; });
  groups_.insert(it, g);
}

void Voter::flushPending() {
  for (const Pending& p : pending_) {
    strength_ = p.strength;
    if (onCode) onCode(p.code, p.confidence, p.agree, p.total);
    auto ch = baudot_.decode(p.code);
    if (ch && onChar) onChar(*ch);
  }
  pending_.clear();
}

void Voter::advance(uint64_t now) {
  while (!groups_.empty() && groups_.front().t0 + hold_ <= now) {
    decide(groups_.front());
    groups_.pop_front();
  }
  // Nothing followed the held characters (the squelch closed, the station stopped):
  // they were the end of the transmission, so print them.
  if (!pending_.empty() &&
      static_cast<double>(now) - static_cast<double>(pendingAt_) > static_cast<double>(hold_) + 3.0 * charSamples_) {
    flushPending();
    inSignal_ = false;
  }
}

void Voter::decide(const Group& g) {
  float weight[32] = {0};
  int count[32] = {0};
  int total = 0;
  for (int s = 0; s < nSlots_; ++s) {
    if (g.code[s] < 0) continue;
    ++total;
    weight[g.code[s]] += std::max(0.05f, g.conf[s]);
    count[g.code[s]]++;
  }
  if (total == 0) return;
  int best = 0;
  for (int c = 1; c < 32; ++c)
    if (weight[c] > weight[best]) best = c;
  float sumAll = 0;
  for (int c = 0; c < 32; ++c) sumAll += weight[c];
  float confidence = sumAll > 0 ? weight[best] / sumAll : 0.f;
  // A lone, weak opinion (others silent, e.g. squelched) is not worth printing.
  if (total == 1 && weight[best] < 0.2f) return;
  float agreed = 0;
  int voters = 0;
  for (int s = 0; s < nSlots_; ++s) {
    if (votes_[s]) ++voters;
    if (g.code[s] == best) agreed += g.conf[s];
  }
  strength_ = voters > 0 ? agreed / static_cast<float>(voters) : 0.f;
  // Between transmissions the matched-filter engines go on printing what the noise
  // spells, each something different, while the correlator mostly stays silent. Voting
  // on that would print the noise of all of them, and the merged pane would read worse
  // than any one engine. So a transmission has to start with a decision the engines
  // stand behind; inside one, a doubtful decision (a fade, a static crash) is held back
  // until a sound one follows it, and a run of doubtful ones ends the transmission and
  // is dropped. A single voting engine is passed through as it is.
  if (voters >= 2) {
    if (!inSignal_) {
      if (strength_ < kEnterStrength) return;
      inSignal_ = true;
    } else if (strength_ < kDoubtStrength) {
      pending_.push_back({static_cast<uint8_t>(best), strength_, confidence, count[best], total});
      pendingAt_ = g.t0;
      if (pending_.size() > kMaxDoubtful) { pending_.clear(); inSignal_ = false; }
      return;
    }
    const float current = strength_;
    flushPending();
    strength_ = current;
  } else {
    flushPending();
    inSignal_ = false;
  }
  if (onCode) onCode(static_cast<uint8_t>(best), confidence, count[best], total);
  auto ch = baudot_.decode(static_cast<uint8_t>(best));
  if (ch && onChar) onChar(*ch);
}

}  // namespace gtty
