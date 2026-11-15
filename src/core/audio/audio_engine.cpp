#include "core/audio/audio_engine.h"

#include <portaudio.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace gtty {

namespace {
std::once_flag g_paInit;
void ensurePa() {
  std::call_once(g_paInit, [] { Pa_Initialize(); });
}
}  // namespace

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
    v.push_back({i, d->name, h ? h->name : "?", d->maxInputChannels, d->maxOutputChannels,
                 d->defaultSampleRate});
  }
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

bool AudioEngine::openInput(int device, double sampleRate, int inChannel, std::string& err) {
  closeInput();
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

bool AudioEngine::openOutput(int device, double sampleRate, std::string& err) {
  closeOutput();
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
