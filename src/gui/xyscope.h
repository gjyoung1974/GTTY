// Crossed-ellipse RTTY tuning indicator: X = mark channel, Y = space channel.
#pragma once
#include <gtk/gtk.h>

#include <deque>
#include <vector>

#include "core/modem/rtty_demodulator.h"

namespace gtty {

class XyScope {
 public:
  XyScope();
  GtkWidget* widget() const { return area_; }
  void addPoints(const std::vector<ScopePoint>& pts);
  void setLevels(float mark, float space) { mark_ = mark; space_ = space; gtk_widget_queue_draw(area_); }

 private:
  static void draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer self);
  GtkWidget* area_;
  std::deque<ScopePoint> pts_;
  float mark_ = 0, space_ = 0;
  static constexpr size_t kMaxPoints = 600;
  static constexpr int kSize = 120;   // the eye's square, in px
};

}  // namespace gtty
