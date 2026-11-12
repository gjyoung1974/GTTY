#include "main_window.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>

#include "core/log/grid.h"
#include "prefs_dialog.h"

#ifndef GTTY_VERSION
#define GTTY_VERSION "dev"
#endif

namespace gtty {

namespace {
const char* kFilterNames[] = {"Rig", "250", "300", "400", "500", "600", "800", "1000", "1500", "2000", "2400", "3000", nullptr};
const int kFilterHz[] = {0, 250, 300, 400, 500, 600, 800, 1000, 1500, 2000, 2400, 3000, 0};
const char* kBauds[] = {"45.45", "50", "75", "100", nullptr};
const double kBaudValues[] = {45.45, 50, 75, 100};
const char* kShifts[] = {"170", "200", "425", "450", "850", nullptr};
const char* kManualUrl = "https://github.com/gjyoung1974/GTTY/wiki";
const double kShiftValues[] = {170, 200, 425, 450, 850};

int indexOf(const double* vals, int n, double v) {
  for (int i = 0; i < n; ++i) if (std::fabs(vals[i] - v) < 0.01) return i;
  return 0;
}
std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

MainWindow::MainWindow(AdwApplication* app)
    : rx_(&engine_, &events_), tx_(&engine_, &rig_, &events_) {
  settings_.load();
  rig_.onState = [this](const RigState& s) { events_.push(RigStateEvent{s}); };
  buildUi(app);
  applyTheme();
  applyCss();
  installActions();
  installShortcuts();
  syncControlsFromSettings();
  applyDecoderSettings();

  rebuildProfilesMenu();
  updateTitle();

  std::string err;
  if (log_.open(settings_.logDbPath, err)) openLogPath_ = settings_.logDbPath;
  else toast("Logbook: " + err);
  if (!settings_.scpPath.empty() && scp_.load(settings_.scpPath, err))
    toast("Loaded " + std::to_string(scp_.size()) + " calls from MASTER.SCP");

  openAudio();
  openRig();
  openGridTracker();
  openRpc();
  openSessionLog();
  openCluster();
  setBandMapVisible(settings_.bandMapVisible);
  setModemOnly(settings_.modemOnly);
  applyRttyParams(true);
  rx_.start();
  tickId_ = g_timeout_add(33, onTick, this);
  ctx_.myCall = settings_.myCall; ctx_.myName = settings_.myName;
  ctx_.myQth = settings_.myQth; ctx_.myGrid = settings_.myGrid;
}

MainWindow::~MainWindow() {
  rpc_.stop();
  cluster_.stop();
  if (callLookupId_) g_source_remove(callLookupId_);
  if (tickId_) g_source_remove(tickId_);
  if (sessionFile_.is_open()) { sessionFile_ << "\n=== session end " << Logbook::nowUtc() << " UTC ===\n"; sessionFile_.close(); }
  if (gt_.isOpen()) gt_.send(wsjtx::encodeClose(gt_.id()));
  tx_.abortTx();
  rx_.stop();
  rig_.close();
  engine_.closeInput();
  engine_.closeOutput();
}

// ---------------------------------------------------------------- UI build

void MainWindow::buildUi(AdwApplication* app) {
  window_ = adw_application_window_new(GTK_APPLICATION(app));
  gtk_window_set_title(GTK_WINDOW(window_), "gtty");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 820);
  g_signal_connect(window_, "close-request", G_CALLBACK(onCloseRequest), this);

  GtkWidget* toolbar = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), buildHeader());

  GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start(vbox, 6);
  gtk_widget_set_margin_end(vbox, 6);
  gtk_widget_set_margin_bottom(vbox, 4);

  // Top: waterfall + scope column
  GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(top), wf_.widget());
  gtk_box_append(GTK_BOX(top), buildScopeColumn());
  gtk_widget_set_size_request(top, -1, 230);

  // Middle: RX panes / TX pane in a vertical paned
  GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_paned_set_start_child(GTK_PANED(paned), buildRxPanes());
  gtk_paned_set_end_child(GTK_PANED(paned), buildTxPane());
  gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  // The RX side may hold several engine panes; let it shrink so the TX pane always keeps its minimum.
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), TRUE);
  gtk_paned_set_position(GTK_PANED(paned), 300);

  GtkWidget* outerPaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_vexpand(outerPaned, TRUE);
  gtk_paned_set_start_child(GTK_PANED(outerPaned), top);
  gtk_paned_set_end_child(GTK_PANED(outerPaned), paned);
  gtk_paned_set_resize_start_child(GTK_PANED(outerPaned), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(outerPaned), FALSE);
  gtk_paned_set_position(GTK_PANED(outerPaned), 240);
  gtk_box_append(GTK_BOX(vbox), outerPaned);

  // Kept so the modem-only view can hide them.
  bandBar_ = buildBandBar();
  macroBar_ = buildMacroBar();
  logRow_ = buildLogRow();
  gtk_box_append(GTK_BOX(vbox), bandBar_);
  gtk_box_append(GTK_BOX(vbox), macroBar_);
  gtk_box_append(GTK_BOX(vbox), logRow_);
  gtk_box_append(GTK_BOX(vbox), buildControlRow());
  gtk_box_append(GTK_BOX(vbox), buildStatusBar());

  GtkWidget* hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child(GTK_PANED(hpaned), vbox);
  gtk_paned_set_end_child(GTK_PANED(hpaned), buildBandMap());
  gtk_paned_set_resize_start_child(GTK_PANED(hpaned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(hpaned), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(hpaned), FALSE);
  gtk_paned_set_shrink_end_child(GTK_PANED(hpaned), FALSE);

  // A banner, not a toast, for audio trouble: without an input stream nothing
  // decodes and a four-second toast at start-up is easy to miss.
  audioBanner_ = adw_banner_new("");
  adw_banner_set_button_label(ADW_BANNER(audioBanner_), "Preferences");
  g_signal_connect(audioBanner_, "button-clicked", G_CALLBACK(+[](AdwBanner*, gpointer p) {
    auto* self = static_cast<MainWindow*>(p);
    gtk_widget_activate_action(self->window_, "win.prefs", nullptr);
  }), this);
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(content), audioBanner_);
  gtk_box_append(GTK_BOX(content), hpaned);
  gtk_widget_set_vexpand(hpaned, TRUE);

  toasts_ = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
  adw_toast_overlay_set_child(toasts_, content);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(toasts_));
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), toolbar);

  wf_.onClickTune = [this](double hz) {
    double half = settings_.rtty.shift / 2.0;
    double mark = settings_.rtty.reverse ? hz + half : hz - half;
    double lo = 100 + half, hi = Receiver::kInternalRate / 2 - 100 - half;
    mark = std::clamp(mark, settings_.rtty.reverse ? lo + half : lo, settings_.rtty.reverse ? hi + half : hi);
    settings_.rtty.markFreq = std::round(mark);
    applyRttyParams(true);
  };
  wf_.onMiddleClick = [this](double hz) { toggleNotch(hz); };
  wf_.onNudge = [this](double deltaHz) {
    settings_.rtty.markFreq = std::round(settings_.rtty.markFreq + deltaHz);
    applyRttyParams(true);
  };
  wf_.onRightClickTune = [this](double hz) {
    if (!rig_.isOpen()) { toast("Rig not connected"); return; }
    double delta = hz - settings_.rtty.centerFreq();
    double f = rigState_.freqHz + (lsbLike() ? -delta : delta);
    rig_.setFreq(std::round(f));
    rx_.resetAfc();
  };
}

GtkWidget* MainWindow::buildHeader() {
  GtkWidget* header = adw_header_bar_new();

  GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  freqLabel_ = gtk_label_new("--.---.--");
  gtk_widget_add_css_class(freqLabel_, "freq-display");
  modeLabel_ = gtk_label_new("");
  gtk_widget_add_css_class(modeLabel_, "dim-label");
  pttLabel_ = gtk_label_new("RX");
  gtk_widget_add_css_class(pttLabel_, "ptt-rx");
  gtk_box_append(GTK_BOX(titleBox), freqLabel_);
  gtk_box_append(GTK_BOX(titleBox), modeLabel_);
  gtk_box_append(GTK_BOX(titleBox), pttLabel_);
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), titleBox);

  GMenu* menu = g_menu_new();
  g_menu_append(menu, "_Preferences", "win.prefs");
  profilesMenu_ = g_menu_new();
  g_menu_append_submenu(menu, "Con_figurations", G_MENU_MODEL(profilesMenu_));
  g_menu_append(menu, "Band _map", "win.bandmap");
  g_menu_append(menu, "S_kimmer", "win.skimmer");
  g_menu_append(menu, "Modem-_only view", "win.modem-only");
  g_menu_append(menu, "Send CW _ID", "win.cw-id");
  g_menu_append(menu, "Reset _AFC", "win.reset-afc");
  g_menu_append(menu, "_Clear RX", "win.clear-rx");
  g_menu_append(menu, "Clear all _text", "win.clear-text");
  g_menu_append(menu, "Clear _notches", "win.clear-notches");
  g_menu_append(menu, "_Export ADIF…", "win.export-adif");
  g_menu_append(menu, "_Help", "win.help");
  g_menu_append(menu, "_About gtty", "win.about");
  GtkWidget* mb = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(mb), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(mb), G_MENU_MODEL(menu));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), mb);

  // Band map (spots) sidebar toggle; mirrors the stateful win.bandmap action.
  GtkWidget* bmBtn = gtk_toggle_button_new();
  gtk_button_set_icon_name(GTK_BUTTON(bmBtn), "sidebar-show-right-symbolic");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(bmBtn), "win.bandmap");
  gtk_widget_set_tooltip_text(bmBtn, "Show or hide the band map (DX cluster spots)");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), bmBtn);

  GtkWidget* prefsBtn = gtk_button_new_from_icon_name("emblem-system-symbolic");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(prefsBtn), "win.prefs");
  gtk_widget_set_tooltip_text(prefsBtn, "Preferences (Ctrl+,)");
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), prefsBtn);
  return header;
}

GtkWidget* MainWindow::buildScopeColumn() {
  // Narrow column: just wide enough for the tuning eye, so the waterfall gets the rest.
  GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request(col, 128, -1);
  gtk_widget_set_hexpand(col, FALSE);   // explicit: stops the hexpand of inner labels propagating up
  gtk_box_append(GTK_BOX(col), scope_.widget());
  afcLabel_ = gtk_label_new("AFC 0 Hz");
  gtk_widget_add_css_class(afcLabel_, "caption");
  gtk_box_append(GTK_BOX(col), afcLabel_);
  qualityBar_ = gtk_level_bar_new();
  gtk_widget_set_tooltip_text(qualityBar_, "Decoder signal quality");
  gtk_box_append(GTK_BOX(col), qualityBar_);
  levelBar_ = gtk_level_bar_new();
  gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(levelBar_), GTK_LEVEL_BAR_OFFSET_HIGH, 0.9);
  // Rig receiver filter controls (enabled while a rig is connected).
  rigCtlBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  {
    GtkWidget* r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* l1 = gtk_label_new("Filter");
    gtk_widget_add_css_class(l1, "caption");
    gtk_widget_set_hexpand(l1, TRUE);
    gtk_label_set_xalign(GTK_LABEL(l1), 0.f);
    gtk_box_append(GTK_BOX(r1), l1);
    filterDrop_ = gtk_drop_down_new_from_strings(kFilterNames);
    gtk_widget_set_tooltip_text(filterDrop_, "Rig receiver filter width (Hz)");
    g_signal_connect(filterDrop_, "notify::selected", G_CALLBACK(onFilterNotify), this);
    gtk_box_append(GTK_BOX(r1), filterDrop_);
    gtk_box_append(GTK_BOX(rigCtlBox_), r1);
    ifShiftRow_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* l2 = gtk_label_new("IF shift");
    gtk_widget_add_css_class(l2, "caption");
    gtk_widget_set_hexpand(l2, TRUE);
    gtk_label_set_xalign(GTK_LABEL(l2), 0.f);
    gtk_box_append(GTK_BOX(ifShiftRow_), l2);
    ifShiftSpin_ = gtk_spin_button_new_with_range(-1500, 1500, 50);
    gtk_editable_set_width_chars(GTK_EDITABLE(ifShiftSpin_), 4);
    gtk_widget_set_tooltip_text(ifShiftSpin_, "Rig IF shift / passband tuning (Hz)");
    g_signal_connect(ifShiftSpin_, "value-changed", G_CALLBACK(onIfShiftChanged), this);
    gtk_box_append(GTK_BOX(ifShiftRow_), ifShiftSpin_);
    gtk_box_append(GTK_BOX(rigCtlBox_), ifShiftRow_);
    gtk_widget_set_sensitive(rigCtlBox_, FALSE);
    gtk_widget_set_visible(ifShiftRow_, FALSE);
  }
  gtk_widget_set_tooltip_text(levelBar_, "Audio input level");
  gtk_box_append(GTK_BOX(col), levelBar_);
  gtk_box_append(GTK_BOX(col), rigCtlBox_);
  return col;
}

GtkWidget* MainWindow::buildRxPanes() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  size_t engines = rx_.bank().size();
  size_t n = engines + 1;   // + the merged best-guess slot
  rxViews_.resize(n); rxBufs_.resize(n); rxEndMarks_.resize(n); rxWord_.resize(n);
  callTags_.resize(n); scpTags_.resize(n); echoTags_.resize(n); timeTags_.resize(n);
  rxLineStart_.assign(n, true);
  rxPanes_.assign(n, nullptr);
  modeDrops_.assign(engines, nullptr);
  // Layout: the best-guess pane full width on top, the engine panes side by
  // side beneath it (the way contesters run MMTTY and 2Tone next to each other).
  GtkWidget* engineRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_set_homogeneous(GTK_BOX(engineRow), TRUE);
  gtk_widget_set_vexpand(engineRow, TRUE);
  std::vector<size_t> order{static_cast<size_t>(rx_.voteSlot())};
  for (size_t i = 0; i < engines; ++i) order.push_back(i);
  for (size_t i : order) {
    bool isVotePane = i == static_cast<size_t>(rx_.voteSlot());
    GtkWidget* pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(pane, TRUE);
    gtk_widget_set_hexpand(pane, TRUE);
    rxPanes_[i] = pane;
    if (isVotePane) { gtk_box_append(GTK_BOX(box), pane); gtk_box_append(GTK_BOX(box), engineRow); }
    else gtk_box_append(GTK_BOX(engineRow), pane);
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    bool isVote = i == static_cast<size_t>(rx_.voteSlot());
    std::string title = isVote ? "Best guess (vote of the engines)" : rx_.bank().slot(i).label;
    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(label, "caption");
    gtk_widget_add_css_class(label, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(head), label);
    if (!isVote && i < 2) {
      static const char* modes[] = {"Normal", "Selective", "Flutter", "Custom", nullptr};
      GtkWidget* drop = gtk_drop_down_new_from_strings(modes);
      gtk_widget_add_css_class(drop, "flat");
      gtk_widget_set_tooltip_text(drop, "Decoder behaviour: Normal, Selective (narrow, for crowded bands), Flutter (wide, fast ATC), or the Custom values from Preferences → Decoders");
      g_object_set_data(G_OBJECT(drop), "slot", GINT_TO_POINTER(static_cast<int>(i)));
      g_signal_connect(drop, "notify::selected", G_CALLBACK(onModeNotify), this);
      gtk_box_append(GTK_BOX(head), drop);
      modeDrops_[i] = drop;
    }
    gtk_box_append(GTK_BOX(pane), head);

    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 6);
    gtk_widget_add_css_class(view, "rx-view");
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    rxEndMarks_[i] = gtk_text_buffer_create_mark(buf, nullptr, &end, FALSE);
    callTags_[i] = gtk_text_buffer_create_tag(buf, nullptr, "foreground", "#2a7fff", "weight", PANGO_WEIGHT_BOLD, nullptr);
    scpTags_[i] = gtk_text_buffer_create_tag(buf, nullptr, "foreground", "#1a9c3c", "weight", PANGO_WEIGHT_BOLD, "underline", PANGO_UNDERLINE_SINGLE, nullptr);
    echoTags_[i] = gtk_text_buffer_create_tag(buf, nullptr, "foreground", "#d03030", nullptr);
    timeTags_[i] = gtk_text_buffer_create_tag(buf, nullptr, "foreground", "#8a8a8a", "scale", 0.85, nullptr);
    rxViews_[i] = view; rxBufs_[i] = buf;

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(onRxClicked), this);
    gtk_widget_add_controller(view, GTK_EVENT_CONTROLLER(click));

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(pane), scroll);
  }
  return box;
}

GtkWidget* MainWindow::buildTxPane() {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget* label = gtk_label_new("Transmit (type ahead; Ctrl+T TX, Ctrl+R RX, Esc abort)");
  gtk_widget_add_css_class(label, "caption");
  gtk_widget_add_css_class(label, "dim-label");
  gtk_label_set_xalign(GTK_LABEL(label), 0.f);
  gtk_box_append(GTK_BOX(box), label);
  txView_ = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(txView_), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(txView_), TRUE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(txView_), 6);
  gtk_widget_add_css_class(txView_, "tx-view");
  txBuf_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(txView_));
  GtkTextIter it;
  gtk_text_buffer_get_start_iter(txBuf_, &it);
  txSentMark_ = gtk_text_buffer_create_mark(txBuf_, nullptr, &it, TRUE);
  txSentTag_ = gtk_text_buffer_create_tag(txBuf_, nullptr, "foreground", "#888888", nullptr);
  g_signal_connect(txBuf_, "changed", G_CALLBACK(onTxBufferChanged), this);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), txView_);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(scroll, -1, 90);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(box), scroll);
  return box;
}

GtkWidget* MainWindow::buildMacroBar() {
  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 3);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
  macroButtons_.resize(settings_.macros.size());
  for (size_t i = 0; i < settings_.macros.size(); ++i) {
    GtkWidget* b = gtk_button_new();
    g_object_set_data(G_OBJECT(b), "macro-index", GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(b, "clicked", G_CALLBACK(onMacroClicked), this);
    GtkGesture* rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(onMacroRightClick), this);
    gtk_widget_add_controller(b, GTK_EVENT_CONTROLLER(rc));
    gtk_grid_attach(GTK_GRID(grid), b, static_cast<int>(i % 6), static_cast<int>(i / 6), 1, 1);
    macroButtons_[i] = b;
  }
  return grid;
}

GtkWidget* MainWindow::buildBandBar() {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  GtkWidget* lbl = gtk_label_new("Band");
  gtk_widget_add_css_class(lbl, "dim-label");
  gtk_box_append(GTK_BOX(row), lbl);
  bandButtons_.clear();
  for (size_t i = 0; i < settings_.bands.size(); ++i) {
    GtkWidget* b = gtk_button_new_with_label(settings_.bands[i].label.c_str());
    gtk_widget_add_css_class(b, "band-button");
    gtk_widget_set_hexpand(b, TRUE);
    g_object_set_data(G_OBJECT(b), "band-index", GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(b, "clicked", G_CALLBACK(onBandClicked), this);
    gtk_box_append(GTK_BOX(row), b);
    bandButtons_.push_back(b);
  }
  return row;
}

void MainWindow::onBandClicked(GtkButton* b, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "band-index"));
  if (idx < 0 || idx >= static_cast<int>(self->settings_.bands.size())) return;
  // Return to where the dial last was on that band. A click on the band you
  // are already on goes to the preset, so the segment centre is one more click away.
  const BandPreset& band = self->settings_.bands[idx];
  bool onThisBand = bandForFrequency(self->dialFreqHz()) == band.label;
  bool remembered = band.lastHz > 0 && bandForFrequency(band.lastHz) == band.label;
  self->setDial(!onThisBand && remembered ? band.lastHz : band.hz);
}

void MainWindow::setDial(double hz) {
  if (rig_.isOpen()) {
    if (!rig_.setFreq(hz)) { toast("Rig QSY failed: " + rig_.state().error); return; }
    rigState_.freqHz = hz;
  } else {
    settings_.manualDialHz = hz;
  }
  rx_.resetAfc();
  updateFreqDisplay(rigState_);
  updateDupe();
}

// gtk_widget_set_tooltip_text() on a mapped widget restarts GTK's tooltip hover
// timer for the whole display, so a call made on every rig poll (this runs from
// updateFreqDisplay) meant no tooltip anywhere could ever appear. Only touch the
// property when the text really changes.
void MainWindow::setTooltipIfChanged(GtkWidget* w, const char* text) {
  const char* cur = gtk_widget_get_tooltip_text(w);
  if (cur && text && std::strcmp(cur, text) == 0) return;
  if (!cur && (!text || !*text)) return;
  gtk_widget_set_tooltip_text(w, text && *text ? text : nullptr);
}

void MainWindow::updateBandHighlight() {
  std::string band = bandForFrequency(dialFreqHz());
  if (band != bandMapBand_) { bandMapBand_ = band; bandMapDirty_ = true; }
  for (size_t i = 0; i < bandButtons_.size() && i < settings_.bands.size(); ++i) {
    bool on = !band.empty() && settings_.bands[i].label == band;
    if (on) settings_.bands[i].lastHz = dialFreqHz();   // per-band memory for the band buttons
    if (on) gtk_widget_add_css_class(bandButtons_[i], "band-active");
    else gtk_widget_remove_css_class(bandButtons_[i], "band-active");
    char tip[48];
    snprintf(tip, sizeof tip, "%.1f kHz", settings_.bands[i].hz / 1000.0);
    setTooltipIfChanged(bandButtons_[i], tip);
  }
}

GtkWidget* MainWindow::buildLogRow() {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  auto entry = [&](const char* placeholder, int chars, GtkWidget** out) {
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_editable_set_width_chars(GTK_EDITABLE(e), chars);
    gtk_box_append(GTK_BOX(row), e);
    *out = e;
  };
  entry("Call", 10, &callEntry_);
  gtk_widget_add_css_class(callEntry_, "call-entry");
  g_signal_connect(callEntry_, "changed", G_CALLBACK(onCallChanged), this);
  entry("RST S", 5, &rstSEntry_);
  gtk_editable_set_text(GTK_EDITABLE(rstSEntry_), "599");
  entry("RST R", 5, &rstREntry_);
  gtk_editable_set_text(GTK_EDITABLE(rstREntry_), "599");
  entry("Exch rcvd", 10, &exchEntry_);
  entry("Name", 10, &nameEntry_);
  GtkWidget* serialLabel = gtk_label_new("Serial");
  gtk_widget_add_css_class(serialLabel, "dim-label");
  gtk_box_append(GTK_BOX(row), serialLabel);
  serialSpin_ = gtk_spin_button_new_with_range(1, 9999, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(serialSpin_), 1);
  gtk_box_append(GTK_BOX(row), serialSpin_);
  dupeLabel_ = gtk_label_new("");
  gtk_widget_add_css_class(dupeLabel_, "dupe-label");
  gtk_box_append(GTK_BOX(row), dupeLabel_);
  callInfoLabel_ = gtk_label_new("");   // worked-before / callbook details for the Call field
  gtk_widget_add_css_class(callInfoLabel_, "dim-label");
  gtk_widget_add_css_class(callInfoLabel_, "caption");
  gtk_label_set_xalign(GTK_LABEL(callInfoLabel_), 0.f);
  gtk_label_set_ellipsize(GTK_LABEL(callInfoLabel_), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(callInfoLabel_, TRUE);
  gtk_box_append(GTK_BOX(row), callInfoLabel_);
  qsoCountLabel_ = gtk_label_new("");
  gtk_widget_add_css_class(qsoCountLabel_, "dim-label");
  gtk_box_append(GTK_BOX(row), qsoCountLabel_);
  GtkWidget* logBtn = gtk_button_new_with_label("Log QSO");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(logBtn), "win.log-qso");
  gtk_widget_add_css_class(logBtn, "suggested-action");
  gtk_box_append(GTK_BOX(row), logBtn);
  GtkWidget* clrBtn = gtk_button_new_with_label("Clear");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(clrBtn), "win.clear-qso");
  gtk_box_append(GTK_BOX(row), clrBtn);
  return row;
}

GtkWidget* MainWindow::buildControlRow() {
  GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  txButton_ = gtk_toggle_button_new_with_label("TX");
  gtk_widget_add_css_class(txButton_, "tx-button");
  gtk_widget_set_size_request(txButton_, 90, -1);
  g_signal_connect(txButton_, "toggled", G_CALLBACK(onTxToggled), this);
  gtk_box_append(GTK_BOX(row), txButton_);
  tuneButton_ = gtk_toggle_button_new_with_label("Tune");
  gtk_widget_add_css_class(tuneButton_, "tune-button");
  gtk_widget_set_tooltip_text(tuneButton_, "Key the transmitter with a steady mark tone to set drive and ALC; switches off after the time-out set in Preferences → Transmit");
  g_signal_connect(tuneButton_, "toggled", G_CALLBACK(onTuneToggled), this);
  gtk_box_append(GTK_BOX(row), tuneButton_);
  GtkWidget* abortBtn = gtk_button_new_with_label("Abort");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(abortBtn), "win.abort-tx");
  gtk_widget_add_css_class(abortBtn, "destructive-action");
  gtk_box_append(GTK_BOX(row), abortBtn);
  GtkWidget* clearTextBtn = gtk_button_new_with_label("Clear text");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(clearTextBtn), "win.clear-text");
  gtk_widget_set_tooltip_text(clearTextBtn, "Clear the receive and transmit text areas (Ctrl+K)");
  gtk_box_append(GTK_BOX(row), clearTextBtn);

  gtk_box_append(GTK_BOX(row), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
  gtk_box_append(GTK_BOX(row), gtk_label_new("Baud"));
  baudDrop_ = gtk_drop_down_new_from_strings(kBauds);
  g_signal_connect(baudDrop_, "notify::selected", G_CALLBACK(onControlNotify), this);
  gtk_box_append(GTK_BOX(row), baudDrop_);
  gtk_box_append(GTK_BOX(row), gtk_label_new("Shift"));
  shiftDrop_ = gtk_drop_down_new_from_strings(kShifts);
  g_signal_connect(shiftDrop_, "notify::selected", G_CALLBACK(onControlNotify), this);
  gtk_box_append(GTK_BOX(row), shiftDrop_);
  gtk_box_append(GTK_BOX(row), gtk_label_new("Tones"));
  {
    std::vector<const char*> names;
    for (const auto& tp : tonePresets()) names.push_back(tp.name);
    names.push_back(nullptr);
    tonesDrop_ = gtk_drop_down_new_from_strings(names.data());
    gtk_widget_set_tooltip_text(tonesDrop_, "Mark / space tone preset (edit freely in Preferences → RTTY)");
    g_signal_connect(tonesDrop_, "notify::selected", G_CALLBACK(onTonesNotify), this);
    gtk_box_append(GTK_BOX(row), tonesDrop_);
  }

  auto check = [&](const char* label, const char* tip) {
    GtkWidget* c = gtk_check_button_new_with_label(label);
    gtk_widget_set_tooltip_text(c, tip);
    g_signal_connect(c, "toggled", G_CALLBACK(onControlChanged), this);
    gtk_box_append(GTK_BOX(row), c);
    return c;
  };
  afcCheck_ = check("AFC", "Automatic frequency control");
  atcCheck_ = check("ATC", "Automatic threshold control");
  revCheck_ = check("Rev", "Reverse: space tone below mark. Unticked (default): space above mark, 2125 / 2295");
  netCheck_ = check("Net", "Transmit on the AFC-tracked receive tones");
  autoRxCheck_ = check("Auto RX", "Return to receive automatically after the TX buffer empties (delay in Preferences)");
  autoCqCheck_ = gtk_check_button_new_with_label("Auto CQ");
  gtk_widget_set_tooltip_text(autoCqCheck_, "Repeat the CQ macro with a pause between calls; stops when a callsign is entered or TX is aborted");
  g_signal_connect(autoCqCheck_, "toggled", G_CALLBACK(+[](GtkCheckButton* b, gpointer p) {
    auto* self = static_cast<MainWindow*>(p);
    if (self->suppressControlEvents_) return;
    self->setAutoCq(gtk_check_button_get_active(b));
  }), this);
  gtk_box_append(GTK_BOX(row), autoCqCheck_);

  gtk_box_append(GTK_BOX(row), gtk_label_new("Squelch"));
  squelchScale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.02);
  gtk_widget_set_size_request(squelchScale_, 140, -1);
  gtk_scale_set_draw_value(GTK_SCALE(squelchScale_), FALSE);
  g_signal_connect(squelchScale_, "value-changed", G_CALLBACK(onControlChanged), this);
  gtk_box_append(GTK_BOX(row), squelchScale_);
  return row;
}

GtkWidget* MainWindow::buildStatusBar() {
  statusLabel_ = gtk_label_new("");
  gtk_widget_add_css_class(statusLabel_, "caption");
  gtk_widget_add_css_class(statusLabel_, "dim-label");
  gtk_label_set_xalign(GTK_LABEL(statusLabel_), 0.f);
  gtk_label_set_ellipsize(GTK_LABEL(statusLabel_), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode(GTK_LABEL(statusLabel_), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(statusLabel_), 60);
  gtk_widget_set_hexpand(statusLabel_, TRUE);
  return statusLabel_;
}

void MainWindow::installActions() {
  struct A { const char* name; };
  static const char* names[] = {"prefs", "reset-afc", "clear-rx", "export-adif", "about",
                                "log-qso", "clear-qso", "abort-tx", "start-tx", "stop-tx", "clear-notches",
                                "clear-text", "profile-clone", "profile-rename", "profile-delete",
                                "profile-reset", "cw-id", "help", nullptr};
  for (int i = 0; names[i]; ++i) {
    GSimpleAction* a = g_simple_action_new(names[i], nullptr);
    g_signal_connect(a, "activate", G_CALLBACK(onAction), this);
    g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(a));
  }
  // Radio-style action whose state is the active configuration profile.
  profileAction_ = g_simple_action_new_stateful("profile", G_VARIANT_TYPE_STRING,
                                                g_variant_new_string(settings_.profile.c_str()));
  g_signal_connect(profileAction_, "activate", G_CALLBACK(onProfileSelected), this);
  g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(profileAction_));
  skimmerAction_ = g_simple_action_new_stateful("skimmer", nullptr, g_variant_new_boolean(settings_.skimmerEnabled));
  g_signal_connect(skimmerAction_, "activate", G_CALLBACK(onSkimmerToggled), this);
  g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(skimmerAction_));
  bandMapAction_ = g_simple_action_new_stateful("bandmap", nullptr, g_variant_new_boolean(settings_.bandMapVisible));
  g_signal_connect(bandMapAction_, "activate", G_CALLBACK(onBandMapToggled), this);
  g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(bandMapAction_));
  modemOnlyAction_ = g_simple_action_new_stateful("modem-only", nullptr, g_variant_new_boolean(settings_.modemOnly));
  g_signal_connect(modemOnlyAction_, "activate", G_CALLBACK(onModemOnlyToggled), this);
  g_action_map_add_action(G_ACTION_MAP(window_), G_ACTION(modemOnlyAction_));
  GtkApplication* app = gtk_window_get_application(GTK_WINDOW(window_));
  const char* prefsAccels[] = {"<Control>comma", nullptr};
  gtk_application_set_accels_for_action(app, "win.prefs", prefsAccels);
  const char* txAccels[] = {"<Control>t", nullptr};
  gtk_application_set_accels_for_action(app, "win.start-tx", txAccels);
  const char* rxAccels[] = {"<Control>r", nullptr};
  gtk_application_set_accels_for_action(app, "win.stop-tx", rxAccels);
  const char* abortAccels[] = {"Escape", nullptr};
  gtk_application_set_accels_for_action(app, "win.abort-tx", abortAccels);
  const char* logAccels[] = {"<Control>l", nullptr};
  gtk_application_set_accels_for_action(app, "win.log-qso", logAccels);
  const char* modemOnlyAccels[] = {"<Control>m", nullptr};
  gtk_application_set_accels_for_action(app, "win.modem-only", modemOnlyAccels);
  const char* clearTextAccels[] = {"<Control>k", nullptr};
  gtk_application_set_accels_for_action(app, "win.clear-text", clearTextAccels);
  // Plain F1 runs a macro, so Help takes Ctrl+F1.
  const char* helpAccels[] = {"<Control>F1", nullptr};
  gtk_application_set_accels_for_action(app, "win.help", helpAccels);
}

void MainWindow::installShortcuts() {
  GtkEventController* sc = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(sc), GTK_SHORTCUT_SCOPE_GLOBAL);
  for (int i = 0; i < 12 && i < static_cast<int>(settings_.macros.size()); ++i) {
    char key[8];
    snprintf(key, sizeof key, "F%d", i + 1);
    GtkShortcut* s = gtk_shortcut_new_with_arguments(gtk_shortcut_trigger_parse_string(key),
                                                     gtk_callback_action_new(onShortcut, this, nullptr),
                                                     "i", i);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(sc), s);
  }
  gtk_widget_add_controller(window_, sc);
}

void MainWindow::applyTheme() {
  AdwColorScheme scheme = settings_.theme == 1 ? ADW_COLOR_SCHEME_FORCE_LIGHT
                        : settings_.theme == 2 ? ADW_COLOR_SCHEME_FORCE_DARK
                                               : ADW_COLOR_SCHEME_DEFAULT;
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), scheme);
}

void MainWindow::applyCss() {
  char css[2048];
  snprintf(css, sizeof css,
           ".freq-display { font-family: monospace; font-size: 22px; font-weight: bold; }"
           ".ptt-rx { color: #2a9d2a; font-weight: bold; }"
           ".ptt-tx { color: #d02020; font-weight: bold; }"
           ".rx-view, .tx-view { font-size: %dpx; }"
           ".tx-view { background: alpha(#ffcc00, 0.10); }"
           ".tx-button:checked { background: #d02020; color: white; }"
           ".dupe-label { color: #d02020; font-weight: bold; }"
           ".call-entry { font-weight: bold; text-transform: uppercase; }"
           ".band-button { padding: 2px 4px; min-width: 36px; }"
           ".band-active { background: @accent_bg_color; color: @accent_fg_color; font-weight: bold; }"
           ".tune-button:checked { background: #e0a000; color: black; }"
           ".spot-freq { font-family: monospace; }"
           ".spot-call { font-weight: bold; }"
           ".spot-dupe { opacity: 0.45; }"
           ".spot-new .spot-call { color: @accent_color; }",
           settings_.fontSize);
  GtkCssProvider* p = gtk_css_provider_new();
  gtk_css_provider_load_from_string(p, css);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(p);
}

// ------------------------------------------------------------- runtime glue

void MainWindow::openAudio() {
  auto devices = AudioEngine::listDevices();
  auto find = [&](const std::string& name, bool input) {
    if (name.empty()) return -1;
    for (const auto& d : devices)
      if (d.name == name && (input ? d.maxInputs > 0 : d.maxOutputs > 0)) return d.index;
    return -1;
  };
  // Name the device in messages: the configured one, or what "system default" resolved to.
  auto describe = [](int index, const std::string& configured) {
    return index >= 0 ? "\"" + configured + "\"" : std::string("the system default device");
  };
  bool anyInput = false, anyOutput = false;
  for (const auto& d : devices) { anyInput |= d.maxInputs > 0; anyOutput |= d.maxOutputs > 0; }

  std::string problem;   // first failure, shown in the banner
  std::string err;
  engine_.closeInput();
  engine_.closeOutput();

  int inIdx = find(settings_.inputDevice, true);
  if (!anyInput) {
    problem = "No audio input devices found. Nothing can be decoded until one is available.";
  } else if (!engine_.openInput(inIdx, 0, settings_.inputChannel, err)) {
    problem = "Audio input failed on " + describe(inIdx, settings_.inputDevice) + ": " + err +
              ". Nothing is being decoded.";
  } else if (!settings_.inputDevice.empty() && inIdx < 0) {
    toast("Audio input device \"" + settings_.inputDevice + "\" not found; using the system default");
  }

  int outIdx = find(settings_.outputDevice, false);
  if (!anyOutput) {
    if (problem.empty()) problem = "No audio output devices found. AFSK transmit and the monitor are unavailable.";
  } else if (!engine_.openOutput(outIdx, 0, err)) {
    if (problem.empty())
      problem = "Audio output failed on " + describe(outIdx, settings_.outputDevice) + ": " + err +
                ". AFSK transmit and the monitor are unavailable.";
  } else if (!settings_.outputDevice.empty() && outIdx < 0) {
    toast("Audio output device \"" + settings_.outputDevice + "\" not found; using the system default");
  }
  if (!problem.empty())
    problem += " Choose another device in Preferences → Audio. On a PipeWire system the pipewire-alsa "
               "package must be installed for the default and pipewire devices to work.";
  showAudioProblem(problem);
  std::string txErr;
  TxConfig txc = settings_.tx;
  if (txc.cwIdText.empty()) txc.cwIdText = settings_.myCall;
  if (!tx_.configure(txc, settings_.rtty, txErr)) toast("TX: " + txErr);
}

void MainWindow::openRig() {
  rig_.close();
  rigState_ = RigState{};
  updateFreqDisplay(rigState_);
  if (!settings_.rig.enabled) return;
  std::string err;
  if (!rig_.open(settings_.rig, err)) { toast("Rig: " + err); updateRigControls(); return; }
  rigHasIfShift_ = rig_.hasIfShift();
  if (settings_.rigFilterHz > 0 && !rig_.setWidth(settings_.rigFilterHz)) toast("Rig filter: " + rig_.state().error);
  updateRigControls();
}

void MainWindow::applyRttyParams(bool resetAfc) {
  DemodConfig c = rx_.config();
  c.params = settings_.rtty;
  c.afc = settings_.afc;
  c.atc = settings_.atc;
  c.afcRangeHz = settings_.afcRangeHz;
  c.squelch = settings_.squelch;
  rx_.configure(c);
  if (resetAfc) rx_.resetAfc();
  tx_.setParams(settings_.rtty);
  wf_.setNominalTones(settings_.rtty.markFreq, settings_.rtty.spaceFreq());
  wf_.setTones(settings_.rtty.markFreq, settings_.rtty.spaceFreq());
  wf_.setRange(settings_.waterfallLoHz, settings_.waterfallHiHz);
  updateWaterfallDirection();
  if (tonesDrop_) {
    bool prev = suppressControlEvents_;
    suppressControlEvents_ = true;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(tonesDrop_), tonePresetIndex(settings_.rtty.markFreq, settings_.rtty.spaceFreq()));
    suppressControlEvents_ = prev;
  }
}

void MainWindow::syncControlsFromSettings() {
  suppressControlEvents_ = true;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(baudDrop_), indexOf(kBaudValues, 4, settings_.rtty.baud));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(shiftDrop_), indexOf(kShiftValues, 5, settings_.rtty.shift));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(tonesDrop_), tonePresetIndex(settings_.rtty.markFreq, settings_.rtty.spaceFreq()));
  gtk_check_button_set_active(GTK_CHECK_BUTTON(afcCheck_), settings_.afc);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(atcCheck_), settings_.atc);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(revCheck_), settings_.rtty.reverse);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(netCheck_), settings_.netTx);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(autoRxCheck_), settings_.autoRx);
  gtk_range_set_value(GTK_RANGE(squelchScale_), settings_.squelch);
  for (size_t i = 0; i < macroButtons_.size(); ++i) {
    char lbl[64];
    snprintf(lbl, sizeof lbl, "F%zu %s", i + 1, settings_.macros[i].label.c_str());
    gtk_button_set_label(GTK_BUTTON(macroButtons_[i]), lbl);
    setTooltipIfChanged(macroButtons_[i], settings_.macros[i].text.c_str());
  }
  suppressControlEvents_ = false;
}

void MainWindow::onControlChanged(GtkWidget*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  self->settings_.afc = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->afcCheck_));
  self->settings_.atc = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->atcCheck_));
  self->settings_.rtty.reverse = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->revCheck_));
  self->settings_.netTx = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->netCheck_));
  self->settings_.autoRx = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->autoRxCheck_));
  self->settings_.squelch = static_cast<float>(gtk_range_get_value(GTK_RANGE(self->squelchScale_)));
  self->applyRttyParams(false);
  self->suppressControlEvents_ = true;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(self->tonesDrop_), tonePresetIndex(self->settings_.rtty.markFreq, self->settings_.rtty.spaceFreq()));
  self->suppressControlEvents_ = false;
}

void MainWindow::onControlNotify(GObject*, GParamSpec*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  self->settings_.rtty.baud = kBaudValues[gtk_drop_down_get_selected(GTK_DROP_DOWN(self->baudDrop_))];
  self->settings_.rtty.shift = kShiftValues[gtk_drop_down_get_selected(GTK_DROP_DOWN(self->shiftDrop_))];
  self->applyRttyParams(true);
  self->syncControlsFromSettings();  // tone preset may no longer match
}

void MainWindow::onTonesNotify(GObject*, GParamSpec*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  int i = static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(self->tonesDrop_)));
  if (i <= 0 || i >= static_cast<int>(tonePresets().size())) return;
  self->settings_.rtty.setTones(tonePresets()[i].mark, tonePresets()[i].space);
  self->applyRttyParams(true);
  self->syncControlsFromSettings();  // update Shift and Rev to match
}

gboolean MainWindow::onTick(gpointer p) {
  static_cast<MainWindow*>(p)->poll();
  return G_SOURCE_CONTINUE;
}

void MainWindow::poll() {
  tx_.tick();
  gtTick();
  Event e;
  int budget = 200;
  while (budget-- > 0 && events_.pop(e)) handle(e);
  // Auto RX: drop back to receive once everything typed or queued has gone out.
  if (tx_.transmitting() && !tx_.stopping() && settings_.autoRx && txSentAny_) {
    std::string unsent = unsentTxText();
    bool empty = unsent.empty() && tx_.pending() == 0;
    int64_t now = g_get_monotonic_time();
    int64_t delay = static_cast<int64_t>(settings_.autoRxDelaySec * G_USEC_PER_SEC);
    if (!empty && settings_.txWordMode && tx_.pending() == 0 &&
        unsent.find_first_of(" \n") == std::string::npos) {
      // Only a partial word is left: after the delay, send it rather than hang.
      if (txEmptySince_ == 0) txEmptySince_ = now;
      else if (now - txEmptySince_ > delay) { sendPendingTxText(true); txEmptySince_ = now; }
    } else if (!empty) {
      txEmptySince_ = 0;
    } else if (txEmptySince_ == 0) {
      txEmptySince_ = now;
    } else if (now - txEmptySince_ > delay) {
      stopTx();
    }
  }
  // TX -> RX transition housekeeping.
  {
    bool txOnNow = tx_.transmitting();
    if (prevTxOn_ && !txOnNow) onTxEnded();
    prevTxOn_ = txOnNow;
  }
  // Auto CQ: repeat the configured macro after each pause while nothing is going on.
  if (autoCq_ && !tx_.transmitting()) {
    int64_t now = g_get_monotonic_time();
    if (now - txEndedAt_ > static_cast<int64_t>(settings_.autoCqIntervalSec) * G_USEC_PER_SEC) {
      std::string call = gtk_editable_get_text(GTK_EDITABLE(callEntry_));
      if (!call.empty()) setAutoCq(false);
      else { txEndedAt_ = now; runMacro(settings_.autoCqMacro - 1); }
    }
  }
  if (sessionFile_.is_open()) sessionFile_.flush();
  // TX / Tune buttons reflect the transmitter (graceful stop finishing).
  bool tuning = tx_.tuning();
  bool txOn = tx_.transmitting() && !tuning;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(txButton_)) != txOn ||
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tuneButton_)) != tuning) {
    suppressControlEvents_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(txButton_), txOn);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tuneButton_), tuning);
    suppressControlEvents_ = false;
  }
  std::string ptt = !tx_.transmitting() ? "RX" : tuning ? "TUNE" : tx_.sendingCwId() ? "ID" : "TX";
  if (ptt != pttText_) { pttText_ = ptt; gtk_label_set_text(GTK_LABEL(pttLabel_), ptt.c_str()); }
  // Band map: rebuild at most a few times a second, age spots every 30 s.
  int64_t nowMono = g_get_monotonic_time();
  if (nowMono - spotsAgedAt_ > 30 * G_USEC_PER_SEC) { spotsAgedAt_ = nowMono; ageSpots(); }
  if (bandMapDirty_ && nowMono - bandMapRebuiltAt_ > 300000) rebuildBandMap();
  if (nowMono - skimRebuiltAt_ > 500000 && (skimDirty_ || !skim_.empty())) { ageSkim(); rebuildSkimList(); }
}

void MainWindow::handle(const Event& e) {
  if (auto* t = std::get_if<RxTextEvent>(&e)) {
    appendRx(t->slot, t->text);
  } else if (auto* s = std::get_if<SpectrumEvent>(&e)) {
    wf_.addSpectrum(s->db, s->binHz);
  } else if (auto* sc = std::get_if<ScopeEvent>(&e)) {
    if (sc->slot == 0) scope_.addPoints(sc->points);
  } else if (auto* st = std::get_if<DemodStatusEvent>(&e)) {
    if (st->slot == 0) {
      primaryAfc_ = st->status.afcOffsetHz;
      char buf[64];
      snprintf(buf, sizeof buf, "AFC %+.0f Hz%s", std::round(st->status.afcOffsetHz) + 0.0, st->status.squelched ? "  SQL" : "");
      gtk_label_set_text(GTK_LABEL(afcLabel_), buf);
      gtk_level_bar_set_value(GTK_LEVEL_BAR(qualityBar_), std::clamp(st->status.quality, 0.f, 1.f));
      scope_.setLevels(st->status.markLevel, st->status.spaceLevel);
      wf_.setTones(settings_.rtty.markFreq + primaryAfc_, settings_.rtty.spaceFreq() + primaryAfc_);
    }
  } else if (auto* lv = std::get_if<LevelEvent>(&e)) {
    gtk_level_bar_set_value(GTK_LEVEL_BAR(levelBar_), std::clamp(lv->rxPeak, 0.f, 1.f));
  } else if (auto* rs = std::get_if<RigStateEvent>(&e)) {
    bool wasConnected = rigState_.connected;
    rigState_ = rs->state;
    updateFreqDisplay(rigState_);
    updateWaterfallDirection();
    if (wasConnected != rigState_.connected) updateRigControls();
  } else if (auto* ptt = std::get_if<PttEvent>(&e)) {
    pttText_.clear();   // poll() rewrites the label
    gtk_widget_remove_css_class(pttLabel_, ptt->on ? "ptt-rx" : "ptt-tx");
    gtk_widget_add_css_class(pttLabel_, ptt->on ? "ptt-tx" : "ptt-rx");
    rx_.setMuted(ptt->on);
  } else if (auto* echo = std::get_if<TxEchoEvent>(&e)) {
    txSentAny_ = true;
    appendEcho(echo->text);
  } else if (auto* m = std::get_if<MessageEvent>(&e)) {
    toast(m->text);
  } else if (auto* sp = std::get_if<ClusterSpotEvent>(&e)) {
    addSpot(sp->spot);
  } else if (auto* cs = std::get_if<ClusterStatusEvent>(&e)) {
    gtk_label_set_text(GTK_LABEL(bandMapStatus_), cs->info.c_str());
  } else if (auto* cl = std::get_if<ClusterLineEvent>(&e)) {
    gtk_label_set_text(GTK_LABEL(bandMapStatus_), cl->line.substr(0, 120).c_str());
  } else if (auto* cb = std::get_if<CallbookEvent>(&e)) {
    applyCallbook(cb->result);
  } else if (auto* sk = std::get_if<SkimmerTextEvent>(&e)) {
    onSkimmerText(*sk);
  }
}

bool MainWindow::lsbLike() const {
  if (!rigState_.connected) return true;
  const std::string& m = rigState_.mode;
  return m == "LSB" || m == "RTTY" || m == "PKTLSB" || m == "CWR";
}

void MainWindow::updateWaterfallDirection() {
  int d = settings_.waterfallDirection;
  wf_.setReversed(d == 2 || (d == 0 && lsbLike()));
}

void MainWindow::updateFreqDisplay(const RigState& s) {
  long hz = std::lround(s.connected ? s.freqHz : settings_.manualDialHz);
  char buf[32];
  snprintf(buf, sizeof buf, "%ld.%03ld.%02ld", hz / 1000000, (hz / 1000) % 1000, (hz % 1000) / 10);
  gtk_label_set_text(GTK_LABEL(freqLabel_), buf);
  updateBandHighlight();
  if (!s.connected) {
    gtk_label_set_text(GTK_LABEL(modeLabel_), settings_.rig.enabled ? "rig offline" : "manual");
    return;
  }
  std::string mode = s.mode + (s.widthHz > 0 ? " " + std::to_string(s.widthHz) : "");
  gtk_label_set_text(GTK_LABEL(modeLabel_), mode.c_str());
  if (!s.error.empty()) gtk_label_set_text(GTK_LABEL(statusLabel_), ("rig: " + s.error).c_str());
  // Keep the Filter control in step with what the rig reports.
  int sel = 0;
  for (int i = 1; kFilterHz[i] > 0; ++i) if (kFilterHz[i] == s.widthHz) sel = i;
  if (static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(filterDrop_))) != sel) {
    suppressControlEvents_ = true;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(filterDrop_), sel);
    suppressControlEvents_ = false;
  }
}

void MainWindow::appendRx(int slot, const std::string& text) {
  if (slot < 0 || slot >= static_cast<int>(rxBufs_.size())) return;
  GtkTextBuffer* buf = rxBufs_[slot];
  std::string& word = rxWord_[slot];
  for (char c : text) {
    if (c == '\r') continue;
    if (slot == rx_.primarySlot()) {
      if (c == '\n') { gtReportLine(rxLine_); rxLine_.clear(); }
      else { rxLine_.push_back(c); if (rxLine_.size() > 400) { gtReportLine(rxLine_); rxLine_.clear(); } }
      rxTextLog_.push_back(c);
      if (rxTextLog_.size() > (8u << 20)) rxTextLog_.erase(0, 4u << 20);
      sessionWrite(std::string(1, c), false);
    }
    GtkTextIter end;
    if (rxLineStart_[slot] && c != '\n' && settings_.rxTimestamps) {
      std::string stamp = "[" + Logbook::nowUtc().substr(11, 8) + "] ";
      gtk_text_buffer_get_end_iter(buf, &end);
      gtk_text_buffer_insert_with_tags(buf, &end, stamp.c_str(), -1, timeTags_[slot], nullptr);
    }
    rxLineStart_[slot] = (c == '\n');
    gtk_text_buffer_get_end_iter(buf, &end);
    char s[2] = {c, 0};
    gtk_text_buffer_insert(buf, &end, s, 1);
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '/') {
      word.push_back(c);
    } else {
      if (!word.empty() && looksLikeCallsign(word)) {
        GtkTextIter a, b;
        gtk_text_buffer_get_end_iter(buf, &b);
        gtk_text_iter_backward_chars(&b, 1);  // before the delimiter
        a = b;
        gtk_text_iter_backward_chars(&a, static_cast<int>(word.size()));
        bool known = scp_.loaded() && scp_.contains(upper(word));
        gtk_text_buffer_apply_tag(buf, known ? scpTags_[slot] : callTags_[slot], &a, &b);
      }
      word.clear();
    }
  }
  // Trim very long buffers.
  if (gtk_text_buffer_get_char_count(buf) > 200000) {
    GtkTextIter a, b;
    gtk_text_buffer_get_start_iter(buf, &a);
    gtk_text_buffer_get_iter_at_offset(buf, &b, 50000);
    gtk_text_buffer_delete(buf, &a, &b);
  }
  gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(rxViews_[slot]), rxEndMarks_[slot]);
}

void MainWindow::appendEcho(const std::string& text) {
  if (rxBufs_.empty()) return;
  GtkTextBuffer* buf = rxBufs_[0];
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(buf, &end);
  std::string t;
  for (char c : text) if (c != '\r') t.push_back(c);
  sessionWrite(t, true);
  if (!t.empty()) rxLineStart_[0] = (t.back() == '\n');
  gtk_text_buffer_insert_with_tags(buf, &end, t.c_str(), -1, echoTags_[0], nullptr);
  gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(rxViews_[0]), rxEndMarks_[0]);
}

void MainWindow::clearRx() {
  for (size_t i = 0; i < rxBufs_.size(); ++i) {
    gtk_text_buffer_set_text(rxBufs_[i], "", -1);
    rxWord_[i].clear();
    rxLineStart_[i] = true;
  }
}

// Clear every text area. While transmitting, only the not-yet-sent TX text is
// dropped so the on-air record of what actually went out stays visible.
void MainWindow::clearText() {
  clearRx();
  if (tx_.transmitting()) clearUnsentTx();
  else clearTxBuffer();
}

// ----------------------------------------------------------------------- TX

void MainWindow::startTx() {
  if (tx_.transmitting() && !tx_.stopping()) return;
  RttyParams p = settings_.rtty;
  if (settings_.netTx) p.markFreq += primaryAfc_;
  tx_.setParams(p);
  txSentAny_ = false;
  txEmptySince_ = 0;
  tx_.startTx();
  sendPendingTxText();
  gtk_widget_grab_focus(txView_);
}

void MainWindow::stopTx() {
  if (!tx_.transmitting()) return;
  sendPendingTxText(true);
  tx_.stopTx();
}

void MainWindow::abortTx() {
  setAutoCq(false);
  tx_.abortTx();
  clearTxBuffer();
}

void MainWindow::sendPendingTxText(bool flushAll) {
  if (!tx_.transmitting() || tx_.stopping()) return;
  GtkTextIter a, b;
  gtk_text_buffer_get_iter_at_mark(txBuf_, &a, txSentMark_);
  gtk_text_buffer_get_end_iter(txBuf_, &b);
  if (gtk_text_iter_equal(&a, &b)) return;
  gchar* text = gtk_text_buffer_get_text(txBuf_, &a, &b, FALSE);
  std::string pending = text;
  g_free(text);
  if (settings_.txWordMode && !flushAll) {
    // Send only through the last word boundary so the current word can still be edited.
    size_t cut = pending.find_last_of(" \n");
    if (cut == std::string::npos) return;
    pending.resize(cut + 1);
    b = a;
    gtk_text_iter_forward_chars(&b, static_cast<int>(g_utf8_strlen(pending.c_str(), -1)));
  }
  tx_.sendText(pending);
  gtk_text_buffer_apply_tag(txBuf_, txSentTag_, &a, &b);
  gtk_text_buffer_move_mark(txBuf_, txSentMark_, &b);
}

void MainWindow::insertTxText(const std::string& text) {
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(txBuf_, &end);
  gtk_text_buffer_insert(txBuf_, &end, text.c_str(), -1);
}

void MainWindow::onTxBufferChanged(GtkTextBuffer*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->sendPendingTxText();
}

void MainWindow::onTxToggled(GtkToggleButton* b, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  if (gtk_toggle_button_get_active(b)) self->startTx(); else self->stopTx();
}

void MainWindow::runMacro(int idx) {
  if (idx < 0 || idx >= static_cast<int>(settings_.macros.size())) return;
  ctx_.call = upper(gtk_editable_get_text(GTK_EDITABLE(callEntry_)));
  ctx_.rstSent = gtk_editable_get_text(GTK_EDITABLE(rstSEntry_));
  ctx_.rstRcvd = gtk_editable_get_text(GTK_EDITABLE(rstREntry_));
  ctx_.exchRcvd = gtk_editable_get_text(GTK_EDITABLE(exchEntry_));
  ctx_.name = gtk_editable_get_text(GTK_EDITABLE(nameEntry_));
  ctx_.serial = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(serialSpin_));
  for (const auto& a : expandMacro(settings_.macros[idx].text, ctx_)) {
    switch (a.type) {
      case MacroAction::Type::Text: insertTxText(a.text); break;
      case MacroAction::Type::Tx: startTx(); break;
      case MacroAction::Type::Rx: stopTx(); break;
      case MacroAction::Type::Log: logQso(); break;
      case MacroAction::Type::ClearRx: clearRx(); break;
      case MacroAction::Type::ClearTx: abortTx(); break;
      case MacroAction::Type::IncSerial: gtk_spin_button_spin(GTK_SPIN_BUTTON(serialSpin_), GTK_SPIN_STEP_FORWARD, 1); break;
      case MacroAction::Type::DecSerial: gtk_spin_button_spin(GTK_SPIN_BUTTON(serialSpin_), GTK_SPIN_STEP_BACKWARD, 1); break;
      case MacroAction::Type::CwId: sendCwId(); break;
    }
  }
}

namespace {
void onMacroEditResponse(AdwAlertDialog* d, const char* response, gpointer p) {
  if (g_strcmp0(response, "save") != 0) return;
  auto* cb = static_cast<std::function<void(const std::string&, const std::string&)>*>(p);
  auto* lbl = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(d), "label"));
  auto* tv = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(d), "text"));
  GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
  GtkTextIter s, e;
  gtk_text_buffer_get_bounds(b, &s, &e);
  gchar* t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
  (*cb)(gtk_editable_get_text(GTK_EDITABLE(lbl)), t);
  g_free(t);
}
void freeStdFunction(gpointer p) {
  delete static_cast<std::function<void(const std::string&, const std::string&)>*>(p);
}
}  // namespace

void MainWindow::editMacro(int idx) {
  AdwDialog* dlg = adw_alert_dialog_new("Edit macro", nullptr);
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget* label = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(label), "Button label");
  gtk_editable_set_text(GTK_EDITABLE(label), settings_.macros[idx].label.c_str());
  GtkWidget* text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
  gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), settings_.macros[idx].text.c_str(), -1);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text);
  gtk_widget_set_size_request(scroll, 420, 120);
  GtkWidget* hint = gtk_label_new("Tags: <TX> <RX> <MYCALL> <CALL> <RST> <MYRST> <CNTR> <XCHG> <NAME> <MYNAME> <QTH> <MYQTH> <LOG> <CLRRX> <CLRTX> <INCR> <DECR> <TIME>");
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
  gtk_widget_add_css_class(hint, "caption");
  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), scroll);
  gtk_box_append(GTK_BOX(box), hint);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dlg), box);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dlg), "cancel", "Cancel", "save", "Save", nullptr);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "save", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "save");
  g_object_set_data(G_OBJECT(dlg), "label", label);
  g_object_set_data(G_OBJECT(dlg), "text", text);
  auto* cb = new std::function<void(const std::string&, const std::string&)>(
      [this, idx](const std::string& l, const std::string& t) {
        settings_.macros[idx].label = l;
        settings_.macros[idx].text = t;
        settings_.save();
        syncControlsFromSettings();
      });
  g_signal_connect_data(dlg, "response", G_CALLBACK(onMacroEditResponse), cb,
                        [](gpointer p, GClosure*) { freeStdFunction(p); }, static_cast<GConnectFlags>(0));
  adw_dialog_present(dlg, window_);
}

void MainWindow::onMacroClicked(GtkButton* b, gpointer p) {
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "macro-index"));
  static_cast<MainWindow*>(p)->runMacro(idx);
}

void MainWindow::onMacroRightClick(GtkGestureClick* g, int, double, double, gpointer p) {
  GtkWidget* b = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "macro-index"));
  static_cast<MainWindow*>(p)->editMacro(idx);
}

gboolean MainWindow::onShortcut(GtkWidget*, GVariant* args, gpointer p) {
  int idx = args ? g_variant_get_int32(args) : -1;
  static_cast<MainWindow*>(p)->runMacro(idx);
  return TRUE;
}

// ------------------------------------------------------------------ logging

void MainWindow::pickWord(GtkTextView* view, double x, double y) {
  int bx, by;
  gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, static_cast<int>(x), static_cast<int>(y), &bx, &by);
  GtkTextIter it;
  if (!gtk_text_view_get_iter_at_location(view, &it, bx, by)) return;
  GtkTextBuffer* buf = gtk_text_view_get_buffer(view);
  auto isWordChar = [](gunichar c) { return g_unichar_isalnum(c) || c == '/'; };
  GtkTextIter a = it, b = it;
  while (!gtk_text_iter_is_start(&a)) {
    GtkTextIter prev = a;
    gtk_text_iter_backward_char(&prev);
    if (!isWordChar(gtk_text_iter_get_char(&prev))) break;
    a = prev;
  }
  while (!gtk_text_iter_is_end(&b) && isWordChar(gtk_text_iter_get_char(&b))) gtk_text_iter_forward_char(&b);
  gchar* w = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
  std::string word = upper(w ? w : "");
  g_free(w);
  if (word.empty()) return;
  if (looksLikeCallsign(word)) {
    gtk_editable_set_text(GTK_EDITABLE(callEntry_), word.c_str());
  } else if (word.size() == 3 && word[0] == '5' && std::isdigit(static_cast<unsigned char>(word[1])) && std::isdigit(static_cast<unsigned char>(word[2]))) {
    gtk_editable_set_text(GTK_EDITABLE(rstREntry_), word.c_str());
  } else if (std::all_of(word.begin(), word.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
    gtk_editable_set_text(GTK_EDITABLE(exchEntry_), word.c_str());
  } else if (std::all_of(word.begin(), word.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }) && word.size() >= 2) {
    gtk_editable_set_text(GTK_EDITABLE(nameEntry_), word.c_str());
  }
}

void MainWindow::onRxClicked(GtkGestureClick* g, int, double x, double y, gpointer p) {
  GtkWidget* w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g));
  static_cast<MainWindow*>(p)->pickWord(GTK_TEXT_VIEW(w), x, y);
}

void MainWindow::onCallChanged(GtkEditable*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->updateDupe();
  self->scheduleCallLookup();
  if (self->autoCq_ && gtk_editable_get_text(GTK_EDITABLE(self->callEntry_))[0] != '\0') self->setAutoCq(false);
}

void MainWindow::updateDupe() {
  std::string call = upper(gtk_editable_get_text(GTK_EDITABLE(callEntry_)));
  std::string band = bandForFrequency(dialFreqHz());
  bool dupe = !call.empty() && log_.isDupe(call, band, "RTTY");
  gtk_label_set_text(GTK_LABEL(dupeLabel_), dupe ? "DUPE" : "");
  if (!call.empty() && scp_.loaded() && call.size() >= 3) {
    auto partials = scp_.partials(call, 12);
    std::string s;
    for (const auto& c : partials) { if (!s.empty()) s += "  "; s += c; }
    gtk_label_set_text(GTK_LABEL(statusLabel_), s.empty() ? "" : ("SCP: " + s).c_str());
  }
}

void MainWindow::logQso() {
  Qso q;
  q.call = upper(gtk_editable_get_text(GTK_EDITABLE(callEntry_)));
  if (q.call.empty()) { toast("No callsign to log"); return; }
  q.freqHz = dialFreqHz();
  q.band = bandForFrequency(q.freqHz);
  q.rstSent = gtk_editable_get_text(GTK_EDITABLE(rstSEntry_));
  q.rstRcvd = gtk_editable_get_text(GTK_EDITABLE(rstREntry_));
  q.exchRcvd = gtk_editable_get_text(GTK_EDITABLE(exchEntry_));
  q.name = gtk_editable_get_text(GTK_EDITABLE(nameEntry_));
  if (q.call == ctx_.call) { q.grid = ctx_.grid; q.qth = ctx_.qth; }  // from GridTracker, callbook or a previous QSO
  char ser[8];
  snprintf(ser, sizeof ser, "%03d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(serialSpin_)));
  q.exchSent = ser;
  std::string err;
  if (!log_.add(q, err)) { toast("Log failed: " + err); return; }
  gtReportQso(q);
  toast("Logged " + q.call);
  bandMapDirty_ = true;   // dupe colouring
  gtk_spin_button_spin(GTK_SPIN_BUTTON(serialSpin_), GTK_SPIN_STEP_FORWARD, 1);
  clearQsoFields();
  char cnt[32];
  snprintf(cnt, sizeof cnt, "%d QSOs", log_.count());
  gtk_label_set_text(GTK_LABEL(qsoCountLabel_), cnt);
}

void MainWindow::clearQsoFields() {
  ctx_.call.clear();
  ctx_.grid.clear();
  ctx_.qth.clear();
  lookedUpCall_.clear();
  callInfo_.clear();
  gtk_label_set_text(GTK_LABEL(callInfoLabel_), "");
  gtk_editable_set_text(GTK_EDITABLE(callEntry_), "");
  gtk_editable_set_text(GTK_EDITABLE(rstREntry_), "599");
  gtk_editable_set_text(GTK_EDITABLE(rstSEntry_), "599");
  gtk_editable_set_text(GTK_EDITABLE(exchEntry_), "");
  gtk_editable_set_text(GTK_EDITABLE(nameEntry_), "");
}

void MainWindow::exportAdif() {
  GtkFileDialog* d = gtk_file_dialog_new();
  gtk_file_dialog_set_initial_name(d, "gtty-log.adi");
  gtk_file_dialog_save(d, GTK_WINDOW(window_), nullptr, onAdifSaved, this);
}

void MainWindow::onAdifSaved(GObject* src, GAsyncResult* res, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  GError* err = nullptr;
  GFile* f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
  if (!f) { if (err) g_error_free(err); return; }
  gchar* path = g_file_get_path(f);
  std::string e;
  if (self->log_.exportAdif(path, e)) self->toast(std::string("Exported to ") + path);
  else self->toast("Export failed: " + e);
  g_free(path);
  g_object_unref(f);
}

void MainWindow::showAudioProblem(const std::string& msg) {
  if (!audioBanner_) return;
  adw_banner_set_title(ADW_BANNER(audioBanner_), msg.c_str());
  adw_banner_set_revealed(ADW_BANNER(audioBanner_), !msg.empty());
}

void MainWindow::toast(const std::string& msg) {
  if (!toasts_) return;
  AdwToast* t = adw_toast_new(msg.c_str());
  adw_toast_set_timeout(t, 4);
  adw_toast_overlay_add_toast(toasts_, t);
}

void MainWindow::onAction(GSimpleAction* a, GVariant*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  std::string name = g_action_get_name(G_ACTION(a));
  if (name == "prefs") {
    showPrefsDialog(self->window_, self->settings_, [self] {
      self->settings_.save();
      self->applySettings();
    });
  } else if (name == "profile-clone") self->cloneProfile();
  else if (name == "profile-rename") self->renameProfile();
  else if (name == "profile-delete") self->deleteProfile();
  else if (name == "profile-reset") self->resetProfile();
  else if (name == "cw-id") self->sendCwId();
  else if (name == "reset-afc") self->rx_.resetAfc();
  else if (name == "clear-rx") self->clearRx();
  else if (name == "clear-text") self->clearText();
  else if (name == "clear-notches") self->clearNotches();
  else if (name == "export-adif") self->exportAdif();
  else if (name == "log-qso") self->logQso();
  else if (name == "clear-qso") self->clearQsoFields();
  else if (name == "abort-tx") self->abortTx();
  else if (name == "start-tx") self->startTx();
  else if (name == "stop-tx") self->stopTx();
  else if (name == "help") {
    // The user manual: the GitHub wiki, generated from docs/.
    GtkUriLauncher* l = gtk_uri_launcher_new(kManualUrl);
    gtk_uri_launcher_launch(l, GTK_WINDOW(self->window_), nullptr, nullptr, nullptr);
    g_object_unref(l);
  } else if (name == "about") {
    adw_show_about_dialog(self->window_, "application-name", "gtty", "version", GTTY_VERSION,
                          "developer-name", "gtty contributors",
                          "comments", "Amateur radio RTTY terminal: AFSK/FSK modem, Hamlib rig control, parallel decoders.",
                          "website", kManualUrl,
                          "license-type", GTK_LICENSE_GPL_3_0, nullptr);
  }
}

// ------------------------------------------------- configuration profiles

void MainWindow::applySettings() {
  ctx_.myCall = settings_.myCall; ctx_.myName = settings_.myName;
  ctx_.myQth = settings_.myQth; ctx_.myGrid = settings_.myGrid;
  std::string err;
  if (settings_.logDbPath != openLogPath_) {
    log_.close();
    openLogPath_.clear();
    if (log_.open(settings_.logDbPath, err)) openLogPath_ = settings_.logDbPath;
    else toast("Logbook: " + err);
    char cnt[32];
    snprintf(cnt, sizeof cnt, "%d QSOs", log_.count());
    gtk_label_set_text(GTK_LABEL(qsoCountLabel_), cnt);
    updateDupe();
  }
  if (!settings_.scpPath.empty()) scp_.load(settings_.scpPath, err);
  openAudio();
  openRig();
  openGridTracker();
  openRpc();
  openSessionLog();
  openCluster();
  setBandMapVisible(settings_.bandMapVisible);
  setModemOnly(settings_.modemOnly);
  applyDecoderSettings();
  lookedUpCall_.clear();
  updateFreqDisplay(rigState_);
  applyRttyParams(true);
  syncControlsFromSettings();
  updateBandHighlight();
  applyTheme();
  applyCss();
}

void MainWindow::rebuildProfilesMenu() {
  g_menu_remove_all(profilesMenu_);
  GMenu* list = g_menu_new();
  for (const auto& name : Profiles::list()) {
    GMenuItem* it = g_menu_item_new(name.c_str(), nullptr);
    g_menu_item_set_action_and_target_value(it, "win.profile", g_variant_new_string(name.c_str()));
    g_menu_append_item(list, it);
    g_object_unref(it);
  }
  g_menu_append_section(profilesMenu_, nullptr, G_MENU_MODEL(list));
  g_object_unref(list);
  GMenu* ops = g_menu_new();
  g_menu_append(ops, "_Clone…", "win.profile-clone");
  g_menu_append(ops, "_Rename…", "win.profile-rename");
  g_menu_append(ops, "Reset to _defaults…", "win.profile-reset");
  g_menu_append(ops, "_Delete…", "win.profile-delete");
  g_menu_append_section(profilesMenu_, "Active configuration", G_MENU_MODEL(ops));
  g_object_unref(ops);
  if (profileAction_) g_simple_action_set_state(profileAction_, g_variant_new_string(settings_.profile.c_str()));
  bool isDefault = settings_.profile == Profiles::kDefault;
  for (const char* n : {"profile-rename", "profile-delete"}) {
    GAction* a = g_action_map_lookup_action(G_ACTION_MAP(window_), n);
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), !isDefault);
  }
}

void MainWindow::updateTitle() {
  std::string title = settings_.profile == Profiles::kDefault ? "gtty" : "gtty — " + settings_.profile;
  gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
}

void MainWindow::onProfileSelected(GSimpleAction*, GVariant* name, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (name && g_variant_is_of_type(name, G_VARIANT_TYPE_STRING)) self->switchProfile(g_variant_get_string(name, nullptr));
}

void MainWindow::switchProfile(const std::string& name) {
  if (name == settings_.profile) return;
  if (!Profiles::exists(name)) { toast("No configuration named " + name); rebuildProfilesMenu(); return; }
  settings_.save();              // keep what we are leaving
  abortTx();
  if (autoCq_) setAutoCq(false);
  Settings s;
  s.loadProfile(name);
  settings_ = std::move(s);
  settings_.save();              // records the active profile (and creates Default's file if needed)
  applySettings();
  rebuildProfilesMenu();
  updateTitle();
  toast("Configuration: " + name);
}

void MainWindow::cloneProfile() {
  settings_.save();
  promptName("Clone configuration", "Copy the \"" + settings_.profile + "\" configuration under a new name and switch to it.",
             settings_.profile + " copy", [this](const std::string& to) {
    std::string err;
    if (!Profiles::clone(settings_.profile, to, err)) { toast(err); return; }
    switchProfile(to);
  });
}

void MainWindow::renameProfile() {
  if (settings_.profile == Profiles::kDefault) return;
  promptName("Rename configuration", "New name for \"" + settings_.profile + "\".", settings_.profile,
             [this](const std::string& to) {
    if (to == settings_.profile) return;
    settings_.save();
    std::string err;
    if (!Profiles::rename(settings_.profile, to, err)) { toast(err); return; }
    settings_.profile = to;
    settings_.save();
    rebuildProfilesMenu();
    updateTitle();
  });
}

void MainWindow::deleteProfile() {
  if (settings_.profile == Profiles::kDefault) return;
  std::string victim = settings_.profile;
  confirm("Delete configuration?", "\"" + victim + "\" will be removed and gtty will switch to the Default configuration.",
          "Delete", [this, victim] {
    switchProfile(Profiles::kDefault);
    std::string err;
    if (!Profiles::remove(victim, err)) toast(err);
    rebuildProfilesMenu();
  });
}

void MainWindow::resetProfile() {
  confirm("Reset configuration?", "All settings in \"" + settings_.profile + "\" return to their built-in defaults.",
          "Reset", [this] {
    abortTx();
    if (autoCq_) setAutoCq(false);
    Settings d;
    d.profile = settings_.profile;
    d.setDefaultPaths();
    settings_ = std::move(d);
    settings_.save();
    applySettings();
    toast("Configuration reset to defaults");
  });
}

void MainWindow::promptName(const std::string& title, const std::string& body, const std::string& initial,
                            std::function<void(const std::string&)> done) {
  AdwDialog* dlg = adw_alert_dialog_new(title.c_str(), body.c_str());
  GtkWidget* entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry), initial.c_str());
  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
  adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dlg), entry);
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dlg), "cancel", "_Cancel", "ok", "_OK", nullptr);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "ok", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "ok");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "cancel");
  struct Ctx { MainWindow* self; GtkWidget* entry; std::function<void(const std::string&)> done; };
  auto* ctx = new Ctx{this, entry, std::move(done)};
  g_signal_connect_data(dlg, "response", G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer p) {
    auto* c = static_cast<Ctx*>(p);
    if (g_strcmp0(response, "ok") != 0) return;
    std::string name = gtk_editable_get_text(GTK_EDITABLE(c->entry));
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (!Profiles::validName(name)) { c->self->toast("Invalid configuration name"); return; }
    c->done(name);
  }), ctx, [](gpointer p, GClosure*) { delete static_cast<Ctx*>(p); }, static_cast<GConnectFlags>(0));
  adw_dialog_present(dlg, window_);
  gtk_widget_grab_focus(entry);
}

void MainWindow::confirm(const std::string& title, const std::string& body, const std::string& verb,
                         std::function<void()> done) {
  AdwDialog* dlg = adw_alert_dialog_new(title.c_str(), body.c_str());
  adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dlg), "cancel", "_Cancel", "ok", verb.c_str(), nullptr);
  adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "ok", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "cancel");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "cancel");
  auto* fn = new std::function<void()>(std::move(done));
  g_signal_connect_data(dlg, "response", G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer p) {
    if (g_strcmp0(response, "ok") == 0) (*static_cast<std::function<void()>*>(p))();
  }), fn, [](gpointer p, GClosure*) { delete static_cast<std::function<void()>*>(p); }, static_cast<GConnectFlags>(0));
  adw_dialog_present(dlg, window_);
}

// ------------------------------------------------------------ station aids

GtkWidget* MainWindow::buildBandMap() {
  bandMapBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request(bandMapBox_, 260, -1);
  gtk_widget_set_margin_start(bandMapBox_, 4);
  gtk_widget_set_margin_end(bandMapBox_, 6);
  gtk_widget_set_margin_bottom(bandMapBox_, 4);
  GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  sideStack_ = gtk_stack_new();
  GtkWidget* switcher = gtk_stack_switcher_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(sideStack_));
  gtk_widget_set_hexpand(switcher, TRUE);
  gtk_box_append(GTK_BOX(head), switcher);
  bandMapThisBandCheck_ = gtk_check_button_new_with_label("This band");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(bandMapThisBandCheck_), settings_.bandMapThisBand);
  g_signal_connect(bandMapThisBandCheck_, "toggled", G_CALLBACK(+[](GtkCheckButton* b, gpointer p) {
    auto* self = static_cast<MainWindow*>(p);
    if (self->suppressControlEvents_) return;
    self->settings_.bandMapThisBand = gtk_check_button_get_active(b);
    self->bandMapDirty_ = true;
  }), this);
  gtk_box_append(GTK_BOX(head), bandMapThisBandCheck_);
  GtkWidget* collapse = gtk_button_new_from_icon_name("pan-end-symbolic");
  gtk_widget_add_css_class(collapse, "flat");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(collapse), "win.bandmap");
  gtk_widget_set_tooltip_text(collapse, "Collapse the band map (reopen with the sidebar button in the header bar)");
  gtk_box_append(GTK_BOX(head), collapse);
  gtk_box_append(GTK_BOX(bandMapBox_), head);
  GtkWidget* clusterPage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_stack_add_titled(GTK_STACK(sideStack_), clusterPage, "cluster", "Cluster");
  bandMapStatus_ = gtk_label_new("cluster off");
  gtk_widget_add_css_class(bandMapStatus_, "caption");
  gtk_widget_add_css_class(bandMapStatus_, "dim-label");
  gtk_label_set_xalign(GTK_LABEL(bandMapStatus_), 0.f);
  gtk_label_set_ellipsize(GTK_LABEL(bandMapStatus_), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(clusterPage), bandMapStatus_);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  bandMapList_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(bandMapList_), GTK_SELECTION_NONE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(bandMapList_), TRUE);
  gtk_widget_add_css_class(bandMapList_, "boxed-list");
  g_signal_connect(bandMapList_, "row-activated", G_CALLBACK(onSpotActivated), this);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), bandMapList_);
  gtk_box_append(GTK_BOX(clusterPage), scroll);
  clusterEntry_ = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(clusterEntry_), "cluster command, e.g. sh/dx 20");
  g_signal_connect(clusterEntry_, "activate", G_CALLBACK(+[](GtkEntry* e, gpointer p) {
    auto* self = static_cast<MainWindow*>(p);
    std::string cmd = gtk_editable_get_text(GTK_EDITABLE(e));
    if (cmd.empty()) return;
    if (!self->cluster_.connected()) { self->toast("Cluster not connected"); return; }
    self->cluster_.send(cmd);
    gtk_editable_set_text(GTK_EDITABLE(e), "");
  }), this);
  gtk_box_append(GTK_BOX(clusterPage), clusterEntry_);

  // Skimmer page: every station decoded across the passband.
  GtkWidget* skimPage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_stack_add_titled(GTK_STACK(sideStack_), skimPage, "skimmer", "Skimmer");
  skimStatus_ = gtk_label_new("skimmer off (menu or Preferences → Decoders)");
  gtk_widget_add_css_class(skimStatus_, "caption");
  gtk_widget_add_css_class(skimStatus_, "dim-label");
  gtk_label_set_xalign(GTK_LABEL(skimStatus_), 0.f);
  gtk_label_set_ellipsize(GTK_LABEL(skimStatus_), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(skimPage), skimStatus_);
  GtkWidget* skimScroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(skimScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(skimScroll, TRUE);
  skimList_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(skimList_), GTK_SELECTION_NONE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(skimList_), TRUE);
  gtk_widget_add_css_class(skimList_, "boxed-list");
  g_signal_connect(skimList_, "row-activated", G_CALLBACK(onSkimActivated), this);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(skimScroll), skimList_);
  gtk_box_append(GTK_BOX(skimPage), skimScroll);
  gtk_box_append(GTK_BOX(bandMapBox_), sideStack_);
  return bandMapBox_;
}

// ------------------------------------------------------------------ decoders

void MainWindow::applyDecoderSettings() {
  size_t engines = rx_.bank().size();
  for (size_t i = 0; i < engines && i < settings_.decoders.size(); ++i) {
    const auto& d = settings_.decoders[i];
    if (i < 2) {
      rx_.configureSlot(i, [&](DemodConfig& c) {
        if (d.mode == 3) { c.filterLengthBits = d.filterBits; c.matchedWindow = d.window; c.atcTauSec = d.atcTau; }
        else applyDecoderMode(c, static_cast<DecoderMode>(d.mode));
      });
      if (modeDrops_[i]) {
        suppressControlEvents_ = true;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(modeDrops_[i]), std::clamp(d.mode, 0, 3));
        suppressControlEvents_ = false;
      }
    }
    rx_.setSlotEnabled(i, d.enabled, d.votes);
    if (rxPanes_[i]) gtk_widget_set_visible(rxPanes_[i], d.enabled && d.show);
  }
  if (rxPanes_[rx_.voteSlot()]) gtk_widget_set_visible(rxPanes_[rx_.voteSlot()], settings_.showVotePane);
  rx_.setSkimmer(settings_.skimmerEnabled, settings_.waterfallLoHz, settings_.waterfallHiHz, settings_.skimmerStepHz);
  if (skimmerAction_) g_simple_action_set_state(skimmerAction_, g_variant_new_boolean(settings_.skimmerEnabled));
  if (!settings_.skimmerEnabled) { skim_.clear(); skimDirty_ = true; }
  gtk_label_set_text(GTK_LABEL(skimStatus_), settings_.skimmerEnabled ? "listening across the passband" : "skimmer off (menu or Preferences → Decoders)");
}

void MainWindow::onModeNotify(GObject* drop, GParamSpec*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  int slot = GPOINTER_TO_INT(g_object_get_data(drop, "slot"));
  if (slot < 0 || slot >= static_cast<int>(self->settings_.decoders.size())) return;
  self->settings_.decoders[slot].mode = static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(drop)));
  self->applyDecoderSettings();
}

void MainWindow::onSkimmerToggled(GSimpleAction*, GVariant*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->settings_.skimmerEnabled = !self->settings_.skimmerEnabled;
  self->applyDecoderSettings();
  if (self->settings_.skimmerEnabled) {
    self->settings_.bandMapVisible = true;
    self->setBandMapVisible(true);
    gtk_stack_set_visible_child_name(GTK_STACK(self->sideStack_), "skimmer");
  }
}

void MainWindow::onSkimmerText(const SkimmerTextEvent& e) {
  int64_t now = g_get_monotonic_time();
  SkimEntry* hit = nullptr;
  for (auto& s : skim_)
    if (std::fabs(s.markHz - e.markHz) <= settings_.skimmerStepHz + 5) { hit = &s; break; }
  if (!hit) {
    skim_.push_back({e.markHz, "", "", now, e.quality});
    hit = &skim_.back();
  }
  hit->markHz = 0.8 * hit->markHz + 0.2 * e.markHz;
  hit->lastSeen = now;
  hit->quality = e.quality;
  for (char c : e.text) hit->text.push_back(c == '\n' || c == '\r' ? ' ' : c);
  if (hit->text.size() > 160) hit->text.erase(0, hit->text.size() - 160);
  for (const auto& [pos, len] : findCallsigns(hit->text)) hit->call = upper(hit->text.substr(pos, len));
  skimDirty_ = true;
}

void MainWindow::ageSkim() {
  int64_t cutoff = g_get_monotonic_time() - 90 * G_USEC_PER_SEC;
  size_t before = skim_.size();
  skim_.erase(std::remove_if(skim_.begin(), skim_.end(), [&](const SkimEntry& s) { return s.lastSeen < cutoff; }), skim_.end());
  if (skim_.size() != before) skimDirty_ = true;
}

void MainWindow::rebuildSkimList() {
  if (!skimDirty_) return;
  skimDirty_ = false;
  skimRebuiltAt_ = g_get_monotonic_time();
  while (GtkWidget* c = gtk_widget_get_first_child(skimList_)) gtk_list_box_remove(GTK_LIST_BOX(skimList_), c);
  std::sort(skim_.begin(), skim_.end(), [](const SkimEntry& a, const SkimEntry& b) { return a.markHz < b.markHz; });
  int64_t now = g_get_monotonic_time();
  for (size_t i = 0; i < skim_.size(); ++i) {
    const SkimEntry& s = skim_[i];
    double rf = lsbLike() ? dialFreqHz() - s.markHz : dialFreqHz() + s.markHz;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    char f[32];
    snprintf(f, sizeof f, "%8.1f", rf / 1000.0);
    GtkWidget* freq = gtk_label_new(f);
    gtk_widget_add_css_class(freq, "spot-freq");
    gtk_widget_set_tooltip_text(freq, (std::to_string(static_cast<int>(std::lround(s.markHz))) + " Hz mark tone").c_str());
    gtk_box_append(GTK_BOX(top), freq);
    GtkWidget* call = gtk_label_new(s.call.empty() ? "?" : s.call.c_str());
    gtk_widget_add_css_class(call, "spot-call");
    gtk_label_set_xalign(GTK_LABEL(call), 0.f);
    gtk_widget_set_hexpand(call, TRUE);
    gtk_box_append(GTK_BOX(top), call);
    char age[16];
    snprintf(age, sizeof age, "%lds", static_cast<long>((now - s.lastSeen) / G_USEC_PER_SEC));
    GtkWidget* when = gtk_label_new(age);
    gtk_widget_add_css_class(when, "caption");
    gtk_widget_add_css_class(when, "dim-label");
    gtk_box_append(GTK_BOX(top), when);
    gtk_box_append(GTK_BOX(box), top);
    std::string tail = s.text.size() > 48 ? s.text.substr(s.text.size() - 48) : s.text;
    GtkWidget* info = gtk_label_new(tail.c_str());
    gtk_widget_add_css_class(info, "caption");
    gtk_widget_add_css_class(info, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(info), 0.f);
    gtk_label_set_ellipsize(GTK_LABEL(info), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(box), info);
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "skim-index", GINT_TO_POINTER(static_cast<int>(i)));
    if (!s.call.empty() && log_.isOpen()) {
      if (log_.isDupe(s.call, bandForFrequency(dialFreqHz()), "RTTY")) gtk_widget_add_css_class(row, "spot-dupe");
      else if (log_.find(s.call).empty()) gtk_widget_add_css_class(row, "spot-new");
    }
    gtk_widget_set_tooltip_text(row, "Click: tune the decoders to this signal and take the callsign");
    gtk_list_box_append(GTK_LIST_BOX(skimList_), row);
  }
}

void MainWindow::onSkimActivated(GtkListBox*, GtkListBoxRow* row, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "skim-index"));
  if (idx < 0 || idx >= static_cast<int>(self->skim_.size())) return;
  const SkimEntry s = self->skim_[idx];
  self->settings_.rtty.markFreq = std::round(s.markHz);
  self->applyRttyParams(true);
  self->syncControlsFromSettings();
  if (!s.call.empty() && !self->tx_.transmitting()) {
    gtk_editable_set_text(GTK_EDITABLE(self->callEntry_), s.call.c_str());
    self->setAutoCq(false);
  }
}

void MainWindow::openCluster() {
  cluster_.stop();
  spots_.clear();
  bandMapDirty_ = true;
  ClusterConfig c;
  c.enabled = settings_.clusterEnabled;
  c.host = settings_.clusterHost;
  c.port = settings_.clusterPort;
  c.login = settings_.clusterLogin.empty() ? settings_.myCall : settings_.clusterLogin;
  c.commands = settings_.clusterCommands;
  if (c.enabled && c.login.empty()) { toast("Cluster: set your callsign in Preferences → Station"); c.enabled = false; }
  cluster_.onSpot = [this](const Spot& s) { events_.push(ClusterSpotEvent{s}); };
  cluster_.onStatus = [this](bool on, const std::string& info) { events_.push(ClusterStatusEvent{on, info}); };
  cluster_.onLine = [this](const std::string& line) { events_.push(ClusterLineEvent{line}); };
  cluster_.start(c);
  if (!c.enabled) gtk_label_set_text(GTK_LABEL(bandMapStatus_), "cluster off (Preferences → Cluster)");
  suppressControlEvents_ = true;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(bandMapThisBandCheck_), settings_.bandMapThisBand);
  suppressControlEvents_ = false;

  CallbookConfig cb;
  cb.provider = settings_.callbookProvider == 1 ? CallbookConfig::Provider::Qrz
              : settings_.callbookProvider == 2 ? CallbookConfig::Provider::HamQth : CallbookConfig::Provider::None;
  cb.username = settings_.callbookUser;
  cb.password = settings_.callbookPass;
  callbook_.onResult = [this](const CallbookResult& r) { events_.push(CallbookEvent{r}); };
  callbook_.configure(cb);
  if (cb.provider != CallbookConfig::Provider::None && !Callbook::available()) toast("Callbook lookups need a build with libcurl");
}

void MainWindow::setBandMapVisible(bool on) {
  gtk_widget_set_visible(bandMapBox_, on);
  if (bandMapAction_) g_simple_action_set_state(bandMapAction_, g_variant_new_boolean(on));
}

// Modem-only view: when a contest logger drives gtty over XML-RPC it has its
// own band switching, macros and log entry, so those rows are only clutter.
void MainWindow::setModemOnly(bool on) {
  for (GtkWidget* w : {bandBar_, macroBar_, logRow_})
    if (w) gtk_widget_set_visible(w, !on);
  if (modemOnlyAction_) g_simple_action_set_state(modemOnlyAction_, g_variant_new_boolean(on));
}

void MainWindow::onModemOnlyToggled(GSimpleAction*, GVariant*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->settings_.modemOnly = !self->settings_.modemOnly;
  self->setModemOnly(self->settings_.modemOnly);
}

void MainWindow::onBandMapToggled(GSimpleAction*, GVariant*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->settings_.bandMapVisible = !self->settings_.bandMapVisible;
  self->setBandMapVisible(self->settings_.bandMapVisible);
}

void MainWindow::addSpot(const Spot& s) {
  if (settings_.clusterRttyOnly && !looksLikeRtty(s)) return;
  for (auto& o : spots_) {
    if (o.call == s.call && std::fabs(o.freqHz - s.freqHz) < 1500) {
      o = s;
      std::sort(spots_.begin(), spots_.end(), [](const Spot& a, const Spot& b) { return a.freqHz < b.freqHz; });
      bandMapDirty_ = true;
      return;
    }
  }
  spots_.insert(std::upper_bound(spots_.begin(), spots_.end(), s,
                                 [](const Spot& a, const Spot& b) { return a.freqHz < b.freqHz; }), s);
  if (spots_.size() > 400) ageSpots();
  bandMapDirty_ = true;
}

void MainWindow::ageSpots() {
  int64_t cutoff = g_get_real_time() / G_USEC_PER_SEC - static_cast<int64_t>(settings_.clusterMaxAgeMin) * 60;
  size_t before = spots_.size();
  spots_.erase(std::remove_if(spots_.begin(), spots_.end(), [&](const Spot& s) { return s.receivedAt < cutoff; }), spots_.end());
  if (spots_.size() != before) bandMapDirty_ = true;
}

void MainWindow::rebuildBandMap() {
  bandMapDirty_ = false;
  bandMapRebuiltAt_ = g_get_monotonic_time();
  while (GtkWidget* c = gtk_widget_get_first_child(bandMapList_)) gtk_list_box_remove(GTK_LIST_BOX(bandMapList_), c);
  std::string band = bandForFrequency(dialFreqHz());
  for (size_t i = 0; i < spots_.size(); ++i) {
    const Spot& s = spots_[i];
    std::string spotBand = bandForFrequency(s.freqHz);
    if (settings_.bandMapThisBand && spotBand != band) continue;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    char f[24];
    snprintf(f, sizeof f, "%8.1f", s.freqHz / 1000.0);
    GtkWidget* freq = gtk_label_new(f);
    gtk_widget_add_css_class(freq, "spot-freq");
    gtk_box_append(GTK_BOX(top), freq);
    GtkWidget* call = gtk_label_new(s.call.c_str());
    gtk_widget_add_css_class(call, "spot-call");
    gtk_label_set_xalign(GTK_LABEL(call), 0.f);
    gtk_widget_set_hexpand(call, TRUE);
    gtk_box_append(GTK_BOX(top), call);
    GtkWidget* when = gtk_label_new(s.timeUtc.c_str());
    gtk_widget_add_css_class(when, "caption");
    gtk_widget_add_css_class(when, "dim-label");
    gtk_box_append(GTK_BOX(top), when);
    gtk_box_append(GTK_BOX(box), top);
    std::string sub = s.comment.empty() ? "de " + s.spotter : s.comment + "  de " + s.spotter;
    GtkWidget* info = gtk_label_new(sub.c_str());
    gtk_widget_add_css_class(info, "caption");
    gtk_widget_add_css_class(info, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(info), 0.f);
    gtk_label_set_ellipsize(GTK_LABEL(info), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), info);
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "spot-index", GINT_TO_POINTER(static_cast<int>(i)));
    if (log_.isOpen()) {
      if (log_.isDupe(s.call, spotBand, "RTTY")) gtk_widget_add_css_class(row, "spot-dupe");
      else if (log_.find(s.call).empty()) gtk_widget_add_css_class(row, "spot-new");
    }
    gtk_widget_set_tooltip_text(row, "Click: QSY to the spot and take the callsign");
    gtk_list_box_append(GTK_LIST_BOX(bandMapList_), row);
  }
}

void MainWindow::onSpotActivated(GtkListBox*, GtkListBoxRow* row, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "spot-index"));
  if (idx < 0 || idx >= static_cast<int>(self->spots_.size())) return;
  const Spot s = self->spots_[idx];
  if (self->tx_.transmitting()) { self->toast("Not while transmitting"); return; }
  self->setDial(s.freqHz);
  gtk_editable_set_text(GTK_EDITABLE(self->callEntry_), s.call.c_str());
  self->setAutoCq(false);
  gtk_widget_grab_focus(self->callEntry_);
}

void MainWindow::scheduleCallLookup() {
  if (callLookupId_) g_source_remove(callLookupId_);
  callLookupId_ = g_timeout_add(600, onCallLookupTimeout, this);
}

gboolean MainWindow::onCallLookupTimeout(gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->callLookupId_ = 0;
  self->lookupCall();
  return G_SOURCE_REMOVE;
}

void MainWindow::lookupCall() {
  std::string call = upper(gtk_editable_get_text(GTK_EDITABLE(callEntry_)));
  if (call == lookedUpCall_) return;
  lookedUpCall_ = call;
  callInfo_.clear();
  if (call.size() < 3 || !looksLikeCallsign(call)) { gtk_label_set_text(GTK_LABEL(callInfoLabel_), ""); return; }
  auto prev = log_.find(call);
  if (!prev.empty()) {
    const Qso& last = prev[0];
    callInfo_ = "Wkd " + std::to_string(prev.size()) + "×, last " + last.timeUtc.substr(0, 10) + " " + last.band;
    if (!last.name.empty()) callInfo_ += " " + last.name;
    if (settings_.callbookAutoFill) {
      if (gtk_editable_get_text(GTK_EDITABLE(nameEntry_))[0] == '\0' && !last.name.empty())
        gtk_editable_set_text(GTK_EDITABLE(nameEntry_), last.name.c_str());
      ctx_.call = call;
      if (ctx_.qth.empty()) ctx_.qth = last.qth;
      if (ctx_.grid.empty()) ctx_.grid = last.grid;
    }
  } else {
    callInfo_ = "New";
  }
  gtk_label_set_text(GTK_LABEL(callInfoLabel_), callInfo_.c_str());
  if (callbook_.enabled()) callbook_.lookup(call);
}

void MainWindow::applyCallbook(const CallbookResult& r) {
  if (r.call != lookedUpCall_) return;
  if (!r.found) {
    if (r.error == "Not found") {
      gtk_label_set_text(GTK_LABEL(callInfoLabel_), (callInfo_ + " · not in callbook").c_str());
    } else if (!r.error.empty() && r.error != lastCallbookError_) {
      lastCallbookError_ = r.error;
      toast(r.error);
    }
    return;
  }
  std::string s = r.summary();
  gtk_label_set_text(GTK_LABEL(callInfoLabel_), (callInfo_.empty() ? s : callInfo_ + " · " + s).c_str());
  if (!settings_.callbookAutoFill) return;
  if (gtk_editable_get_text(GTK_EDITABLE(nameEntry_))[0] == '\0' && !r.name.empty())
    gtk_editable_set_text(GTK_EDITABLE(nameEntry_), r.name.substr(0, r.name.find(' ')).c_str());
  ctx_.call = r.call;
  if (!r.qth.empty()) ctx_.qth = r.state.empty() ? r.qth : r.qth + " " + r.state;
  if (ctx_.grid.empty()) ctx_.grid = r.grid;
}

void MainWindow::sendCwId() {
  if (settings_.tx.cwIdText.empty() && settings_.myCall.empty()) { toast("CW ID: set your callsign in Preferences → Station"); return; }
  if (tx_.config().method != TxConfig::Method::Afsk && !settings_.tx.pttViaRig)
    toast("CW ID with FSK keying needs PTT through the rig");
  tx_.sendCwId();
}

void MainWindow::startTune() {
  if (tx_.transmitting() && !tx_.tuning()) { toast("Stop transmitting first"); return; }
  RttyParams p = settings_.rtty;
  if (settings_.netTx) p.markFreq += primaryAfc_;
  tx_.setParams(p);
  if (!tx_.startTune()) toast("Tune failed");
}

void MainWindow::onTuneToggled(GtkToggleButton* b, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  if (gtk_toggle_button_get_active(b)) self->startTune();
  else if (self->tx_.tuning()) self->tx_.abortTx();
}

void MainWindow::updateRigControls() {
  gtk_widget_set_sensitive(rigCtlBox_, rigState_.connected);
  gtk_widget_set_visible(ifShiftRow_, rigState_.connected && rigHasIfShift_);
}

void MainWindow::onFilterNotify(GObject*, GParamSpec*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_) return;
  int i = static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(self->filterDrop_)));
  if (i <= 0 || !self->rig_.isOpen()) return;
  if (!self->rig_.setWidth(kFilterHz[i])) self->toast("Rig filter: " + self->rig_.state().error);
}

void MainWindow::onIfShiftChanged(GtkSpinButton* s, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  if (self->suppressControlEvents_ || !self->rig_.isOpen()) return;
  if (!self->rig_.setIfShift(gtk_spin_button_get_value_as_int(s))) self->toast("IF shift: " + self->rig_.state().error);
}

// ------------------------------------------------------ operating features

void MainWindow::clearTxBuffer() {
  gtk_text_buffer_set_text(txBuf_, "", -1);
  GtkTextIter it;
  gtk_text_buffer_get_start_iter(txBuf_, &it);
  gtk_text_buffer_move_mark(txBuf_, txSentMark_, &it);
}

void MainWindow::clearUnsentTx() {
  GtkTextIter a, b;
  gtk_text_buffer_get_iter_at_mark(txBuf_, &a, txSentMark_);
  gtk_text_buffer_get_end_iter(txBuf_, &b);
  gtk_text_buffer_delete(txBuf_, &a, &b);
}

std::string MainWindow::unsentTxText() const {
  GtkTextIter a, b;
  gtk_text_buffer_get_iter_at_mark(txBuf_, &a, txSentMark_);
  gtk_text_buffer_get_end_iter(txBuf_, &b);
  gchar* t = gtk_text_buffer_get_text(txBuf_, &a, &b, FALSE);
  std::string s = t ? t : "";
  g_free(t);
  return s;
}

void MainWindow::onTxEnded() {
  txEndedAt_ = g_get_monotonic_time();
  if (settings_.clearTxOnRx) clearTxBuffer();
  if (!rxBufs_.empty() && !rxLineStart_[0]) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(rxBufs_[0], &end);
    gtk_text_buffer_insert(rxBufs_[0], &end, "\n", 1);
    rxLineStart_[0] = true;
    sessionWrite("\n", true);
  }
}

void MainWindow::setAutoCq(bool on) {
  autoCq_ = on;
  suppressControlEvents_ = true;
  gtk_check_button_set_active(GTK_CHECK_BUTTON(autoCqCheck_), on);
  suppressControlEvents_ = false;
  if (on) {
    // Start immediately if idle.
    txEndedAt_ = g_get_monotonic_time() - static_cast<int64_t>(settings_.autoCqIntervalSec) * G_USEC_PER_SEC - 1;
    toast("Auto CQ on: F" + std::to_string(settings_.autoCqMacro) + " every " + std::to_string(settings_.autoCqIntervalSec) + " s");
  }
}

void MainWindow::toggleNotch(double hz) {
  for (auto it = notches_.begin(); it != notches_.end(); ++it) {
    if (std::fabs(*it - hz) < 30) { notches_.erase(it); rx_.setNotches(notches_); wf_.setNotches(notches_); return; }
  }
  if (notches_.size() >= 4) notches_.erase(notches_.begin());
  notches_.push_back(std::round(hz));
  rx_.setNotches(notches_);
  wf_.setNotches(notches_);
}

void MainWindow::clearNotches() {
  notches_.clear();
  rx_.setNotches(notches_);
  wf_.setNotches(notches_);
}

void MainWindow::openSessionLog() {
  if (sessionFile_.is_open()) sessionFile_.close();
  if (!settings_.rxLogToFile) return;
  g_mkdir_with_parents(settings_.rxLogDir.c_str(), 0755);
  sessionDate_ = Logbook::nowUtc().substr(0, 10);
  std::string path = settings_.rxLogDir + "/gtty-" + sessionDate_ + ".txt";
  sessionFile_.open(path, std::ios::app);
  if (!sessionFile_) { toast("Cannot write session file " + path); return; }
  sessionFile_ << "\n=== session start " << Logbook::nowUtc() << " UTC ===\n";
}

void MainWindow::sessionWrite(const std::string& text, bool tx) {
  if (!sessionFile_.is_open()) return;
  static bool lineStart = true;
  static bool lastTx = false;
  for (char c : text) {
    if (lineStart) {
      sessionFile_ << "[" << Logbook::nowUtc().substr(11, 8) << (tx ? " TX] " : " RX] ");
      lineStart = false;
    } else if (tx != lastTx) {
      sessionFile_ << (tx ? " <TX> " : " <RX> ");
    }
    lastTx = tx;
    sessionFile_ << c;
    if (c == '\n') lineStart = true;
  }
}

// ------------------------------------------------- fldigi-compatible XML-RPC

namespace {
struct RpcCall {
  MainWindow* self;
  std::string method;
  std::vector<xmlrpc::Value> params;
  std::promise<xmlrpc::Value> promise;
  xmlrpc::Value (MainWindow::*dispatch)(const std::string&, const std::vector<xmlrpc::Value>&);
};
}  // namespace

void MainWindow::openRpc() {
  rpc_.stop();
  if (!settings_.rpcEnabled) return;
  rpc_.setHandler([this](const std::string& m, const std::vector<xmlrpc::Value>& p) { return rpcHandler(m, p); });
  std::string err;
  if (!rpc_.start(settings_.rpcBind, static_cast<uint16_t>(settings_.rpcPort), err)) toast("XML-RPC: " + err);
}

xmlrpc::Value MainWindow::rpcHandler(const std::string& method, const std::vector<xmlrpc::Value>& params) {
  // Runs on the server thread: hop to the GTK main loop and wait for the answer.
  auto* call = new RpcCall{this, method, params, {}, &MainWindow::rpcDispatch};
  auto fut = call->promise.get_future();
  g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer p) -> gboolean {
    auto* c = static_cast<RpcCall*>(p);
    try { c->promise.set_value((c->self->*(c->dispatch))(c->method, c->params)); }
    catch (...) { c->promise.set_exception(std::current_exception()); }
    delete c;
    return G_SOURCE_REMOVE;
  }, call, nullptr);
  if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) throw std::runtime_error("gtty did not respond");
  return fut.get();
}

xmlrpc::Value MainWindow::rpcDispatch(const std::string& m, const std::vector<xmlrpc::Value>& p) {
  using V = xmlrpc::Value;
  auto arg = [&](size_t i) -> const V& { static const V none; return i < p.size() ? p[i] : none; };
  auto entryText = [](GtkWidget* e) { return std::string(gtk_editable_get_text(GTK_EDITABLE(e))); };
  auto setEntry = [](GtkWidget* e, const std::string& v) { gtk_editable_set_text(GTK_EDITABLE(e), v.c_str()); };
  static const char* kMethods[] = {
      "system.listMethods", "system.methodHelp", "system.methodSignature", "fldigi.name", "fldigi.version",
      "fldigi.version_struct", "fldigi.name_version", "fldigi.list", "fldigi.terminate",
      "modem.get_name", "modem.get_names", "modem.set_by_name", "modem.get_id", "modem.get_carrier",
      "modem.set_carrier", "modem.inc_carrier", "modem.get_afc_search_range", "modem.get_bandwidth",
      "main.get_frequency", "main.set_frequency", "main.inc_frequency", "main.get_trx_status", "main.get_trx_state",
      "main.tx", "main.rx", "main.abort", "main.tune", "main.get_afc", "main.set_afc", "main.toggle_afc",
      "main.get_squelch", "main.set_squelch", "main.toggle_squelch", "main.get_squelch_level",
      "main.set_squelch_level", "main.get_reverse", "main.set_reverse", "main.toggle_reverse",
      "main.get_sideband", "main.set_sideband", "main.run_macro", "main.get_max_macro_id", "main.get_status1",
      "main.get_status2", "main.get_lock", "main.set_lock", "main.get_rsid", "main.set_rsid",
      "text.get_rx_length", "text.get_rx", "text.clear_rx", "text.add_tx", "text.add_tx_bytes", "text.clear_tx",
      "log.get_call", "log.set_call", "log.get_name", "log.set_name", "log.get_rst_in", "log.set_rst_in",
      "log.get_rst_out", "log.set_rst_out", "log.get_exchange", "log.set_exchange", "log.get_serial_number",
      "log.set_serial_number", "log.get_serial_number_sent", "log.get_frequency", "log.get_time_on",
      "log.get_time_off", "log.get_band", "log.get_locator", "log.set_locator", "log.get_qth", "log.set_qth",
      "log.get_country", "log.get_province", "log.get_state", "log.get_az", "log.get_notes", "log.set_notes",
      "log.clear",
      "rig.get_name", "rig.set_name", "rig.get_frequency", "rig.set_frequency", "rig.get_modes", "rig.set_modes",
      "rig.get_mode", "rig.set_mode", "rig.get_bandwidths", "rig.set_bandwidths", "rig.get_bandwidth",
      "rig.set_bandwidth", "rig.take_control", "rig.release_control", "rig.get_notch", "rig.set_notch",
      "spot.get_auto", "spot.set_auto", "spot.toggle_auto", "spot.pskrep.get_count", nullptr};

  if (m == "system.listMethods") {
    std::vector<V> v;
    for (int i = 0; kMethods[i]; ++i) v.push_back(V::string(kMethods[i]));
    return V::array(std::move(v));
  }
  if (m == "system.methodHelp") return V::string("gtty: fldigi-compatible method");
  if (m == "system.methodSignature") return V::array({V::string("undef")});
  if (m == "fldigi.name") return V::string("gtty");
  if (m == "fldigi.version") return V::string(GTTY_VERSION);
  if (m == "fldigi.name_version") return V::string(std::string("gtty ") + GTTY_VERSION);
  if (m == "fldigi.version_struct") {
    int maj = 0, min = 0, pat = 0;
    sscanf(GTTY_VERSION, "%d.%d.%d", &maj, &min, &pat);
    return V::strct({{"major", V::integer(maj)}, {"minor", V::integer(min)}, {"patch", V::integer(pat)}});
  }
  if (m == "fldigi.list") {
    std::vector<V> v;
    for (int i = 0; kMethods[i]; ++i)
      v.push_back(V::strct({{"name", V::string(kMethods[i])}, {"signature", V::string("undef")}, {"help", V::string("")}}));
    return V::array(std::move(v));
  }
  if (m == "fldigi.terminate") return V::nil();

  if (m == "modem.get_name") return V::string("RTTY");
  if (m == "modem.get_names") return V::array({V::string("RTTY")});
  if (m == "modem.set_by_name") return V::string("RTTY");
  if (m == "modem.get_id") return V::integer(0);
  if (m == "modem.get_bandwidth") return V::integer(static_cast<int>(settings_.rtty.shift));
  if (m == "modem.get_afc_search_range") return V::integer(static_cast<int>(settings_.afcRangeHz));
  if (m == "modem.get_carrier") return V::integer(static_cast<int64_t>(std::lround(settings_.rtty.centerFreq() + primaryAfc_)));
  if (m == "modem.set_carrier" || m == "modem.inc_carrier") {
    double center = m == "modem.inc_carrier" ? settings_.rtty.centerFreq() + arg(0).asDouble() : arg(0).asDouble();
    double half = settings_.rtty.shift / 2.0;
    settings_.rtty.markFreq = std::round(settings_.rtty.reverse ? center + half : center - half);
    applyRttyParams(true);
    return V::integer(static_cast<int64_t>(std::lround(settings_.rtty.centerFreq())));
  }

  if (m == "main.get_frequency" || m == "rig.get_frequency") return V::number(dialFreqHz());
  if (m == "main.set_frequency" || m == "rig.set_frequency") { double old = dialFreqHz(); setDial(arg(0).asDouble()); return V::number(old); }
  if (m == "main.inc_frequency") { setDial(dialFreqHz() + arg(0).asDouble()); return V::number(dialFreqHz()); }
  if (m == "main.get_trx_status") return V::string(tx_.transmitting() ? "tx" : "rx");
  if (m == "main.get_trx_state") return V::string(tx_.transmitting() ? "TX" : "RX");
  if (m == "main.tx") { startTx(); return V::nil(); }
  if (m == "main.rx") { stopTx(); return V::nil(); }
  if (m == "main.abort") { abortTx(); return V::nil(); }
  if (m == "main.tune") return V::nil();
  if (m == "main.get_afc") return V::boolean(settings_.afc);
  if (m == "main.set_afc" || m == "main.toggle_afc") {
    bool old = settings_.afc;
    settings_.afc = m == "main.toggle_afc" ? !old : arg(0).asBool();
    applyRttyParams(false); syncControlsFromSettings();
    return V::boolean(old);
  }
  if (m == "main.get_squelch") return V::boolean(settings_.squelch > 0);
  if (m == "main.set_squelch" || m == "main.toggle_squelch") {
    bool old = settings_.squelch > 0;
    bool on = m == "main.toggle_squelch" ? !old : arg(0).asBool();
    settings_.squelch = on ? (settings_.squelch > 0 ? settings_.squelch : 0.5f) : 0.f;
    applyRttyParams(false); syncControlsFromSettings();
    return V::boolean(old);
  }
  if (m == "main.get_squelch_level") return V::number(settings_.squelch * 100.0);
  if (m == "main.set_squelch_level") {
    double old = settings_.squelch * 100.0;
    settings_.squelch = static_cast<float>(std::clamp(arg(0).asDouble() / 100.0, 0.0, 1.0));
    applyRttyParams(false); syncControlsFromSettings();
    return V::number(old);
  }
  if (m == "main.get_reverse") return V::boolean(settings_.rtty.reverse);
  if (m == "main.set_reverse" || m == "main.toggle_reverse") {
    bool old = settings_.rtty.reverse;
    settings_.rtty.reverse = m == "main.toggle_reverse" ? !old : arg(0).asBool();
    applyRttyParams(true); syncControlsFromSettings();
    return V::boolean(old);
  }
  if (m == "main.get_sideband") {
    return V::string(rigState_.connected && !lsbLike() ? "USB" : "LSB");
  }
  if (m == "main.set_sideband") return V::nil();
  if (m == "main.run_macro") { runMacro(static_cast<int>(arg(0).asInt())); return V::nil(); }
  if (m == "main.get_max_macro_id") return V::integer(static_cast<int64_t>(settings_.macros.size()) - 1);
  if (m == "main.get_status1" || m == "main.get_status2") return V::string("");
  if (m == "main.get_lock" || m == "main.get_rsid") return V::boolean(false);
  if (m == "main.set_lock" || m == "main.set_rsid") return V::boolean(false);

  if (m == "text.get_rx_length") return V::integer(static_cast<int64_t>(rxTextLog_.size()));
  if (m == "text.get_rx") {
    int64_t start = arg(0).asInt(), len = arg(1).asInt();
    if (start < 0) start = 0;
    if (start > static_cast<int64_t>(rxTextLog_.size())) start = static_cast<int64_t>(rxTextLog_.size());
    if (len < 0 || start + len > static_cast<int64_t>(rxTextLog_.size())) len = static_cast<int64_t>(rxTextLog_.size()) - start;
    return V::base64(rxTextLog_.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
  }
  if (m == "text.clear_rx") { clearRx(); rxTextLog_.clear(); return V::nil(); }
  if (m == "text.add_tx" || m == "text.add_tx_bytes") { insertTxText(arg(0).asString()); return V::nil(); }
  if (m == "text.clear_tx") { clearUnsentTx(); return V::nil(); }

  if (m == "log.get_call") return V::string(entryText(callEntry_));
  if (m == "log.set_call") { setEntry(callEntry_, upper(arg(0).asString())); return V::nil(); }
  if (m == "log.get_name") return V::string(entryText(nameEntry_));
  if (m == "log.set_name") { setEntry(nameEntry_, arg(0).asString()); return V::nil(); }
  if (m == "log.get_rst_in") return V::string(entryText(rstREntry_));
  if (m == "log.set_rst_in") { setEntry(rstREntry_, arg(0).asString()); return V::nil(); }
  if (m == "log.get_rst_out") return V::string(entryText(rstSEntry_));
  if (m == "log.set_rst_out") { setEntry(rstSEntry_, arg(0).asString()); return V::nil(); }
  if (m == "log.get_exchange") return V::string(entryText(exchEntry_));
  if (m == "log.set_exchange") { setEntry(exchEntry_, arg(0).asString()); return V::nil(); }
  if (m == "log.get_serial_number" || m == "log.get_serial_number_sent") {
    char buf[8]; snprintf(buf, sizeof buf, "%03d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(serialSpin_)));
    return V::string(buf);
  }
  if (m == "log.set_serial_number") { gtk_spin_button_set_value(GTK_SPIN_BUTTON(serialSpin_), arg(0).asDouble()); return V::nil(); }
  if (m == "log.get_frequency") { char buf[32]; snprintf(buf, sizeof buf, "%.0f", dialFreqHz()); return V::string(buf); }
  if (m == "log.get_time_on" || m == "log.get_time_off") return V::string(Logbook::nowUtc().substr(11, 2) + Logbook::nowUtc().substr(14, 2));
  if (m == "log.get_band") return V::string(bandForFrequency(dialFreqHz()));
  if (m == "log.get_locator") return V::string(ctx_.grid);
  if (m == "log.set_locator") { ctx_.grid = arg(0).asString(); return V::nil(); }
  if (m == "log.get_qth") return V::string(ctx_.qth);
  if (m == "log.set_qth") { ctx_.qth = arg(0).asString(); return V::nil(); }
  if (m == "log.get_country" || m == "log.get_province" || m == "log.get_state" || m == "log.get_az" || m == "log.get_notes") return V::string("");
  if (m == "log.set_notes") return V::nil();
  if (m == "log.clear") { clearQsoFields(); return V::nil(); }

  if (m == "rig.get_name") {
    if (!rpcRigName_.empty()) return V::string(rpcRigName_);
    if (!rig_.isOpen()) return V::string("none");
    for (const auto& mi : RigControl::listModels()) if (mi.model == settings_.rig.model) return V::string(mi.mfg + " " + mi.name);
    return V::string("rig");
  }
  if (m == "rig.set_name") { rpcRigName_ = arg(0).asString(); return V::nil(); }
  if (m == "rig.get_modes") return V::array({V::string("RTTY"), V::string("RTTYR"), V::string("USB"), V::string("LSB"), V::string("PKTUSB"), V::string("PKTLSB")});
  if (m == "rig.set_modes" || m == "rig.set_bandwidths" || m == "rig.set_bandwidth") return V::nil();
  if (m == "rig.get_mode") return V::string(rigState_.connected ? rigState_.mode : "RTTY");
  if (m == "rig.set_mode") { if (rig_.isOpen()) rig_.setMode(arg(0).asString()); return V::nil(); }
  if (m == "rig.get_bandwidths") return V::array({});
  if (m == "rig.get_bandwidth") return V::string("");
  if (m == "rig.take_control" || m == "rig.release_control") return V::nil();
  if (m == "rig.get_notch") return V::integer(notches_.empty() ? 0 : static_cast<int64_t>(notches_[0]));
  if (m == "rig.set_notch") { notches_.clear(); if (arg(0).asDouble() > 0) toggleNotch(arg(0).asDouble()); else clearNotches(); return V::nil(); }
  if (m == "spot.get_auto") return V::boolean(false);
  if (m == "spot.set_auto" || m == "spot.toggle_auto") return V::boolean(false);
  if (m == "spot.pskrep.get_count") return V::integer(0);

  throw std::runtime_error("unknown method " + m);
}

// ------------------------------------------------------- GridTracker2 / UDP

void MainWindow::openGridTracker() {
  gt_.close();
  gtLastStatusKey_.clear();
  if (!settings_.gtEnabled) return;
  std::string err;
  if (!gt_.open(settings_.gtHost, static_cast<uint16_t>(settings_.gtPort), "gtty", err)) {
    toast("GridTracker: " + err);
    return;
  }
  gt_.onIncoming = [this](const wsjtx::Incoming& in) { gtHandle(in); };
  gt_.send(wsjtx::encodeHeartbeat(gt_.id(), GTTY_VERSION));
  gtLastHeartbeat_ = g_get_monotonic_time();
  gtSendStatus();
}

void MainWindow::gtTick() {
  if (!gt_.isOpen()) return;
  gt_.poll();
  int64_t now = g_get_monotonic_time();
  if (now - gtLastHeartbeat_ > 15 * G_USEC_PER_SEC) {
    gt_.send(wsjtx::encodeHeartbeat(gt_.id(), GTTY_VERSION));
    gtLastHeartbeat_ = now;
  }
  gtSendStatus();
}

void MainWindow::gtSendStatus() {
  if (!gt_.isOpen()) return;
  WsjtxStatus st;
  st.dialFreqHz = static_cast<uint64_t>(std::llround(dialFreqHz()));
  st.mode = "RTTY";
  st.txMode = "RTTY";
  st.dxCall = upper(gtk_editable_get_text(GTK_EDITABLE(callEntry_)));
  st.report = gtk_editable_get_text(GTK_EDITABLE(rstSEntry_));
  st.transmitting = tx_.transmitting();
  st.txEnabled = st.transmitting;
  st.decoding = !st.transmitting;
  st.rxDf = static_cast<uint32_t>(std::lround(settings_.rtty.markFreq + primaryAfc_));
  st.txDf = st.rxDf;
  st.deCall = settings_.myCall;
  st.deGrid = settings_.myGrid;
  std::string key = std::to_string(st.dialFreqHz) + "|" + st.dxCall + "|" + (st.transmitting ? "T" : "R") + "|" + std::to_string(st.rxDf / 10);
  int64_t now = g_get_monotonic_time();
  if (key == gtLastStatusKey_ && now - gtLastStatus_ < 15 * G_USEC_PER_SEC) return;
  gtLastStatusKey_ = key;
  gtLastStatus_ = now;
  gt_.send(wsjtx::encodeStatus(gt_.id(), st));
}

void MainWindow::gtReportLine(const std::string& line) {
  if (!gt_.isOpen() || !settings_.gtSendDecodes || line.empty()) return;
  std::string grid = findGrid(line);
  int64_t nowUnix = static_cast<int64_t>(g_get_real_time() / G_USEC_PER_SEC);
  std::string my = upper(settings_.myCall);
  bool toMe = !my.empty() && upper(line).find(my) != std::string::npos;
  for (const auto& span : findCallsigns(line)) {
    std::string call = upper(line.substr(span.first, span.second));
    if (call == my) continue;
    auto it = gtLastReported_.find(call);
    if (it != gtLastReported_.end() && nowUnix - it->second < 60) continue;  // once a minute per call
    gtLastReported_[call] = nowUnix;
    WsjtxDecode d;
    d.timeMsSinceMidnightUtc = wsjtx::msSinceMidnightUtc(nowUnix);
    float q = 0;
    if (rx_.bank().size() > 0) q = rx_.bank().slot(0).demod->status().quality;
    d.snr = static_cast<int32_t>(std::lround(q * 30.0f - 15.0f));
    d.deltaFreq = static_cast<uint32_t>(std::lround(settings_.rtty.markFreq + primaryAfc_));
    d.mode = "RTTY";
    // Shape it like a WSJT-X message so GridTracker's parser finds the call and grid.
    d.message = (toMe ? my : std::string("CQ")) + " " + call + (grid.empty() ? "" : " " + grid);
    gt_.send(wsjtx::encodeDecode(gt_.id(), d));
  }
  if (gtLastReported_.size() > 5000) gtLastReported_.clear();
}

void MainWindow::gtReportQso(const Qso& q) {
  if (!gt_.isOpen() || !settings_.gtSendLogged) return;
  WsjtxQso w;
  int64_t now = static_cast<int64_t>(g_get_real_time() / G_USEC_PER_SEC);
  w.timeOffUnix = now;
  w.timeOnUnix = now;
  w.dxCall = q.call;
  w.dxGrid = q.grid;
  w.txFreqHz = static_cast<uint64_t>(std::llround(q.freqHz));
  w.mode = q.mode;
  w.reportSent = q.rstSent;
  w.reportRcvd = q.rstRcvd;
  w.name = q.name;
  w.comments = q.comment;
  w.operatorCall = settings_.myCall;
  w.myCall = settings_.myCall;
  w.myGrid = settings_.myGrid;
  w.exchSent = q.exchSent;
  w.exchRcvd = q.exchRcvd;
  gt_.send(wsjtx::encodeQsoLogged(gt_.id(), w));
  // Also the ADIF form, which newer GridTracker builds prefer for log forwarding.
  std::string date = q.timeUtc.substr(0, 4) + q.timeUtc.substr(5, 2) + q.timeUtc.substr(8, 2);
  std::string time = q.timeUtc.size() >= 19 ? q.timeUtc.substr(11, 2) + q.timeUtc.substr(14, 2) + q.timeUtc.substr(17, 2) : "";
  auto f = [](const char* n, const std::string& v) {
    return v.empty() ? std::string() : "<" + std::string(n) + ":" + std::to_string(v.size()) + ">" + v;
  };
  char freq[32];
  snprintf(freq, sizeof freq, "%.6f", q.freqHz / 1e6);
  std::string adif = "<adif_ver:5>3.1.4<programid:4>gtty<EOH>\n" + f("call", q.call) + f("gridsquare", q.grid) +
                     f("mode", q.mode) + f("rst_sent", q.rstSent) + f("rst_rcvd", q.rstRcvd) + f("qso_date", date) +
                     f("time_on", time) + f("qso_date_off", date) + f("time_off", time) + f("band", q.band) +
                     (q.freqHz > 0 ? f("freq", freq) : "") + f("station_callsign", settings_.myCall) +
                     f("my_gridsquare", settings_.myGrid) + f("stx_string", q.exchSent) + f("srx_string", q.exchRcvd) +
                     f("name", q.name) + f("comment", q.comment) + "<EOR>";
  gt_.send(wsjtx::encodeLoggedAdif(gt_.id(), adif));
}

void MainWindow::gtHandle(const wsjtx::Incoming& in) {
  using T = wsjtx::Incoming::Type;
  switch (in.type) {
    case T::Reply: {
      if (!settings_.gtAcceptReply) return;
      // Message is "CQ CALL GRID" / "MYCALL CALL GRID" style: pick the first callsign that is not ours.
      std::string my = upper(settings_.myCall);
      std::string picked, grid = findGrid(in.reply.message);
      for (const auto& span : findCallsigns(in.reply.message)) {
        std::string c = upper(in.reply.message.substr(span.first, span.second));
        if (c != my) { picked = c; break; }
      }
      if (picked.empty()) return;
      gtk_editable_set_text(GTK_EDITABLE(callEntry_), picked.c_str());
      ctx_.call = picked;
      ctx_.grid = grid;
      toast("GridTracker: " + picked + (grid.empty() ? "" : " (" + grid + ")"));
      break;
    }
    case T::HaltTx:
      if (!in.autoTxOnly) abortTx();
      break;
    case T::FreeText:
      if (!in.text.empty()) insertTxText(in.text);
      if (in.send) startTx();
      break;
    case T::Clear:
    case T::Close:
    case T::Unknown:
      break;
  }
}

gboolean MainWindow::onCloseRequest(GtkWindow*, gpointer p) {
  auto* self = static_cast<MainWindow*>(p);
  self->settings_.save();
  self->tx_.abortTx();
  return FALSE;
}

}  // namespace gtty
