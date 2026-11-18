#include "core/audio/audio_engine.h"

#include <portaudio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

#ifdef GTTY_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif
#ifdef GTTY_HAVE_PULSE
#include <pulse/pulseaudio.h>
#endif

namespace gtty {

namespace {
std::once_flag g_paInit;
void ensurePa() {
  std::call_once(g_paInit, [] { Pa_Initialize(); });
}

#ifdef GTTY_HAVE_ALSA
// Fills inputKind/outputKind/card for a PortAudio ALSA device named "...(hw:C,D)".
// USB audio cards name their mixer controls after the USB terminal types the device reports
// (Mic, Line, Speaker, Headphone, IEC958 ...), so those controls say what each direction is
// wired to. Other cards' controls are card-wide and don't map to one PCM, so for those only
// nothing is added (their PCM names, e.g. "ALC892 Analog"/"HDMI 0", are already in `name`).
void describeAlsa(AudioDeviceInfo& d) {
  int card = -1, dev = -1;
  auto at = d.name.rfind("(hw:");
  if (at == std::string::npos || std::sscanf(d.name.c_str() + at, "(hw:%d,%d)", &card, &dev) != 2) return;
  d.hwId = "hw:" + std::to_string(card) + "," + std::to_string(dev);
  snd_ctl_t* ctl = nullptr;
  std::string hw = "hw:" + std::to_string(card);
  if (snd_ctl_open(&ctl, hw.c_str(), 0) < 0) return;
  snd_ctl_card_info_t* info;
  snd_ctl_card_info_alloca(&info);
  bool usb = false;
  if (snd_ctl_card_info(ctl, info) >= 0) {
    std::string name = snd_ctl_card_info_get_name(info), lng = snd_ctl_card_info_get_longname(info);
    usb = std::string(snd_ctl_card_info_get_driver(info)) == "USB-Audio";
    auto cut = lng.find(" at ");  // drop " at usb-xhci-hcd.4.auto-1.2, full speed"
    if (cut != std::string::npos) lng.resize(cut);
    if (usb && !lng.empty() && lng != name) d.card = lng;  // adds the maker; on-board cards' long names are board IDs
  }
  if (usb) {
    snd_ctl_elem_list_t* list;
    snd_ctl_elem_list_alloca(&list);
    std::set<std::string> capture, playback;  // control-name prefixes, e.g. "Mic", "Speaker"
    if (snd_ctl_elem_list(ctl, list) >= 0) {
      unsigned n = snd_ctl_elem_list_get_count(list);
      if (snd_ctl_elem_list_alloc_space(list, n) >= 0 && snd_ctl_elem_list(ctl, list) >= 0) {
        for (unsigned i = 0; i < snd_ctl_elem_list_get_used(list); ++i) {
          if (snd_ctl_elem_list_get_interface(list, i) != SND_CTL_ELEM_IFACE_MIXER) continue;
          std::string e = snd_ctl_elem_list_get_name(list, i);
          for (const char* dir : {" Capture ", " Playback "}) {
            auto pos = e.find(dir);
            if (pos == std::string::npos || pos == 0) continue;
            std::string what = e.substr(0, pos);
            if (what == "PCM" || what == "Master") continue;  // unnamed feature units: no terminal type
            (dir[1] == 'C' ? capture : playback).insert(what);
          }
        }
      }
      snd_ctl_elem_list_free_space(list);
    }
    // A "Mic Playback" control is the mic monitored into the output, not an output itself.
    for (const auto& c : capture) playback.erase(c);
    auto join = [](const std::set<std::string>& s) {
      std::string r;
      for (std::string k : s) {
        if (k.rfind("IEC958", 0) == 0) k = "S/PDIF" + k.substr(6);
        r += (r.empty() ? "" : ", ") + k;
      }
      return r;
    };
    if (d.maxInputs > 0) d.inputKind = join(capture);
    if (d.maxOutputs > 0) d.outputKind = join(playback);
  }
  snd_ctl_close(ctl);
}
#endif

#ifdef GTTY_HAVE_PULSE
// One output (sink) or input (source) as the sound server lists it.
struct ServerDevice {
  std::string name;         // stable id, e.g. "alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback"
  std::string description;  // what pavucontrol shows, including names set in WirePlumber
  std::string port;         // the active port, e.g. "Microphone", "Speakers"
  std::string vendor;       // device.vendor.name, from udev for USB and PCI cards
  int alsaCard = -1, alsaDevice = -1;  // the ALSA PCM behind it, if any
  std::string alsaCardName;            // ALSA's card name, the start of PortAudio's name for that PCM
  bool input = false;
  bool monitor = false;  // "Monitor of ...": what an output is playing
  bool unplugged = false;  // its port is a jack with nothing in it
  int channels = 0;
  double rate = 0;
};

// Asks the sound server (PipeWire through pipewire-pulse, or PulseAudio) for its outputs and
// inputs. Gives up after a short wait, so a missing or hung server only costs those entries.
std::vector<ServerDevice> queryServerDevices() {
  struct Q {
    std::vector<ServerDevice> devices;
    int pending = 0;
  } q;
  pa_mainloop* ml = pa_mainloop_new();
  if (!ml) return {};
  pa_context* ctx = pa_context_new(pa_mainloop_get_api(ml), "GTTY device list");
  if (!ctx || pa_context_connect(ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
    if (ctx) pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return {};
  }
  static auto add = [](Q& q, const char* name, const char* description, pa_proplist* pl, const auto* port,
                       const pa_sample_spec& ss, bool input, bool monitor) {
    if (!name || !*name) return;
    ServerDevice d;
    d.name = name;
    d.description = description && *description ? description : name;
    if (port) {
      if (port->description) d.port = port->description;
      d.unplugged = port->available == PA_PORT_AVAILABLE_NO;
    }
    if (const char* v = pa_proplist_gets(pl, PA_PROP_DEVICE_VENDOR_NAME)) d.vendor = v;
    const char* card = pa_proplist_gets(pl, "alsa.card");
    const char* dev = pa_proplist_gets(pl, "alsa.device");
    if (card && dev) { d.alsaCard = std::atoi(card); d.alsaDevice = std::atoi(dev); }
    if (const char* cn = pa_proplist_gets(pl, "alsa.card_name")) d.alsaCardName = cn;
    d.input = input;
    d.monitor = monitor;
    d.channels = ss.channels;
    d.rate = ss.rate;
    q.devices.push_back(std::move(d));
  };
  bool asked = false, done = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
  while (!done && std::chrono::steady_clock::now() < deadline) {
    switch (pa_context_get_state(ctx)) {
      case PA_CONTEXT_FAILED:
      case PA_CONTEXT_TERMINATED:
        done = true;
        continue;
      case PA_CONTEXT_READY:
        if (!asked) {
          asked = true;
          if (pa_operation* o = pa_context_get_sink_info_list(ctx, +[](pa_context*, const pa_sink_info* i, int eol, void* u) {
                auto& q = *static_cast<Q*>(u);
                if (eol) { --q.pending; return; }
                add(q, i->name, i->description, i->proplist, i->active_port,
                    i->sample_spec, false, false);
              }, &q)) { ++q.pending; pa_operation_unref(o); }
          if (pa_operation* o = pa_context_get_source_info_list(ctx, +[](pa_context*, const pa_source_info* i, int eol, void* u) {
                auto& q = *static_cast<Q*>(u);
                if (eol) { --q.pending; return; }
                add(q, i->name, i->description, i->proplist, i->active_port,
                    i->sample_spec, true, i->monitor_of_sink != PA_INVALID_INDEX);
              }, &q)) { ++q.pending; pa_operation_unref(o); }
        }
        if (q.pending == 0) { done = true; continue; }
        break;
      default:
        break;
    }
    // Wait at most 50 ms for the server, so the deadline is checked.
    if (pa_mainloop_prepare(ml, 50 * 1000) < 0 || pa_mainloop_poll(ml) < 0 || pa_mainloop_dispatch(ml) < 0) break;
  }
  bool complete = asked && q.pending == 0;
  pa_context_disconnect(ctx);
  pa_context_unref(ctx);
  pa_mainloop_free(ml);
  if (!complete) return {};
  // Hardware first, then other outputs/inputs (Bluetooth, virtual), then empty jacks, then monitors.
  std::stable_sort(q.devices.begin(), q.devices.end(), [](const ServerDevice& a, const ServerDevice& b) {
    return std::make_tuple(a.monitor, a.unplugged, a.alsaCard < 0) < std::make_tuple(b.monitor, b.unplugged, b.alsaCard < 0);
  });
  return q.devices;
}
#endif

// While it lives, PortAudio's "pipewire" or "pulse" ALSA device opens the sound server's device
// `node` instead of the default one: the pipewire ALSA plugin reads PIPEWIRE_NODE when a stream
// is opened, and libpulse (under the pulse plugin) reads PULSE_SINK / PULSE_SOURCE.
class ServerTarget {
 public:
  ServerTarget(int device, const std::string& node, bool input) {
    const PaDeviceInfo* d = node.empty() ? nullptr : Pa_GetDeviceInfo(device);
    if (!d) return;
    const std::string via = d->name;
    var_ = via == "pipewire" ? "PIPEWIRE_NODE" : via == "pulse" ? (input ? "PULSE_SOURCE" : "PULSE_SINK") : "";
    if (var_.empty()) return;
    if (const char* old = std::getenv(var_.c_str())) { had_ = true; old_ = old; }
    ::setenv(var_.c_str(), node.c_str(), 1);
  }
  ~ServerTarget() {
    if (var_.empty()) return;
    if (had_) ::setenv(var_.c_str(), old_.c_str(), 1);
    else ::unsetenv(var_.c_str());
  }
  ServerTarget(const ServerTarget&) = delete;
  ServerTarget& operator=(const ServerTarget&) = delete;

 private:
  std::string var_, old_;
  bool had_ = false;
};

// "Name (hw:C,D)" -> card C and device D; false for other names.
bool parseHw(const std::string& name, int& card, int& dev, size_t& at) {
  at = name.rfind("(hw:");
  return at != std::string::npos && std::sscanf(name.c_str() + at, "(hw:%d,%d)", &card, &dev) == 2;
}
}  // namespace

std::string AudioDeviceInfo::label(bool input) const {
  auto lower = [](std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  auto says = [&](const std::string& text, const std::string& part) {
    return lower(text).find(lower(part)) != std::string::npos;
  };
  const std::string& server = input ? serverInputName : serverOutputName;
  const std::string& port = input ? serverInputPort : serverOutputPort;
  const std::string& kind = !port.empty() ? port : input ? inputKind : outputKind;
  std::string l, details;
  if (!server.empty()) {
    // e.g. "USB Audio Device Mono — Microphone (C-Media Electronics, Inc.)"
    l = server;
    if (!serverVendor.empty() && !says(l, serverVendor)) details = serverVendor;
  } else if (!card.empty()) {
    // No sound server: a USB card by what it reports, e.g. "C-Media Electronics Inc. USB Audio
    // Device — Mic (hw:1,0)", in place of PortAudio's "USB Audio Device: USB Audio (hw:1,0)".
    l = card;
  } else {
    return name;
  }
  if (!kind.empty() && !says(l, kind)) l += " — " + kind;
  if (!hwId.empty()) details += (details.empty() ? "" : ", ") + hwId;
  if (serverUnplugged) details += (details.empty() ? "" : ", ") + std::string("unplugged");
  return details.empty() ? l : l + " (" + details + ")";
}

const AudioDeviceInfo* findAudioDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& name, bool input) {
  if (name.empty()) return nullptr;
  auto usable = [&](const AudioDeviceInfo& d) { return input ? d.maxInputs > 0 : d.maxOutputs > 0; };
  for (const auto& d : devices)
    if (d.name == name && usable(d)) return &d;
  int card, dev;
  size_t at;
  if (!parseHw(name, card, dev, at)) return nullptr;
  // A hardware device saved under another card number: PortAudio names it "<card>: <pcm> (hw:C,D)".
  const std::string stem = name.substr(0, at);
  for (const auto& d : devices) {
    int c, e;
    size_t a;
    if (usable(d) && parseHw(d.name, c, e, a) && e == dev && a == stem.size() && d.name.compare(0, a, stem) == 0)
      return &d;
  }
  // Not listed at all (the sound server has it open, so PortAudio can't): the sound server's
  // device for the same card and PCM, which reaches it through the server instead.
  const std::string cardName = stem.substr(0, stem.find(':'));
  for (const auto& d : devices)
    if (usable(d) && !d.serverNode.empty() && d.serverPcm == dev && !d.serverCardName.empty() && d.serverCardName == cardName)
      return &d;
  return nullptr;
}

AudioEngine::AudioEngine() { ensurePa(); }

AudioEngine::~AudioEngine() {
  closeInput();
  closeOutput();
}

std::vector<AudioDeviceInfo> AudioEngine::listDevices() {
  ensurePa();
  std::vector<AudioDeviceInfo> v;
  int n = Pa_GetDeviceCount();
  for (int i = 0; i < n; ++i) {
    const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
    if (!d) continue;
    const PaHostApiInfo* h = Pa_GetHostApiInfo(d->hostApi);
    AudioDeviceInfo info{};
    info.index = i;
    info.name = d->name;
    info.hostApi = h ? h->name : "?";
    info.maxInputs = d->maxInputChannels;
    info.maxOutputs = d->maxOutputChannels;
    info.defaultSampleRate = d->defaultSampleRate;
    v.push_back(std::move(info));
    if (h && h->type == paALSA) {
      int card, dev;
      size_t at;
      if (parseHw(v.back().name, card, dev, at)) v.back().hwId = "hw:" + std::to_string(card) + "," + std::to_string(dev);
#ifdef GTTY_HAVE_ALSA
      describeAlsa(v.back());
#endif
    }
  }
#ifdef GTTY_HAVE_PULSE
  // The sound server's own outputs and inputs, reached through PortAudio's "pipewire" (or
  // "pulse") device pointed at each one; see ServerTarget. These work while the server has the
  // card open, which the hw: devices don't, and their ids survive cards being renumbered.
  int route = -1;
  for (const char* via : {"pipewire", "pulse"}) {
    for (const auto& d : v)
      if (d.name == via && d.maxInputs > 0 && d.maxOutputs > 0) { route = d.index; break; }
    if (route >= 0) break;
  }
  std::vector<ServerDevice> server = queryServerDevices();
  auto join = [](std::string a, const std::string& b) {
    if (a.empty() || a == b) return b;
    return a.find(b) == std::string::npos ? a + " / " + b : a;
  };
  // Name the hw: devices as the server does.
  for (auto& d : v) {
    int card, dev;
    size_t at;
    if (!parseHw(d.name, card, dev, at)) continue;
    for (const auto& s : server) {
      if (s.monitor || s.alsaCard != card || s.alsaDevice != dev) continue;
      if (s.input && d.maxInputs > 0) { d.serverInputName = join(d.serverInputName, s.description); d.serverInputPort = join(d.serverInputPort, s.port); }
      if (!s.input && d.maxOutputs > 0) { d.serverOutputName = join(d.serverOutputName, s.description); d.serverOutputPort = join(d.serverOutputPort, s.port); }
      if (d.serverVendor.empty()) d.serverVendor = s.vendor;
    }
  }
  if (route >= 0) {
    const std::string api = v[static_cast<size_t>(route)].name == "pipewire" ? "PipeWire" : "PulseAudio";
    std::vector<AudioDeviceInfo> served;
    for (const auto& s : server) {
      AudioDeviceInfo d{};
      d.index = route;
      d.name = kServerPrefix + s.name;
      d.hostApi = api;
      d.maxInputs = s.input ? std::max(1, s.channels) : 0;
      d.maxOutputs = s.input ? 0 : std::max(1, s.channels);
      d.defaultSampleRate = s.rate > 0 ? s.rate : v[static_cast<size_t>(route)].defaultSampleRate;
      d.serverNode = s.name;
      (s.input ? d.serverInputName : d.serverOutputName) = s.description;
      (s.input ? d.serverInputPort : d.serverOutputPort) = s.port;
      d.serverVendor = s.vendor;
      d.serverCardName = s.alsaCardName;
      d.serverPcm = s.alsaDevice;
      d.serverUnplugged = s.unplugged;
      served.push_back(std::move(d));
    }
    v.insert(v.begin(), served.begin(), served.end());
  }
#endif
  return v;
}

int AudioEngine::defaultInputDevice() { ensurePa(); return Pa_GetDefaultInputDevice(); }
int AudioEngine::defaultOutputDevice() { ensurePa(); return Pa_GetDefaultOutputDevice(); }

int AudioEngine::inCallback(const void* in, void*, unsigned long frames,
                            const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* user) {
  auto* self = static_cast<AudioEngine*>(user);
  const float* x = static_cast<const float*>(in);
  if (!x) return paContinue;
  if (flags & paInputOverflow) self->overruns_.fetch_add(1);
  float peak = 0;
  const int ch = self->inChannels_;
  float* s = self->scratch_.data();
  const unsigned long cap = self->scratch_.size();
  // The host decides the block size (see openInput), so take whatever arrives, in
  // pieces if it is larger than the scratch buffer. Nothing is ever skipped here.
  for (unsigned long off = 0; off < frames;) {
    const unsigned long n = std::min(frames - off, cap);
    const float* blk = x + off * ch;
    for (unsigned long i = 0; i < n; ++i) {
      float v;
      if (ch == 1) v = blk[i];
      else if (self->inChannel_ == 0) v = blk[i * ch];
      else if (self->inChannel_ == 1) v = blk[i * ch + 1];
      else { v = 0; for (int c = 0; c < ch; ++c) v += blk[i * ch + c]; v /= ch; }
      s[i] = v;
      peak = std::max(peak, std::fabs(v));
    }
    if (self->rx_.push(s, n) < n) self->overruns_.fetch_add(1);   // the receiver fell behind
    off += n;
  }
  self->inPeak_.store(peak);
  return paContinue;
}

int AudioEngine::outCallback(const void*, void* out, unsigned long frames,
                             const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* user) {
  auto* self = static_cast<AudioEngine*>(user);
  if (flags & paOutputUnderflow) self->underruns_.fetch_add(1);
  float* y = static_cast<float*>(out);
  TxSource* src = self->txSource_.load();
  if (src) src->generate(y, frames);
  else std::fill(y, y + frames, 0.f);
  return paContinue;
}

bool AudioEngine::openInput(int device, double sampleRate, int inChannel, std::string& err, const std::string& serverNode) {
  closeInput();
  ServerTarget target(device, serverNode, true);
  if (device < 0) device = Pa_GetDefaultInputDevice();
  const PaDeviceInfo* d = Pa_GetDeviceInfo(device);
  if (!d) { err = "invalid input device"; return false; }
  if (sampleRate <= 0) sampleRate = d->defaultSampleRate;
  PaStreamParameters p{};
  p.device = device;
  p.channelCount = std::min(2, d->maxInputChannels);
  if (p.channelCount < 1) { err = "device has no inputs"; return false; }
  p.sampleFormat = paFloat32;
  p.suggestedLatency = d->defaultLowInputLatency;
  if (Pa_IsFormatSupported(&p, nullptr, sampleRate) != paFormatIsSupported) {
    sampleRate = d->defaultSampleRate;
  }
  inChannels_ = p.channelCount;
  inChannel_ = inChannel;
  scratch_.assign(8192, 0.f);
  overruns_.store(0);
  // Receive latency does not matter for RTTY; lost samples do. Asking for the device's
  // "low" latency together with a fixed callback size makes PortAudio adapt a host
  // buffer smaller than one callback block, and on some codecs it then overflows all
  // the time: the C-Media codec in a Yaesu FT-710 delivered 58 % of the samples at
  // 44.1 kHz with 1024-frame callbacks, which decodes as garbage under a waterfall that
  // looks normal. So: let the host choose the block size (nothing to adapt, measured
  // 100 % on that codec at any latency) and ask for a generous buffer. The later
  // attempts are fallbacks for a host that refuses the first; the last is what gtty
  // used before, which works on codecs that report a larger low latency.
  struct Attempt { double latency; unsigned long frames; };
  const Attempt attempts[] = {
      {std::max(d->defaultHighInputLatency, 0.1), paFramesPerBufferUnspecified},
      {d->defaultHighInputLatency, paFramesPerBufferUnspecified},
      {d->defaultLowInputLatency, 1024},
  };
  PaError e = paNoError;
  for (const Attempt& a : attempts) {
    p.suggestedLatency = a.latency;
    e = Pa_OpenStream(&in_, &p, nullptr, sampleRate, a.frames, paClipOff, inCallback, this);
    if (e != paNoError) { in_ = nullptr; continue; }
    e = Pa_StartStream(in_);
    if (e == paNoError) break;
    Pa_CloseStream(in_);
    in_ = nullptr;
  }
  if (e != paNoError) { err = Pa_GetErrorText(e); return false; }
  inRate_ = sampleRate;
  rx_.clear();
  return true;
}

bool AudioEngine::openOutput(int device, double sampleRate, std::string& err, const std::string& serverNode) {
  closeOutput();
  ServerTarget target(device, serverNode, false);
  if (device < 0) device = Pa_GetDefaultOutputDevice();
  const PaDeviceInfo* d = Pa_GetDeviceInfo(device);
  if (!d) { err = "invalid output device"; return false; }
  if (sampleRate <= 0) sampleRate = d->defaultSampleRate;
  PaStreamParameters p{};
  p.device = device;
  p.channelCount = 1;
  if (d->maxOutputChannels < 1) { err = "device has no outputs"; return false; }
  p.sampleFormat = paFloat32;
  p.suggestedLatency = d->defaultLowOutputLatency;
  if (Pa_IsFormatSupported(nullptr, &p, sampleRate) != paFormatIsSupported) {
    sampleRate = d->defaultSampleRate;
  }
  underruns_.store(0);
  // As for the input: the host chooses the block size. The buffer is kept short because
  // the transmitter unkeys 120 ms after its queue drains, and whatever is still in this
  // buffer at that moment would be cut off.
  struct Attempt { double latency; unsigned long frames; };
  const Attempt attempts[] = {
      {std::clamp(d->defaultHighOutputLatency, 0.03, 0.06), paFramesPerBufferUnspecified},
      {d->defaultLowOutputLatency, paFramesPerBufferUnspecified},
      {d->defaultLowOutputLatency, 1024},
  };
  PaError e = paNoError;
  for (const Attempt& a : attempts) {
    p.suggestedLatency = a.latency;
    e = Pa_OpenStream(&out_, nullptr, &p, sampleRate, a.frames, paClipOff, outCallback, this);
    if (e != paNoError) { out_ = nullptr; continue; }
    e = Pa_StartStream(out_);
    if (e == paNoError) break;
    Pa_CloseStream(out_);
    out_ = nullptr;
  }
  if (e != paNoError) { err = Pa_GetErrorText(e); return false; }
  outRate_ = sampleRate;
  return true;
}

void AudioEngine::closeInput() {
  if (in_) { Pa_StopStream(in_); Pa_CloseStream(in_); in_ = nullptr; }
}

void AudioEngine::closeOutput() {
  if (out_) { Pa_StopStream(out_); Pa_CloseStream(out_); out_ = nullptr; }
}

}  // namespace gtty
