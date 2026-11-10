// Scrolling waterfall + spectrum with mark/space cursors. GtkDrawingArea based.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <vector>

namespace gtty {

class Waterfall {
 public:
  Waterfall();
  ~Waterfall();
  GtkWidget* widget() const { return area_; }

  void setRange(double loHz, double hiHz);
  void setTones(double markHz, double spaceHz);  // cursor positions (AFC-tracked)
  void setNominalTones(double markHz, double spaceHz);
  // Draw high audio frequencies on the left, so the display matches a rig's
  // RF scope in a lower-sideband mode (RTTY-L, LSB).
  void setReversed(bool reversed);
  bool reversed() const { return reversed_; }
  void addSpectrum(const std::vector<float>& db, double binHz);

  // Left click or drag: desired centre of the tone pair (Hz). Right click: same, for rig retune.
  // Scroll wheel: nudge by deltaHz.
  std::function<void(double hz)> onClickTune;
  std::function<void(double hz)> onRightClickTune;
  std::function<void(double deltaHz)> onNudge;
  std::function<void(double hz)> onMiddleClick;   // toggle a notch
  void setNotches(const std::vector<double>& hz) { notches_ = hz; gtk_widget_queue_draw(area_); }

 private:
  static void draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer self);
  static void pressed(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static void middlePressed(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static void dragBegin(GtkGestureDrag* g, double x, double y, gpointer self);
  static void dragUpdate(GtkGestureDrag* g, double dx, double dy, gpointer self);
  static void dragEnd(GtkGestureDrag* g, double dx, double dy, gpointer self);
  static gboolean scrolled(GtkEventControllerScroll* c, double dx, double dy, gpointer self);
  static void motion(GtkEventControllerMotion*, double x, double y, gpointer self);
  static void resized(GtkDrawingArea*, int w, int h, gpointer self);
  void ensureSurface(int w, int h);
  double xToHz(double x, int w) const {
    double t = x / w;
    return lo_ + (reversed_ ? 1.0 - t : t) * (hi_ - lo_);
  }
  double hzToX(double hz, int w) const {
    double t = (hz - lo_) / (hi_ - lo_);
    return (reversed_ ? 1.0 - t : t) * w;
  }
  void blankHistory();
  static void colormap(float v, unsigned char* bgra);

  GtkWidget* area_;
  cairo_surface_t* surf_ = nullptr;
  int sw_ = 0, sh_ = 0;
  double lo_ = 300, hi_ = 3000;
  bool reversed_ = false;
  double mark_ = 2125, space_ = 2295;
  double nomMark_ = 2125, nomSpace_ = 2295;
  std::vector<float> lastDb_;
  double binHz_ = 1;
  float floorDb_ = -80, rangeDb_ = 60;  // auto-tracked
  double hoverHz_ = -1;
  std::vector<double> notches_;
  // drag state
  double dragStartX_ = 0;
  double dragStartCenter_ = 0;
  bool dragInsideWindow_ = false;
  bool dragging_ = false;
  bool dragMoved_ = false;
  static constexpr int kSpectrumHeight = 70;
};

}  // namespace gtty
