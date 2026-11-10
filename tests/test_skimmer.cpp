// Skimmer: two stations at different audio frequencies decoded and attributed.
#include <cstdio>
#include <map>

#include "check.h"
#include "rtty_sim.h"

using namespace gtty;

int main() {
  RttyParams p1; p1.markFreq = 1000;
  RttyParams p2; p2.markFreq = 2125;
  auto a = modulate("CQ CQ DE W1AW W1AW K", p1, 48000);
  auto b = modulate("VK3ABC VK3ABC DE JA1XYZ K", p2, 48000);
  size_t n = std::max(a.size(), b.size());
  std::vector<float> mix(n, 0.f);
  for (size_t i = 0; i < n; ++i) mix[i] = 0.5f * ((i < a.size() ? a[i] : 0.f) + (i < b.size() ? b[i] : 0.f));

  EventQueue q;
  Receiver rx(nullptr, &q);
  DemodConfig c; c.params = p2;
  rx.configure(c);
  rx.setSkimmer(true, 500, 2500, 50);
  CHECK(rx.skimmerOn());
  for (size_t pos = 0; pos < mix.size(); pos += 4096)
    rx.processSamples(mix.data() + pos, std::min<size_t>(4096, mix.size() - pos), 48000);
  rx.flushEvents();
  std::map<int, std::string> byChannel;   // rounded to 50 Hz
  Event e;
  while (q.pop(e))
    if (auto* t = std::get_if<SkimmerTextEvent>(&e)) byChannel[static_cast<int>(std::lround(t->markHz / 50.0) * 50)] += t->text;
  std::string near1000, near2125;
  for (const auto& [hz, text] : byChannel) {
    std::printf("skimmer %5d Hz: %s\n", hz, text.c_str());
    if (std::abs(hz - 1000) <= 100) near1000 += text;
    if (std::abs(hz - 2125) <= 100) near2125 += text;
  }
  CHECK(near1000.find("W1AW") != std::string::npos);
  CHECK(near2125.find("JA1XYZ") != std::string::npos);
  CHECK(near1000.find("JA1XYZ") == std::string::npos);
  CHECK(near2125.find("W1AW") == std::string::npos);
  // Nothing decoded far from either signal.
  for (const auto& [hz, text] : byChannel)
    if (std::abs(hz - 1000) > 150 && std::abs(hz - 2125) > 150) CHECK(text.find("W1AW") == std::string::npos && text.find("JA1XYZ") == std::string::npos);
  TEST_MAIN_END();
}
