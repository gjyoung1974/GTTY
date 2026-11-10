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
  if (self->scratch_.size() < frames) return paContinue;  // never happens: pre-sized
  float* s = self->scratch_.data();
  for (unsigned long i = 0; i < frames; ++i) {
    float v;
    if (ch == 1) v = x[i];
    else if (self->inChannel_ == 0) v = x[i * ch];
    else if (self->inChannel_ == 1) v = x[i * ch + 1];
    else { v = 0; for (int c = 0; c < ch; ++c) v += x[i * ch + c]; v /= ch; }
    s[i] = v;
    peak = std::max(peak, std::fabs(v));
  }
  self->rx_.push(s, frames);
  self->inPeak_.store(peak);
  return paContinue;
}

int AudioEngine::outCallback(const void*, void* out, unsigned long frames,
                             const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) {
  auto* self = static_cast<AudioEngine*>(user);
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
  PaError e = Pa_OpenStream(&in_, &p, nullptr, sampleRate, 1024, paClipOff, inCallback, this);
  if (e != paNoError) { err = Pa_GetErrorText(e); in_ = nullptr; return false; }
  e = Pa_StartStream(in_);
  if (e != paNoError) { err = Pa_GetErrorText(e); Pa_CloseStream(in_); in_ = nullptr; return false; }
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
  PaError e = Pa_OpenStream(&out_, nullptr, &p, sampleRate, 1024, paClipOff, outCallback, this);
  if (e != paNoError) { err = Pa_GetErrorText(e); out_ = nullptr; return false; }
  e = Pa_StartStream(out_);
  if (e != paNoError) { err = Pa_GetErrorText(e); Pa_CloseStream(out_); out_ = nullptr; return false; }
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
