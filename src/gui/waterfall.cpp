#include "waterfall.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gtty {

Waterfall::Waterfall() {
  area_ = gtk_drawing_area_new();
  gtk_widget_set_hexpand(area_, TRUE);
  gtk_widget_set_vexpand(area_, TRUE);
  gtk_widget_set_size_request(area_, 400, 200);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_), draw, this, nullptr);
  g_signal_connect(area_, "resize", G_CALLBACK(resized), this);

  // Right button: rig retune. Left button is handled by the drag gesture below
  // (a press without movement acts as a click).
  GtkGesture* click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
  g_signal_connect(click, "pressed", G_CALLBACK(pressed), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(click));

  GtkGesture* middle = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), GDK_BUTTON_MIDDLE);
  g_signal_connect(middle, "pressed", G_CALLBACK(middlePressed), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(middle));

  GtkGesture* drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(dragBegin), this);
  g_signal_connect(drag, "drag-update", G_CALLBACK(dragUpdate), this);
  g_signal_connect(drag, "drag-end", G_CALLBACK(dragEnd), this);
  gtk_widget_add_controller(area_, GTK_EVENT_CONTROLLER(drag));

  GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll, "scroll", G_CALLBACK(scrolled), this);
  gtk_widget_add_controller(area_, scroll);

  GtkEventController* motionCtl = gtk_event_controller_motion_new();
  g_signal_connect(motionCtl, "motion", G_CALLBACK(&Waterfall::motion), this);
  g_signal_connect_swapped(motionCtl, "leave", G_CALLBACK(+[](Waterfall* self) {
    self->hoverHz_ = -1;
    gtk_widget_queue_draw(self->area_);
  }), this);
  gtk_widget_add_controller(area_, motionCtl);
}

Waterfall::~Waterfall() {
  if (surf_) cairo_surface_destroy(surf_);
}

void Waterfall::setRange(double loHz, double hiHz) {
  hiHz = std::max(hiHz, loHz + 100);
  if (loHz == lo_ && hiHz == hi_) return;
  lo_ = loHz; hi_ = hiHz;
  blankHistory();
}

void Waterfall::setReversed(bool reversed) {
  if (reversed == reversed_) return;
  reversed_ = reversed;
  blankHistory();
}

// The history no longer lines up with the new scale: blank it, keep the surface.
void Waterfall::blankHistory() {
  if (surf_) {
    cairo_t* cr = cairo_create(surf_);
    cairo_set_source_rgb(cr, 0, 0, 0.08);
    cairo_paint(cr);
    cairo_destroy(cr);
  }
  gtk_widget_queue_draw(area_);
}

void Waterfall::setTones(double markHz, double spaceHz) {
  mark_ = markHz; space_ = spaceHz;
  gtk_widget_queue_draw(area_);
}

void Waterfall::setNominalTones(double markHz, double spaceHz) {
  nomMark_ = markHz; nomSpace_ = spaceHz;
}

void Waterfall::resized(GtkDrawingArea*, int w, int h, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  self->ensureSurface(w, std::max(1, h - kSpectrumHeight));
}

void Waterfall::ensureSurface(int w, int h) {
  if (surf_ && sw_ == w && sh_ == h) return;
  cairo_surface_t* n = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
  cairo_t* cr = cairo_create(n);
  cairo_set_source_rgb(cr, 0, 0, 0.08);
  cairo_paint(cr);
  if (surf_) {
    cairo_set_source_surface(cr, surf_, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surf_);
  }
  cairo_destroy(cr);
  surf_ = n;
  sw_ = w; sh_ = h;
}

void Waterfall::colormap(float v, unsigned char* px) {
  // 0..1 -> dark blue -> blue -> cyan -> yellow -> white
  v = std::clamp(v, 0.f, 1.f);
  float r, g, b;
  if (v < 0.25f) { float t = v / 0.25f; r = 0; g = 0; b = 0.2f + 0.8f * t; }
  else if (v < 0.5f) { float t = (v - 0.25f) / 0.25f; r = 0; g = t; b = 1; }
  else if (v < 0.75f) { float t = (v - 0.5f) / 0.25f; r = t; g = 1; b = 1 - t; }
  else { float t = (v - 0.75f) / 0.25f; r = 1; g = 1; b = t; }
  px[0] = static_cast<unsigned char>(b * 255);
  px[1] = static_cast<unsigned char>(g * 255);
  px[2] = static_cast<unsigned char>(r * 255);
  px[3] = 255;
}

void Waterfall::addSpectrum(const std::vector<float>& db, double binHz) {
  lastDb_ = db;
  binHz_ = binHz;
  if (!surf_) {
    int w = gtk_widget_get_width(area_), h = gtk_widget_get_height(area_) - kSpectrumHeight;
    if (w > 0 && h > 0) ensureSurface(w, h);
  }
  if (!surf_ || sw_ <= 0 || sh_ <= 0) { gtk_widget_queue_draw(area_); return; }

  // Track the noise floor (median-ish) and peak for auto-ranging.
  size_t b0 = std::min(db.size() - 1, static_cast<size_t>(lo_ / binHz)), b1 = std::min(db.size() - 1, static_cast<size_t>(hi_ / binHz));
  if (b1 <= b0 + 2) return;
  std::vector<float> tmp(db.begin() + b0, db.begin() + b1);
  std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 3, tmp.end());
  float fl = tmp[tmp.size() / 3];
  floorDb_ = 0.95f * floorDb_ + 0.05f * fl;
  float peak = *std::max_element(db.begin() + b0, db.begin() + b1);
  float desired = std::max(30.f, peak - floorDb_ + 6.f);
  rangeDb_ = 0.95f * rangeDb_ + 0.05f * desired;

  // Scroll down one row.
  cairo_surface_flush(surf_);
  unsigned char* data = cairo_image_surface_get_data(surf_);
  int stride = cairo_image_surface_get_stride(surf_);
  std::memmove(data + stride, data, static_cast<size_t>(stride) * (sh_ - 1));
  // Draw the new top row: for each pixel take the max over the bins it spans.
  for (int x = 0; x < sw_; ++x) {
    double f0 = std::min(xToHz(x, sw_), xToHz(x + 1, sw_)), f1 = std::max(xToHz(x, sw_), xToHz(x + 1, sw_));
    size_t k0 = static_cast<size_t>(f0 / binHz), k1 = std::max(k0 + 1, static_cast<size_t>(f1 / binHz));
    float m = -200;
    for (size_t k = k0; k < k1 && k < db.size(); ++k) m = std::max(m, db[k]);
    float v = (m - floorDb_ + 3.f) / rangeDb_;
    colormap(v, data + x * 4);
  }
  cairo_surface_mark_dirty(surf_);
  gtk_widget_queue_draw(area_);
}

void Waterfall::draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  const int sh = kSpectrumHeight;
  // Spectrum panel
  cairo_set_source_rgb(cr, 0.08, 0.08, 0.1);
  cairo_rectangle(cr, 0, 0, w, sh);
  cairo_fill(cr);
  if (!self->lastDb_.empty()) {
    cairo_set_source_rgb(cr, 0.6, 0.9, 0.6);
    cairo_set_line_width(cr, 1.0);
    bool first = true;
    for (int x = 0; x < w; ++x) {
      double f0 = std::min(self->xToHz(x, w), self->xToHz(x + 1, w)), f1 = std::max(self->xToHz(x, w), self->xToHz(x + 1, w));
      size_t k0 = static_cast<size_t>(f0 / self->binHz_), k1 = std::max(k0 + 1, static_cast<size_t>(f1 / self->binHz_));
      float m = -200;
      for (size_t k = k0; k < k1 && k < self->lastDb_.size(); ++k) m = std::max(m, self->lastDb_[k]);
      double v = std::clamp((m - self->floorDb_ + 3.0) / self->rangeDb_, 0.0, 1.0);
      double y = sh - 4 - v * (sh - 8);
      if (first) { cairo_move_to(cr, x, y); first = false; } else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
  }
  // Waterfall image
  if (self->surf_) {
    cairo_set_source_surface(cr, self->surf_, 0, sh);
    cairo_paint(cr);
  }
  // Frequency scale ticks on the spectrum panel
  cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
  cairo_set_font_size(cr, 10);
  double step = (self->hi_ - self->lo_) > 2000 ? 500 : 250;
  for (double f = std::ceil(self->lo_ / step) * step; f <= self->hi_; f += step) {
    double x = self->hzToX(f, w);
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, 6);
    cairo_stroke(cr);
    char buf[16];
    snprintf(buf, sizeof buf, "%.0f", f);
    cairo_move_to(cr, self->reversed_ ? x - 2 - 6.0 * std::strlen(buf) : x + 2, 14);
    cairo_show_text(cr, buf);
  }
  // Tone window: translucent band between the tracked mark and space tones.
  {
    double xa = self->hzToX(std::min(self->mark_, self->space_), w);
    double xb = self->hzToX(std::max(self->mark_, self->space_), w);
    cairo_set_source_rgba(cr, 1, 1, 1, self->dragging_ ? 0.22 : 0.12);
    cairo_rectangle(cr, xa, 0, xb - xa, h);
    cairo_fill(cr);
  }
  // Mark / space cursors
  auto cursor = [&](double hz, double r, double g, double b, bool dashed) {
    double x = self->hzToX(hz, w);
    cairo_set_source_rgba(cr, r, g, b, 0.9);
    cairo_set_line_width(cr, dashed ? 1.0 : 1.5);
    if (dashed) { double d[] = {3, 3}; cairo_set_dash(cr, d, 2, 0); } else cairo_set_dash(cr, nullptr, 0, 0);
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, h);
    cairo_stroke(cr);
  };
  for (double nz : self->notches_) {
    double x = self->hzToX(nz, w);
    cairo_set_source_rgba(cr, 1, 0.85, 0.2, 0.9);
    cairo_set_line_width(cr, 2.0);
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_move_to(cr, x, 0);
    cairo_line_to(cr, x, h);
    cairo_stroke(cr);
    cairo_move_to(cr, x - 5, 4);
    cairo_line_to(cr, x + 5, 4);
    cairo_line_to(cr, x, 12);
    cairo_close_path(cr);
    cairo_fill(cr);
  }
  cursor(self->nomMark_, 1, 1, 1, true);
  cursor(self->nomSpace_, 1, 1, 1, true);
  cursor(self->mark_, 1, 0.3, 0.3, false);
  cursor(self->space_, 0.3, 0.6, 1, false);
  cairo_set_dash(cr, nullptr, 0, 0);
  // Hover readout
  if (self->hoverHz_ >= 0) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.0f Hz", self->hoverHz_);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_set_font_size(cr, 11);
    cairo_move_to(cr, w - 70, sh - 6);
    cairo_show_text(cr, buf);
  }
}

void Waterfall::pressed(GtkGestureClick*, int, double x, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  int w = gtk_widget_get_width(self->area_);
  if (self->onRightClickTune) self->onRightClickTune(self->xToHz(x, w));
}

void Waterfall::middlePressed(GtkGestureClick*, int, double x, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  int w = gtk_widget_get_width(self->area_);
  if (self->onMiddleClick) self->onMiddleClick(self->xToHz(x, w));
}

void Waterfall::dragBegin(GtkGestureDrag*, double x, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  int w = gtk_widget_get_width(self->area_);
  self->dragStartX_ = x;
  self->dragStartCenter_ = (self->nomMark_ + self->nomSpace_) / 2.0;
  double lo = std::min(self->nomMark_, self->nomSpace_), hi = std::max(self->nomMark_, self->nomSpace_);
  double margin = 12.0 * (self->hi_ - self->lo_) / w;  // 12 px grab margin
  double hz = self->xToHz(x, w);
  self->dragInsideWindow_ = hz >= lo - margin && hz <= hi + margin;
  self->dragging_ = true;
  self->dragMoved_ = false;
}

void Waterfall::dragUpdate(GtkGestureDrag*, double dx, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  if (!self->dragging_) return;
  if (!self->dragMoved_ && std::fabs(dx) < 3.0) return;  // treat tiny movements as a click
  self->dragMoved_ = true;
  int w = gtk_widget_get_width(self->area_);
  double hzPerPx = (self->hi_ - self->lo_) / w * (self->reversed_ ? -1.0 : 1.0);
  double hz = self->dragInsideWindow_ ? self->dragStartCenter_ + dx * hzPerPx
                                      : self->xToHz(self->dragStartX_ + dx, w);
  self->hoverHz_ = self->xToHz(self->dragStartX_ + dx, w);
  if (self->onClickTune) self->onClickTune(hz);
}

void Waterfall::dragEnd(GtkGestureDrag*, double dx, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  if (!self->dragging_) return;
  self->dragging_ = false;
  if (!self->dragMoved_) {  // plain click: jump to the pressed position
    int w = gtk_widget_get_width(self->area_);
    if (self->onClickTune) self->onClickTune(self->xToHz(self->dragStartX_, w));
  }
}

gboolean Waterfall::scrolled(GtkEventControllerScroll*, double, double dy, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  if (self->onNudge && dy != 0) self->onNudge((dy > 0 ? -10.0 : 10.0) * (self->reversed_ ? -1.0 : 1.0));
  return TRUE;
}

void Waterfall::motion(GtkEventControllerMotion*, double x, double, gpointer p) {
  auto* self = static_cast<Waterfall*>(p);
  int w = gtk_widget_get_width(self->area_);
  self->hoverHz_ = self->xToHz(x, w);
  gtk_widget_queue_draw(self->area_);
}

}  // namespace gtty
