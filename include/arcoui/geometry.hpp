#pragma once

// Pure geometry for ArcoUI application-surface shapes. Deliberately dependency-free (no arco::
// anything, no backend headers) so it can be shared, unchanged, between:
//   - a backend's fill/mask rendering (glfw_backend.cpp's new fill_polygon, canvas_backend.cpp's
//     canvas path fill), and
//   - ArcoUI core hit-testing (Surface point membership, click-through decisions).
// The RFC's shape-aware requirement (see RFC-ArcoUI section 9.3/42) only holds if a shape's
// visible fill and its input hit-test come from the exact same polygon -- that is the entire
// reason this lives in one place instead of being reimplemented per call site.

#include <vector>

namespace arcoui {

enum class Shape {
    Rectangle,
    Diamond,
    Hexagon,
    Polygon, // arbitrary caller-supplied point list
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

// Returns the shape's boundary as a closed polygon (implicit closing edge from the last point
// back to the first) in local surface coordinates, i.e. spanning [0,width] x [0,height].
// `custom` is only consulted (and must be non-empty) when shape == Shape::Polygon; for any other
// shape it is ignored.
std::vector<Point> shape_polygon(Shape shape, double width, double height, const std::vector<Point>& custom = {});

// Standard even-odd ray-casting point-in-polygon test. `polygon` is treated as implicitly closed.
// A polygon with fewer than 3 points contains no points.
bool point_in_polygon(const std::vector<Point>& polygon, double px, double py);

// Convenience: point_in_polygon(shape_polygon(shape, width, height, custom), px, py).
bool point_in_shape(Shape shape, double width, double height, const std::vector<Point>& custom, double px, double py);

} // namespace arcoui
