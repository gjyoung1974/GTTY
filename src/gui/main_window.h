#pragma once
#include <adwaita.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/audio_engine.h"
#include "core/events.h"
#include "core/log/callbook.h"
#include "core/log/callsign.h"
#include "core/log/logbook.h"
#include "core/macros.h"
#include "core/net/cluster_client.h"
#include "core/net/wsjtx_udp.h"
#include "core/net/xmlrpc_server.h"

#include <fstream>
#include "core/receiver.h"
#include "core/rig/rig_control.h"
#include "core/transmitter.h"
#include "settings.h"
#include "waterfall.h"
#include "xyscope.h"

namespace gtty {

class MainWindow {
 public:
  explicit MainWindow(AdwApplication* app);
  ~MainWindow();
  void present() { gtk_window_present(GTK_WINDOW(window_)); }

 private:
  // construction
  void buildUi(AdwApplication* app);
  GtkWidget* buildHeader();
  GtkWidget* buildScopeColumn();
  GtkWidget* buildRxPanes();
  GtkWidget* buildTxPane();
  GtkWidget* buildMacroBar();
  GtkWidget* buildBandBar();
  GtkWidget* buildLogRow();
  GtkWidget* buildControlRow();
  GtkWidget* buildStatusBar();
  GtkWidget* buildBandMap();
  void installActions();
  void installShortcuts();
  void applyCss();
  void applyTheme();

  // runtime
  void openAudio();
  void openRig();
  void applyRttyParams(bool resetAfc);
  void syncControlsFromSettings();
  void poll();
  void handle(const Event& e);
  void appendRx(int slot, const std::string& text);
  void appendEcho(const std::string& text);
  void clearRx();
  void clearText();              // RX panes + TX buffer
  void updateFreqDisplay(const RigState& s);
  double dialFreqHz() const { return rigState_.connected ? rigState_.freqHz : settings_.manualDialHz; }
  // Lower-sideband operation: audio frequency rises as RF falls. RTTY is LSB by
  // convention, so this is also the assumption without a rig.
  bool lsbLike() const;
  void updateWaterfallDirection();
  void setDial(double hz);          // QSY the rig, or the manual dial when no rig
  void updateBandHighlight();
  static void setTooltipIfChanged(GtkWidget* w, const char* text);
  static void onBandClicked(GtkButton* b, gpointer self);

  // TX
  void startTx();
  void stopTx();
  void abortTx();
  void sendPendingTxText(bool flushAll = false);
  void insertTxText(const std::string& text);
  void runMacro(int idx);
  void editMacro(int idx);

  // logging
  void logQso();
  void clearQsoFields();
  void updateDupe();
  void pickWord(GtkTextView* view, double x, double y);
  void exportAdif();
  void toast(const std::string& msg);
  void showAudioProblem(const std::string& msg);   // empty hides the banner

  // configuration profiles (WSJT-X style "Configurations")
  void applySettings();            // re-open devices and services after settings_ changed
  void rebuildProfilesMenu();
  void updateTitle();
  void switchProfile(const std::string& name);
  void cloneProfile();             // copy the active profile under a new name and switch to it
  void renameProfile();
  void deleteProfile();
  void resetProfile();             // active profile back to built-in defaults
  void promptName(const std::string& title, const std::string& body, const std::string& initial,
                  std::function<void(const std::string&)> done);
  void confirm(const std::string& title, const std::string& body, const std::string& verb,
               std::function<void()> done);

  // decoders: per-engine modes, best-guess pane, skimmer
  void applyDecoderSettings();
  void onSkimmerText(const SkimmerTextEvent& e);
  void rebuildSkimList();
  void ageSkim();
  static void onModeNotify(GObject* drop, GParamSpec*, gpointer self);
  static void onSkimActivated(GtkListBox*, GtkListBoxRow* row, gpointer self);
  static void onSkimmerToggled(GSimpleAction* a, GVariant* v, gpointer self);

  // station aids: DX cluster band map, callbook, rig filter, tune, CW ID
  void openCluster();
  void addSpot(const Spot& s);
  void rebuildBandMap();
  void ageSpots();
  void setBandMapVisible(bool on);
  void setModemOnly(bool on);
  void scheduleCallLookup();
  void lookupCall();
  void applyCallbook(const CallbookResult& r);
  void sendCwId();
  void startTune();
  void updateRigControls();
  static void onSpotActivated(GtkListBox*, GtkListBoxRow* row, gpointer self);
  static void onFilterNotify(GObject*, GParamSpec*, gpointer self);
  static void onIfShiftChanged(GtkSpinButton* s, gpointer self);
  static void onTuneToggled(GtkToggleButton* b, gpointer self);
  static gboolean onCallLookupTimeout(gpointer self);
  static void onBandMapToggled(GSimpleAction* a, GVariant* v, gpointer self);
  static void onModemOnlyToggled(GSimpleAction* a, GVariant* v, gpointer self);

  // operating features
  void clearTxBuffer();
  void clearUnsentTx();
  void onTxEnded();
  void setAutoCq(bool on);
  void toggleNotch(double hz);
  void clearNotches();
  void openSessionLog();
  void sessionWrite(const std::string& text, bool tx);
  std::string unsentTxText() const;
  // fldigi-compatible XML-RPC
  void openRpc();
  xmlrpc::Value rpcHandler(const std::string& method, const std::vector<xmlrpc::Value>& params);  // server thread
  xmlrpc::Value rpcDispatch(const std::string& method, const std::vector<xmlrpc::Value>& params); // main thread

  // GridTracker2 / WSJT-X UDP
  void openGridTracker();
  void gtTick();
  void gtSendStatus();
  void gtReportLine(const std::string& line);
  void gtReportQso(const Qso& q);
  void gtHandle(const wsjtx::Incoming& in);

  // callbacks
  static gboolean onTick(gpointer self);
  static void onTxToggled(GtkToggleButton* b, gpointer self);
  static void onTxBufferChanged(GtkTextBuffer*, gpointer self);
  static void onRxClicked(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static void onMacroClicked(GtkButton* b, gpointer self);
  static void onMacroRightClick(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static gboolean onShortcut(GtkWidget*, GVariant* args, gpointer self);
  static void onControlChanged(GtkWidget*, gpointer self);
  static void onControlNotify(GObject*, GParamSpec*, gpointer self);
  static void onTonesNotify(GObject*, GParamSpec*, gpointer self);
  static void onCallChanged(GtkEditable*, gpointer self);
  static void onAction(GSimpleAction* a, GVariant*, gpointer self);
  static void onProfileSelected(GSimpleAction* a, GVariant* name, gpointer self);
  static void onAdifSaved(GObject* src, GAsyncResult* res, gpointer self);
  static gboolean onCloseRequest(GtkWindow*, gpointer self);

  // core
  Settings settings_;
  EventQueue events_;
  AudioEngine engine_;
  RigControl rig_;
  Receiver rx_;
  Transmitter tx_;
  Logbook log_;
  ScpDatabase scp_;
  MacroContext ctx_;
  RigState rigState_;
  double primaryAfc_ = 0;
  WsjtxUdpClient gt_;
  std::string rxLine_;                       // current line of the primary decoder
  std::map<std::string, int64_t> gtLastReported_;
  int64_t gtLastHeartbeat_ = 0, gtLastStatus_ = 0;
  std::string gtLastStatusKey_;
  bool suppressControlEvents_ = false;
  std::string openLogPath_;                  // logbook file currently open in log_
  ClusterClient cluster_;
  Callbook callbook_;
  std::vector<Spot> spots_;                  // band map, sorted by frequency
  bool bandMapDirty_ = false;
  int64_t bandMapRebuiltAt_ = 0, spotsAgedAt_ = 0;
  std::string bandMapBand_;
  std::string lookedUpCall_, callInfo_;      // worked-before / callbook line for the Call field
  guint callLookupId_ = 0;
  std::string lastCallbookError_;
  bool rigHasIfShift_ = false;
  std::string pttText_;
  struct SkimEntry { double markHz; std::string text, call; int64_t lastSeen; float quality; };
  std::vector<SkimEntry> skim_;
  bool skimDirty_ = false;
  int64_t skimRebuiltAt_ = 0;

  // widgets
  GtkWidget* window_ = nullptr;
  AdwToastOverlay* toasts_ = nullptr;
  GtkWidget* audioBanner_ = nullptr;         // persistent "audio not working" notice above the content
  GMenu* profilesMenu_ = nullptr;
  GSimpleAction* bandMapAction_ = nullptr;
  GSimpleAction* modemOnlyAction_ = nullptr;
  GtkWidget* bandBar_ = nullptr;
  GtkWidget* macroBar_ = nullptr;
  GtkWidget* logRow_ = nullptr;
  GSimpleAction* skimmerAction_ = nullptr;
  GtkWidget* sideStack_ = nullptr;
  GtkWidget* skimList_ = nullptr;
  GtkWidget* skimStatus_ = nullptr;
  std::vector<GtkWidget*> rxPanes_;          // per text slot (engines, then the vote slot)
  std::vector<GtkWidget*> modeDrops_;        // per bank slot; null where the engine has no modes
  GtkWidget* bandMapBox_ = nullptr;
  GtkWidget* bandMapList_ = nullptr;
  GtkWidget* bandMapStatus_ = nullptr;
  GtkWidget* bandMapThisBandCheck_ = nullptr;
  GtkWidget* clusterEntry_ = nullptr;
  GtkWidget* callInfoLabel_ = nullptr;
  GtkWidget* tuneButton_ = nullptr;
  GtkWidget* rigCtlBox_ = nullptr;
  GtkWidget* ifShiftRow_ = nullptr;
  GtkWidget* filterDrop_ = nullptr;
  GtkWidget* ifShiftSpin_ = nullptr;
  GSimpleAction* profileAction_ = nullptr;   // stateful "win.profile": the active profile name
  GtkWidget* freqLabel_ = nullptr;
  GtkWidget* modeLabel_ = nullptr;
  GtkWidget* pttLabel_ = nullptr;
  Waterfall wf_;
  XyScope scope_;
  GtkWidget* afcLabel_ = nullptr;
  GtkWidget* qualityBar_ = nullptr;
  GtkWidget* levelBar_ = nullptr;
  GtkWidget* statusLabel_ = nullptr;
  std::vector<GtkWidget*> rxViews_;
  std::vector<GtkTextBuffer*> rxBufs_;
  std::vector<GtkTextMark*> rxEndMarks_;
  std::vector<std::string> rxWord_;
  std::vector<GtkTextTag*> callTags_;
  std::vector<GtkTextTag*> scpTags_;
  std::vector<GtkTextTag*> echoTags_;
  GtkWidget* txView_ = nullptr;
  GtkTextBuffer* txBuf_ = nullptr;
  GtkTextMark* txSentMark_ = nullptr;
  GtkTextTag* txSentTag_ = nullptr;
  GtkWidget* txButton_ = nullptr;
  GtkWidget* afcCheck_ = nullptr;
  GtkWidget* atcCheck_ = nullptr;
  GtkWidget* revCheck_ = nullptr;
  GtkWidget* netCheck_ = nullptr;
  GtkWidget* autoRxCheck_ = nullptr;
  GtkWidget* autoCqCheck_ = nullptr;
  bool prevTxOn_ = false;
  int64_t txEndedAt_ = 0;
  bool autoCq_ = false;
  std::vector<bool> rxLineStart_;
  std::vector<GtkTextTag*> timeTags_;
  std::ofstream sessionFile_;
  std::string sessionDate_;
  std::string rxTextLog_;        // all primary-decoder text, for XML-RPC text.get_rx
  std::vector<double> notches_;
  xmlrpc::Server rpc_;
  std::string rpcRigName_;
  int64_t txEmptySince_ = 0;   // monotonic µs when the TX buffer last became empty (0 = not empty)
  bool txSentAny_ = false;     // something was sent in this transmission (arms Auto RX)
  GtkWidget* squelchScale_ = nullptr;
  GtkWidget* baudDrop_ = nullptr;
  GtkWidget* shiftDrop_ = nullptr;
  GtkWidget* tonesDrop_ = nullptr;
  GtkWidget* callEntry_ = nullptr;
  GtkWidget* rstSEntry_ = nullptr;
  GtkWidget* rstREntry_ = nullptr;
  GtkWidget* exchEntry_ = nullptr;
  GtkWidget* nameEntry_ = nullptr;
  GtkWidget* serialSpin_ = nullptr;
  GtkWidget* dupeLabel_ = nullptr;
  GtkWidget* qsoCountLabel_ = nullptr;
  std::vector<GtkWidget*> macroButtons_;
  std::vector<GtkWidget*> bandButtons_;
  guint tickId_ = 0;
};

}  // namespace gtty
