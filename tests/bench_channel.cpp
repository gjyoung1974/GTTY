// Character error rate of each engine and of the best-guess stream on simulated HF
// channels (core/dsp/hf_channel.h), over a range of signal-to-noise ratios.
//
//   bench_channel [--profiles awgn,mid-moderate,...] [--snr -6,0,6] [--seeds N]
//                 [--messages N] [--scenario text|contest|both] [--jobs N] [--csv] [--show] [--detail]
//
// Two scenarios, because they fail differently:
//   text     one long transmission. Measures copy through the fades.
//   contest  short transmissions with band noise between them, as in a contest. Also
//            measures what the decoders print when nobody is sending.
// The error rate is the edit distance (substitutions, deletions and insertions) between
// what was sent and what was printed, over the length of what was sent; runs of white
// space count as one space. It is summed over the seeds.
//
// Not run by ctest: it is a measurement, not a pass/fail test. Use it before and after a
// decoder change, with the same arguments.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/baudot.h"
#include "core/dsp/hf_channel.h"
#include "core/events.h"
#include "core/modem/rtty_modulator.h"
#include "core/receiver.h"

using namespace gtty;

namespace {

const double kFs = Receiver::kInternalRate;

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) if (!item.empty()) out.push_back(item);
  return out;
}

// Contest traffic: CQs, exchanges with serial numbers, acknowledgements. Callsigns and
// numbers are random, so figures shifts and unshift-on-space are exercised throughout.
std::vector<std::string> makeMessages(int count, unsigned seed) {
  std::mt19937 rng(seed);
  auto pick = [&](const std::string& set) { return set[rng() % set.size()]; };
  const std::string letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ", digits = "0123456789";
  auto call = [&]() {
    static const char* prefixes[] = {"W", "K", "N", "DL", "G", "JA", "VE", "PY", "UA", "OH", "EA", "I", "F", "SP", "ZL", "VK"};
    std::string c = prefixes[rng() % 16];
    c += pick(digits);
    const int n = 1 + static_cast<int>(rng() % 3);
    for (int i = 0; i < n; ++i) c += pick(letters);
    return c;
  };
  std::vector<std::string> out;
  for (int i = 0; i < count; ++i) {
    const std::string a = call(), b = call();
    char serial[8];
    std::snprintf(serial, sizeof serial, "%03u", static_cast<unsigned>(rng() % 2000));
    switch (i % 4) {
      case 0: out.push_back("CQ TEST DE " + a + " " + a + " CQ"); break;
      case 1: out.push_back(b + " " + b); break;
      case 2: out.push_back(b + " 599 " + serial + " " + serial + " " + b); break;
      default: out.push_back("TU " + a + " 599 " + serial + " " + serial + " DE " + b); break;
    }
  }
  return out;
}

std::vector<float> modulateOne(const std::string& text, const RttyParams& p) {
  RttyModulator mod(kFs, p);
  mod.setIdleMode(RttyModulator::Idle::Silence);
  mod.setAmplitude(1.0f);
  BaudotEncoder enc(p.variant);
  enc.setUnshiftOnSpace(p.unshiftOnSpace);
  for (int i = 0; i < 2; ++i) mod.queueCode(baudot::LTRS);
  for (uint8_t c : enc.encode(text)) mod.queueCode(c);
  mod.queueCode(baudot::LTRS);
  std::vector<float> out, buf(512);
  while (mod.pending() > 0 || mod.busy()) {
    mod.generate(buf.data(), buf.size());
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

std::string normalize(const std::string& s) {
  std::string out;
  bool space = true;   // drops leading white space
  for (char c : s) {
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
      if (!space) out.push_back(' ');
      space = true;
    } else {
      out.push_back(c);
      space = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

size_t editDistance(const std::string& a, const std::string& b) {
  std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    prev.swap(cur);
  }
  return prev[b.size()];
}

// Figures as the letters on the same keys, so a character printed in the wrong shift
// counts as right: what is left is the errors that are not shift errors.
std::string foldShift(const std::string& s) {
  static const char* figs = "1234567890-?:$!&#'().,;/\"";
  static const char* ltrs = "QWERTYUIOPABCDFGHJKLMNVXZ";
  std::string out = s;
  for (auto& c : out)
    if (const char* f = std::strchr(figs, c); f && c) c = ltrs[f - figs];
  return out;
}

std::string stripSpaces(const std::string& s) {
  std::string out;
  for (char c : s) if (c != ' ') out.push_back(c);
  return out;
}

std::mutex gPlanMutex;   // FFTW plans (the receiver's spectrum) are not made thread-safely

std::vector<std::string> receive(const std::vector<float>& x, Voter::Combine combine, int* voteSlot) {
  EventQueue q;
  std::unique_ptr<Receiver> rx;
  {
    std::lock_guard<std::mutex> lk(gPlanMutex);
    rx = std::make_unique<Receiver>(nullptr, &q);
  }
  DemodConfig c;
  rx->configure(c);
  rx->setVoteCombine(combine);
  *voteSlot = rx->voteSlot();
  std::vector<std::string> text(rx->voteSlot() + 1);
  auto drain = [&] {
    Event e;
    while (q.pop(e))
      if (auto* t = std::get_if<RxTextEvent>(&e))
        if (t->slot < static_cast<int>(text.size())) text[t->slot] += t->text;
  };
  for (size_t pos = 0; pos < x.size(); pos += 4096) {
    rx->processSamples(x.data() + pos, std::min<size_t>(4096, x.size() - pos), kFs);
    drain();
  }
  rx->flushEvents();
  drain();
  {
    std::lock_guard<std::mutex> lk(gPlanMutex);
    rx.reset();
  }
  return text;
}

enum Column { kA, kB, kCorr, kMajority, kSoft, kColumns };
const char* const kColumnNames[kColumns] = {"A", "B", "Correlator", "Majority", "Soft"};

struct Job {
  const HfChannelProfile* profile;
  bool contest;
  double snr;
  unsigned seed;
  size_t edits[kColumns] = {0};
  size_t refLen = 0;
  // --detail, for the Correlator and Soft columns: [0] shift errors forgiven, [1] spaces ignored as well
  size_t detail[2][2] = {{0, 0}, {0, 0}};
};

bool gShow = false;   // --show: print what was sent and what each column printed

void runJob(Job& j, int messages) {
  const RttyParams p;
  const auto msgs = makeMessages(messages, 1000 + j.seed);
  std::vector<float> x(static_cast<size_t>(kFs * 1.0), 0.f);
  std::string ref;
  if (j.contest) {
    for (const auto& m : msgs) {
      auto s = modulateOne(m, p);
      x.insert(x.end(), s.begin(), s.end());
      x.insert(x.end(), static_cast<size_t>(kFs * 2.5), 0.f);
      ref += m + " ";
    }
  } else {
    for (const auto& m : msgs) ref += m + " ";
    auto s = modulateOne(ref, p);
    x.insert(x.end(), s.begin(), s.end());
    x.insert(x.end(), static_cast<size_t>(kFs * 1.0), 0.f);
  }
  ref = normalize(ref);
  x = applyHfChannel(x, kFs, *j.profile, 7919u * j.seed + 13u);
  addWhiteNoise(x, kFs, j.snr, 104729u * j.seed + 71u);

  int vote = 0;
  const auto soft = receive(x, Voter::Combine::Soft, &vote);
  const auto majority = receive(x, Voter::Combine::Majority, &vote);
  j.refLen = ref.size();
  j.edits[kA] = editDistance(ref, normalize(soft[kSlotA]));
  j.edits[kB] = editDistance(ref, normalize(soft[kSlotB]));
  j.edits[kCorr] = editDistance(ref, normalize(soft[kSlotCorrelator]));
  j.edits[kMajority] = editDistance(ref, normalize(majority[vote]));
  j.edits[kSoft] = editDistance(ref, normalize(soft[vote]));
  const std::string* two[2] = {&soft[kSlotCorrelator], &soft[vote]};
  for (int i = 0; i < 2; ++i) {
    const std::string got = foldShift(normalize(*two[i])), want = foldShift(ref);
    j.detail[i][0] = editDistance(want, got);
    j.detail[i][1] = editDistance(stripSpaces(want), stripSpaces(got));
  }
  if (gShow) {
    static std::mutex out;
    std::lock_guard<std::mutex> lk(out);
    std::printf("--- %s %s %.1f dB seed %u\nsent:       %s\n", j.profile->key, j.contest ? "contest" : "text", j.snr, j.seed, ref.c_str());
    const std::string* texts[kColumns] = {&soft[kSlotA], &soft[kSlotB], &soft[kSlotCorrelator], &majority[vote], &soft[vote]};
    for (int c = 0; c < kColumns; ++c) std::printf("%-11s %s\n", kColumnNames[c], normalize(*texts[c]).c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> profileKeys;
  std::vector<double> snrs = {-6, -3, 0, 3, 6, 10};
  int seeds = 3, messages = 24;
  unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
  bool text = true, contest = true, csv = false, detail = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--profiles") profileKeys = split(next());
    else if (a == "--snr") { snrs.clear(); for (const auto& s : split(next())) snrs.push_back(atof(s.c_str())); }
    else if (a == "--seeds") seeds = std::max(1, atoi(next().c_str()));
    else if (a == "--messages") messages = std::max(1, atoi(next().c_str()));
    else if (a == "--jobs") jobs = static_cast<unsigned>(std::max(1, atoi(next().c_str())));
    else if (a == "--csv") csv = true;
    else if (a == "--show") gShow = true;
    else if (a == "--detail") detail = true;
    else if (a == "--scenario") {
      const std::string s = next();
      text = s == "text" || s == "both";
      contest = s == "contest" || s == "both";
    } else {
      std::fprintf(stderr, "usage: bench_channel [--profiles a,b] [--snr -6,0,6] [--seeds N] [--messages N]\n"
                           "                     [--scenario text|contest|both] [--jobs N] [--csv] [--show] [--detail]\nprofiles:");
      for (const auto& p : hfChannelProfiles()) std::fprintf(stderr, " %s", p.key);
      std::fprintf(stderr, "\n");
      return 2;
    }
  }
  std::vector<const HfChannelProfile*> profiles;
  if (profileKeys.empty())
    for (const auto& p : hfChannelProfiles()) profiles.push_back(&p);
  for (const auto& k : profileKeys) {
    const HfChannelProfile* p = findHfChannelProfile(k);
    if (!p) { std::fprintf(stderr, "unknown profile %s\n", k.c_str()); return 2; }
    profiles.push_back(p);
  }

  std::vector<Job> all;
  for (const auto* p : profiles)
    for (int c = 0; c < 2; ++c) {
      if ((c == 0 && !text) || (c == 1 && !contest)) continue;
      for (double snr : snrs)
        for (int s = 0; s < seeds; ++s) all.push_back(Job{p, c == 1, snr, static_cast<unsigned>(s + 1)});
    }
  std::atomic<size_t> nextJob{0};
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < std::min<size_t>(jobs, all.size()); ++t)
    pool.emplace_back([&] {
      for (size_t i; (i = nextJob.fetch_add(1)) < all.size();) runJob(all[i], messages);
    });
  for (auto& t : pool) t.join();

  if (csv) std::printf("profile,scenario,snr_db,engine,cer_percent,ref_chars\n");
  for (size_t i = 0; i < all.size(); i += static_cast<size_t>(seeds)) {
    const Job& first = all[i];
    size_t edits[kColumns] = {0}, refLen = 0, det[2][2] = {{0, 0}, {0, 0}};
    for (int s = 0; s < seeds; ++s) {
      for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) det[a][b] += all[i + s].detail[a][b];
      for (int c = 0; c < kColumns; ++c) edits[c] += all[i + s].edits[c];
      refLen += all[i + s].refLen;
    }
    const char* scenario = first.contest ? "contest" : "text";
    if (csv) {
      for (int c = 0; c < kColumns; ++c)
        std::printf("%s,%s,%g,%s,%.2f,%zu\n", first.profile->key, scenario, first.snr, kColumnNames[c],
                    100.0 * edits[c] / refLen, refLen);
      continue;
    }
    if (first.snr == snrs.front()) {
      std::printf("\n%s: %s (%.1f ms, %.1f Hz), %s, %zu characters per point\n", first.profile->key, first.profile->name,
                  first.profile->delayMs, first.profile->dopplerHz, scenario, refLen);
      std::printf("  SNR dB");
      for (int c = 0; c < kColumns; ++c) std::printf(" %10s", kColumnNames[c]);
      if (detail) std::printf("   | Correlator: no shift errs, and no spaces | Soft: no shift errs, and no spaces");
      std::printf("\n");
    }
    std::printf("  %6.1f", first.snr);
    for (int c = 0; c < kColumns; ++c) std::printf(" %9.1f%%", 100.0 * edits[c] / refLen);
    if (detail)
      std::printf("   | %24.1f%% %19.1f%% | %18.1f%% %19.1f%%", 100.0 * det[0][0] / refLen, 100.0 * det[0][1] / refLen,
                  100.0 * det[1][0] / refLen, 100.0 * det[1][1] / refLen);
    std::printf("\n");
  }
  return 0;
}
