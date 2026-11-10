#pragma once
#include <adwaita.h>

#include <functional>

#include "settings.h"

namespace gtty {

// Shows the preferences dialog; `onApply` runs after the dialog closes with
// the settings written back into `s`.
void showPrefsDialog(GtkWidget* parent, Settings& s, std::function<void()> onApply);

}  // namespace gtty
