#include "xyscope.h"

#include <algorithm>
#include <cmath>

namespace gtty {

XyScope::XyScope() {
  area_ = gtk_drawing_area_new();
  gtk_widget_set_size_request(area_, kSize, kSize);
  gtk_widget_set_halign(area_, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(area_, GTK_ALIGN_START);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area_), draw, this, nullptr);
}

void XyScope::addPoints(const std::vector<ScopePoint>& pts) {
  for (const auto& p : pts) pts_.push_back(p);
  while (pts_.size() > kMaxPoints) pts_.pop_front();
  gtk_widget_queue_draw(area_);
}

void XyScope::draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer p) {
  auto* self = static_cast<XyScope*>(p);
  int s = std::min(w, h);
  double cx = w / 2.0, cy = h / 2.0, r = s / 2.0 - 2;
  cairo_set_source_rgb(cr, 0.05, 0.08, 0.05);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  // graticule
  cairo_set_source_rgba(cr, 0.3, 0.6, 0.3, 0.5);
  cairo_set_line_width(cr, 1);
  cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
  cairo_stroke(cr);
  cairo_move_to(cr, cx - r, cy); cairo_line_to(cr, cx + r, cy);
  cairo_move_to(cr, cx, cy - r); cairo_line_to(cr, cx, cy + r);
  cairo_stroke(cr);
  // trace with fading persistence
  size_t n = self->pts_.size();
  if (n > 1) {
    cairo_set_line_width(cr, 1.2);
    size_t i = 0;
    for (auto it = self->pts_.begin(); std::next(it) != self->pts_.end(); ++it, ++i) {
      double a = 0.15 + 0.85 * (static_cast<double>(i) / n);
      cairo_set_source_rgba(cr, 0.4, 1.0, 0.4, a);
      auto nx = std::next(it);
      cairo_move_to(cr, cx + std::clamp(it->x, -1.2f, 1.2f) * r, cy - std::clamp(it->y, -1.2f, 1.2f) * r);
      cairo_line_to(cr, cx + std::clamp(nx->x, -1.2f, 1.2f) * r, cy - std::clamp(nx->y, -1.2f, 1.2f) * r);
      cairo_stroke(cr);
    }
  }
  // mark / space level bars along the bottom
  double bw = w - 8;
  cairo_set_source_rgba(cr, 1, 0.3, 0.3, 0.9);
  cairo_rectangle(cr, 4, h - 8, bw * std::clamp(self->mark_, 0.f, 1.f), 3);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.3, 0.6, 1, 0.9);
  cairo_rectangle(cr, 4, h - 4, bw * std::clamp(self->space_, 0.f, 1.f), 3);
  cairo_fill(cr);
}

}  // namespace gtty
