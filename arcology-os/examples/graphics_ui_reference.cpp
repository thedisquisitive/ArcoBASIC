#include "arco/graphics.hpp"

#include <algorithm>

// Reference composition for the future ArcoBASIC UEFI graphics fixture. Application code talks
// only to Surface, colors, layout, primitives, text, and passive UI elements; framebuffer address
// arithmetic remains inside the GOP/bootstrap binding.
void render_arcology_ui(arco::graphics::Surface& backbuffer) {
    using namespace arco::graphics;
    const Rect root{0, 0, static_cast<std::int32_t>(backbuffer.Width), static_cast<std::int32_t>(backbuffer.Height)};
    FillRect(backbuffer, root.x, root.y, root.width, root.height, Color::RGB(7, 12, 24));

    const Rect panel = Centered(root, std::min<std::int32_t>(root.width - 80, 720), std::min<std::int32_t>(root.height - 80, 480));
    Render(backbuffer, Panel{panel, Color::RGB(18, 28, 48), Color::RGB(72, 128, 192)});
    Render(backbuffer, Label{Align(panel, 300, 16, HorizontalAlign::Center, VerticalAlign::Top), "ARCOLOGY", Color::White()});
    Render(backbuffer, Label{{panel.x + 32, panel.y + 52, panel.width - 64, 16}, "WELCOME TO THE MACHINE", Color::RGB(170, 195, 220)});

    const std::int32_t row_x = panel.x + 48;
    const std::int32_t row_width = panel.width - 96;
    Render(backbuffer, Button{{row_x, panel.y + 104, row_width, 34}, "INSTALL ARCOLOGY"});
    Render(backbuffer, Button{{row_x, panel.y + 148, row_width, 34}, "RECOVERY"});
    Render(backbuffer, Button{{row_x, panel.y + 192, row_width, 34}, "DIAGNOSTICS"});
    Render(backbuffer, ProgressBar{{row_x, panel.y + panel.height - 72, row_width, 12}, 35});
    Render(backbuffer, Label{{row_x, panel.y + panel.height - 50, row_width, 16}, "SYSTEM READY", Color::RGB(110, 220, 160)});
}
