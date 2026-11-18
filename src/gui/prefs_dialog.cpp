#include "prefs_dialog.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/audio_engine.h"
#include "core/log/callbook.h"
#include "core/modem/codec2_demod.h"
#include "core/rig/rig_control.h"
#include "core/rig/serial_ports.h"

namespace gtty {

namespace {

const int kRadioDecoderBauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
const int kRadioDecoderBaudCount = 8;
// Offered in the rig's serial speed list; any other speed can be typed in.
const int kSerialSpeeds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};

struct Prefs {
  Settings* s;
  std::function<void()> onApply;
  std::vector<std::string> inputs, outputs;
  std::vector<RigModelInfo> models;
  GtkWidget *inDev, *outDev, *inChan;
  GtkWidget *rigEnabled, *rigModel, *rigDevice, *rigBaud, *pttType, *pttDevice, *pollMs, *manualDial, *rigFilter;
  GtkWidget *monitorLevel, *cwIdAtEnd, *cwIdText, *cwIdWpm, *cwIdTone, *tuneTimeout;
  GtkWidget *clEnabled, *clHost, *clPort, *clLogin, *clCommands, *clRttyOnly, *clMaxAge, *clThisBand;
  GtkWidget *cbProvider, *cbUser, *cbPass, *cbAutoFill;
  struct DecRows { GtkWidget *enabled = nullptr, *show = nullptr, *votes = nullptr, *mode = nullptr, *filterBits = nullptr, *window = nullptr, *atcTau = nullptr; };
  std::vector<DecRows> dec;
  GtkWidget *showVote, *voteBelow, *skimmer, *skimmerStep, *skimmerCalls, *radioDec, *radioDecDevice, *radioDecBaud;
  GtkWidget *baud, *mark, *space, *preset, *stopBits, *variant, *uos, *afcRange, *netTx, *followSideband;
  bool applyingPreset = false;
  GtkWidget *txMethod, *fskDevice, *fskInvert, *diddle, *afskLevel, *pttViaRig, *txMode, *preamble, *pttLead;
  GtkWidget *autoRx, *autoRxDelay, *clearTxOnRx, *wordMode, *autoCqInterval, *autoCqMacro;
  GtkWidget *rxTimestamps, *rxLogToFile, *rxLogDir;
  GtkWidget *rpcEnabled, *rpcBind, *rpcPort;
  GtkWidget *fllogEnabled, *fllogHost, *fllogPort;
  GtkWidget *xlogEnabled, *xlogHost, *xlogPort;
  GtkWidget *spotEnabled, *spotCqOnly, *spotBind, *spotPort;
  GtkWidget* pskrEnabled;
  GtkWidget *myCall, *myName, *myQth, *myGrid;
  GtkWidget *scpPath, *logPath, *wfLo, *wfHi, *wfDir, *fontSize, *theme, *modemOnly;
  GtkWidget *gtEnabled, *gtHost, *gtPort, *gtDecodes, *gtLogged, *gtReply;
  std::vector<GtkWidget*> bandRows;
};

// The pages sit in a view stack in a window of their own: an AdwPreferencesDialog is
// drawn inside the main window at a fixed size and cannot be resized (issue #50).
// A page is a scrolled column of groups, spaced as an AdwPreferencesPage spaces them but
// without its width limit (a clamp of about 600 px), so the rows widen with the window.
GtkWidget* page(GtkWidget* stack, const char* title, const char* icon) {
  GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
  gtk_widget_set_margin_top(column, 24);
  gtk_widget_set_margin_bottom(column, 24);
  gtk_widget_set_margin_start(column, 12);
  gtk_widget_set_margin_end(column, 12);
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), column);
  adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(stack), scroll, title, title, icon);
  return column;
}
GtkWidget* group(GtkWidget* pg, const char* title) {
  GtkWidget* g = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
  gtk_box_append(GTK_BOX(pg), g);
  return g;
}
GtkWidget* entry(GtkWidget* g, const char* title, const std::string& value) {
  GtkWidget* r = adw_entry_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  gtk_editable_set_text(GTK_EDITABLE(r), value.c_str());
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
  return r;
}
// A flat button in a pick list: clicking it puts value into the entry row and closes
// the list.
GtkWidget* pickButton(gpointer row, const std::string& value, const std::string& label) {
  GtkWidget* b = gtk_button_new();
  gtk_widget_add_css_class(b, "flat");
  GtkWidget* l = gtk_label_new(label.c_str());
  gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
  gtk_button_set_child(GTK_BUTTON(b), l);
  g_object_set_data_full(G_OBJECT(b), "gtty-value", g_strdup(value.c_str()), g_free);
  g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer r) {
    gtk_editable_set_text(GTK_EDITABLE(r), static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "gtty-value")));
    if (GtkWidget* p = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER)) gtk_popover_popdown(GTK_POPOVER(p));
  }), row);
  return b;
}
// A drop-down button at the end of an entry row, opening pop.
void addPickButton(GtkWidget* row, GtkWidget* pop, const char* tooltip) {
  GtkWidget* btn = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(btn), "pan-down-symbolic");
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), pop);
  gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(btn, "flat");
  gtk_widget_set_tooltip_text(btn, tooltip);
  adw_entry_row_add_suffix(ADW_ENTRY_ROW(row), btn);
}
// The ports are listed afresh each time the list opens, so a USB cable plugged in with
// Preferences open shows up. Picking one fills the field, which stays free to type in.
void fillPortList(GtkPopover* pop, gpointer row) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  bool offerNet = g_object_get_data(G_OBJECT(row), "gtty-offer-net") != nullptr;
  std::vector<SerialPortInfo> ports = listSerialPorts();
  if (offerNet) ports.push_back({"localhost:4532", "rigctld on this machine (NET rigctl)"});
  for (const auto& p : ports)
    gtk_box_append(GTK_BOX(box), pickButton(row, p.path, p.detail.empty() ? p.path : p.path + "  (" + p.detail + ")"));
  if (ports.empty()) {
    GtkWidget* l = gtk_label_new("No serial ports found");
    gtk_widget_add_css_class(l, "dim-label");
    gtk_widget_set_margin_top(l, 6); gtk_widget_set_margin_bottom(l, 6);
    gtk_widget_set_margin_start(l, 6); gtk_widget_set_margin_end(l, 6);
    gtk_box_append(GTK_BOX(box), l);
  }
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  gtk_popover_set_child(pop, scroll);
}
// An entry row for a serial device, with a button listing the ports found. offerNet adds
// rigctld's address, for the Hamlib device field.
GtkWidget* deviceEntry(GtkWidget* g, const char* title, const std::string& value, bool offerNet = false) {
  GtkWidget* r = entry(g, title, value);
  if (offerNet) g_object_set_data(G_OBJECT(r), "gtty-offer-net", GINT_TO_POINTER(1));
  GtkWidget* pop = gtk_popover_new();
  g_signal_connect(pop, "show", G_CALLBACK(fillPortList), r);
  addPickButton(r, pop, "Serial ports found on this computer");
  return r;
}
// The serial speed as typed: blank (or 0) for the backend's default, otherwise a whole
// number of bits per second. -1 when it is neither.
int parseSerialSpeed(const std::string& t) {
  size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
  if (a == std::string::npos) return 0;
  std::string v = t.substr(a, b - a + 1);
  if (v.size() > 7 || v.find_first_not_of("0123456789") != std::string::npos) return -1;
  return std::stoi(v);
}
// An entry row for a serial speed, with a list of the common ones. Any other speed can
// be typed; a field that is not a number is marked and leaves the saved speed as it was.
GtkWidget* speedEntry(GtkWidget* g, const char* title, int value) {
  GtkWidget* r = entry(g, title, value > 0 ? std::to_string(value) : "");
  g_signal_connect(r, "changed", G_CALLBACK(+[](GtkEditable* e, gpointer) {
    if (parseSerialSpeed(gtk_editable_get_text(e)) < 0) gtk_widget_add_css_class(GTK_WIDGET(e), "error");
    else gtk_widget_remove_css_class(GTK_WIDGET(e), "error");
  }), nullptr);
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(box), pickButton(r, "", "Rig default"));
  for (int v : kSerialSpeeds) gtk_box_append(GTK_BOX(box), pickButton(r, std::to_string(v), std::to_string(v)));
  GtkWidget* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
  GtkWidget* pop = gtk_popover_new();
  gtk_popover_set_child(GTK_POPOVER(pop), scroll);
  addPickButton(r, pop, "Common serial speeds");
  return r;
}
GtkWidget* spin(GtkWidget* g, const char* title, double lo, double hi, double step, double value, int digits = 0) {
  GtkWidget* r = adw_spin_row_new_with_range(lo, hi, step);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  adw_spin_row_set_digits(ADW_SPIN_ROW(r), digits);
  adw_spin_row_set_value(ADW_SPIN_ROW(r), value);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
  return r;
}
GtkWidget* toggle(GtkWidget* g, const char* title, bool value, const char* subtitle = nullptr) {
  GtkWidget* r = adw_switch_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(r), subtitle);
  adw_switch_row_set_active(ADW_SWITCH_ROW(r), value);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
  return r;
}
// A popover list factory whose items show the whole string: the default one ellipsizes them
// to a narrow popover, so similar long names (sound card devices) can't be told apart.
// Each item keeps the checkmark on the row's current selection.
GtkListItemFactory* wideListFactory(GtkWidget* row) {
  GtkListItemFactory* f = gtk_signal_list_item_factory_new();
  g_signal_connect(f, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GObject* obj, gpointer) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 70);
    gtk_widget_set_hexpand(label, TRUE);
    GtkWidget* check = gtk_image_new_from_icon_name("object-select-symbolic");
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), check);
    gtk_list_item_set_child(GTK_LIST_ITEM(obj), box);
  }), nullptr);
  g_signal_connect(f, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GObject* obj, gpointer r) {
    GtkListItem* item = GTK_LIST_ITEM(obj);
    GtkWidget* box = gtk_list_item_get_child(item);
    GtkWidget* label = gtk_widget_get_first_child(box);
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_list_item_get_item(item))));
    auto update = +[](GObject* row, GParamSpec*, gpointer li) {
      GtkListItem* it = GTK_LIST_ITEM(li);
      GtkWidget* c = gtk_widget_get_last_child(gtk_list_item_get_child(it));
      gtk_widget_set_opacity(c, adw_combo_row_get_selected(ADW_COMBO_ROW(row)) == gtk_list_item_get_position(it) ? 1 : 0);
    };
    update(G_OBJECT(r), nullptr, item);
    gulong id = g_signal_connect(r, "notify::selected", G_CALLBACK(update), item);
    g_object_set_data(obj, "sel-handler", reinterpret_cast<gpointer>(id));
  }), row);
  g_signal_connect(f, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory*, GObject* obj, gpointer r) {
    auto id = reinterpret_cast<gulong>(g_object_get_data(obj, "sel-handler"));
    if (id) g_signal_handler_disconnect(r, id);
    g_object_set_data(obj, "sel-handler", nullptr);
  }), row);
  return f;
}
// longValues: list the choices in full (wideListFactory) and show the selection as the row's
// subtitle, which gets the full row width, instead of truncated beside the title.
GtkWidget* combo(GtkWidget* g, const char* title, const std::vector<std::string>& items, int selected, bool search = false,
                 bool longValues = false) {
  GtkWidget* r = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  if (longValues) {
    adw_combo_row_set_use_subtitle(ADW_COMBO_ROW(r), TRUE);
    GtkListItemFactory* f = wideListFactory(r);
    adw_combo_row_set_list_factory(ADW_COMBO_ROW(r), f);
    g_object_unref(f);
  }
  std::vector<const char*> arr;
  for (const auto& s : items) arr.push_back(s.c_str());
  arr.push_back(nullptr);
  adw_combo_row_set_model(ADW_COMBO_ROW(r), G_LIST_MODEL(gtk_string_list_new(arr.data())));
  if (search) {
    adw_combo_row_set_enable_search(ADW_COMBO_ROW(r), TRUE);
    adw_combo_row_set_expression(ADW_COMBO_ROW(r), gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, nullptr, "string"));
  }
  if (selected >= 0 && selected < static_cast<int>(items.size())) adw_combo_row_set_selected(ADW_COMBO_ROW(r), selected);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
  return r;
}
std::string text(GtkWidget* w) { return gtk_editable_get_text(GTK_EDITABLE(w)); }
double value(GtkWidget* w) { return adw_spin_row_get_value(ADW_SPIN_ROW(w)); }
bool active(GtkWidget* w) { return adw_switch_row_get_active(ADW_SWITCH_ROW(w)); }
int selected(GtkWidget* w) { return static_cast<int>(adw_combo_row_get_selected(ADW_COMBO_ROW(w))); }

gboolean onCloseRequest(GtkWindow*, gpointer p) {
  std::unique_ptr<Prefs> pr(static_cast<Prefs*>(p));
  Settings& s = *pr->s;
  int in = selected(pr->inDev), out = selected(pr->outDev);
  s.inputDevice = in > 0 ? pr->inputs[in] : "";
  s.outputDevice = out > 0 ? pr->outputs[out] : "";
  s.inputChannel = selected(pr->inChan) - 1;

  s.rig.enabled = active(pr->rigEnabled);
  int m = selected(pr->rigModel);
  if (m >= 0 && m < static_cast<int>(pr->models.size())) s.rig.model = pr->models[m].model;
  s.rig.device = text(pr->rigDevice);
  if (int baud = parseSerialSpeed(text(pr->rigBaud)); baud >= 0) s.rig.baud = baud;
  static const char* ptts[] = {"RIG", "DTR", "RTS", "NONE"};
  s.rig.pttType = ptts[selected(pr->pttType)];
  s.rig.pttDevice = text(pr->pttDevice);
  s.rig.pollMs = static_cast<int>(value(pr->pollMs));
  s.manualDialHz = value(pr->manualDial) * 1000.0;
  s.rigFilterHz = static_cast<int>(value(pr->rigFilter));
  s.tx.monitorLevel = static_cast<float>(value(pr->monitorLevel) / 100.0);
  s.tx.cwIdAtEnd = active(pr->cwIdAtEnd);
  s.tx.cwIdText = text(pr->cwIdText);
  s.tx.cwIdWpm = value(pr->cwIdWpm);
  s.tx.cwIdToneHz = value(pr->cwIdTone);
  s.tx.tuneTimeoutSec = static_cast<int>(value(pr->tuneTimeout));
  s.clusterEnabled = active(pr->clEnabled);
  s.clusterHost = text(pr->clHost);
  s.clusterPort = static_cast<int>(value(pr->clPort));
  s.clusterLogin = text(pr->clLogin);
  s.clusterCommands = text(pr->clCommands);
  s.clusterRttyOnly = active(pr->clRttyOnly);
  s.clusterMaxAgeMin = static_cast<int>(value(pr->clMaxAge));
  s.bandMapThisBand = active(pr->clThisBand);
  s.callbookProvider = selected(pr->cbProvider);
  s.callbookUser = text(pr->cbUser);
  s.callbookPass = text(pr->cbPass);
  s.callbookAutoFill = active(pr->cbAutoFill);
  for (size_t i = 0; i < pr->dec.size() && i < s.decoders.size(); ++i) {
    auto& d = s.decoders[i];
    const auto& r = pr->dec[i];
    if (r.enabled) d.enabled = active(r.enabled);
    if (r.show) d.show = active(r.show);
    if (r.votes) d.votes = active(r.votes);
    if (r.mode) d.mode = selected(r.mode);
    if (r.filterBits) d.filterBits = value(r.filterBits);
    if (r.window) d.window = selected(r.window);
    if (r.atcTau) d.atcTau = value(r.atcTau);
  }
  s.showVotePane = active(pr->showVote);
  s.votePaneBelow = active(pr->voteBelow);
  s.skimmerEnabled = active(pr->skimmer);
  s.skimmerStepHz = value(pr->skimmerStep);
  s.skimmerWaterfallCalls = active(pr->skimmerCalls);
  s.radioDecoderEnabled = active(pr->radioDec);
  s.radioDecoderDevice = text(pr->radioDecDevice);
  s.radioDecoderBaud = kRadioDecoderBauds[std::clamp(selected(pr->radioDecBaud), 0, kRadioDecoderBaudCount - 1)];

  s.rtty.baud = value(pr->baud);
  s.rtty.setTones(value(pr->mark), value(pr->space));
  s.rtty.stopBits = selected(pr->stopBits) == 0 ? 1.5 : 2.0;
  s.rtty.variant = selected(pr->variant) == 0 ? BaudotVariant::ITA2 : BaudotVariant::USTTY;
  s.rtty.unshiftOnSpace = active(pr->uos);
  s.afcRangeHz = value(pr->afcRange);
  s.netTx = active(pr->netTx);
  s.followRigSideband = active(pr->followSideband);

  s.tx.method = static_cast<TxConfig::Method>(selected(pr->txMethod));
  s.tx.fskDevice = text(pr->fskDevice);
  s.tx.fskInvert = active(pr->fskInvert);
  s.tx.diddle = active(pr->diddle);
  s.tx.afskLevel = static_cast<float>(value(pr->afskLevel) / 100.0);
  s.tx.pttViaRig = active(pr->pttViaRig);
  static const char* modes[] = {"", "RTTY", "RTTYR", "LSB", "USB", "PKTLSB", "PKTUSB"};
  s.tx.txMode = modes[selected(pr->txMode)];
  s.tx.ltrsPreamble = static_cast<int>(value(pr->preamble));
  s.tx.pttLeadMs = static_cast<int>(value(pr->pttLead));
  s.autoRx = active(pr->autoRx);
  s.autoRxDelaySec = value(pr->autoRxDelay);
  s.clearTxOnRx = active(pr->clearTxOnRx);
  s.txWordMode = active(pr->wordMode);
  s.autoCqIntervalSec = static_cast<int>(value(pr->autoCqInterval));
  s.autoCqMacro = static_cast<int>(value(pr->autoCqMacro));
  s.rxTimestamps = active(pr->rxTimestamps);
  s.rxLogToFile = active(pr->rxLogToFile);
  s.rxLogDir = text(pr->rxLogDir);
  s.rpcEnabled = active(pr->rpcEnabled);
  s.rpcBind = text(pr->rpcBind);
  s.rpcPort = static_cast<int>(value(pr->rpcPort));
  s.fllogEnabled = active(pr->fllogEnabled);
  s.fllogHost = text(pr->fllogHost);
  s.fllogPort = static_cast<int>(value(pr->fllogPort));
  s.xlogEnabled = active(pr->xlogEnabled);
  s.xlogHost = text(pr->xlogHost);
  s.xlogPort = static_cast<int>(value(pr->xlogPort));
  s.spotServerEnabled = active(pr->spotEnabled);
  s.spotServerCqOnly = active(pr->spotCqOnly);
  s.spotServerBind = text(pr->spotBind);
  s.spotServerPort = static_cast<int>(value(pr->spotPort));
  s.pskReporterEnabled = active(pr->pskrEnabled);

  s.myCall = text(pr->myCall);
  s.myName = text(pr->myName);
  s.myQth = text(pr->myQth);
  s.myGrid = text(pr->myGrid);
  s.scpPath = text(pr->scpPath);
  s.logDbPath = text(pr->logPath);
  s.waterfallLoHz = value(pr->wfLo);
  s.waterfallHiHz = value(pr->wfHi);
  s.waterfallDirection = selected(pr->wfDir);
  s.fontSize = static_cast<int>(value(pr->fontSize));
  s.theme = selected(pr->theme);
  s.modemOnly = active(pr->modemOnly);
  s.gtEnabled = active(pr->gtEnabled);
  s.gtHost = text(pr->gtHost);
  s.gtPort = static_cast<int>(value(pr->gtPort));
  s.gtSendDecodes = active(pr->gtDecodes);
  s.gtSendLogged = active(pr->gtLogged);
  s.gtAcceptReply = active(pr->gtReply);
  for (size_t i = 0; i < pr->bandRows.size() && i < s.bands.size(); ++i)
    s.bands[i].hz = value(pr->bandRows[i]) * 1000.0;
  if (pr->onApply) pr->onApply();
  return FALSE;   // close the window
}

}  // namespace

void showPrefsDialog(GtkWidget* parent, Settings& s, std::function<void()> onApply) {
  auto* pr = new Prefs{};
  pr->s = &s;
  pr->onApply = std::move(onApply);
  GtkWidget* win = adw_window_new();
  std::string title = s.profile == Profiles::kDefault ? "Preferences" : "Preferences — " + s.profile;
  gtk_window_set_title(GTK_WINDOW(win), title.c_str());
  if (parent) gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent));
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(win), 760, 720);
  GtkWidget* dlg = adw_view_stack_new();   // the pages
  GtkWidget* switcher = adw_view_switcher_new();
  adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(dlg));
  adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_NARROW);
  GtkWidget* header = adw_header_bar_new();
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);
  GtkWidget* view = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), header);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), dlg);
  adw_window_set_content(ADW_WINDOW(win), view);
  // Escape closes it, as it closed the dialog.
  GtkEventController* keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType, gpointer w) -> gboolean {
    if (key != GDK_KEY_Escape) return FALSE;
    gtk_window_close(GTK_WINDOW(w));
    return TRUE;
  }), win);
  gtk_widget_add_controller(win, keys);

  // --- Audio
  {
    GtkWidget* pg = page(dlg, "Audio", "audio-card-symbolic");
    GtkWidget* g = group(pg, "Sound card");
    std::vector<std::string> ins{"(default)"}, outs{"(default)"};
    int inSel = 0, outSel = 0;
    for (const auto& d : AudioEngine::listDevices()) {
      // A USB card by what it reports, e.g. "C-Media Electronics Inc. USB Audio Device — Mic (hw:1,0)",
      // in place of PortAudio's "USB Audio Device: USB Audio (hw:1,0)". Others: PortAudio's name.
      auto label = [&](const std::string& kind) {
        if (d.card.empty()) return d.name;
        std::string l = d.card;
        if (!kind.empty()) l += " — " + kind;
        return l + " (" + d.hwId + ")";
      };
      if (d.maxInputs > 0) { ins.push_back(label(d.inputKind)); pr->inputs.push_back(d.name); if (d.name == s.inputDevice) inSel = static_cast<int>(ins.size()) - 1; }
      if (d.maxOutputs > 0) { outs.push_back(label(d.outputKind)); pr->outputs.push_back(d.name); if (d.name == s.outputDevice) outSel = static_cast<int>(outs.size()) - 1; }
    }
    pr->inputs.insert(pr->inputs.begin(), "");
    pr->outputs.insert(pr->outputs.begin(), "");
    pr->inDev = combo(g, "Input device", ins, inSel, true, true);
    pr->outDev = combo(g, "Output device", outs, outSel, true, true);
    pr->inChan = combo(g, "Input channel", {"Mix", "Left", "Right"}, s.inputChannel + 1);
  }
  // --- Rig
  {
    GtkWidget* pg = page(dlg, "Rig", "network-wireless-symbolic");
    GtkWidget* g = group(pg, "Hamlib");
    pr->rigEnabled = toggle(g, "Enable rig control", s.rig.enabled);
    pr->models = RigControl::listModels();
    std::vector<std::string> names;
    int sel = 0;
    for (size_t i = 0; i < pr->models.size(); ++i) {
      const auto& m = pr->models[i];
      names.push_back(m.mfg + " " + m.name + "  (" + std::to_string(m.model) + ")");
      if (m.model == s.rig.model) sel = static_cast<int>(i);
    }
    pr->rigModel = combo(g, "Rig model", names, sel, true);
    pr->rigDevice = deviceEntry(g, "Device (serial port, or host:port for NET rigctl)", s.rig.device, true);
    pr->rigBaud = speedEntry(g, "Serial speed (bps, blank = rig default)", s.rig.baud);
    GtkWidget* g2 = group(pg, "PTT");
    static const char* ptts[] = {"RIG", "DTR", "RTS", "NONE"};
    int pttSel = 0;
    for (int i = 0; i < 4; ++i) if (s.rig.pttType == ptts[i]) pttSel = i;
    pr->pttType = combo(g2, "PTT method", {"CAT command", "Serial DTR", "Serial RTS", "None (VOX)"}, pttSel);
    pr->pttDevice = deviceEntry(g2, "PTT serial device (blank = rig device; a port of its own also works with NET rigctl)", s.rig.pttDevice);
    pr->pollMs = spin(g2, "Poll interval (ms)", 100, 5000, 50, s.rig.pollMs);
    GtkWidget* gf = group(pg, "Receiver filter");
    pr->rigFilter = spin(gf, "Filter width to select on connect (Hz, 0 = leave)", 0, 6000, 50, s.rigFilterHz);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->rigFilter), "The Filter and IF shift controls under the tuning scope change it while operating");
    GtkWidget* g3 = group(pg, "No rig");
    pr->manualDial = spin(g3, "Dial frequency without rig (kHz)", 100, 500000, 0.1, s.manualDialHz / 1000.0, 1);
  }
  // --- RTTY
  {
    GtkWidget* pg = page(dlg, "RTTY", "preferences-system-symbolic");
    GtkWidget* g = group(pg, "Signal");
    pr->baud = spin(g, "Baud rate", 20, 300, 0.05, s.rtty.baud, 2);
    std::vector<std::string> presetNames;
    for (const auto& tp : tonePresets()) presetNames.push_back(tp.name);
    pr->preset = combo(g, "Tone preset", presetNames, tonePresetIndex(s.rtty.markFreq, s.rtty.spaceFreq()));
    pr->mark = spin(g, "Mark tone (Hz)", 300, 5000, 5, s.rtty.markFreq);
    pr->space = spin(g, "Space tone (Hz)", 300, 5000, 5, s.rtty.spaceFreq());
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->mark), "Shift and polarity follow from the two tones. Default 2125 / 2295; tick Rev on the main window to swap");
    g_signal_connect(pr->preset, "notify::selected", G_CALLBACK(+[](GObject* row, GParamSpec*, gpointer p) {
      auto* pr = static_cast<Prefs*>(p);
      int i = static_cast<int>(adw_combo_row_get_selected(ADW_COMBO_ROW(row)));
      if (i <= 0 || pr->applyingPreset) return;
      pr->applyingPreset = true;
      adw_spin_row_set_value(ADW_SPIN_ROW(pr->mark), tonePresets()[i].mark);
      adw_spin_row_set_value(ADW_SPIN_ROW(pr->space), tonePresets()[i].space);
      pr->applyingPreset = false;
    }), pr);
    auto toCustom = +[](GObject*, GParamSpec*, gpointer p) {
      auto* pr = static_cast<Prefs*>(p);
      if (!pr->applyingPreset) adw_combo_row_set_selected(ADW_COMBO_ROW(pr->preset), 0);
    };
    g_signal_connect(pr->mark, "notify::value", G_CALLBACK(toCustom), pr);
    g_signal_connect(pr->space, "notify::value", G_CALLBACK(toCustom), pr);
    pr->followSideband = toggle(g, "Follow the rig's sideband", s.followRigSideband,
                                "Swap mark and space while the rig is in USB or PKTUSB, as Fldigi does. Rev is then only for a station that sends reversed. Needs rig control");
    pr->stopBits = combo(g, "Stop bits (TX)", {"1.5", "2"}, s.rtty.stopBits >= 2.0 ? 1 : 0);
    GtkWidget* g2 = group(pg, "Code");
    pr->variant = combo(g2, "Character set", {"ITA2", "US-TTY"}, s.rtty.variant == BaudotVariant::ITA2 ? 0 : 1);
    pr->uos = toggle(g2, "Unshift on space", s.rtty.unshiftOnSpace);
    GtkWidget* g3 = group(pg, "Decoder");
    pr->afcRange = spin(g3, "AFC range (±Hz)", 10, 300, 10, s.afcRangeHz);
    pr->netTx = toggle(g3, "Net: transmit on received tones", s.netTx);
  }
  // --- Decoders
  {
    GtkWidget* pg = page(dlg, "Decoders", "view-dual-symbolic");
    static const char* titles[] = {"A: matched filter", "B: matched filter", "Correlator (joint-timing character decoder)", "codec2 FSK (comparison engine)"};
    pr->dec.resize(s.decoders.size());
    for (size_t i = 0; i < s.decoders.size() && i < 4; ++i) {
      const auto& d = s.decoders[i];
      GtkWidget* g = group(pg, titles[i]);
      if (i == 3 && !codec2Available()) {
        GtkWidget* note = gtk_label_new("This build has no libcodec2; the engine is not available.");
        gtk_widget_add_css_class(note, "dim-label");
        gtk_label_set_xalign(GTK_LABEL(note), 0.f);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), note);
        continue;
      }
      pr->dec[i].enabled = toggle(g, "Enabled", d.enabled);
      pr->dec[i].show = toggle(g, "Show its RX pane", d.show);
      pr->dec[i].votes = toggle(g, "Takes part in the best-guess vote", d.votes,
                                i == 3 ? "codec2 decides about half a second after the others, which delays the merged pane" : nullptr);
      if (i < 2) {
        pr->dec[i].mode = combo(g, "Mode", {"Normal", "Selective (narrow, crowded band)", "Flutter (wide, fast ATC)", "Custom"}, d.mode);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->dec[i].mode), "Also switchable from the pane header; Custom uses the three values below");
        pr->dec[i].filterBits = spin(g, "Custom: matched filter length (bit periods)", 0.3, 2.0, 0.1, d.filterBits, 1);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->dec[i].filterBits), "Shorter = wider bandwidth, follows flutter; longer = narrower, rejects neighbors");
        pr->dec[i].window = combo(g, "Custom: filter window", {"Boxcar (true matched filter)", "Hann (better adjacent-signal rejection)"}, d.window);
        pr->dec[i].atcTau = spin(g, "Custom: ATC time constant (s)", 0.02, 3.0, 0.02, d.atcTau, 2);
      }
    }
    GtkWidget* gv = group(pg, "Best guess");
    pr->showVote = toggle(gv, "Show the merged best-guess pane", s.showVotePane,
                          "Each character is decided by a confidence-weighted vote of the engines; this pane also feeds GridTracker, XML-RPC and the session file");
    pr->voteBelow = toggle(gv, "Put it below the engine panes", s.votePaneBelow,
                           "Off: full width above the engine panes. On: under them, for reading the engines first");
    GtkWidget* gs = group(pg, "Skimmer");
    pr->skimmer = toggle(gs, "Decode the whole passband", s.skimmerEnabled,
                         "A decoder every step across the waterfall range; stations appear in the Skimmer list of the sidebar. Uses CPU");
    pr->skimmerStep = spin(gs, "Channel step (Hz)", 20, 200, 10, s.skimmerStepHz);
    pr->skimmerCalls = toggle(gs, "Callsigns on the waterfall", s.skimmerWaterfallCalls,
                              "Label each station at its signal, as WSJT-X does: green calling CQ, red calling you, grey worked. Click a label to tune to it");
    GtkWidget* gr = group(pg, "Radio's built-in decoder");
    pr->radioDec = toggle(gr, "Show what the radio decodes", s.radioDecoderEnabled,
                          "For radios that print their own RTTY decoder on a serial port, such as the second USB port of an Icom IC-7610 or IC-9700 set to RTTY Decode. It gets a pane of its own and does not take part in the best-guess vote");
    pr->radioDecDevice = deviceEntry(gr, "Serial device the radio prints on (may be the FSK or PTT port)", s.radioDecoderDevice);
    std::vector<std::string> bauds;
    int baudSel = 3;
    for (int i = 0; i < kRadioDecoderBaudCount; ++i) {
      bauds.push_back(std::to_string(kRadioDecoderBauds[i]));
      if (kRadioDecoderBauds[i] == s.radioDecoderBaud) baudSel = i;
    }
    pr->radioDecBaud = combo(gr, "Serial speed (the radio's Decode Baud Rate)", bauds, baudSel);
  }
  // --- Transmit
  {
    GtkWidget* pg = page(dlg, "Transmit", "media-record-symbolic");
    GtkWidget* g = group(pg, "Keying");
    pr->txMethod = combo(g, "Method", {"AFSK (audio)", "FSK via serial UART TXD", "FSK via DTR", "FSK via RTS", "CAT text (rig keyer, K3/K4/TS-890)"},
                         static_cast<int>(s.tx.method));
    pr->fskDevice = deviceEntry(g, "FSK serial device", s.tx.fskDevice);
    pr->fskInvert = toggle(g, "Invert FSK polarity (DTR/RTS)", s.tx.fskInvert);
    pr->diddle = toggle(g, "Diddle (send LTRS when idle)", s.tx.diddle);
    pr->wordMode = toggle(g, "Word mode type-ahead", s.txWordMode, "Send typed text only after each space or Enter, so typos can be fixed");
    pr->afskLevel = spin(g, "AFSK level (%)", 1, 100, 1, s.tx.afskLevel * 100.0);
    GtkWidget* g2 = group(pg, "Sequencing");
    pr->pttViaRig = toggle(g2, "Key PTT through the rig", s.tx.pttViaRig,
                           "Off for VOX or external keying. A PTT line on a serial port of its own (Rig page) is keyed either way");
    static const char* modes[] = {"", "RTTY", "RTTYR", "LSB", "USB", "PKTLSB", "PKTUSB"};
    int modeSel = 0;
    for (int i = 0; i < 7; ++i) if (s.tx.txMode == modes[i]) modeSel = i;
    pr->txMode = combo(g2, "Rig mode on TX", {"(leave)", "RTTY", "RTTYR", "LSB", "USB", "PKTLSB", "PKTUSB"}, modeSel);
    pr->preamble = spin(g2, "LTRS preamble characters", 0, 10, 1, s.tx.ltrsPreamble);
    pr->pttLead = spin(g2, "PTT lead time (ms)", 0, 1000, 10, s.tx.pttLeadMs);
    GtkWidget* g3 = group(pg, "After transmitting");
    pr->autoRx = toggle(g3, "Auto RX", s.autoRx,
                        "Return to receive once the TX buffer has been empty for the delay below. Off: stay keyed and idle until RX is pressed");
    pr->autoRxDelay = spin(g3, "Auto RX delay (s)", 0.5, 30, 0.5, s.autoRxDelaySec, 1);
    pr->clearTxOnRx = toggle(g3, "Clear the TX pane on return to receive", s.clearTxOnRx);
    GtkWidget* g4 = group(pg, "Auto CQ");
    pr->autoCqInterval = spin(g4, "Pause between repeats (s)", 1, 120, 1, s.autoCqIntervalSec);
    pr->autoCqMacro = spin(g4, "Macro button to repeat (F1–F12)", 1, 12, 1, s.autoCqMacro);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->autoCqMacro), "Start/stop with the Auto CQ box in the control row; stops when a callsign is entered");
    GtkWidget* g5 = group(pg, "TX monitor");
    pr->monitorLevel = spin(g5, "Monitor level for FSK / CAT keying (%, 0 = off)", 0, 100, 5, s.tx.monitorLevel * 100.0);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->monitorLevel), "Plays an AFSK copy of the keyed text on the output device, which is free when the rig is keyed by FSK");
    GtkWidget* g6 = group(pg, "CW identifier");
    pr->cwIdAtEnd = toggle(g6, "Send CW ID at the end of every transmission", s.tx.cwIdAtEnd);
    pr->cwIdText = entry(g6, "ID text (blank = callsign)", s.tx.cwIdText);
    pr->cwIdWpm = spin(g6, "Speed (WPM)", 5, 40, 1, s.tx.cwIdWpm);
    pr->cwIdTone = spin(g6, "AFSK tone (Hz, 0 = mark tone)", 0, 3000, 25, s.tx.cwIdToneHz);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->cwIdTone), "With FSK or CAT keying the ID is sent by keying PTT, which carries the mark tone; also available from the menu and the <CWID> macro tag");
    GtkWidget* g7 = group(pg, "Tune");
    pr->tuneTimeout = spin(g7, "Tune time-out (s, 0 = until stopped)", 0, 120, 5, s.tx.tuneTimeoutSec);
  }
  // --- Station
  {
    GtkWidget* pg = page(dlg, "Station", "avatar-default-symbolic");
    GtkWidget* g = group(pg, "Operator");
    pr->myCall = entry(g, "Callsign", s.myCall);
    pr->myName = entry(g, "Name", s.myName);
    pr->myQth = entry(g, "QTH", s.myQth);
    pr->myGrid = entry(g, "Grid locator", s.myGrid);
    GtkWidget* g2 = group(pg, "Logging and display");
    pr->logPath = entry(g2, "Logbook database", s.logDbPath);
    pr->scpPath = entry(g2, "Super Check Partial file (MASTER.SCP)", s.scpPath);
    pr->wfLo = spin(g2, "Waterfall low edge (Hz)", 0, 4000, 50, s.waterfallLoHz);
    pr->wfHi = spin(g2, "Waterfall high edge (Hz)", 500, 6000, 50, s.waterfallHiHz);
    pr->wfDir = combo(g2, "Waterfall direction",
                      {"Follow rig sideband (matches the rig's scope)", "Low audio on the left", "Low audio on the right"},
                      s.waterfallDirection);
    pr->fontSize = spin(g2, "Text font size (px)", 8, 32, 1, s.fontSize);
    pr->rxTimestamps = toggle(g2, "Timestamps at the start of each received and sent line", s.rxTimestamps);
    pr->rxLogToFile = toggle(g2, "Save received and sent text to a daily session file", s.rxLogToFile);
    pr->rxLogDir = entry(g2, "Session file directory", s.rxLogDir);
    pr->modemOnly = toggle(g2, "Modem-only view", s.modemOnly,
                           "Hide the band buttons, macros, log row and sidebar (Ctrl+M)");
    pr->theme = combo(g2, "Appearance", {"Follow system", "Light", "Dark"}, s.theme);
    // Preview immediately while the dialog is open.
    g_signal_connect(pr->theme, "notify::selected", G_CALLBACK(+[](GObject* row, GParamSpec*, gpointer) {
      int i = static_cast<int>(adw_combo_row_get_selected(ADW_COMBO_ROW(row)));
      adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
          i == 1 ? ADW_COLOR_SCHEME_FORCE_LIGHT : i == 2 ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_DEFAULT);
    }), nullptr);
  }

  // --- DX cluster
  {
    GtkWidget* pg = page(dlg, "Cluster", "network-server-symbolic");
    GtkWidget* g = group(pg, "DX cluster (telnet)");
    pr->clEnabled = toggle(g, "Connect to a DX cluster", s.clusterEnabled, "Spots fill the band map; click one to QSY and take the call");
    pr->clHost = entry(g, "Host (for example dxc.nc7j.com, dxspider.co.uk, ve7cc.net)", s.clusterHost);
    pr->clPort = spin(g, "Port (DXSpider 7300, AR-Cluster 7373, CC Cluster 23)", 1, 65535, 1, s.clusterPort);
    pr->clLogin = entry(g, "Login callsign (blank = station callsign)", s.clusterLogin);
    pr->clCommands = entry(g, "Commands after login, separated by ';'", s.clusterCommands);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->clCommands), "For example: set/filter doc/pass rtty ; sh/dx 20");
    GtkWidget* g2 = group(pg, "Band map");
    pr->clRttyOnly = toggle(g2, "RTTY spots only", s.clusterRttyOnly, "Comment mentions RTTY, or no other mode is named and the frequency is in a digital segment");
    pr->clThisBand = toggle(g2, "Current band only", s.bandMapThisBand);
    pr->clMaxAge = spin(g2, "Drop spots older than (min)", 5, 240, 5, s.clusterMaxAgeMin);
  }
  // --- Callbook
  {
    GtkWidget* pg = page(dlg, "Callbook", "contact-new-symbolic");
    GtkWidget* g = group(pg, "Callsign lookup");
    pr->cbProvider = combo(g, "Service", {"None", "QRZ.com XML (subscription)", "HamQTH (free account)"}, s.callbookProvider);
    if (!Callbook::available())
      adw_action_row_set_subtitle(ADW_ACTION_ROW(pr->cbProvider), "This build has no libcurl; lookups are disabled");
    pr->cbUser = entry(g, "Username", s.callbookUser);
    pr->cbPass = adw_password_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pr->cbPass), "Password");
    gtk_editable_set_text(GTK_EDITABLE(pr->cbPass), s.callbookPass.c_str());
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), pr->cbPass);
    pr->cbAutoFill = toggle(g, "Fill Name, QTH and grid from the lookup", s.callbookAutoFill,
                            "Previous QSOs from the logbook are shown first; a lookup runs once a complete callsign is typed");
    GtkWidget* note = gtk_label_new("The password is stored in plain text in this configuration's file under ~/.config/gtty/profiles.");
    gtk_widget_add_css_class(note, "dim-label");
    gtk_widget_add_css_class(note, "caption");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.f);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), note);
  }
  // --- Bands
  {
    GtkWidget* pg = page(dlg, "Bands", "view-list-symbolic");
    GtkWidget* g = group(pg, "Band button dial frequencies (kHz)");
    for (const auto& b : s.bands)
      pr->bandRows.push_back(spin(g, b.label.c_str(), 100, 500000, 0.5, b.hz / 1000.0, 1));
  }
  // --- GridTracker
  {
    GtkWidget* pg = page(dlg, "Integration", "network-transmit-receive-symbolic");
    GtkWidget* g = group(pg, "WSJT-X UDP protocol (GridTracker2, JTAlert, log bridges)");
    pr->gtEnabled = toggle(g, "Enable", s.gtEnabled, "Sends heartbeat, status, decoded callsigns and logged QSOs");
    pr->gtHost = entry(g, "Host (127.0.0.1, or multicast 224.0.0.1)", s.gtHost);
    pr->gtPort = spin(g, "UDP port", 1, 65535, 1, s.gtPort);
    GtkWidget* g2 = group(pg, "What to exchange");
    pr->gtDecodes = toggle(g2, "Report decoded callsigns", s.gtSendDecodes, "Each callsign heard appears on the GridTracker map");
    pr->gtLogged = toggle(g2, "Report logged QSOs", s.gtSendLogged, "GridTracker can forward them to your online logs");
    pr->gtReply = toggle(g2, "Accept replies from GridTracker", s.gtAcceptReply, "Clicking a station in GridTracker fills the Call field");
    GtkWidget* g3 = group(pg, "fldigi-compatible XML-RPC (CQRLOG, TLF, other loggers)");
    pr->rpcEnabled = toggle(g3, "Enable XML-RPC server", s.rpcEnabled, "Loggers that drive fldigi can drive gtty: text, TX/RX, frequency, log fields");
    pr->rpcBind = entry(g3, "Bind address", s.rpcBind);
    pr->rpcPort = spin(g3, "Port (fldigi default 7362)", 1, 65535, 1, s.rpcPort);
    GtkWidget* g4 = group(pg, "Logbook server (fllog, QLog)");
    pr->fllogEnabled = toggle(g4, "Send logged QSOs", s.fllogEnabled,
                              "Each QSO logged in gtty is handed over as fldigi hands it to fllog (XML-RPC log.add_record)");
    pr->fllogHost = entry(g4, "Host", s.fllogHost);
    pr->fllogPort = spin(g4, "Port (fllog and QLog: 8421)", 1, 65535, 1, s.fllogPort);
    GtkWidget* gx = group(pg, "Xlog");
    pr->xlogEnabled = toggle(gx, "Send logged QSOs", s.xlogEnabled,
                             "Each QSO logged in gtty is handed to Xlog as fldigi hands it over (Xlog remote data)");
    pr->xlogHost = entry(gx, "Host", s.xlogHost);
    pr->xlogPort = spin(gx, "Port (Xlog: 7311)", 1, 65535, 1, s.xlogPort);
    GtkWidget* g5 = group(pg, "Skimmer spot feed (telnet)");
    pr->spotEnabled = toggle(g5, "Serve skimmer spots", s.spotServerEnabled,
                             "Loggers connect as to a DX cluster and get a spot for every station the Skimmer identifies. Needs the Skimmer on");
    pr->spotCqOnly = toggle(g5, "Only stations calling CQ", s.spotServerCqOnly,
                            "What a band map wants, and what the Reverse Beacon Network spots: not the stations answering them");
    pr->spotBind = entry(g5, "Bind address", s.spotServerBind);
    pr->spotPort = spin(g5, "Port", 1, 65535, 1, s.spotServerPort);
    GtkWidget* g6 = group(pg, "PSK Reporter");
    pr->pskrEnabled = toggle(g6, "Report decoded stations", s.pskReporterEnabled,
                             "Stations the receiver and the Skimmer identify appear on pskreporter.info. Needs your callsign and grid");
  }

  g_signal_connect(win, "close-request", G_CALLBACK(onCloseRequest), pr);
  gtk_window_present(GTK_WINDOW(win));
}

}  // namespace gtty
