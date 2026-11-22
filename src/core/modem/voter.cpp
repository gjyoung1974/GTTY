#include "core/modem/voter.h"

#include <algorithm>
#include <cmath>

namespace gtty {

void Voter::configure(int nSlots, double charSamples, uint64_t holdSamples, BaudotVariant v, bool uos) {
  nSlots_ = nSlots;
  votes_.assign(nSlots, true);
  witness_.resize(nSlots);
  for (int i = 0; i < nSlots; ++i) witness_[i] = i;
  charSamples_ = charSamples;
  hold_ = holdSamples;
  baudot_.setVariant(v);
  baudot_.setUnshiftOnSpace(uos);
  groups_.clear();
}

void Voter::setVoting(int slot, bool votes) {
  if (slot >= 0 && slot < nSlots_) votes_[slot] = votes;
}

void Voter::setWitness(int slot, int witness) {
  if (slot >= 0 && slot < nSlots_) witness_[slot] = witness;
}

void Voter::reset() {
  pending_.clear();
  inSignal_ = false;
  lastSound_ = 0;
  lastOut_ = 0;
  groups_.clear();
  baudot_.reset();
}

void Voter::add(int slot, const CodeEvent& e) {
  if (slot < 0 || slot >= nSlots_ || !votes_[slot] || !e.valid) return;
  // An engine that reports no soft bits (codec2) stands behind each bit of its
  // character with its confidence in the whole.
  std::array<float, 5> soft;
  for (int k = 0; k < 5; ++k)
    soft[k] = e.hasSoft ? std::clamp(e.soft[k], -1.f, 1.f) : (((e.code >> k) & 1) ? e.confidence : -e.confidence);
  // Join the group whose end time is within half a character and that has no
  // vote from this engine yet; otherwise open a new group.
  const double half = charSamples_ * 0.5;
  for (auto& g : groups_) {
    double dt = static_cast<double>(e.sample) - static_cast<double>(g.t0);
    if (std::fabs(dt) < half && g.code[slot] < 0) {
      g.code[slot] = static_cast<int8_t>(e.code);
      g.conf[slot] = e.confidence;
      g.soft[slot] = soft;
      if (e.sample < g.t0) g.t0 = e.sample;
      return;
    }
  }
  Group g;
  g.t0 = e.sample;
  g.code.assign(nSlots_, -1);
  g.conf.assign(nSlots_, 0.f);
  g.soft.assign(nSlots_, std::array<float, 5>{});
  g.code[slot] = static_cast<int8_t>(e.code);
  g.conf[slot] = e.confidence;
  g.soft[slot] = soft;
  // Keep groups ordered by time.
  auto it = std::upper_bound(groups_.begin(), groups_.end(), g, [](const Group& a, const Group& b) { return a.t0 < b.t0; });
  groups_.insert(it, g);
}

void Voter::emit(const Pending& p) {
  strength_ = p.strength;
  if (onCode) onCode(p.code, p.confidence, p.agree, p.total);
  auto ch = baudot_.decode(p.code);
  if (ch && onChar) { onChar(*ch); lastOut_ = *ch; }
}

void Voter::flushPending() {
  for (const Pending& p : pending_) emit(p);
  pending_.clear();
}

// The transmission is over. Of the doubtful characters held back at its end, those that
// two witnesses agreed on were its last ones, sent into a fade; the rest is what the
// noise after it spelled. A space sets the next transmission apart from this one: with
// the noise between them no longer printed, nothing else would.
void Voter::endTransmission() {
  for (const Pending& p : pending_)
    if (p.backers >= 2) emit(p);
  pending_.clear();
  inSignal_ = false;
  if (lastOut_ && lastOut_ != ' ' && lastOut_ != '\n' && lastOut_ != '\r' && onChar) {
    strength_ = 0;
    onChar(' ');
    lastOut_ = ' ';
  }
}

void Voter::advance(uint64_t now) {
  while (!groups_.empty() && groups_.front().t0 + hold_ <= now) {
    decide(groups_.front());
    groups_.pop_front();
  }
  // Decisions arrive hold_ late, so allow for that before calling it quiet.
  if (inSignal_ && static_cast<double>(now) - static_cast<double>(lastSound_) > static_cast<double>(hold_) + kQuietChars * charSamples_)
    endTransmission();
}

void Voter::finish() {
  while (!groups_.empty()) {
    decide(groups_.front());
    groups_.pop_front();
  }
  flushPending();
  inSignal_ = false;
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
  float softMargin = -1;   // Soft: the mean |summed bit|, in engines
  if (combine_ == Combine::Soft && total >= 2) {
    float sum[5] = {0, 0, 0, 0, 0};
    for (int s = 0; s < nSlots_; ++s) {
      if (g.code[s] < 0) continue;
      for (int k = 0; k < 5; ++k) sum[k] += g.soft[s][k];
    }
    best = 0;
    float margin = 0;
    for (int k = 0; k < 5; ++k) {
      if (sum[k] >= 0) best |= 1 << k;
      margin += std::min(1.f, std::fabs(sum[k]) / static_cast<float>(total));
    }
    confidence = margin / 5.f;
    softMargin = margin / 5.f * static_cast<float>(total);
  }
  // A lone, weak opinion (others silent, e.g. squelched) is not worth printing.
  if (total == 1 && weight[best] < 0.2f) return;
  // What stands behind the decision: the confidence of each witness that agrees with it
  // (the surest of its engines), averaged over all witnesses that may vote. With fewer
  // than two witnesses among the voters, every engine is taken as one.
  int voters = 0;
  std::vector<int> ids;
  for (int s = 0; s < nSlots_; ++s) {
    if (!votes_[s]) continue;
    ++voters;
    if (std::find(ids.begin(), ids.end(), witness_[s]) == ids.end()) ids.push_back(witness_[s]);
  }
  const bool grouped = ids.size() >= 2;
  if (!grouped) { ids.clear(); for (int s = 0; s < nSlots_; ++s) if (votes_[s]) ids.push_back(s); }
  float agreed = 0;
  int backers = 0;
  for (int id : ids) {
    float conf = -1;
    for (int s = 0; s < nSlots_; ++s)
      if (votes_[s] && (grouped ? witness_[s] : s) == id && g.code[s] == best) conf = std::max(conf, g.conf[s]);
    if (conf >= 0) { agreed += conf; ++backers; }
  }
  const int witnesses = static_cast<int>(ids.size());
  strength_ = witnesses > 0 ? agreed / static_cast<float>(witnesses) : 0.f;
  // Soft: the character may be one that no engine printed, so agreement cannot measure
  // it. The margin of the summed bits does: near the engines' own confidence where their
  // bits point the same way, small where they cancel or an engine is silent.
  if (softMargin >= 0 && voters > 0) strength_ = softMargin / static_cast<float>(voters);
  const Pending now{static_cast<uint8_t>(best), strength_, confidence, count[best], total, backers};
  // Between transmissions the matched-filter engines go on printing what the noise
  // spells, each something different, while the correlator mostly stays silent. Voting
  // on that would print the noise of all of them, and the merged pane would read worse
  // than any one engine. So a transmission has to start with a decision two witnesses
  // stand behind; inside one, a doubtful decision (a fade, a static crash) is held back
  // until a sound one follows it, and a run of doubtful ones, or a while without a sound
  // one, ends the transmission. A single voting engine is passed through as it is.
  if (witnesses >= 2) {
    if (!inSignal_) {
      if (strength_ < kEnterStrength || backers < 2) return;
      inSignal_ = true;
    } else if (strength_ < kDoubtStrength) {
      pending_.push_back(now);
      if (pending_.size() > kMaxDoubtful) endTransmission();
      return;
    }
    flushPending();
    lastSound_ = g.t0;
  } else {
    flushPending();
    inSignal_ = false;
  }
  emit(now);
}

}  // namespace gtty
