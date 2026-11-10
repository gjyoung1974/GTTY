// gtty-cli: command-line RTTY tools built on the gtty core.
//   gtty-cli encode "TEXT" out.wav [opts]     generate an AFSK RTTY WAV file
//   gtty-cli decode in.wav [opts]             decode a recording
//   gtty-cli listen [opts]                    decode live audio to stdout
//   gtty-cli record out.wav [--seconds N]     capture live audio to a WAV file
//   gtty-cli devices                          list audio devices
//   gtty-cli rigs [filter]                    list Hamlib rig models
//   gtty-cli rig --model N --device DEV       show rig frequency / mode
// Options: --baud 45.45 --shift 170 --mark 2125 --reverse --rate 48000
//          --no-afc --no-atc --squelch Q (0..1, as the GUI slider) --in DEV --out DEV --usos/--no-usos --ustty
//          --start S --limit S (decode window) --quiet (stats only)
//          decoder knobs: --filter-bits F --window 0|1 --atc-tau S --bit-window F --afc-gain G
//                         --mode normal|selective|flutter --engines all --corr-edge F --corr-accept F
//          --iq [--center HZ]  treat a stereo file as I/Q baseband (SDR recording)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/audio_engine.h"
#include "core/audio/wav_file.h"
#include "core/dsp/resampler.h"
#include "core/events.h"
#include "core/modem/rtty_modulator.h"
#include "core/receiver.h"
#include "core/rig/rig_control.h"

#include <hamlib/rig.h>

using namespace gtty;

namespace {

struct Opts {
  RttyParams params;
  double rate = 48000;
  bool afc = true, atc = true;
  double squelch = 0;
  double filterBits = -1, atcTau = -1, bitWindow = -1, afcGain = -1;
  int window = -1;
  int mode = -1;              // preset for the matched-filter engines (0 normal, 1 selective, 2 flutter)
  bool allEngines = false;    // enable every engine, including the codec2 comparison decoder
  double corrEdge = -1, corrAccept = -1;
  int inDev = -1, outDev = -1;
  int model = 1;
  std::string device;
  int baudRate = 0;
  double seconds = 10;
  double start = 0, limit = 0;   // decode window (seconds)
  bool debug = false;
  bool quiet = false;
  bool iq = false;        // treat a stereo file as I/Q baseband
  double iqCenter = 0;    // audio frequency the baseband centre is shifted to (0 = rate/4)
  std::vector<std::string> positional;
};

Opts parse(int argc, char** argv) {
  Opts o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--baud") o.params.baud = atof(next().c_str());
    else if (a == "--shift") o.params.shift = atof(next().c_str());
    else if (a == "--mark") o.params.markFreq = atof(next().c_str());
    else if (a == "--reverse") o.params.reverse = true;
    else if (a == "--stop") o.params.stopBits = atof(next().c_str());
    else if (a == "--rate") o.rate = atof(next().c_str());
    else if (a == "--no-afc") o.afc = false;
    else if (a == "--no-atc") o.atc = false;
    else if (a == "--squelch") o.squelch = atof(next().c_str());
    else if (a == "--filter-bits") o.filterBits = atof(next().c_str());
    else if (a == "--window") o.window = atoi(next().c_str());
    else if (a == "--atc-tau") o.atcTau = atof(next().c_str());
    else if (a == "--bit-window") o.bitWindow = atof(next().c_str());
    else if (a == "--afc-gain") o.afcGain = atof(next().c_str());
    else if (a == "--mode") { std::string m = next(); o.mode = m == "selective" ? 1 : m == "flutter" ? 2 : m == "normal" ? 0 : atoi(m.c_str()); }
    else if (a == "--engines") o.allEngines = next() == "all";
    else if (a == "--corr-edge") o.corrEdge = atof(next().c_str());
    else if (a == "--corr-accept") o.corrAccept = atof(next().c_str());
    else if (a == "--no-usos") o.params.unshiftOnSpace = false;
    else if (a == "--ustty") o.params.variant = BaudotVariant::USTTY;
    else if (a == "--in") o.inDev = atoi(next().c_str());
    else if (a == "--out") o.outDev = atoi(next().c_str());
    else if (a == "--model") o.model = atoi(next().c_str());
    else if (a == "--device") o.device = next();
    else if (a == "--serial-baud") o.baudRate = atoi(next().c_str());
    else if (a == "--seconds") o.seconds = atof(next().c_str());
    else if (a == "--debug") o.debug = true;
    else if (a == "--start") o.start = atof(next().c_str());
    else if (a == "--limit") o.limit = atof(next().c_str());
    else if (a == "--quiet") o.quiet = true;
    else if (a == "--iq") o.iq = true;
    else if (a == "--center") o.iqCenter = atof(next().c_str());
    else o.positional.push_back(a);
  }
  return o;
}

DemodConfig demodConfig(const Opts& o) {
  DemodConfig c;
  c.params = o.params;
  c.afc = o.afc;
  c.atc = o.atc;
  c.squelch = static_cast<float>(o.squelch);
  if (o.bitWindow > 0) c.bitWindow = o.bitWindow;
  if (o.afcGain > 0) c.afcGain = o.afcGain;
  if (o.corrEdge > 0) c.corrEdgeThreshold = o.corrEdge;
  if (o.corrAccept > 0) c.corrAcceptScore = o.corrAccept;
  return c;
}

// Apply per-slot filter overrides after the bank has been configured.
void applyFilterOverrides(Receiver& rx, const Opts& o) {
  if (o.allEngines)   // run every engine; voting membership stays as configured (codec2 does not vote)
    for (size_t i = 0; i < rx.bank().size(); ++i) rx.setSlotEnabled(i, true, rx.bank().slot(i).votes);
  if (o.filterBits <= 0 && o.window < 0 && o.atcTau <= 0 && o.mode < 0) return;
  for (size_t i = 0; i < std::min<size_t>(2, rx.bank().size()); ++i) {   // matched-filter engines only
    rx.configureSlot(i, [&](DemodConfig& c) {
      if (o.mode >= 0) applyDecoderMode(c, static_cast<DecoderMode>(o.mode));
      if (o.filterBits > 0) c.filterLengthBits = o.filterBits;
      if (o.window >= 0) c.matchedWindow = o.window;
      if (o.atcTau > 0) c.atcTauSec = o.atcTau;
    });
  }
}

int cmdEncode(const Opts& o) {
  if (o.positional.size() < 3) { std::cerr << "usage: encode TEXT out.wav\n"; return 2; }
  RttyModulator mod(o.rate, o.params);
  mod.setIdleMode(RttyModulator::Idle::Silence);
  mod.setAmplitude(0.6f);
  BaudotEncoder enc(o.params.variant);
  enc.setUnshiftOnSpace(o.params.unshiftOnSpace);
  for (int i = 0; i < 2; ++i) mod.queueCode(baudot::LTRS);
  for (uint8_t c : enc.encode(o.positional[1] + "\n")) mod.queueCode(c);
  mod.queueCode(baudot::LTRS);
  std::vector<float> out;
  std::vector<float> buf(1024);
  // lead-in silence
  out.assign(static_cast<size_t>(o.rate * 0.2), 0.f);
  while (mod.pending() > 0 || mod.busy()) {
    mod.generate(buf.data(), buf.size());
    out.insert(out.end(), buf.begin(), buf.end());
  }
  out.insert(out.end(), static_cast<size_t>(o.rate * 0.2), 0.f);
  std::string err;
  if (!writeWavFile(o.positional[2], out, o.rate, err)) { std::cerr << err << "\n"; return 1; }
  std::cout << "wrote " << o.positional[2] << " (" << out.size() / o.rate << " s)\n";
  return 0;
}

void drainText(EventQueue& q, std::vector<std::string>& perSlot) {
  Event e;
  while (q.pop(e)) {
    if (auto* t = std::get_if<RxTextEvent>(&e)) {
      if (t->slot >= static_cast<int>(perSlot.size())) perSlot.resize(t->slot + 1);
      perSlot[t->slot] += t->text;
    }
  }
}

int cmdDecode(const Opts& o) {
  if (o.positional.size() < 2) { std::cerr << "usage: decode in.wav\n"; return 2; }
  std::vector<float> samples;
  double rate = 0;
  std::string err;
  if (o.iq) {
    // Complex baseband (SDR recording): shift the centre to an audio frequency and take the real part.
    std::vector<float> i, q;
    if (!readIqFile(o.positional[1], i, q, rate, err)) { std::cerr << err << "\n"; return 1; }
    double fc = o.iqCenter > 0 ? o.iqCenter : rate / 4.0 + rate / 2.0;
    // The complex baseband spans +-rate/2 around the centre: upsample so the real
    // signal (centre + rate/2) fits comfortably below the new Nyquist frequency.
    double outRate = std::max(rate, 2.5 * (fc + rate / 2.0));
    if (outRate > rate) {
      Resampler ri(rate, outRate, 0 /* SRC_SINC_BEST_QUALITY */), rq(rate, outRate, 0);
      std::vector<float> i2, q2;
      ri.process(i.data(), i.size(), i2);
      rq.process(q.data(), q.size(), q2);
      size_t n = std::min(i2.size(), q2.size());
      i2.resize(n); q2.resize(n);
      i.swap(i2); q.swap(q2);
      rate = outRate;
    }
    samples.resize(i.size());
    double ph = 0, inc = 2.0 * M_PI * fc / rate;
    float peak = 1e-9f;
    for (size_t n = 0; n < i.size(); ++n) {
      samples[n] = static_cast<float>(i[n] * std::cos(ph) - q[n] * std::sin(ph));
      peak = std::max(peak, std::fabs(samples[n]));
      ph += inc; if (ph > 2 * M_PI) ph -= 2 * M_PI;
    }
    for (auto& v : samples) v = v / peak * 0.8f;
    std::cerr << "I/Q input: centre shifted to " << fc << " Hz at " << rate << " Hz sample rate (tones are relative to the centre)\n";
  } else if (!readAudioFile(o.positional[1], samples, rate, err)) { std::cerr << err << "\n"; return 1; }
  if (o.start > 0) {
    size_t s0 = std::min(samples.size(), static_cast<size_t>(o.start * rate));
    samples.erase(samples.begin(), samples.begin() + s0);
  }
  if (o.limit > 0 && samples.size() > static_cast<size_t>(o.limit * rate)) samples.resize(static_cast<size_t>(o.limit * rate));
  EventQueue q;
  Receiver rx(nullptr, &q);
  rx.configure(demodConfig(o));
  applyFilterOverrides(rx, o);
  size_t pos = 0;
  std::vector<std::string> text;
  while (pos < samples.size()) {
    size_t n = std::min<size_t>(4096, samples.size() - pos);
    rx.processSamples(samples.data() + pos, n, rate);
    pos += n;
    drainText(q, text);   // the queue keeps only the newest 10000 events; do not let text fall off the end
  }
  rx.flushEvents();
  drainText(q, text);
  for (size_t i = 0; i < rx.bank().size(); ++i) {
    auto st = rx.bank().slot(i).demod->status();
    std::cout << "== " << rx.bank().slot(i).label << " (chars " << st.chars << ", framing errors "
              << st.framingErrors << ", AFC " << st.afcOffsetHz << " Hz) ==\n";
    if (i < text.size() && !o.quiet) std::cout << text[i] << "\n";
  }
  int v = rx.voteSlot();
  size_t voteChars = v < static_cast<int>(text.size()) ? text[v].size() : 0;
  std::cout << "== Vote (chars " << voteChars << ", framing errors 0, AFC 0 Hz) ==\n";
  if (v < static_cast<int>(text.size()) && !o.quiet) std::cout << text[v] << "\n";
  return 0;
}

int cmdListen(const Opts& o) {
  AudioEngine engine;
  std::string err;
  if (!engine.openInput(o.inDev, o.rate, -1, err)) { std::cerr << "audio: " << err << "\n"; return 1; }
  std::cerr << "listening at " << engine.inputRate() << " Hz; Ctrl-C to stop\n";
  EventQueue q;
  Receiver rx(&engine, &q);
  rx.configure(demodConfig(o));
  rx.start();
  std::vector<std::string> text;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    text.clear();
    drainText(q, text);
    if (!text.empty() && !text[0].empty()) { std::cout << text[0] << std::flush; }
  }
}

int cmdRecord(const Opts& o) {
  if (o.positional.size() < 2) { std::cerr << "usage: record out.wav [--in DEV] [--seconds N]\n"; return 2; }
  AudioEngine engine;
  std::string err;
  if (!engine.openInput(o.inDev, o.rate, -1, err)) { std::cerr << "audio: " << err << "\n"; return 1; }
  double rate = engine.inputRate();
  std::cerr << "recording " << o.seconds << " s at " << rate << " Hz\n";
  std::vector<float> all, buf(4096);
  auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::duration<double>(o.seconds)) {
    size_t n = engine.rxRing().pop(buf.data(), buf.size());
    if (n == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
    all.insert(all.end(), buf.begin(), buf.begin() + n);
  }
  std::cerr << "captured " << all.size() << " samples (" << all.size() / rate << " s of audio in "
            << o.seconds << " s wall clock)\n";
  if (!writeWavFile(o.positional[1], all, rate, err)) { std::cerr << err << "\n"; return 1; }
  return 0;
}

int cmdDevices() {
  for (const auto& d : AudioEngine::listDevices()) {
    std::printf("%3d  %-50s %-10s in:%d out:%d %.0f Hz\n", d.index, d.name.c_str(), d.hostApi.c_str(),
                d.maxInputs, d.maxOutputs, d.defaultSampleRate);
  }
  std::printf("default input %d, default output %d\n", AudioEngine::defaultInputDevice(),
              AudioEngine::defaultOutputDevice());
  return 0;
}

int cmdRigs(const Opts& o) {
  std::string filter = o.positional.size() > 1 ? o.positional[1] : "";
  for (auto& c : filter) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  for (const auto& m : RigControl::listModels()) {
    std::string hay = m.mfg + " " + m.name;
    for (auto& c : hay) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (!filter.empty() && hay.find(filter) == std::string::npos) continue;
    std::printf("%5d  %-20s %-28s %-10s %s\n", m.model, m.mfg.c_str(), m.name.c_str(),
                m.version.c_str(), m.status.c_str());
  }
  return 0;
}

int cmdRig(const Opts& o) {
  RigControl rig;
  RigConfig c;
  c.model = o.model;
  c.device = o.device;
  c.baud = o.baudRate;
  c.pttType = "NONE";
  std::string err;
  if (o.debug) rig_set_debug(RIG_DEBUG_TRACE);
  if (!rig.open(c, err)) { std::cerr << err << "\n"; return 1; }
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  RigState s = rig.state();
  std::printf("freq %.0f Hz  mode %s  width %d  ptt %d  %s\n", s.freqHz, s.mode.c_str(), s.widthHz,
              s.ptt, s.error.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Opts o = parse(argc, argv);
  if (o.positional.empty()) {
    std::cerr << "usage: gtty-cli encode|decode|listen|devices|rigs|rig [options]\n";
    return 2;
  }
  const std::string& cmd = o.positional[0];
  if (cmd == "encode") return cmdEncode(o);
  if (cmd == "decode") return cmdDecode(o);
  if (cmd == "listen") return cmdListen(o);
  if (cmd == "devices") return cmdDevices();
  if (cmd == "record") return cmdRecord(o);
  if (cmd == "rigs") return cmdRigs(o);
  if (cmd == "rig") return cmdRig(o);
  std::cerr << "unknown command " << cmd << "\n";
  return 2;
}
