#include <adwaita.h>

#include <memory>

#include "main_window.h"

namespace {
std::unique_ptr<gtty::MainWindow> g_window;

void activate(AdwApplication* app, gpointer) {
  if (!g_window) g_window = std::make_unique<gtty::MainWindow>(app);
  g_window->present();
}
}  // namespace

int main(int argc, char** argv) {
  g_set_prgname("gtty");
  AdwApplication* app = adw_application_new("io.github.gtty", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  int rc = g_application_run(G_APPLICATION(app), argc, argv);
  g_window.reset();
  g_object_unref(app);
  return rc;
}
