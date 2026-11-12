// Configuration profiles: storage layout, legacy migration, clone/rename/delete.
#include <glib.h>
#include <glib/gstdio.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "check.h"
#include "settings.h"

using namespace gtty;

int main() {
  // Point XDG dirs at a scratch directory before GLib caches them.
  char tmpl[] = "/tmp/gtty-profiles-XXXXXX";
  const char* tmp = g_mkdtemp(tmpl);
  CHECK(tmp != nullptr);
  setenv("XDG_CONFIG_HOME", tmp, 1);
  setenv("XDG_DATA_HOME", tmp, 1);

  // Fresh install: only Default, which exists without a file.
  auto names = Profiles::list();
  CHECK_EQ(names.size(), size_t(1));
  CHECK_EQ(names[0], std::string("Default"));
  CHECK_EQ(Profiles::current(), std::string("Default"));
  CHECK(Profiles::exists("Default"));
  CHECK(!Profiles::exists("Contest"));

  // Name validation.
  CHECK(Profiles::validName("Contest"));
  CHECK(Profiles::validName("Field Day 2026"));
  CHECK(!Profiles::validName(""));
  CHECK(!Profiles::validName(".hidden"));
  CHECK(!Profiles::validName("a/b"));
  CHECK(!Profiles::validName("tab\there"));
  CHECK(!Profiles::validName(" padded"));

  // Legacy single-file gtty.conf becomes the Default profile.
  {
    std::ofstream f(Settings::configPath());
    f << "[rtty]\nbaud=50\n\n[station]\ncall=N0CALL\n";
  }
  Settings s;
  s.load();
  CHECK_EQ(s.profile, std::string("Default"));
  CHECK_EQ(s.rtty.baud, 50.0);
  CHECK_EQ(s.myCall, std::string("N0CALL"));
  CHECK(g_file_test(Profiles::path("Default").c_str(), G_FILE_TEST_IS_REGULAR));
  CHECK_EQ(Profiles::current(), std::string("Default"));
  CHECK(!s.logDbPath.empty());

  // Clone carries the settings; saving the clone makes it current.
  std::string err;
  CHECK(Profiles::clone("Default", "Contest", err));
  CHECK(!Profiles::clone("Default", "Contest", err));   // already exists
  CHECK(!Profiles::clone("Nope", "Other", err));        // no source
  CHECK(!Profiles::clone("Default", "bad/name", err));
  Settings c;
  CHECK(c.loadProfile("Contest"));
  CHECK_EQ(c.rtty.baud, 50.0);
  c.rtty.baud = 75;
  CHECK(c.modemOnly);                                  // modem-only view is the default
  CHECK_EQ(c.bands[4].lastHz, 0.0);                    // 20m, not visited yet
  c.modemOnly = false;
  c.bands[4].lastHz = 14091500;
  c.save();
  CHECK_EQ(Profiles::current(), std::string("Contest"));
  Settings again;
  again.load();
  CHECK_EQ(again.profile, std::string("Contest"));
  CHECK_EQ(again.rtty.baud, 75.0);
  CHECK(!again.modemOnly);                             // modem-only view and band memory persist
  CHECK_EQ(again.bands[4].lastHz, 14091500.0);
  CHECK_EQ(again.bands[3].lastHz, 0.0);
  Settings def;
  CHECK(def.loadProfile("Default"));
  CHECK_EQ(def.rtty.baud, 50.0);                       // the source is untouched

  // Rename follows the active profile; Default is immutable.
  CHECK(Profiles::rename("Contest", "Field Day", err));
  CHECK_EQ(Profiles::current(), std::string("Field Day"));
  CHECK(!Profiles::exists("Contest"));
  CHECK(Profiles::exists("Field Day"));
  CHECK(!Profiles::rename("Default", "Main", err));
  CHECK(!Profiles::rename("Field Day", "Default", err));

  // Listing: Default first, then case-insensitive order.
  CHECK(Profiles::clone("Default", "beta", err));
  CHECK(Profiles::clone("Default", "Alpha", err));
  names = Profiles::list();
  CHECK_EQ(names.size(), size_t(4));
  CHECK_EQ(names[0], std::string("Default"));
  CHECK_EQ(names[1], std::string("Alpha"));
  CHECK_EQ(names[2], std::string("beta"));
  CHECK_EQ(names[3], std::string("Field Day"));

  // Deleting the active profile falls back to Default.
  CHECK(Profiles::remove("Field Day", err));
  CHECK_EQ(Profiles::current(), std::string("Default"));
  CHECK(!Profiles::remove("Default", err));
  CHECK(!Profiles::remove("Field Day", err));
  CHECK(Profiles::remove("beta", err));
  CHECK(Profiles::remove("Alpha", err));
  CHECK_EQ(Profiles::list().size(), size_t(1));

  // Profiles::current() ignores a dangling name.
  Profiles::setCurrent("Gone");
  CHECK_EQ(Profiles::current(), std::string("Default"));

  TEST_MAIN_END();
}
