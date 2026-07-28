#include "widgets.hpp"
#include "gtk4_compat.hpp"

#include <cstdlib>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace lazarus::gui {

namespace {

std::string active_accent = "#f39a22";
std::string active_icon_color = "#f39a22";

std::string css_color_or(const std::string& value, const std::string& fallback) {
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#') {
        return fallback;
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return fallback;
        }
    }
    return value;
}

bool is_light_color(const std::string& value) {
    if (value.size() < 7 || value.front() != '#') {
        return false;
    }
    const auto component = [&value](std::size_t offset) {
        return std::stoi(value.substr(offset, 2), nullptr, 16);
    };
    return component(1) + component(3) + component(5) > 560;
}

std::filesystem::path icon_path(const std::string& name) {
    const char* configured = std::getenv("LAZARUS_ASSET_DIR");
    if (configured != nullptr && *configured != '\0') {
        const auto candidate = std::filesystem::path(configured) / "icons" / (name + ".svg");
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#ifdef LAZARUS_SOURCE_ICON_DIR
    const auto source_candidate = std::filesystem::path(LAZARUS_SOURCE_ICON_DIR) / (name + ".svg");
    if (std::filesystem::exists(source_candidate)) {
        return source_candidate;
    }
#endif
    const auto installed_candidate = std::filesystem::path("/usr/share/arcology-lazarus/assets/icons") / (name + ".svg");
    if (std::filesystem::exists(installed_candidate)) {
        return installed_candidate;
    }
    return {};
}

}  // namespace

void add_class(GtkWidget* widget, const char* css_class) {
    gtk_widget_add_css_class(widget, css_class);
}

void install_theme(const std::string& accent, const std::string& background, const std::string& surface, const std::string& text, const std::string& icon_color) {
    const auto safe_accent = css_color_or(accent, "#f39a22");
    const auto safe_background = css_color_or(background, "#10161b");
    const auto safe_surface = css_color_or(surface, "#171f25");
    const auto safe_text = css_color_or(text, "#edf1f3");
    const auto safe_icon_color = css_color_or(icon_color, safe_accent);
    const auto accent_sum = std::stoi(safe_accent.substr(1, 2), nullptr, 16) +
                            std::stoi(safe_accent.substr(3, 2), nullptr, 16) +
                            std::stoi(safe_accent.substr(5, 2), nullptr, 16);
    const std::string accent_text = accent_sum > 380 ? "#10161b" : "#ffffff";
    active_accent = safe_accent;
    active_icon_color = safe_icon_color;
    std::string css = R"CSS(
        .lazarus-window {
            background: {{BACKGROUND}};
            color: {{TEXT}};
        }
        .admin-shell {
            background: #0f151a;
            color: #edf1f3;
        }
        #lazarus-admin-dialog,
        #lazarus-admin-dialog > box,
        #lazarus-admin-dialog action-area {
            background: #0f151a;
            color: #edf1f3;
        }
        #lazarus-admin-dialog action-area {
            border-top: 1px solid #2e3a43;
            padding: 8px 14px;
        }
        #lazarus-dialog,
        #lazarus-dialog > box,
        #lazarus-dialog action-area {
            background: #0f151a;
            color: #edf1f3;
        }
        #lazarus-dialog action-area {
            border-top: 1px solid #2e3a43;
            padding: 10px 14px;
        }
        .dialog-content {
            background: #151d23;
            color: #edf1f3;
            padding: 20px;
        }
        .dialog-title {
            color: #ffffff;
            font-size: 20px;
            font-weight: 700;
        }
        .dialog-intro {
            color: #b8c3ca;
            font-size: 13px;
        }
        .dialog-label {
            color: #d9e0e4;
            font-size: 12px;
            font-weight: 700;
        }
        .dialog-warning {
            background: #2b2210;
            border: 1px solid #d49f28;
            border-radius: 8px;
            padding: 14px;
        }
        #lazarus-dialog button {
            background: #1a232a;
            color: #edf1f3;
            border: 1px solid #46535d;
            border-radius: 8px;
            padding: 10px 16px;
        }
        #lazarus-dialog button:hover {
            border-color: #f39a22;
        }
        #lazarus-dialog button.destructive {
            background: #5a241d;
            border-color: #df6a3f;
            color: #ffffff;
        }
        .app-header {
            background: #151c22;
            border-bottom: 1px solid #2e3a43;
            padding: 8px 18px;
        }
        .admin-header {
            background: #131a20;
            border-bottom: 1px solid #2b3842;
            padding: 16px 18px;
        }
        .brand-mark {
            color: {{ACCENT}};
            font-size: 34px;
            font-weight: 700;
        }
        .product-logo {
            background: #10161b;
        }
        .app-title {
            color: #f4f7f8;
            font-size: 30px;
            font-weight: 700;
            letter-spacing: 0;
        }
        .subtitle {
            color: #f39a22;
            font-size: 12px;
            font-weight: 700;
        }
        .surface {
            background: {{SURFACE}};
            border: 1px solid #34414b;
            border-radius: 8px;
            padding: 12px;
        }
        frame > border {
            background: #171f25;
            border: 1px solid #34414b;
            border-radius: 8px;
        }
        frame > label {
            color: #d9e0e4;
            background: #171f25;
            padding: 0 6px;
        }
        .admin-panel {
            background: #151d23;
            border: 1px solid #31404a;
            border-radius: 8px;
            padding: 12px;
        }
        .admin-section {
            background: #151d23;
            border: 1px solid #2f3d47;
            border-radius: 8px;
            padding: 12px;
        }
        .admin-warning {
            background: #2b2210;
            border: 1px solid #d49f28;
            border-radius: 8px;
            padding: 12px;
        }
        .admin-rail {
            background: #11181e;
            border: 1px solid #2c3943;
            border-radius: 8px;
            padding: 8px;
        }
        stacksidebar {
            background: #11181e;
            color: #edf1f3;
        }
        stacksidebar row {
            background: transparent;
            color: #c4ced4;
            border: 1px solid transparent;
            border-radius: 6px;
            margin: 2px 4px;
            padding: 10px 12px;
        }
        stacksidebar row:hover {
            background: #182129;
        }
        stacksidebar row:selected {
            background: #1a232a;
            border-color: #f39a22;
            color: #ffffff;
        }
        .status-card {
            background: #192128;
            border: 1px solid #34414b;
            border-radius: 8px;
            padding: 10px;
        }
        .summary-card {
            background: #192128;
            border: 1px solid #33414b;
            border-radius: 8px;
            padding: 10px 12px;
        }
        .status-key {
            color: #9aa8b2;
            font-size: 11px;
            font-weight: 700;
        }
        .status-value {
            color: #edf1f3;
            font-size: 14px;
        }
        .action-card {
            background: #1a232a;
            color: #edf1f3;
            border: 1px solid #46535d;
            border-radius: 8px;
            padding: 10px;
        }
        .action-card:hover {
            background: #212c34;
            border-color: {{ACCENT}};
        }
        .action-card:disabled {
            background: #151b20;
            color: #65727b;
            border-color: #29343c;
        }
        .action-card:disabled label { color: #65727b; }
        .action-primary {
            border-color: #f39a22;
        }
        .action-title {
            color: #ffffff;
            font-size: 16px;
            font-weight: 700;
        }
        .action-detail {
            color: #b9c2c9;
            font-size: 13px;
        }
        .panel-title {
            color: #edf1f3;
            font-size: 16px;
            font-weight: 700;
        }
        .device-card {
            background: #151d23;
            border: 1px solid #303b44;
            border-radius: 6px;
            padding: 10px;
        }
        .device-card:selected,
        .device-card:selected * {
            background: #202830;
            border-color: #f39a22;
        }
        .device-card:selected {
            background: #202830;
            border-color: #f39a22;
        }
        .device-title {
            color: #ffffff;
            font-size: 15px;
            font-weight: 700;
        }
        .device-detail {
            color: #b8c3ca;
            font-size: 12px;
        }
        .role-badge {
            color: #ffffff;
            border-radius: 5px;
            padding: 3px 8px;
            font-size: 11px;
            font-weight: 700;
        }
        .role-source { background: #7043a0; }
        .role-destination { background: #236fac; }
        .role-storage { background: #3d8d4b; }
        .role-removable { background: #397d92; }
        .role-system { background: #6b737a; }
        .role-external { background: #6b737a; }
        .backup-row {
            background: #151d23;
            border-bottom: 1px solid #2a343c;
            padding: 8px;
        }
        .backup-row:selected {
            background: #202830;
        }
        .accent {
            color: {{ACCENT}};
            font-weight: 700;
        }
        .disconnect-safe {
            color: #61c97a;
            font-size: 12px;
            font-weight: 800;
        }
        .disconnect-unsafe {
            color: #f1b84b;
            font-size: 12px;
            font-weight: 800;
        }
        .action-arrow { color: {{ICON}}; font-size: 30px; font-weight: 700; }
        .bottom-bar {
            background: #11181e;
            border-top: 1px solid #2e3a43;
            padding: 10px 16px;
            color: #b9c2c9;
        }
        .dark-list,
        .dark-list row,
        scrolledwindow,
        viewport {
            background: #10161b;
            color: #edf1f3;
        }
        .admin-button {
            background: #1a232a;
            color: #edf1f3;
            border: 1px solid #46535d;
            border-radius: 8px;
            padding: 10px 14px;
        }
        .admin-button:hover {
            border-color: {{ACCENT}};
        }
        .admin-button-active {
            background: #202830;
            border-color: #f39a22;
            color: #ffffff;
        }
        button:focus,
        entry:focus,
        row:focus {
            border-color: {{ACCENT}};
            box-shadow: 0 0 0 2px rgba(74, 135, 180, 0.28);
        }
        entry {
            background: #12191f;
            color: #edf1f3;
            border: 1px solid #34414b;
            border-radius: 8px;
            padding: 8px;
        }
        combobox button {
            background: #12191f;
            color: #edf1f3;
            border: 1px solid #34414b;
            border-radius: 8px;
            padding: 8px;
        }
        textview, textview text, scrolledwindow text {
            background: #12191f;
            color: #edf1f3;
        }
        progressbar trough {
            background: #11181e;
            border: 1px solid #34414b;
            min-height: 8px;
        }
        progressbar progress {
            background: {{ACCENT}};
            min-height: 8px;
        }
    )CSS";
    const auto replace_all = [&css](const std::string& token, const std::string& value) {
        std::size_t position = 0;
        while ((position = css.find(token, position)) != std::string::npos) {
            css.replace(position, token.size(), value);
            position += value.size();
        }
    };
    replace_all("{{ACCENT}}", safe_accent);
    replace_all("{{BACKGROUND}}", safe_background);
    replace_all("{{SURFACE}}", safe_surface);
    replace_all("{{TEXT}}", safe_text);
    replace_all("{{ICON}}", safe_icon_color);

    if (is_light_color(safe_background)) {
        css += R"CSS(
        .lazarus-window, .admin-shell { background: #ffffff; color: {{TEXT}}; }
        .app-header, .admin-header { background: #ffffff; border-color: #d9e1e8; }
        .product-logo { background: #ffffff; }
        .surface, .admin-panel, .admin-section, .device-card, .status-card,
        .progress-panel, .action-card, .dialog-content { background: {{SURFACE}}; color: {{TEXT}}; border-color: #d5dee6; }
        frame > border, frame > label { background: {{SURFACE}}; color: {{TEXT}}; border-color: #d5dee6; }
        .dark-list, .dark-list row, scrolledwindow, viewport,
        .backup-row, .device-card, .device-card:selected, .device-card:selected *,
        .backup-row:selected { background: #ffffff; color: {{TEXT}}; border-color: #d5dee6; }
        .admin-rail, .admin-rail stacksidebar { background: #f1f5f8; color: {{TEXT}}; border-color: #d5dee6; }
        .admin-rail label, .admin-rail stacksidebar row { color: #526372; }
        .admin-rail stacksidebar row:hover { background: #e6eef4; color: {{TEXT}}; }
        .admin-rail stacksidebar row:selected { background: #ffffff; border-color: {{ACCENT}}; color: {{TEXT}}; }
        .app-title, .dialog-title, .panel-title, .device-title,
        .action-title, .status-value { color: {{TEXT}}; }
        .status-card label, .summary-card label { color: {{TEXT}}; }
        .device-detail, .dialog-intro, .subtitle { color: #526372; }
        .action-detail, .status-key, .backup-row, .device-detail { color: #526372; }
        .accent, .action-arrow { color: {{ICON}}; }
        .action-card:hover, .action-card-primary, .action-primary { background: #ffffff; border-color: {{ACCENT}}; }
        .admin-button, #lazarus-dialog button { background: #ffffff; color: {{TEXT}}; border-color: #b9c8d4; }
        .admin-button:hover, #lazarus-dialog button:hover { border-color: {{ACCENT}}; }
        #lazarus-dialog, #lazarus-dialog > box, #lazarus-dialog action-area,
        #lazarus-admin-dialog, #lazarus-admin-dialog > box, #lazarus-admin-dialog action-area { background: #ffffff; color: {{TEXT}}; }
        entry, textview, textview text, scrolledwindow text { background: #ffffff; color: {{TEXT}}; border-color: #cbd6df; }
        scrolledwindow > viewport { background: #ffffff; }
        .bottom-bar { background: #f1f5f8; color: #526372; border-color: #d5dee6; }
        )CSS";
        replace_all("{{ACCENT}}", safe_accent);
        replace_all("{{SURFACE}}", safe_surface);
        replace_all("{{TEXT}}", safe_text);
        replace_all("{{ICON}}", safe_icon_color);
    }

    // Native GTK4 workflow surfaces. Kept last so old prototype selectors
    // cannot leak colors or geometry into the replacement shell.
    css += R"CSS(
        .lazarus-window { background: {{BACKGROUND}}; color: {{TEXT}}; }
        .home-title { color: {{TEXT}}; font-size: 32px; font-weight: 800; }
        .page-brand { color: #8fa0ab; font-size: 11px; font-weight: 700; }
        .home-subtitle, .eyebrow { color: {{ACCENT}}; font-size: 12px; font-weight: 700; }
        .workflow-title { color: {{TEXT}}; font-size: 26px; font-weight: 700; }
        .workflow-detail, .instruction, .choice-detail { color: #aebbc4; font-size: 14px; }
        .workflow-header { margin-bottom: 2px; }
        .workflow-section, .operation-panel, .warning-panel {
            background: {{SURFACE}};
            border: 1px solid #34434e;
            border-radius: 8px;
            padding: 14px;
        }
        .section-title { color: {{TEXT}}; font-size: 15px; font-weight: 700; }
        .section-detail { color: #91a0aa; font-size: 12px; }
        .form-label { color: #c9d2d8; font-size: 13px; font-weight: 600; }
        .warning-panel { background: #2b2210; border-color: #a97c21; }
        .warning-text { color: #f0c86b; font-size: 13px; font-weight: 700; }
        .recovery-panel {
            background: #16271d;
            border: 1px solid #4e9a61;
            border-radius: 8px;
            padding: 16px;
        }
        .recovery-code {
            background: #0d1418;
            color: {{TEXT}};
            border: 1px solid {{ACCENT}};
            border-radius: 6px;
            padding: 14px;
            font-family: monospace;
            font-size: 18px;
            font-weight: 700;
        }
        .operation-panel { background: #121a20; padding: 14px; }
        .operation-status { color: {{TEXT}}; font-size: 14px; font-weight: 700; }
        button.workflow-card { background: {{SURFACE}}; color: {{TEXT}}; border: 1px solid #3b4b56; border-radius: 8px; min-height: 102px; padding: 8px; }
        button.workflow-card:hover, button.workflow-card:focus-visible { background: #202b33; border-color: {{ACCENT}}; }
        .workflow-card .action-title, .choice-title { color: {{TEXT}}; font-size: 18px; font-weight: 700; }
        .workflow-card .action-detail { color: #b8c4cc; font-size: 14px; }
        button.primary-command { background: {{ACCENT}}; color: {{ACCENT_TEXT}}; border: 1px solid {{ACCENT}}; border-radius: 6px; min-height: 44px; padding: 0 18px; font-weight: 700; }
        button.primary-command:disabled { background: #2b3740; color: #82909a; border-color: #2b3740; }
        button.secondary-command, button.admin-task { background: transparent; color: {{TEXT}}; border: 1px solid #455560; border-radius: 6px; min-height: 42px; padding: 0 16px; }
        button.secondary-command:hover, button.admin-task:hover { border-color: {{ACCENT}}; }
        button.danger-command { background: #8f252b; color: #ffffff; border: 1px solid #c74a50; border-radius: 6px; min-height: 42px; padding: 0 16px; font-weight: 700; }
        button.danger-command:hover { background: #a52d34; border-color: #e06066; }
        button.danger-command:disabled { background: #34272a; color: #806c70; border-color: #49363a; }
        button.compact-back { min-width: 96px; min-height: 40px; padding: 0 12px; }
        button.admin-task { min-height: 92px; padding: 8px; }
        .admin-task-title { color: {{TEXT}}; font-size: 16px; font-weight: 700; }
        .admin-task-detail { color: #aebbc4; font-size: 13px; }
        .choice-list { background: {{BACKGROUND}}; border: 1px solid #34434e; border-radius: 8px; }
        .choice-row { background: {{SURFACE}}; border-bottom: 1px solid #34434e; min-height: 54px; }
        .choice-row:hover, .choice-row:selected { background: #202b33; box-shadow: inset 3px 0 {{ACCENT}}; }
        .choice-row:disabled { opacity: 0.45; }
        .empty-state { background: #1a242b; color: #c7d1d7; border: 1px solid #3e515d; border-radius: 8px; padding: 18px; }
        .bottom-bar { background: #11191f; color: #b8c4cc; border-top: 1px solid #31404a; padding: 12px 18px; }
        .operation-panel textview, .operation-panel textview text { padding: 8px; }
        dropdown > button {
            background: #12191f;
            color: {{TEXT}};
            border: 1px solid #34414b;
            border-radius: 8px;
            min-height: 40px;
            padding: 0 10px;
        }
        dropdown > button:hover, dropdown > button:focus-visible { border-color: {{ACCENT}}; }
        dropdown popover contents, dropdown popover listview, dropdown popover row {
            background: {{SURFACE}};
            color: {{TEXT}};
        }
        dropdown popover row:hover, dropdown popover row:selected { background: #202b33; }
        .calibration-guide {
            background: #121a20;
            border: 1px solid #34434e;
            border-radius: 8px;
            padding: 12px;
        }
        .calibration-step { color: #c6d0d7; font-size: 13px; font-weight: 600; }
        .port-label-list {
            background: {{BACKGROUND}};
            border: 1px solid #34434e;
            border-radius: 8px;
        }
        .port-label-row { background: {{SURFACE}}; border-bottom: 1px solid #34434e; }
        .port-label-row entry { min-height: 40px; }
        .port-role, .port-role-offline { color: {{ACCENT}}; font-size: 12px; font-weight: 700; }
        .port-role-offline { color: #82909a; }
        .home-utility {
            background: {{SURFACE}};
            border: 1px solid #34434e;
            border-radius: 8px;
            padding: 10px 12px;
        }
        .heartbeat-strip {
            background: #11191f;
            border: 1px solid #34434e;
            border-radius: 8px;
            padding: 6px;
        }
        .heartbeat-item { padding: 2px 8px; }
        .heartbeat-ok, .heartbeat-warning {
            border-radius: 999px;
            min-width: 10px;
            min-height: 10px;
        }
        .heartbeat-ok { background: #53c86b; }
        .heartbeat-warning { background: #d2a43b; }
        .heartbeat-title { color: #82919b; font-size: 10px; font-weight: 800; }
        .heartbeat-value { color: {{TEXT}}; font-size: 13px; font-weight: 700; }
        .bench-ribbon { color: #aebbc4; font-size: 12px; font-weight: 600; }
    )CSS";
    replace_all("{{ACCENT}}", safe_accent);
    replace_all("{{BACKGROUND}}", safe_background);
    replace_all("{{SURFACE}}", safe_surface);
    replace_all("{{TEXT}}", safe_text);
    replace_all("{{ACCENT_TEXT}}", accent_text);
    if (is_light_color(safe_background)) {
        css += R"CSS(
        .workflow-detail, .instruction, .choice-detail, .section-detail,
        .admin-task-detail { color: #526372; }
        .form-label { color: #364957; }
        .workflow-section, .operation-panel { background: {{SURFACE}}; border-color: #c8d5df; }
        .recovery-panel { background: #edf8f0; border-color: #4e9a61; }
        .recovery-code { background: #ffffff; color: {{TEXT}}; border-color: {{ACCENT}}; }
        .operation-panel { background: #f7fafc; }
        .choice-list { background: #ffffff; border-color: #c8d5df; }
        .choice-row { background: #ffffff; border-color: #d5dee6; }
        .choice-row:hover, .choice-row:selected { background: #eaf2f8; }
        .empty-state { background: #f1f5f8; color: #526372; border-color: #c8d5df; }
        button.workflow-card { background: {{SURFACE}}; border-color: #c4d1db; }
        button.workflow-card:hover, button.workflow-card:focus-visible { background: #eef4f8; border-color: {{ACCENT}}; }
        button.secondary-command, button.admin-task { color: {{TEXT}}; border-color: #b9c8d4; }
        button.danger-command { background: #b52f37; color: #ffffff; border-color: #9b252c; }
        .warning-text { color: #8a5a00; }
        .operation-panel textview, .operation-panel textview text { background: #ffffff; color: {{TEXT}}; }
        dropdown > button { background: #ffffff; color: {{TEXT}}; border-color: #b9c8d4; }
        dropdown popover contents, dropdown popover listview, dropdown popover row { background: #ffffff; color: {{TEXT}}; }
        dropdown popover row:hover, dropdown popover row:selected { background: #eaf2f8; }
        .calibration-guide { background: #f1f5f8; border-color: #c8d5df; }
        .calibration-step { color: #364957; }
        .port-label-list { background: #ffffff; border-color: #c8d5df; }
        .port-label-row { background: #ffffff; border-color: #d5dee6; }
        .home-utility { background: {{SURFACE}}; border-color: #c8d5df; }
        .heartbeat-strip { background: #f1f5f8; border-color: #c8d5df; }
        .heartbeat-title { color: #627584; }
        .heartbeat-value { color: {{TEXT}}; }
        .bench-ribbon { color: #526372; }
        )CSS";
        replace_all("{{ACCENT}}", safe_accent);
        replace_all("{{SURFACE}}", safe_surface);
        replace_all("{{TEXT}}", safe_text);
    }
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css.c_str());
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget* make_label(const std::string& text, const char* css_class) {
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    if (css_class != nullptr) {
        add_class(label, css_class);
    }
    return label;
}

GtkWidget* make_icon(const std::string& name, int size) {
    const auto path = icon_path(name);
    std::filesystem::path themed_path = path;
    if (!path.empty() && active_icon_color != "#f39a22") {
        const auto cache_path = std::filesystem::path("/tmp") /
                                ("arcology-lazarus-icon-" + name + "-" + active_icon_color.substr(1) + ".svg");
        if (!std::filesystem::exists(cache_path)) {
            std::ifstream input(path);
            std::string svg((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            std::size_t position = 0;
            while ((position = svg.find("#f39a22", position)) != std::string::npos) {
                svg.replace(position, 7, active_icon_color);
                position += active_icon_color.size();
            }
            position = 0;
            while ((position = svg.find("#ef981e", position)) != std::string::npos) {
                svg.replace(position, 7, active_icon_color);
                position += active_icon_color.size();
            }
            std::ofstream output(cache_path);
            output << svg;
        }
        themed_path = cache_path;
    }
    GtkWidget* image = themed_path.empty() ? gtk_image_new() : gtk_image_new_from_file(themed_path.c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(image), size);
    gtk_widget_set_size_request(image, size, size);
    add_class(image, "line-icon");
    if (path.empty()) {
        gtk_widget_set_tooltip_text(image, "Icon unavailable");
    }
    return image;
}

GtkWidget* make_logo(int size, const std::string& custom_path) {
    std::filesystem::path path;
    if (!custom_path.empty() && std::filesystem::exists(custom_path)) {
        path = custom_path;
    }
    const char* configured = std::getenv("LAZARUS_ASSET_DIR");
    if (path.empty() && configured != nullptr && *configured != '\0') {
        path = std::filesystem::path(configured) / "arcology-lazarus-logo.png";
    }
#ifdef LAZARUS_SOURCE_ASSET_DIR
    if (path.empty() || !std::filesystem::exists(path)) {
        path = std::filesystem::path(LAZARUS_SOURCE_ASSET_DIR) / "arcology-lazarus-logo.png";
    }
#endif
    if (path.empty() || !std::filesystem::exists(path)) {
        path = "/usr/share/arcology-lazarus/assets/arcology-lazarus-logo.png";
    }
    GtkWidget* picture = std::filesystem::exists(path)
        ? gtk_picture_new_for_filename(path.c_str())
        : gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), FALSE);
    gtk_widget_set_size_request(picture, size, size);
    gtk_widget_set_hexpand(picture, FALSE);
    gtk_widget_set_vexpand(picture, FALSE);
    gtk_widget_set_halign(picture, GTK_ALIGN_START);
    gtk_widget_set_valign(picture, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(picture, "Arcology Lazarus");
    add_class(picture, "product-logo");
    return picture;
}

void set_logo_path(GtkWidget* image, const std::string& custom_path) {
    if (image == nullptr) {
        return;
    }
    std::filesystem::path path;
    if (!custom_path.empty() && std::filesystem::exists(custom_path)) {
        path = custom_path;
    }
    const char* configured = std::getenv("LAZARUS_ASSET_DIR");
    if (path.empty() && configured != nullptr && *configured != '\0') {
        const auto candidate = std::filesystem::path(configured) / "arcology-lazarus-logo.png";
        if (std::filesystem::exists(candidate)) {
            path = candidate;
        }
    }
#ifdef LAZARUS_SOURCE_ASSET_DIR
    if (path.empty()) {
        const auto candidate = std::filesystem::path(LAZARUS_SOURCE_ASSET_DIR) / "arcology-lazarus-logo.png";
        if (std::filesystem::exists(candidate)) {
            path = candidate;
        }
    }
#endif
    if (path.empty()) {
        path = "/usr/share/arcology-lazarus/assets/arcology-lazarus-logo.png";
    }
    if (!std::filesystem::exists(path)) {
        return;
    }
    const int slot = std::max(32, gtk_widget_get_allocated_width(image));
    GError* error = nullptr;
    GdkPixbuf* scaled = gdk_pixbuf_new_from_file_at_scale(path.c_str(), slot, slot, TRUE, &error);
    if (scaled != nullptr) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled);
        g_object_unref(scaled);
    }
    if (error != nullptr) {
        g_error_free(error);
    }
}

GtkWidget* make_badge(const std::string& text, const std::string& role) {
    GtkWidget* badge = gtk_label_new(text.c_str());
    add_class(badge, "role-badge");
    if (role == "source-only") {
        add_class(badge, "role-source");
    } else if (role == "destination-only") {
        add_class(badge, "role-destination");
    } else if (role == "image-storage") {
        add_class(badge, "role-storage");
    } else if (role == "removable-media") {
        add_class(badge, "role-removable");
    } else if (role == "system-disk") {
        add_class(badge, "role-system");
    } else {
        add_class(badge, "role-external");
    }
    return badge;
}

GtkWidget* make_status_card(const std::string& key, const std::string& value) {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    add_class(card, "status-card");
    gtk_box_pack_start(GTK_BOX(card), make_label(key, "status-key"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), make_label(value, "status-value"), FALSE, FALSE, 0);
    return card;
}

GtkWidget* make_action_card(const std::string& title, const std::string& detail, const std::string& icon_name, bool primary) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_set_size_request(button, 0, 96);
    add_class(button, "action-card");
    if (primary) {
        add_class(button, "action-primary");
    }
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_box_pack_start(GTK_BOX(box), make_icon(icon_name, 30), FALSE, FALSE, 0);
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* title_label = make_label(title, "action-title");
    GtkWidget* detail_label = make_label(detail, "action-detail");
    gtk_label_set_width_chars(GTK_LABEL(title_label), 12);
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 16);
    gtk_label_set_width_chars(GTK_LABEL(detail_label), 14);
    gtk_label_set_max_width_chars(GTK_LABEL(detail_label), 18);
    gtk_box_pack_start(GTK_BOX(text), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(text), detail_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), make_label(">", "action-arrow"), FALSE, FALSE, 0);
    return button;
}

GtkWidget* make_section(const std::string& title, GtkWidget* child) {
    GtkWidget* frame = gtk_frame_new(title.c_str());
    add_class(frame, "surface");
    gtk_container_set_border_width(GTK_CONTAINER(frame), 8);
    gtk_container_add(GTK_CONTAINER(frame), child);
    return frame;
}

}  // namespace lazarus::gui
