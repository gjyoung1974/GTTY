#include "prefs_dialog.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/audio_engine.h"
#include "core/log/callbook.h"
#include "core/modem/codec2_demod.h"
#include "core/rig/rig_control.h"

namespace gtty {

namespace {

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
  GtkWidget *showVote, *skimmer, *skimmerStep;
  GtkWidget *baud, *mark, *space, *preset, *stopBits, *variant, *uos, *afcRange, *netTx;
  bool applyingPreset = false;
  GtkWidget *txMethod, *fskDevice, *fskInvert, *diddle, *afskLevel, *pttViaRig, *txMode, *preamble, *pttLead;
  GtkWidget *autoRx, *autoRxDelay, *clearTxOnRx, *wordMode, *autoCqInterval, *autoCqMacro;
  GtkWidget *rxTimestamps, *rxLogToFile, *rxLogDir;
  GtkWidget *rpcEnabled, *rpcBind, *rpcPort;
  GtkWidget *myCall, *myName, *myQth, *myGrid;
  GtkWidget *scpPath, *logPath, *wfLo, *wfHi, *wfDir, *fontSize, *theme, *modemOnly;
  GtkWidget *gtEnabled, *gtHost, *gtPort, *gtDecodes, *gtLogged, *gtReply;
  std::vector<GtkWidget*> bandRows;
};

GtkWidget* page(AdwDialog* dlg, const char* title, const char* icon) {
  GtkWidget* p = adw_preferences_page_new();
  adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(p), title);
  adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(p), icon);
  adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dlg), ADW_PREFERENCES_PAGE(p));
  return p;
}
GtkWidget* group(GtkWidget* pg, const char* title) {
  GtkWidget* g = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(g), title);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(pg), ADW_PREFERENCES_GROUP(g));
  return g;
}
GtkWidget* entry(GtkWidget* g, const char* title, const std::string& value) {
  GtkWidget* r = adw_entry_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
  gtk_editable_set_text(GTK_EDITABLE(r), value.c_str());
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(g), r);
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
GtkWidget* combo(GtkWidget* g, const char* title, const std::vector<std::string>& items, int selected, bool search = false) {
  GtkWidget* r = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title);
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

void onClosed(AdwDialog*, gpointer p) {
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
  s.rig.baud = static_cast<int>(value(pr->rigBaud));
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
  s.skimmerEnabled = active(pr->skimmer);
  s.skimmerStepHz = value(pr->skimmerStep);

  s.rtty.baud = value(pr->baud);
  s.rtty.setTones(value(pr->mark), value(pr->space));
  s.rtty.stopBits = selected(pr->stopBits) == 0 ? 1.5 : 2.0;
  s.rtty.variant = selected(pr->variant) == 0 ? BaudotVariant::ITA2 : BaudotVariant::USTTY;
  s.rtty.unshiftOnSpace = active(pr->uos);
  s.afcRangeHz = value(pr->afcRange);
  s.netTx = active(pr->netTx);

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
}

}  // namespace

void showPrefsDialog(GtkWidget* parent, Settings& s, std::function<void()> onApply) {
  auto* pr = new Prefs{};
  pr->s = &s;
  pr->onApply = std::move(onApply);
  AdwDialog* dlg = ADW_DIALOG(adw_preferences_dialog_new());
  std::string title = s.profile == Profiles::kDefault ? "Preferences" : "Preferences — " + s.profile;
  adw_dialog_set_title(dlg, title.c_str());
  adw_dialog_set_content_width(dlg, 640);
  adw_dialog_set_content_height(dlg, 720);

  // --- Audio
  {
    GtkWidget* pg = page(dlg, "Audio", "audio-card-symbolic");
    GtkWidget* g = group(pg, "Sound card");
    std::vector<std::string> ins{"(default)"}, outs{"(default)"};
    int inSel = 0, outSel = 0;
    for (const auto& d : AudioEngine::listDevices()) {
      std::string label = d.name + "  [" + d.hostApi + "]";
      if (d.maxInputs > 0) { ins.push_back(label); pr->inputs.push_back(d.name); if (d.name == s.inputDevice) inSel = static_cast<int>(ins.size()) - 1; }
      if (d.maxOutputs > 0) { outs.push_back(label); pr->outputs.push_back(d.name); if (d.name == s.outputDevice) outSel = static_cast<int>(outs.size()) - 1; }
    }
    pr->inputs.insert(pr->inputs.begin(), "");
    pr->outputs.insert(pr->outputs.begin(), "");
    pr->inDev = combo(g, "Input device", ins, inSel, true);
    pr->outDev = combo(g, "Output device", outs, outSel, true);
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
    pr->rigDevice = entry(g, "Device (serial port, or host:port for NET rigctl)", s.rig.device);
    pr->rigBaud = spin(g, "Serial speed (0 = default)", 0, 115200, 1200, s.rig.baud);
    GtkWidget* g2 = group(pg, "PTT");
    static const char* ptts[] = {"RIG", "DTR", "RTS", "NONE"};
    int pttSel = 0;
    for (int i = 0; i < 4; ++i) if (s.rig.pttType == ptts[i]) pttSel = i;
    pr->pttType = combo(g2, "PTT method", {"CAT command", "Serial DTR", "Serial RTS", "None (VOX)"}, pttSel);
    pr->pttDevice = entry(g2, "PTT serial device (blank = rig device)", s.rig.pttDevice);
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
    GtkWidget* gs = group(pg, "Skimmer");
    pr->skimmer = toggle(gs, "Decode the whole passband", s.skimmerEnabled,
                         "A decoder every step across the waterfall range; stations appear in the Skimmer list of the sidebar. Uses CPU");
    pr->skimmerStep = spin(gs, "Channel step (Hz)", 20, 200, 10, s.skimmerStepHz);
  }
  // --- Transmit
  {
    GtkWidget* pg = page(dlg, "Transmit", "media-record-symbolic");
    GtkWidget* g = group(pg, "Keying");
    pr->txMethod = combo(g, "Method", {"AFSK (audio)", "FSK via serial UART TXD", "FSK via DTR", "FSK via RTS", "CAT text (rig keyer, K3/K4/TS-890)"},
                         static_cast<int>(s.tx.method));
    pr->fskDevice = entry(g, "FSK serial device", s.tx.fskDevice);
    pr->fskInvert = toggle(g, "Invert FSK polarity (DTR/RTS)", s.tx.fskInvert);
    pr->diddle = toggle(g, "Diddle (send LTRS when idle)", s.tx.diddle);
    pr->wordMode = toggle(g, "Word mode type-ahead", s.txWordMode, "Send typed text only after each space or Enter, so typos can be fixed");
    pr->afskLevel = spin(g, "AFSK level (%)", 1, 100, 1, s.tx.afskLevel * 100.0);
    GtkWidget* g2 = group(pg, "Sequencing");
    pr->pttViaRig = toggle(g2, "Key PTT through the rig", s.tx.pttViaRig);
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
    pr->rxTimestamps = toggle(g2, "Timestamps at the start of each received line", s.rxTimestamps);
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
  }

  g_signal_connect(dlg, "closed", G_CALLBACK(onClosed), pr);
  adw_dialog_present(dlg, parent);
}

}  // namespace gtty
