// libcodec2 FSK engine loopback (skipped when not built with libcodec2).
#include <cstdio>

#include "core/modem/codec2_demod.h"
#include "check.h"
#include "rtty_sim.h"

using namespace gtty;

int main() {
  if (!codec2Available()) { std::printf("codec2 not built in; skipping\n"); return 0; }
  const std::string msg = "CQ CQ DE W1AW W1AW 599 001 TEST K";
  RttyParams p;
  DemodConfig c; c.params = p; c.sampleRate = Receiver::kInternalRate;
  {
    Codec2Demod d(c);
    auto got = demodulateWith(d, modulate("RYRYRYRY " + msg, p, 48000), 48000);
    std::printf("codec2 clean: '%s' (AFC %.1f Hz, %llu chars, %llu framing errors)\n", trim(got).c_str(),
                d.status().afcOffsetHz, (unsigned long long)d.status().chars, (unsigned long long)d.status().framingErrors);
    CHECK(got.find("599 001 TEST K") != std::string::npos);
    size_t at = got.find("CQ");
    CHECK(at != std::string::npos && errors(msg, got.substr(at)) <= 2);
  }
  {
    auto y = modulate("RYRYRYRY " + msg, p, 48000);
    addNoise(y, 6.0, 48000, 3);
    Codec2Demod d(c);
    auto got = demodulateWith(d, y, 48000);
    size_t at = got.find("CQ");
    std::string body = at == std::string::npos ? got : got.substr(at);
    std::printf("codec2 6 dB SNR: '%s' (%d errors)\n", trim(body).c_str(), errors(msg, body));
    CHECK(errors(msg, body) <= 8);
  }
  TEST_MAIN_END();
}
