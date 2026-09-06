#include "arcoui/geometry.hpp"

#include <cmath>

namespace arcoui {
namespace {

// M_PI is not standard C++ -- glibc defines it unconditionally (why this built fine on every
// Linux target so far), but MinGW/MSVC only define it when _USE_MATH_DEFINES is set before
// <cmath> is included. Found by actually exercising the Windows cross-compile target
// (tests/integration/arcofission_windows_capsule_smoke.sh) -- matches this project's own existing
// convention elsewhere (runtime.cpp, glfw_backend.cpp) of a local literal instead of M_PI.
constexpr double kPi = 3.14159265358979323846;

// Regular hexagon inscribed in the [0,width] x [0,height] box, flat-top orientation (matches the
// hex intent-menu cells in the RFC's reference mockup), centered in the box, radius bounded by
// whichever dimension is tighter so the hexagon never spills outside its bounding box.
std::vector<Point> hexagon_polygon(double width, double height) {
    const double cx = width / 2.0;
    const double cy = height / 2.0;
    const double radius = std::min(width, height) / 2.0;
    std::vector<Point> points;
    points.reserve(6);
    for (int i = 0; i < 6; ++i) {
        const double angle = (kPi / 180.0) * (60.0 * i); // flat-top: vertices at 0/60/120/...
        points.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
    }
    return points;
}

std::vector<Point> diamond_polygon(double width, double height) {
    return {
        {width / 2.0, 0.0},
        {width, height / 2.0},
        {width / 2.0, height},
        {0.0, height / 2.0},
    };
}

std::vector<Point> rectangle_polygon(double width, double height) {
    return {
        {0.0, 0.0},
        {width, 0.0},
        {width, height},
        {0.0, height},
    };
}

} // namespace

std::vector<Point> shape_polygon(Shape shape, double width, double height, const std::vector<Point>& custom) {
    switch (shape) {
        case Shape::Diamond: return diamond_polygon(width, height);
        case Shape::Hexagon: return hexagon_polygon(width, height);
        case Shape::Polygon: return custom;
        case Shape::Rectangle:
        default: return rectangle_polygon(width, height);
    }
}

bool point_in_polygon(const std::vector<Point>& polygon, double px, double py) {
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        const bool straddles = (a.y > py) != (b.y > py);
        if (!straddles) continue;
        const double x_at_py = a.x + (py - a.y) * (b.x - a.x) / (b.y - a.y);
        if (px < x_at_py) inside = !inside;
    }
    return inside;
}

bool point_in_shape(Shape shape, double width, double height, const std::vector<Point>& custom, double px, double py) {
    return point_in_polygon(shape_polygon(shape, width, height, custom), px, py);
}

} // namespace arcoui
