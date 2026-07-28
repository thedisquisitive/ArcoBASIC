#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "gtk4_compat.hpp"

#include "widgets.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using lazarus::gui::add_class;
using lazarus::gui::install_theme;
using lazarus::gui::make_action_card;
using lazarus::gui::make_icon;
using lazarus::gui::make_logo;
using lazarus::gui::set_logo_path;
using lazarus::gui::make_badge;
using lazarus::gui::make_label;
using lazarus::gui::make_section;
using lazarus::gui::make_status_card;

struct BackupSummary {
    std::string image_directory;
    std::string title;
    std::string search_text;
};

struct DeviceSummary {
    std::string linux_path;
    std::string title;
    std::string detail;
    std::string identity;
    bool system_disk = false;
    std::string label;
    std::string model;
    std::uint64_t size_bytes = 0;
    std::string role;
};

struct ProfileData {
    std::string name;
    std::string branding_theme;
    std::string branding_product_name;
    std::string branding_subtitle;
    std::string branding_accent;
    std::string branding_background;
    std::string branding_surface;
    std::string branding_text;
    std::string branding_icon;
    std::string branding_logo;
    std::string branding_report_footer;
    std::string image_storage_device;
    std::string summary_text;
    std::string image_storage_text;
    std::string source_text;
    std::string destination_text;
    std::string ignored_text;
    std::string labels_text;
};

struct JobInfo {
    std::string ticket_number;
    std::string customer_name;
    std::string technician;
    std::string purpose;
};

enum class CompressionMode {
    None,
    Zstd,
};

struct AppState {
    std::string bench_path;
    ProfileData profile;
    std::vector<DeviceSummary> devices;
    std::vector<BackupSummary> backups;
    GtkWidget* window = nullptr;
    GtkWidget* main_logo = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* storage_label = nullptr;
    GtkWidget* device_list = nullptr;
    GtkWidget* backup_list = nullptr;
    GtkWidget* search_entry = nullptr;
    GtkWidget* backup_action = nullptr;
    GtkWidget* verify_action = nullptr;
    GtkWidget* restore_action = nullptr;
    GtkWidget* diagnostics_action = nullptr;
    GtkWidget* progress_bar = nullptr;
    GtkWidget* main_stack = nullptr;
    GtkWidget* operation_title = nullptr;
    GtkWidget* operation_detail = nullptr;
    GtkWidget* operation_stage = nullptr;
    GtkWidget* operation_bytes = nullptr;
    GtkWidget* operation_events = nullptr;
    GtkWidget* operation_done_button = nullptr;
};

std::string human_size(std::uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 5) {
        value /= 1000.0;
        ++unit;
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(unit == 0 ? 0 : 1);
    out << value << " " << units[unit];
    return out.str();
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return "";
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return "";
    }
    const auto first_quote = text.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return "";
    }
    std::string out;
    bool escape = false;
    for (auto pos = first_quote + 1; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escape) {
            switch (ch) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(ch);
                    break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        out.push_back(ch);
    }
    return out;
}

std::uint64_t extract_json_uint64(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return 0;
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return 0;
    }
    const auto begin = text.find_first_of("0123456789", colon + 1);
    if (begin == std::string::npos) {
        return 0;
    }
    const auto end = text.find_first_not_of("0123456789", begin);
    try {
        return static_cast<std::uint64_t>(std::stoull(text.substr(begin, end - begin)));
    } catch (...) {
        return 0;
    }
}

bool extract_json_bool(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return false;
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return false;
    }
    const auto value = text.substr(colon + 1, 5);
    return value.find("true") != std::string::npos;
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(static_cast<char>(ch));
                break;
        }
    }
    return out;
}

std::string quote_json(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_case_insensitive(const std::string& text, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    return lowercase(text).find(lowercase(query)) != std::string::npos;
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) {
        text += line + "\n";
    }
    return text;
}

std::string sanitize_path_component(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            out.push_back(ch);
        } else if (ch == '-' || ch == '_') {
            out.push_back(ch);
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back('_');
        }
    }
    return out.empty() ? "unknown" : out;
}

std::string timestamp_component() {
    const auto now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M", &local);
    return buffer;
}

std::string build_image_directory(const std::string& storage_root, const JobInfo& job) {
    return (std::filesystem::path(storage_root) / sanitize_path_component(job.ticket_number) / sanitize_path_component(job.customer_name) / timestamp_component()).string();
}

bool is_complete(const JobInfo& job) {
    return !job.ticket_number.empty() && !job.customer_name.empty() && !job.technician.empty() && !job.purpose.empty();
}

bool is_source_device(const DeviceSummary& device) {
    return device.role == "source-only" && !device.system_disk;
}

bool is_destination_device(const DeviceSummary& device) {
    return device.role == "destination-only" && !device.system_disk;
}

bool is_install_target_device(const DeviceSummary& device) {
    if (device.system_disk) {
        return false;
    }
    return device.role != "source-only" && device.role != "image-storage" && device.role != "ignored";
}

GtkWidget* make_row(const std::string& title, const std::string& detail) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_box_pack_start(GTK_BOX(box), make_label(title, "row-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label(detail, "row-detail"), FALSE, FALSE, 0);
    gtk_widget_show_all(row);
    return row;
}

std::string role_title(const DeviceSummary& device) {
    if (device.role == "source-only") {
        return "SOURCE - READ ONLY";
    }
    if (device.role == "destination-only") {
        return "DESTINATION";
    }
    if (device.role == "image-storage") {
        return "IMAGE STORAGE";
    }
    if (device.system_disk || device.role == "system-disk") {
        return "SYSTEM DISK";
    }
    return "EXTERNAL";
}

std::string role_badge_text(const DeviceSummary& device) {
    if (device.role == "source-only") {
        return "SOURCE";
    }
    if (device.role == "destination-only") {
        return "DESTINATION";
    }
    if (device.role == "image-storage") {
        return "STORAGE";
    }
    if (device.system_disk || device.role == "system-disk") {
        return "SYSTEM";
    }
    return "EXTERNAL";
}

std::string device_icon_name(const DeviceSummary& device) {
    if (device.role == "image-storage") {
        return "image";
    }
    const auto type = lowercase(device.model + " " + device.title + " " + device.detail);
    if (type.find("nvme") != std::string::npos) {
        return "nvme";
    }
    if (type.find("optical") != std::string::npos || type.find("dvd") != std::string::npos || type.find("cd-rom") != std::string::npos) {
        return "optical";
    }
    if (type.find("usb") != std::string::npos || type.find("flash") != std::string::npos || type.find("removable") != std::string::npos) {
        return "usb";
    }
    if (type.find("ssd") != std::string::npos) {
        return "ssd";
    }
    return "hdd";
}

std::string device_display_name(const DeviceSummary& device) {
    if (!device.label.empty()) {
        return device.label;
    }
    if (!device.model.empty() && device.model != "unknown") {
        return device.model;
    }
    if (!device.title.empty()) {
        const auto separator = device.title.find('|');
        return trim(separator == std::string::npos ? device.title : device.title.substr(0, separator));
    }
    return device.linux_path;
}

std::string device_technical_detail(const DeviceSummary& device) {
    std::string detail = human_size(device.size_bytes);
    if (!device.linux_path.empty()) {
        detail += " | " + device.linux_path;
    }
    if (!device.identity.empty() && device.identity != device.linux_path) {
        detail += "\n" + device.identity;
    }
    if (device.system_disk) {
        detail += "\nProtected system disk";
    } else if (device.role == "source-only") {
        detail += "\nApplication opens this source read-only";
    } else if (device.role == "destination-only") {
        detail += "\nWritable destination role";
    }
    return detail;
}

GtkWidget* make_device_card(const DeviceSummary& device) {
    GtkWidget* row = gtk_list_box_row_new();
    add_class(row, "device-card");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(row), box);

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(top), make_icon(device_icon_name(device), 28), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), make_label(role_title(device), "device-title"), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(top), make_badge(role_badge_text(device), device.system_disk ? "system-disk" : device.role), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), make_label(device_display_name(device), "device-title"), FALSE, FALSE, 0);
    GtkWidget* technical_detail = make_label(device_technical_detail(device), "device-detail");
    gtk_box_pack_start(GTK_BOX(box), technical_detail, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(row), "technical-detail", technical_detail);
    gtk_widget_show_all(row);
    return row;
}

GtkWidget* make_backup_card(const BackupSummary& backup) {
    GtkWidget* row = gtk_list_box_row_new();
    add_class(row, "backup-row");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_box_pack_start(GTK_BOX(box), make_label(backup.title.empty() ? "Lazarus backup" : backup.title, "accent"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label(backup.image_directory, "device-detail"), FALSE, FALSE, 0);
    gtk_widget_show_all(row);
    return row;
}

void clear_list(GtkWidget* list) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* item = children; item != nullptr; item = item->next) {
        gtk_widget_destroy(GTK_WIDGET(item->data));
    }
    g_list_free(children);
}

std::string backup_search_text(const BackupSummary& backup) {
    return backup.search_text;
}

std::vector<std::string> split_tab_row(const std::string& row) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(row);
    while (std::getline(in, part, '\t')) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<DeviceSummary> parse_device_rows(const std::string& rows_text) {
    std::vector<DeviceSummary> devices;
    std::istringstream rows(rows_text);
    std::string row;
    while (std::getline(rows, row)) {
        if (row.empty()) {
            continue;
        }
        const auto parts = split_tab_row(row);
        if (parts.size() < 9) {
            continue;
        }
        DeviceSummary device;
        device.linux_path = parts[0];
        device.title = parts[1];
        device.detail = parts[2];
        device.identity = parts[3];
        device.system_disk = parts[4] == "1";
        device.label = parts[5];
        device.model = parts[6];
        try {
            device.size_bytes = static_cast<std::uint64_t>(std::stoull(parts[7]));
        } catch (...) {
            device.size_bytes = 0;
        }
        device.role = parts[8];
        devices.push_back(device);
    }
    return devices;
}

std::vector<BackupSummary> parse_backup_rows(const std::string& rows_text) {
    std::vector<BackupSummary> backups;
    std::istringstream rows(rows_text);
    std::string row;
    while (std::getline(rows, row)) {
        if (row.empty()) {
            continue;
        }
        const auto parts = split_tab_row(row);
        if (parts.size() < 3) {
            continue;
        }
        std::string search = parts[2];
        for (std::size_t i = 3; i < parts.size(); ++i) {
            search += "\t" + parts[i];
        }
        backups.push_back(BackupSummary{parts[0], parts[1], search});
    }
    return backups;
}

void pump_events();

void set_status(AppState* state, const std::string& text) {
    gtk_label_set_text(GTK_LABEL(state->status_label), text.c_str());
}

void append_operation_event(AppState* state, const std::string& text) {
    if (state->operation_events == nullptr) {
        return;
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->operation_events));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, (text + "\n").c_str(), -1);
}

void reset_operation_view(AppState* state, const std::string& title, const std::string& detail) {
    gtk_label_set_text(GTK_LABEL(state->operation_title), title.c_str());
    gtk_label_set_text(GTK_LABEL(state->operation_detail), detail.c_str());
    gtk_label_set_text(GTK_LABEL(state->operation_stage), "Preparing operation.");
    gtk_label_set_text(GTK_LABEL(state->operation_bytes), "");
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->operation_events));
    gtk_text_buffer_set_text(buffer, "", -1);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), 0.0);
    gtk_widget_set_sensitive(state->operation_done_button, FALSE);
    gtk_stack_set_visible_child_name(GTK_STACK(state->main_stack), "operation");
    pump_events();
}

void acknowledge_operation(AppState* state, const std::string& message) {
    gtk_label_set_text(GTK_LABEL(state->operation_stage), message.c_str());
    gtk_label_set_text(GTK_LABEL(state->operation_bytes), "Waiting for Lazarus service...");
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(state->progress_bar), 0.08);
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progress_bar));
    append_operation_event(state, message);
    set_status(state, message);
    // The service request is synchronous. Flush this acknowledgement before
    // entering it so a slow socket or device discovery cannot look like a dead click.
    pump_events();
}

void finish_operation_view(AppState* state, bool ok, const std::string& message) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), ok ? 1.0 : 0.0);
    gtk_label_set_text(GTK_LABEL(state->operation_stage), message.c_str());
    append_operation_event(state, message);
    gtk_widget_set_sensitive(state->operation_done_button, TRUE);
    set_status(state, message);
}

void update_operation_progress(AppState* state, const std::string& line) {
    const auto operation = extract_json_string(line, "operation");
    const auto phase = extract_json_string(line, "phase");
    const auto message = extract_json_string(line, "message");
    const auto bytes_done = extract_json_uint64(line, "bytes_done");
    const auto bytes_total = extract_json_uint64(line, "bytes_total");
    const auto chunks_done = extract_json_uint64(line, "chunks_done");
    const auto chunks_total = extract_json_uint64(line, "chunks_total");
    const bool indeterminate = extract_json_bool(line, "indeterminate");

    std::string stage = operation + ": " + phase;
    if (!message.empty()) {
        stage += " - " + message;
    }
    gtk_label_set_text(GTK_LABEL(state->operation_stage), stage.c_str());

    std::ostringstream bytes;
    if (bytes_total > 0) {
        bytes << human_size(bytes_done) << " / " << human_size(bytes_total);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), std::min(1.0, static_cast<double>(bytes_done) / static_cast<double>(bytes_total)));
    } else if (chunks_total > 0) {
        bytes << "chunks " << chunks_done << " / " << chunks_total;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), std::min(1.0, static_cast<double>(chunks_done) / static_cast<double>(chunks_total)));
    } else if (indeterminate) {
        bytes << "Working...";
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progress_bar));
    }
    if (chunks_total > 0 && bytes_total > 0) {
        bytes << " | chunks " << chunks_done << " / " << chunks_total;
    }
    gtk_label_set_text(GTK_LABEL(state->operation_bytes), bytes.str().c_str());
    append_operation_event(state, stage);
    set_status(state, stage);
    pump_events();
}

std::string bench_summary_text(const AppState* state) {
    if (state->profile.name.empty()) {
        return "Lazarus service profile is loading.";
    }
    std::size_t storage_count = split_lines(state->profile.image_storage_text).size();
    std::ostringstream out;
    out << "Bench profile loaded: " << state->profile.name << ". ";
    out << storage_count << " image storage " << (storage_count == 1 ? "directory" : "directories") << " configured.";
    return out.str();
}

void refresh_bench_summary(AppState* state) {
    gtk_label_set_text(GTK_LABEL(state->storage_label), bench_summary_text(state).c_str());
}

void pump_events() {
    lazarus_pump_events();
}

void apply_lazarus_pointer(GtkWidget* widget) {
    GdkCursor* cursor = gdk_cursor_new_from_name("default", nullptr);
    if (cursor != nullptr) {
        gtk_widget_set_cursor(widget, cursor);
        g_object_unref(cursor);
    }
}

void install_lazarus_pointer(GtkWidget* widget) {
    g_signal_connect(widget, "realize", G_CALLBACK(+[](GtkWidget* realized, gpointer) {
        apply_lazarus_pointer(realized);
    }), nullptr);
}

void style_dialog(GtkWidget* dialog, int width = 620, int height = 420, bool destructive = false) {
    gtk_widget_set_name(dialog, "lazarus-dialog");
    install_lazarus_pointer(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), width, height);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 0);
    add_class(area, "dialog-content");
    (void)destructive;
}

void show_message(GtkWindow* parent, GtkMessageType type, const std::string& text) {
    GtkWidget* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", text.c_str());
    style_dialog(dialog, 560, 260, false);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

std::string service_socket_path() {
    const char* value = std::getenv("LAZARUS_SERVICE_SOCKET");
    return value == nullptr || std::string(value).empty() ? "/run/arcology-lazarus/service.sock" : value;
}

bool send_all(int fd, const std::string& text) {
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t written = ::send(fd, data, remaining, 0);
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

bool service_request(AppState* state, const std::string& request, std::string& final_response, std::string& error) {
    const auto socket_path = service_socket_path();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::strerror(errno);
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        error = "Service socket path is too long.";
        ::close(fd);
        return false;
    }
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "Could not connect to Lazarus service: " + std::string(std::strerror(errno));
        ::close(fd);
        return false;
    }
    timeval send_timeout{};
    send_timeout.tv_sec = 5;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    // Imaging, verification, and OS installation can be silent while a
    // filesystem is being created or data is being copied. A short receive
    // timeout turns that normal pause into a misleading EAGAIN failure.
    // The service streams progress and closes with a final response; let the
    // receive wait for that protocol terminator.
    if (!send_all(fd, request + "\n")) {
        error = "Could not send request to Lazarus service.";
        ::close(fd);
        return false;
    }

    std::string buffer;
    char chunk[4096];
    while (true) {
        const ssize_t count = ::recv(fd, chunk, sizeof(chunk), 0);
        if (count < 0) {
            error = "Could not read service response: " + std::string(std::strerror(errno));
            ::close(fd);
            return false;
        }
        if (count == 0) {
            break;
        }
        buffer.append(chunk, chunk + count);
        std::size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            const auto line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (line.find("\"type\":\"progress\"") != std::string::npos) {
                update_operation_progress(state, line);
            } else if (line.find("\"type\":\"final\"") != std::string::npos) {
                final_response = line;
                ::close(fd);
                return true;
            }
        }
    }
    ::close(fd);
    error = "Service closed the connection without a final response.";
    return false;
}

bool service_response_ok(const std::string& response) {
    return response.find("\"ok\":true") != std::string::npos;
}

std::string service_failure_summary(const std::string& response, const std::string& fallback) {
    const auto error = extract_json_string(response, "error");
    if (!error.empty()) {
        return error;
    }
    const auto observed = extract_json_string(response, "observed");
    const auto action = extract_json_string(response, "action");
    if (!observed.empty() && !action.empty()) {
        return observed + " Recommended: " + action;
    }
    if (!observed.empty()) {
        return observed;
    }
    return fallback;
}

std::string smart_attribute_summary(const std::string& response, const std::string& key, const std::string& unit) {
    const auto start = response.find("\"" + key + "\":");
    if (start == std::string::npos) {
        return "not reported";
    }
    const auto end = response.find('}', start);
    const auto attribute = response.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
    if (!extract_json_bool(attribute, "present")) {
        return "not reported";
    }
    return std::to_string(extract_json_uint64(attribute, "value")) + unit;
}

enum class DeferredOperationKind {
    Verify,
    Backup,
    Restore,
    Install,
};

void refresh_devices(AppState* state);
void refresh_backups(AppState* state);

struct DeferredServiceOperation {
    AppState* state = nullptr;
    DeferredOperationKind kind = DeferredOperationKind::Verify;
    std::string request;
    guint flash_source = 0;
    bool flash_visible = true;
};

gboolean flash_starting_message(gpointer data) {
    auto* operation = static_cast<DeferredServiceOperation*>(data);
    operation->flash_visible = !operation->flash_visible;
    gtk_widget_set_opacity(operation->state->operation_stage, operation->flash_visible ? 1.0 : 0.35);
    return G_SOURCE_CONTINUE;
}

gboolean start_deferred_service_operation(gpointer data) {
    std::unique_ptr<DeferredServiceOperation> operation(static_cast<DeferredServiceOperation*>(data));
    if (operation->flash_source != 0) {
        g_source_remove(operation->flash_source);
        operation->flash_source = 0;
    }

    AppState* state = operation->state;
    gtk_widget_set_opacity(state->operation_stage, 1.0);
    gtk_label_set_text(GTK_LABEL(state->operation_stage), "Connecting to Lazarus service...");
    gtk_label_set_text(GTK_LABEL(state->operation_bytes), "Starting operation...");
    append_operation_event(state, "Starting operation after acknowledgement delay.");
    set_status(state, "Starting operation through Lazarus service...");
    pump_events();

    std::string response;
    std::string error;
    if (!service_request(state, operation->request, response, error)) {
        std::string message;
        switch (operation->kind) {
            case DeferredOperationKind::Verify:
                message = "Verification could not complete: " + error;
                break;
            case DeferredOperationKind::Backup:
                message = "Backup could not complete: " + error;
                break;
            case DeferredOperationKind::Restore:
                message = "Restore could not complete: " + error;
                break;
            case DeferredOperationKind::Install:
                message = "Install could not complete: " + error;
                break;
        }
        finish_operation_view(state, false, message);
        return G_SOURCE_REMOVE;
    }

    const bool success = service_response_ok(response);
    switch (operation->kind) {
        case DeferredOperationKind::Verify:
            finish_operation_view(state, success, success ? "Image verified by service." : "Image verification failed in service.");
            break;
        case DeferredOperationKind::Backup:
            finish_operation_view(state, success,
                                  success ? "Backup completed through service." :
                                            "Backup failed: " + service_failure_summary(response, "The service rejected the source image request."));
            refresh_backups(state);
            break;
        case DeferredOperationKind::Restore:
            finish_operation_view(state, success, success ? "Restore completed through service." : "Restore failed in service.");
            break;
        case DeferredOperationKind::Install:
            if (success) {
                finish_operation_view(state, true, "Lazarus OS installation completed.");
                refresh_devices(state);
            } else {
                const auto reason = extract_json_string(response, "error");
                finish_operation_view(state, false, "Lazarus OS installation failed: " +
                                                        (reason.empty() ? "service rejected the request." : reason));
            }
            break;
    }
    return G_SOURCE_REMOVE;
}

void schedule_service_operation(AppState* state, DeferredOperationKind kind, const std::string& request) {
    auto* operation = new DeferredServiceOperation{state, kind, request};
    gtk_widget_set_opacity(state->operation_stage, 1.0);
    gtk_label_set_text(GTK_LABEL(state->operation_stage), "Starting now.");
    gtk_label_set_text(GTK_LABEL(state->operation_bytes), "Launching in 2 seconds...");
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(state->progress_bar), 0.08);
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(state->progress_bar));
    append_operation_event(state, "Request acknowledged. Starting now.");
    set_status(state, "Starting now.");
    pump_events();
    operation->flash_source = g_timeout_add(250, flash_starting_message, operation);
    g_timeout_add(2000, start_deferred_service_operation, operation);
}

bool load_profile_from_service(AppState* state, std::string& error) {
    std::string response;
    if (!service_request(state, "{\"command\":\"profile\"}", response, error)) {
        return false;
    }
    if (!service_response_ok(response)) {
        error = extract_json_string(response, "error");
        return false;
    }
    state->profile.name = extract_json_string(response, "name");
    state->profile.branding_theme = extract_json_string(response, "branding_theme");
    state->profile.branding_product_name = extract_json_string(response, "branding_product_name");
    state->profile.branding_subtitle = extract_json_string(response, "branding_subtitle");
    state->profile.branding_accent = extract_json_string(response, "branding_accent");
    state->profile.branding_background = extract_json_string(response, "branding_background");
    state->profile.branding_surface = extract_json_string(response, "branding_surface");
    state->profile.branding_text = extract_json_string(response, "branding_text");
    state->profile.branding_icon = extract_json_string(response, "branding_icon");
    state->profile.branding_logo = extract_json_string(response, "branding_logo");
    state->profile.branding_report_footer = extract_json_string(response, "branding_report_footer");
    install_theme(state->profile.branding_accent,
                  state->profile.branding_background,
                  state->profile.branding_surface,
                  state->profile.branding_text,
                  state->profile.branding_icon);
    set_logo_path(state->main_logo, state->profile.branding_logo);
    state->profile.image_storage_device = extract_json_string(response, "image_storage_device");
    state->profile.summary_text = extract_json_string(response, "summary_text");
    state->profile.image_storage_text = extract_json_string(response, "image_storage_text");
    state->profile.source_text = extract_json_string(response, "source_text");
    state->profile.destination_text = extract_json_string(response, "destination_text");
    state->profile.ignored_text = extract_json_string(response, "ignored_text");
    state->profile.labels_text = extract_json_string(response, "labels_text");
    state->devices = parse_device_rows(extract_json_string(response, "devices_rows"));
    state->backups = parse_backup_rows(extract_json_string(response, "backups_rows"));
    return true;
}

std::optional<DeviceSummary> selected_device(AppState* state) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(state->device_list));
    if (row == nullptr) {
        return std::nullopt;
    }
    const char* identity = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "device-identity"));
    if (identity == nullptr) {
        return std::nullopt;
    }
    for (const auto& device : state->devices) {
        if (device.identity == identity || device.linux_path == identity) {
            return device;
        }
    }
    return std::nullopt;
}

std::optional<DeviceSummary> find_device_by_identity(AppState* state, const std::string& identity) {
    for (const auto& device : state->devices) {
        if (device.identity == identity || device.linux_path == identity) {
            return device;
        }
    }
    return std::nullopt;
}

std::string selected_backup_path(AppState* state);

void refresh_device_card_expansion(AppState* state) {
    GtkListBoxRow* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(state->device_list));
    GList* children = gtk_container_get_children(GTK_CONTAINER(state->device_list));
    for (GList* item = children; item != nullptr; item = item->next) {
        GtkWidget* row = GTK_WIDGET(item->data);
        auto* detail = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(row), "technical-detail"));
        if (detail != nullptr) {
            gtk_widget_set_visible(detail, row == GTK_WIDGET(selected));
        }
    }
    g_list_free(children);
}

void refresh_action_availability(AppState* state) {
    const auto device = selected_device(state);
    const bool source_selected = device && is_source_device(*device);
    const bool destination_selected = device && is_destination_device(*device);
    const bool backup_selected = !selected_backup_path(state).empty();

    gtk_widget_set_sensitive(state->backup_action, source_selected);
    gtk_widget_set_sensitive(state->verify_action, backup_selected);
    gtk_widget_set_sensitive(state->restore_action, backup_selected && destination_selected);
    gtk_widget_set_sensitive(state->diagnostics_action, device.has_value());

    if (source_selected) {
        set_status(state, "Source selected. Enter job details to create a backup.");
    } else if (destination_selected && backup_selected) {
        set_status(state, "Backup and destination selected. Restore is ready for review.");
    } else if (backup_selected) {
        set_status(state, "Backup selected. Choose a destination-only drive to restore, or verify it.");
    } else if (device) {
        set_status(state, "Select a source-only drive to create a backup, or select a backup to continue.");
    } else {
        set_status(state, "Select a device or backup to enable the next safe action.");
    }
}

void refresh_devices(AppState* state) {
    clear_list(state->device_list);
    std::string error;
    if (!load_profile_from_service(state, error)) {
        set_status(state, "Service profile load failed: " + error);
        return;
    }
    for (const auto& device : state->devices) {
        GtkWidget* row = make_device_card(device);
        g_object_set_data_full(G_OBJECT(row), "device-identity", g_strdup(device.identity.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(state->device_list), row, -1);
    }
    gtk_widget_show_all(state->device_list);
    refresh_device_card_expansion(state);
    refresh_bench_summary(state);
    refresh_action_availability(state);
}

void refresh_backups(AppState* state) {
    clear_list(state->backup_list);
    std::string response;
    std::string error;
    if (service_request(state, "{\"command\":\"backups\"}", response, error) && service_response_ok(response)) {
        state->backups = parse_backup_rows(extract_json_string(response, "backups_rows"));
    }
    const char* query_text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    const std::string query = query_text == nullptr ? "" : query_text;
    int visible_count = 0;
    for (const auto& backup : state->backups) {
        if (!contains_case_insensitive(backup_search_text(backup), query)) {
            continue;
        }
        GtkWidget* row = make_backup_card(backup);
        g_object_set_data_full(G_OBJECT(row), "image-directory", g_strdup(backup.image_directory.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(state->backup_list), row, -1);
        ++visible_count;
    }
    if (visible_count == 0) {
        GtkWidget* row = make_backup_card(BackupSummary{
            query.empty() ? "Create a backup from a source drive, or connect configured image storage." : "No backups match the current search.",
            "No Lazarus backups found",
            ""
        });
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_insert(GTK_LIST_BOX(state->backup_list), row, -1);
    }
    gtk_widget_show_all(state->backup_list);
    refresh_action_availability(state);
}

std::string selected_backup_path(AppState* state) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(state->backup_list));
    if (row == nullptr) {
        return "";
    }
    const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "image-directory"));
    return path == nullptr ? "" : path;
}

void verify_selected_backup(AppState* state) {
    const auto image_path = selected_backup_path(state);
    if (image_path.empty()) {
        set_status(state, "Select a backup before verifying.");
        return;
    }
    reset_operation_view(state, "Verify Backup", image_path);
    const auto request = "{\"command\":\"verify_image\",\"image_directory\":" + quote_json(image_path) + "}";
    schedule_service_operation(state, DeferredOperationKind::Verify, request);
}

struct BackupForm {
    JobInfo job;
    std::string storage_root;
    CompressionMode compression = CompressionMode::Zstd;
};

std::optional<BackupForm> run_backup_dialog(AppState* state) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Create Backup", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Start Backup", GTK_RESPONSE_OK, nullptr);
    style_dialog(dialog, 680, 500, false);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_container_add(GTK_CONTAINER(area), grid);
    gtk_grid_attach(GTK_GRID(grid), make_label("Create a verified Lazarus image", "dialog-title"), 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Record the job details before imaging begins.", "dialog-intro"), 0, 1, 2, 1);

    GtkWidget* ticket = gtk_entry_new();
    GtkWidget* customer = gtk_entry_new();
    GtkWidget* tech = gtk_entry_new();
    GtkWidget* purpose = gtk_entry_new();
    GtkWidget* storage = gtk_combo_box_text_new();
    GtkWidget* compression = gtk_combo_box_text_new();
    for (const auto& root : split_lines(state->profile.image_storage_text)) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(storage), root.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(storage), 0);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(compression), "zstd");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(compression), "none");
    gtk_combo_box_set_active(GTK_COMBO_BOX(compression), 0);

    gtk_grid_attach(GTK_GRID(grid), make_label("Ticket number", "dialog-label"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ticket, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Customer name", "dialog-label"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), customer, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Technician", "dialog-label"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tech, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Purpose", "dialog-label"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), purpose, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Image storage", "dialog-label"), 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), storage, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_label("Compression", "dialog-label"), 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), compression, 1, 7, 1, 1);
    gtk_widget_show_all(dialog);

    std::optional<BackupForm> form;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        BackupForm value;
        value.job.ticket_number = gtk_entry_get_text(GTK_ENTRY(ticket));
        value.job.customer_name = gtk_entry_get_text(GTK_ENTRY(customer));
        value.job.technician = gtk_entry_get_text(GTK_ENTRY(tech));
        value.job.purpose = gtk_entry_get_text(GTK_ENTRY(purpose));
        char* storage_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(storage));
        char* compression_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(compression));
        value.storage_root = storage_text == nullptr ? "" : storage_text;
        value.compression = compression_text != nullptr && std::string(compression_text) == "none" ? CompressionMode::None : CompressionMode::Zstd;
        g_free(storage_text);
        g_free(compression_text);
        form = value;
    }
    gtk_widget_destroy(dialog);
    return form;
}

void create_backup(AppState* state) {
    auto source = selected_device(state);
    if (!source || !is_source_device(*source)) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a source-only device first.");
        return;
    }
    auto form = run_backup_dialog(state);
    if (!form) {
        return;
    }
    if (!is_complete(form->job) || form->storage_root.empty()) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Ticket, customer, technician, purpose, and image storage are required.");
        return;
    }
    const auto output_directory = build_image_directory(form->storage_root, form->job);
    reset_operation_view(state, "Create Backup", device_display_name(*source) + " -> " + output_directory);
    const auto request = "{\"command\":\"image_source\",\"selector\":" + quote_json(source->identity) +
                         ",\"output_directory\":" + quote_json(output_directory) +
                         ",\"ticket_number\":" + quote_json(form->job.ticket_number) +
                         ",\"customer_name\":" + quote_json(form->job.customer_name) +
                         ",\"technician\":" + quote_json(form->job.technician) +
                         ",\"purpose\":" + quote_json(form->job.purpose) +
                         ",\"compression\":" + quote_json(form->compression == CompressionMode::Zstd ? "zstd" : "none") + "}";
    schedule_service_operation(state, DeferredOperationKind::Backup, request);
}

std::string run_erase_dialog(AppState* state, const DeviceSummary& destination) {
    std::string text = "The following drive will be erased:\n\n";
    text += destination.linux_path + "\n";
    if (!destination.label.empty()) {
        text += "Label: " + destination.label + "\n";
    }
    text += destination.model + "\n";
    text += human_size(destination.size_bytes) + "\n\nType ERASE to continue.";
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Confirm Restore", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Restore", GTK_RESPONSE_OK, nullptr);
    style_dialog(dialog, 620, 420, true);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("Confirm destructive restore", "dialog-title"), FALSE, FALSE, 0);
    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(warning, "dialog-warning");
    gtk_box_pack_start(GTK_BOX(warning), make_label(text, "dialog-intro"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), warning, FALSE, FALSE, 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    std::string confirmation;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        confirmation = gtk_entry_get_text(GTK_ENTRY(entry));
    }
    gtk_widget_destroy(dialog);
    return confirmation;
}

std::string run_install_dialog(AppState* state, const DeviceSummary& target) {
    std::string text = "Arcology Lazarus will be installed to this disk:\n\n";
    text += target.linux_path + "\n";
    text += device_display_name(target) + "\n";
    text += human_size(target.size_bytes) + "\n";
    if (!target.identity.empty()) {
        text += target.identity + "\n";
    }
    text += "\nThe selected disk will be partitioned and erased.\n";
    text += "Type ERASE to install Lazarus OS.";

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Install Lazarus OS",
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL,
        "_Cancel",
        GTK_RESPONSE_CANCEL,
        "_Install",
        GTK_RESPONSE_OK,
        nullptr);
    style_dialog(dialog, 660, 460, true);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("Install Lazarus OS", "dialog-title"), FALSE, FALSE, 0);
    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(warning, "dialog-warning");
    gtk_box_pack_start(GTK_BOX(warning), make_label(text, "dialog-intro"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), warning, FALSE, FALSE, 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "ERASE");
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    std::string confirmation;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        confirmation = gtk_entry_get_text(GTK_ENTRY(entry));
    }
    gtk_widget_destroy(dialog);
    return confirmation;
}

void restore_selected_backup(AppState* state) {
    const auto image_path = selected_backup_path(state);
    if (image_path.empty()) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a backup first.");
        return;
    }
    auto destination = selected_device(state);
    if (!destination || !is_destination_device(*destination)) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a destination-only device first.");
        return;
    }
    const auto confirmation = run_erase_dialog(state, *destination);
    const auto request = "{\"command\":\"restore_image\",\"image_directory\":" + quote_json(image_path) +
                         ",\"selector\":" + quote_json(destination->identity) +
                         ",\"confirmation\":" + quote_json(confirmation) + "}";
    reset_operation_view(state, "Restore Backup", image_path + " -> " + device_display_name(*destination));
    schedule_service_operation(state, DeferredOperationKind::Restore, request);
}

GtkWidget* make_text_view(const std::string& text) {
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
    return view;
}

std::string text_view_text(GtkWidget* view) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string result = text == nullptr ? "" : text;
    g_free(text);
    return result;
}

void set_text_view_text(GtkWidget* view, const std::string& text) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text.c_str(), -1);
}

bool contains_exact(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void append_line_unique(GtkWidget* view, const std::string& line) {
    auto lines = split_lines(text_view_text(view));
    if (!contains_exact(lines, line)) {
        lines.push_back(line);
    }
    set_text_view_text(view, join_lines(lines));
}

void remove_line(GtkWidget* view, const std::string& line) {
    auto lines = split_lines(text_view_text(view));
    lines.erase(std::remove(lines.begin(), lines.end(), line), lines.end());
    set_text_view_text(view, join_lines(lines));
}

GtkWidget* make_scrolled_text_view(GtkWidget* view) {
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, 640, 90);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    return scroll;
}

void upsert_label_line(GtkWidget* labels, const std::string& identity, const std::string& label) {
    auto existing = split_lines(text_view_text(labels));
    bool updated = false;
    for (auto& line : existing) {
        const auto separator = line.find('|');
        if (separator != std::string::npos && trim(line.substr(0, separator)) == identity) {
            line = identity + "|" + label;
            updated = true;
            break;
        }
    }
    if (!updated) {
        existing.push_back(identity + "|" + label);
    }
    set_text_view_text(labels, join_lines(existing));
}

struct ProfileEditorState {
    AppState* app = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* device_list = nullptr;
    GtkWidget* install_list = nullptr;
    GtkWidget* storage = nullptr;
    GtkWidget* storage_device = nullptr;
    GtkWidget* source = nullptr;
    GtkWidget* destination = nullptr;
    GtkWidget* ignored = nullptr;
    GtkWidget* labels = nullptr;
    GtkWidget* label_list = nullptr;
    GtkWidget* label_entry = nullptr;
    GtkWidget* port_label_entry = nullptr;
    GtkWidget* storage_entry = nullptr;
    GtkWidget* pending_status = nullptr;
    GtkWidget* selected_title = nullptr;
    GtkWidget* selected_detail = nullptr;
    GtkWidget* selected_label = nullptr;
    GtkWidget* selected_badge = nullptr;
    GtkWidget* selected_note = nullptr;
    GtkWidget* install_title = nullptr;
    GtkWidget* install_detail = nullptr;
    GtkWidget* install_badge = nullptr;
    GtkWidget* install_note = nullptr;
    GtkWidget* branding_profile = nullptr;
    GtkWidget* branding_theme = nullptr;
    GtkWidget* branding_product_name = nullptr;
    GtkWidget* branding_subtitle = nullptr;
    GtkWidget* branding_accent = nullptr;
    GtkWidget* branding_background = nullptr;
    GtkWidget* branding_surface = nullptr;
    GtkWidget* branding_text = nullptr;
    GtkWidget* branding_icon = nullptr;
    GtkWidget* branding_logo = nullptr;
    GtkWidget* branding_report_footer = nullptr;
};

void set_entry_value(GtkWidget* entry, const char* value) {
    if (entry != nullptr) {
        gtk_entry_set_text(GTK_ENTRY(entry), value);
    }
}

void apply_branding_profile(ProfileEditorState* editor) {
    const char* profile = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor->branding_profile));
    if (profile == nullptr || std::string(profile) == "custom") {
        return;
    }
    if (std::string(profile) == "lazarus-default") {
        set_entry_value(editor->branding_theme, "Lazarus Default Theme");
        set_entry_value(editor->branding_product_name, "Arcology Lazarus");
        set_entry_value(editor->branding_subtitle, "Offline Imaging | Recovery | Hardware Migration");
        set_entry_value(editor->branding_accent, "#f39a22");
        set_entry_value(editor->branding_background, "#10161b");
        set_entry_value(editor->branding_surface, "#171f25");
        set_entry_value(editor->branding_text, "#edf1f3");
        set_entry_value(editor->branding_icon, "#f39a22");
        set_entry_value(editor->branding_logo, "");
        set_entry_value(editor->branding_report_footer, "Generated locally by Arcology Lazarus. SMART results describe reported device facts.");
        return;
    }
    if (std::string(profile) == "acs") {
        set_entry_value(editor->branding_theme, "ACS Computer Services");
        set_entry_value(editor->branding_product_name, "ACS Lazarus");
        set_entry_value(editor->branding_subtitle, "Offline Imaging | Recovery | Hardware Migration");
        set_entry_value(editor->branding_accent, "#cf3b2b");
        set_entry_value(editor->branding_background, "#ffffff");
        set_entry_value(editor->branding_surface, "#f5f8fb");
        set_entry_value(editor->branding_text, "#15171a");
        set_entry_value(editor->branding_icon, "#1787c4");
        set_entry_value(editor->branding_logo, "/usr/share/arcology-lazarus/assets/acs-computer-services-logo.png");
        set_entry_value(editor->branding_report_footer, "Prepared by ACS Computer Services. SMART results describe reported device facts.");
    }
}

std::string selected_editor_identity(ProfileEditorState* editor) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(editor->device_list));
    if (row == nullptr) {
        return "";
    }
    const char* identity = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "profile-identity"));
    return identity == nullptr ? "" : identity;
}

std::string selected_install_identity(ProfileEditorState* editor) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(editor->install_list));
    if (row == nullptr) {
        return "";
    }
    const char* identity = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "profile-identity"));
    return identity == nullptr ? "" : identity;
}

bool selected_editor_system_disk(ProfileEditorState* editor) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(editor->device_list));
    if (row == nullptr) {
        return false;
    }
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "system-disk")) != 0;
}

void set_label_text(GtkWidget* widget, const std::string& text) {
    if (widget != nullptr) {
        gtk_label_set_text(GTK_LABEL(widget), text.c_str());
    }
}

bool profile_has_identity(GtkWidget* view, const std::string& identity) {
    return contains_exact(split_lines(text_view_text(view)), identity);
}

std::string editor_role(ProfileEditorState* editor, const std::string& identity) {
    if (trim(text_view_text(editor->storage_device)) == identity) {
        return "image-storage";
    }
    if (profile_has_identity(editor->source, identity)) {
        return "source-only";
    }
    if (profile_has_identity(editor->destination, identity)) {
        return "destination-only";
    }
    if (profile_has_identity(editor->ignored, identity)) {
        return "ignored";
    }
    return "unconfigured";
}

std::string editor_label(ProfileEditorState* editor, const std::string& identity) {
    for (const auto& line : split_lines(text_view_text(editor->labels))) {
        const auto separator = line.find('|');
        if (separator != std::string::npos && trim(line.substr(0, separator)) == identity) {
            return trim(line.substr(separator + 1));
        }
    }
    return "";
}

std::string editor_role_badge(const std::string& role) {
    if (role == "source-only") return "SOURCE ONLY";
    if (role == "destination-only") return "DESTINATION ONLY";
    if (role == "image-storage") return "IMAGE STORAGE";
    if (role == "ignored") return "IGNORED";
    return "UNCONFIGURED";
}

void refresh_label_list(ProfileEditorState* editor) {
    if (editor->label_list == nullptr) {
        return;
    }

    GList* children = gtk_container_get_children(GTK_CONTAINER(editor->label_list));
    for (GList* child = children; child != nullptr; child = child->next) {
        gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);

    for (const auto& device : editor->app->devices) {
        const auto label = editor_label(editor, device.identity);
        const auto title = label.empty() ? device_display_name(device) : label;
        const std::string detail = (label.empty() ? std::string("No label assigned") : device_display_name(device)) +
                                    "\n" + device.identity;
        GtkWidget* row = make_row(title, detail);
        g_object_set_data_full(G_OBJECT(row), "profile-identity", g_strdup(device.identity.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(editor->label_list), row, -1);
    }
    gtk_widget_show_all(editor->label_list);
}

void mark_editor_pending(ProfileEditorState* editor, const std::string& message) {
    set_label_text(editor->pending_status, "Unsaved changes: " + message + " Click Save to apply.");
    add_class(editor->pending_status, "admin-warning");
}

void refresh_editor_selection(ProfileEditorState* editor) {
    const auto identity = selected_editor_identity(editor);
    auto device = identity.empty() ? std::nullopt : find_device_by_identity(editor->app, identity);
    if (!device) {
        set_label_text(editor->selected_title, "No device selected");
        set_label_text(editor->selected_detail, "Select a device from the list to assign a role or edit a friendly label.");
        set_label_text(editor->selected_label, "Friendly label: None");
        set_label_text(editor->selected_badge, "UNSELECTED");
        set_label_text(editor->selected_note, "Source-only and destination-only assignments are blocked on the running system disk.");
        set_label_text(editor->install_title, "Select a destination disk");
        set_label_text(editor->install_detail, "Choose a writable destination device before attempting installation.");
        set_label_text(editor->install_badge, "READY");
        set_label_text(editor->install_note, "The install action only accepts a destination that is not marked as source-only or storage.");
        return;
    }

    const auto configured_role = editor_role(editor, identity);
    const auto label = editor_label(editor, identity);
    gtk_entry_set_text(GTK_ENTRY(editor->label_entry), label.c_str());
    gtk_entry_set_text(GTK_ENTRY(editor->port_label_entry), label.c_str());
    set_label_text(editor->selected_title, label.empty() ? device_display_name(*device) : label);
    set_label_text(editor->selected_detail, device_technical_detail(*device));
    set_label_text(editor->selected_label, label.empty() ? "Friendly label: None" : "Friendly label: " + label);
    set_label_text(editor->selected_badge, editor_role_badge(configured_role));
    if (device->system_disk) {
        set_label_text(editor->selected_note, "The running system disk is protected. Lazarus will not assign it as source-only or destination-only.");
    } else if (configured_role == "source-only") {
        set_label_text(editor->selected_note, "Pending change: this device will be read-only for source imaging after Save.");
    } else if (configured_role == "destination-only") {
        set_label_text(editor->selected_note, "Pending change: this device will be writable for restore and install after Save.");
    } else if (configured_role == "image-storage") {
        set_label_text(editor->selected_note, "Pending change: this device will be mounted read-write for persistent Lazarus images after Save.");
    } else if (configured_role == "ignored") {
        set_label_text(editor->selected_note, "Pending change: Lazarus will not use this device after Save.");
    } else if (configured_role == "unconfigured" && device->role == "source-only") {
        set_label_text(editor->selected_note, "This device is currently source-only in the saved profile.");
    } else if (configured_role == "unconfigured" && device->role == "destination-only") {
        set_label_text(editor->selected_note, "This device is currently destination-only in the saved profile.");
    } else if (device->role == "image-storage") {
        set_label_text(editor->selected_note, "This device is reserved for Lazarus image storage.");
    } else {
        set_label_text(editor->selected_note, "Assign exactly one role or leave it unconfigured for later review.");
    }

    set_label_text(editor->install_title, device_display_name(*device));
    set_label_text(editor->install_detail, device_technical_detail(*device));
    set_label_text(editor->install_badge, role_badge_text(*device));
    if (device->system_disk) {
        set_label_text(editor->install_note, "Installation is blocked because this is the running Lazarus system disk.");
    } else if (device->role == "source-only" || device->role == "image-storage") {
        set_label_text(editor->install_note, "Installation is blocked for source-only and storage devices.");
    } else {
        set_label_text(editor->install_note, "This is a valid installation target once the ERASE confirmation is entered.");
    }
}

enum class EditorRole {
    SourceOnly,
    DestinationOnly,
    ImageStorage,
    Ignored,
    Unknown,
};

void assign_editor_role(ProfileEditorState* editor, EditorRole role) {
    const auto identity = selected_editor_identity(editor);
    if (identity.empty()) {
        show_message(GTK_WINDOW(editor->app->window), GTK_MESSAGE_WARNING, "Select a detected device first.");
        return;
    }
    if ((role == EditorRole::SourceOnly || role == EditorRole::DestinationOnly || role == EditorRole::ImageStorage) && selected_editor_system_disk(editor)) {
        show_message(GTK_WINDOW(editor->app->window), GTK_MESSAGE_WARNING, "The running system disk cannot be configured as a source, destination, or image-storage device.");
        return;
    }

    remove_line(editor->source, identity);
    remove_line(editor->destination, identity);
    remove_line(editor->ignored, identity);
    if (role == EditorRole::ImageStorage) {
        set_text_view_text(editor->storage_device, identity);
        set_text_view_text(editor->storage, "/mnt/lazarus-storage/images");
    } else if (trim(text_view_text(editor->storage_device)) == identity) {
        set_text_view_text(editor->storage_device, "");
    }

    if (role == EditorRole::SourceOnly) {
        append_line_unique(editor->source, identity);
    } else if (role == EditorRole::DestinationOnly) {
        append_line_unique(editor->destination, identity);
    } else if (role == EditorRole::Ignored) {
        append_line_unique(editor->ignored, identity);
    }

    refresh_editor_selection(editor);
    const char* role_name = role == EditorRole::SourceOnly ? "source-only" :
                            role == EditorRole::DestinationOnly ? "destination-only" :
                            role == EditorRole::ImageStorage ? "image-storage" :
                            role == EditorRole::Ignored ? "ignored" : "unconfigured";
    mark_editor_pending(editor, "role set to " + std::string(role_name) + ".");
}

void apply_editor_label(ProfileEditorState* editor, GtkWidget* entry) {
    const auto identity = selected_editor_identity(editor);
    if (identity.empty()) {
        show_message(GTK_WINDOW(editor->app->window), GTK_MESSAGE_WARNING, "Select a detected device first.");
        return;
    }
    const char* label_text = gtk_entry_get_text(GTK_ENTRY(entry));
    const std::string label = trim(label_text == nullptr ? "" : label_text);
    if (label.empty()) {
        show_message(GTK_WINDOW(editor->app->window), GTK_MESSAGE_WARNING, "Enter a friendly port label first.");
        return;
    }
    upsert_label_line(editor->labels, identity, label);
    gtk_entry_set_text(GTK_ENTRY(editor->label_entry), "");
    gtk_entry_set_text(GTK_ENTRY(editor->port_label_entry), "");
    refresh_editor_selection(editor);
    refresh_label_list(editor);
    mark_editor_pending(editor, "friendly label set to \"" + label + "\".");
}

void add_editor_storage(ProfileEditorState* editor) {
    const char* path_text = gtk_entry_get_text(GTK_ENTRY(editor->storage_entry));
    const std::string path = trim(path_text == nullptr ? "" : path_text);
    if (path.empty()) {
        show_message(GTK_WINDOW(editor->app->window), GTK_MESSAGE_WARNING, "Enter an image storage path first.");
        return;
    }
    append_line_unique(editor->storage, path);
    gtk_entry_set_text(GTK_ENTRY(editor->storage_entry), "");
    mark_editor_pending(editor, "image storage path added.");
}

bool is_lazarus_storage_folder(const std::filesystem::path& candidate) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    if (error || !std::filesystem::is_directory(normalized, error)) {
        return false;
    }
    for (const auto& root : {std::filesystem::path("/mnt/lazarus-storage"),
                             std::filesystem::path("/var/lib/arcology-lazarus/images")}) {
        const auto normalized_root = std::filesystem::weakly_canonical(root, error);
        if (error || !std::filesystem::is_directory(normalized_root, error)) {
            error.clear();
            continue;
        }
        auto path_part = normalized.begin();
        auto root_part = normalized_root.begin();
        for (; root_part != normalized_root.end() && path_part != normalized.end(); ++root_part, ++path_part) {
            if (*root_part != *path_part) {
                break;
            }
        }
        if (root_part == normalized_root.end()) {
            return true;
        }
    }
    return false;
}

struct StorageBrowserState {
    ProfileEditorState* editor = nullptr;
    GtkWidget* dialog = nullptr;
    GtkWidget* path_label = nullptr;
    GtkWidget* folder_list = nullptr;
    std::filesystem::path current_path;
};

void refresh_storage_browser(StorageBrowserState* browser) {
    clear_list(browser->folder_list);
    gtk_label_set_text(GTK_LABEL(browser->path_label), browser->current_path.c_str());

    const auto parent = browser->current_path.parent_path();
    if (parent != browser->current_path && is_lazarus_storage_folder(parent)) {
        GtkWidget* row = make_row("Up one folder", parent.string());
        g_object_set_data_full(G_OBJECT(row), "storage-folder", g_strdup(parent.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(browser->folder_list), row, -1);
    }

    std::vector<std::filesystem::path> directories;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(browser->current_path,
                                                                   std::filesystem::directory_options::skip_permission_denied,
                                                                   error)) {
        if (entry.is_directory(error) && is_lazarus_storage_folder(entry.path())) {
            directories.push_back(entry.path());
        }
        error.clear();
    }
    std::sort(directories.begin(), directories.end());
    for (const auto& directory : directories) {
        GtkWidget* row = make_row(directory.filename().string(), "Folder");
        g_object_set_data_full(G_OBJECT(row), "storage-folder", g_strdup(directory.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(browser->folder_list), row, -1);
    }
    if (directories.empty()) {
        GtkWidget* row = make_row("No subfolders", "Use this folder or create a folder for a customer, bench, or retention policy.");
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_insert(GTK_LIST_BOX(browser->folder_list), row, -1);
    }
    gtk_widget_show_all(browser->folder_list);
}

void create_storage_browser_folder(StorageBrowserState* browser) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Create Storage Folder", GTK_WINDOW(browser->dialog), GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_OK, nullptr);
    style_dialog(dialog, 480, 250, false);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("Create Folder", "dialog-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label("The folder will be created inside the current approved Lazarus storage location.", "dialog-intro"), FALSE, FALSE, 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Folder name");
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const std::string name = trim(gtk_entry_get_text(GTK_ENTRY(entry)));
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
            show_message(GTK_WINDOW(browser->dialog), GTK_MESSAGE_WARNING, "Enter a simple folder name without path separators.");
        } else {
            std::error_code error;
            const auto new_folder = browser->current_path / name;
            if (!std::filesystem::create_directory(new_folder, error) && error) {
                show_message(GTK_WINDOW(browser->dialog), GTK_MESSAGE_WARNING, "Could not create folder: " + error.message());
            } else {
                browser->current_path = new_folder;
                refresh_storage_browser(browser);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void browse_editor_storage(ProfileEditorState* editor) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Choose Image Storage Folder", GTK_WINDOW(editor->dialog), GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL, "_Use This Folder", GTK_RESPONSE_ACCEPT, nullptr);
    style_dialog(dialog, 720, 520, false);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("Choose Image Storage Folder", "dialog-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label("Only folders on mounted Lazarus image storage are shown.", "dialog-intro"), FALSE, FALSE, 0);

    GtkWidget* path_label = make_label("", "accent");
    gtk_box_pack_start(GTK_BOX(box), path_label, FALSE, FALSE, 0);
    GtkWidget* folder_list = gtk_list_box_new();
    add_class(folder_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(folder_list), GTK_SELECTION_SINGLE);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(scroll, "dark-list");
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), folder_list);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget* new_folder = gtk_button_new_with_label("Create Folder");
    add_class(new_folder, "admin-button");
    gtk_box_pack_start(GTK_BOX(box), new_folder, FALSE, FALSE, 0);

    const auto entered = trim(gtk_entry_get_text(GTK_ENTRY(editor->storage_entry)));
    StorageBrowserState browser{editor, dialog, path_label, folder_list,
                                is_lazarus_storage_folder(entered) ? std::filesystem::path(entered) :
                                    (is_lazarus_storage_folder("/mnt/lazarus-storage/images") ?
                                         std::filesystem::path("/mnt/lazarus-storage/images") :
                                         std::filesystem::path("/var/lib/arcology-lazarus/images"))};
    refresh_storage_browser(&browser);
    g_signal_connect(folder_list, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* browser = static_cast<StorageBrowserState*>(data);
        const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "storage-folder"));
        if (path != nullptr && is_lazarus_storage_folder(path)) {
            browser->current_path = path;
            refresh_storage_browser(browser);
        }
    }), &browser);
    g_signal_connect_swapped(new_folder, "clicked", G_CALLBACK(create_storage_browser_folder), &browser);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gtk_entry_set_text(GTK_ENTRY(editor->storage_entry), browser.current_path.c_str());
        mark_editor_pending(editor, "storage folder selected. Click Add Storage to use it.");
    }
    gtk_widget_destroy(dialog);
}

bool load_smart_report(AppState* state, const DeviceSummary& device, std::string& report, std::string& health, std::string& error) {
    std::string response;
    const auto request = "{\"command\":\"smart\",\"selector\":" + quote_json(device.identity) + "}";
    set_status(state, "Reading SMART diagnostics through Lazarus service...");
    pump_events();
    if (!service_request(state, request, response, error)) {
        return false;
    }

    health = extract_json_string(response, "health");
    const auto model = extract_json_string(response, "model");
    const auto serial = extract_json_string(response, "serial");
    const auto smartctl_available = extract_json_bool(response, "smartctl_available");
    const auto command_completed = extract_json_bool(response, "command_completed");
    report = "SMART DIAGNOSTICS\n\n";
    report += device_display_name(device) + "\n";
    report += device.linux_path + "\n\n";
    report += "Overall health: " + (health.empty() ? "unknown" : health) + "\n";
    report += "smartctl available: " + std::string(smartctl_available ? "yes" : "no") + "\n";
    report += "Command completed: " + std::string(command_completed ? "yes" : "no") + "\n";
    if (!model.empty()) report += "Model: " + model + "\n";
    if (!serial.empty()) report += "Serial: " + serial + "\n";
    report += "Power-on hours: " + smart_attribute_summary(response, "power_on_hours", "") + "\n";
    report += "Temperature: " + smart_attribute_summary(response, "temperature_celsius", " C") + "\n";
    report += "Reallocated sectors: " + smart_attribute_summary(response, "reallocated_sectors", "") + "\n";
    report += "Pending sectors: " + smart_attribute_summary(response, "pending_sectors", "") + "\n";
    report += "Uncorrectable errors: " + smart_attribute_summary(response, "uncorrectable_errors", "") + "\n";
    if (!serial.empty()) {
        report += "\nSMART data was read locally by Lazarus.\n";
    }
    const auto error_text = extract_json_string(response, "error");
    if (!error_text.empty()) {
        report += "\nService result: " + error_text + "\n";
    }
    return true;
}

struct SmartPrintData {
    std::string report;
    std::string product_name;
    std::string report_footer;
};

void draw_smart_print_page(GtkPrintOperation*, GtkPrintContext* context, gint, gpointer data) {
    auto* print_data = static_cast<SmartPrintData*>(data);
    cairo_t* cr = gtk_print_context_get_cairo_context(context);
    const double width = gtk_print_context_get_width(context);
    double y = 42.0;

    cairo_set_source_rgb(cr, 0.92, 0.45, 0.03);
    cairo_rectangle(cr, 0, 0, width, 18);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.08, 0.10, 0.12);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, 36, y);
    cairo_show_text(cr, print_data->product_name.c_str());
    y += 26;
    cairo_set_font_size(cr, 11);
    cairo_set_source_rgb(cr, 0.35, 0.38, 0.40);
    cairo_show_text(cr, "SMART DRIVE DIAGNOSTIC REPORT");
    y += 28;

    cairo_set_source_rgb(cr, 0.12, 0.14, 0.16);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    std::istringstream lines(print_data->report);
    std::string line;
    while (std::getline(lines, line)) {
        if (y > gtk_print_context_get_height(context) - 30) {
            break;
        }
        cairo_move_to(cr, 36, y);
        cairo_show_text(cr, line.c_str());
        y += 16;
    }
    cairo_set_source_rgb(cr, 0.45, 0.47, 0.48);
    cairo_set_font_size(cr, 8);
    cairo_move_to(cr, 36, gtk_print_context_get_height(context) - 24);
    cairo_show_text(cr, print_data->report_footer.c_str());
}

void print_smart_report(AppState* state, GtkWindow* parent, const std::string& report) {
    auto* operation = gtk_print_operation_new();
    gtk_print_operation_set_n_pages(operation, 1);
    SmartPrintData data{report,
                        state->profile.branding_product_name.empty() ? "ARCOLOGY LAZARUS" : state->profile.branding_product_name,
                        state->profile.branding_report_footer.empty() ? "Generated locally by Arcology Lazarus. SMART results describe reported device facts." : state->profile.branding_report_footer};
    g_signal_connect(operation, "draw-page", G_CALLBACK(draw_smart_print_page), &data);
    GError* error = nullptr;
    gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, parent, &error);
    if (error != nullptr) {
        show_message(parent, GTK_MESSAGE_ERROR, "SMART report could not be printed: " + std::string(error->message));
        g_error_free(error);
    }
    g_object_unref(operation);
}

void attach_smart_report(AppState* state, GtkWindow* parent, const std::string& report) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Attach SMART Report", parent, GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Attach Report", GTK_RESPONSE_OK,
                                                     nullptr);
    style_dialog(dialog, 560, 300, false);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("Attach SMART report to a ticket", "dialog-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label("The report will be saved inside the matching Lazarus backup directory.", "dialog-intro"), FALSE, FALSE, 0);
    GtkWidget* ticket = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ticket), "Ticket number, for example 45127");
    gtk_box_pack_start(GTK_BOX(box), ticket, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const std::string query = trim(gtk_entry_get_text(GTK_ENTRY(ticket)));
        std::string target;
        for (const auto& backup : state->backups) {
            if (contains_case_insensitive(backup.search_text, query) && !query.empty()) {
                target = backup.image_directory;
                break;
            }
        }
        if (target.empty()) {
            show_message(parent, GTK_MESSAGE_WARNING, "No existing Lazarus backup was found for ticket " + query + ". Create or locate the backup first.");
        } else {
            std::ofstream out(std::filesystem::path(target) / "smart-report.txt");
            if (!out) {
                show_message(parent, GTK_MESSAGE_ERROR, "The SMART report could not be attached to the selected ticket.");
            } else {
                out << report;
                set_status(state, "SMART report attached to ticket " + query + ".");
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void show_smart_report(AppState* state, const DeviceSummary& device) {
    std::string report;
    std::string health;
    std::string error;
    if (!load_smart_report(state, device, report, health, error)) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR, "SMART diagnostics could not complete: " + error);
        return;
    }

    GtkWidget* dialog = gtk_dialog_new_with_buttons("SMART Diagnostics Report", GTK_WINDOW(state->window), GTK_DIALOG_MODAL,
                                                     "_Close", GTK_RESPONSE_CANCEL,
                                                     "_Print Report", GTK_RESPONSE_APPLY,
                                                     "_Attach to Ticket", GTK_RESPONSE_YES,
                                                     "_Save Report", GTK_RESPONSE_OK,
                                                     nullptr);
    style_dialog(dialog, 720, 620, false);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_container_add(GTK_CONTAINER(area), box);
    gtk_box_pack_start(GTK_BOX(box), make_label("SMART diagnostic report", "dialog-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), make_label("Local drive health facts for technician review and customer records.", "dialog-intro"), FALSE, FALSE, 0);
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), report.c_str(), -1);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 220);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    while (true) {
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_APPLY) {
            print_smart_report(state, GTK_WINDOW(dialog), report);
            continue;
        }
        if (response == GTK_RESPONSE_YES) {
            attach_smart_report(state, GTK_WINDOW(dialog), report);
            continue;
        }
        if (response == GTK_RESPONSE_OK) {
            GtkWidget* chooser = gtk_file_chooser_dialog_new("Save SMART Report", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                              "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "lazarus-smart-report.txt");
            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
                char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
                if (path != nullptr) {
                    std::ofstream out(path);
                    out << report;
                    if (!out) {
                        show_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, "SMART report could not be saved.");
                    }
                    g_free(path);
                }
            }
            gtk_widget_destroy(chooser);
            continue;
        }
        break;
    }
    gtk_widget_destroy(dialog);
    set_status(state, "SMART diagnostics completed for " + device_display_name(device) + ".");
}

void show_smart_diagnostics(AppState* state, ProfileEditorState* editor) {
    const auto identity = selected_editor_identity(editor);
    if (identity.empty()) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a detected device first.");
        return;
    }
    auto device = find_device_by_identity(state, identity);
    if (!device) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "The selected device is no longer available.");
        return;
    }
    show_smart_report(state, *device);
}

void install_to_disk(AppState* state, ProfileEditorState* editor) {
    const auto identity = selected_install_identity(editor);
    if (identity.empty()) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a detected disk first.");
        return;
    }
    auto target = find_device_by_identity(state, identity);
    if (!target) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "The selected device is no longer available.");
        return;
    }
    if (!is_install_target_device(*target)) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_WARNING, "Select a writable destination disk. Source-only and storage disks cannot be installed over.");
        return;
    }

    const auto confirmation = run_install_dialog(state, *target);
    if (confirmation != "ERASE") {
        set_status(state, "Install to disk cancelled.");
        return;
    }

    if (editor->dialog != nullptr) {
        gtk_widget_hide(editor->dialog);
        gtk_dialog_response(GTK_DIALOG(editor->dialog), GTK_RESPONSE_CANCEL);
    }
    reset_operation_view(state, "Install to Disk", device_display_name(*target) + " -> Lazarus OS");
    const auto request = "{\"command\":\"install_os\",\"selector\":" + quote_json(target->identity) +
                         ",\"confirmation\":" + quote_json(confirmation) + "}";
    schedule_service_operation(state, DeferredOperationKind::Install, request);
}

void edit_profile(AppState* state) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("Admin Center", GTK_WINDOW(state->window), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, nullptr);
    gtk_widget_set_name(dialog, "lazarus-admin-dialog");
    install_lazarus_pointer(dialog);
    // Keep the modal admin center inside small live-appliance displays. The
    // old fixed 1320x900 request placed the dialog partially above the QEMU
    // viewport when the guest was running at 1280x800.
    int dialog_width = 1080;
    int dialog_height = 660;
    gtk_window_set_default_size(GTK_WINDOW(dialog), dialog_width, dialog_height);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 0);
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    GtkWidget* shell_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(shell_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(shell_scroll, TRUE);
    gtk_widget_set_vexpand(shell_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(area), shell_scroll);
    GtkWidget* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(shell, "admin-shell");
    gtk_container_set_border_width(GTK_CONTAINER(shell), 8);
    gtk_container_add(GTK_CONTAINER(shell_scroll), shell);

    std::string load_error;
    if (!load_profile_from_service(state, load_error)) {
        show_message(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR, "Profile could not be loaded from Lazarus service: " + load_error);
        gtk_widget_destroy(dialog);
        return;
    }

    GtkWidget* name = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name), state->profile.name.c_str());
    GtkWidget* storage = make_text_view(state->profile.image_storage_text);
    GtkWidget* storage_device = make_text_view(state->profile.image_storage_device);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(storage_device), FALSE);
    GtkWidget* source = make_text_view(state->profile.source_text);
    GtkWidget* destination = make_text_view(state->profile.destination_text);
    GtkWidget* ignored = make_text_view(state->profile.ignored_text);
    GtkWidget* labels = make_text_view(state->profile.labels_text);
    GtkWidget* storage_entry = gtk_entry_new();
    GtkWidget* label_entry = gtk_entry_new();
    GtkWidget* port_label_entry = gtk_entry_new();
    GtkWidget* pending_status = make_label("No unsaved profile changes.", "device-detail");

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    add_class(header, "admin-header");
    gtk_box_pack_start(GTK_BOX(shell), header, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(shell), pending_status, FALSE, FALSE, 0);

    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(brand), make_logo(72, state->profile.branding_logo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brand), make_label("ADMIN CENTER", "subtitle"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), brand, FALSE, FALSE, 0);

    GtkWidget* metrics = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(metrics), make_status_card("BENCH PROFILE", state->profile.name.empty() ? "Loading" : state->profile.name), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(metrics), make_status_card("PROFILE PATH", state->bench_path), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(metrics), make_status_card("IMAGE STORAGE", state->profile.image_storage_text.empty() ? "Unset" : split_lines(state->profile.image_storage_text).front()), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(metrics), make_status_card("BENCH MODE", "Protected"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header), metrics, TRUE, TRUE, 0);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_pack_start(GTK_BOX(shell), body, TRUE, TRUE, 0);

    GtkWidget* rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(rail, "admin-rail");
    gtk_widget_set_size_request(rail, 200, -1);
    gtk_box_pack_start(GTK_BOX(body), rail, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rail), make_label("ADMIN AREAS", "subtitle"), FALSE, FALSE, 0);

    GtkWidget* stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    GtkWidget* sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
    gtk_widget_set_hexpand(sidebar, TRUE);
    gtk_widget_set_vexpand(sidebar, TRUE);
    gtk_box_pack_start(GTK_BOX(rail), sidebar, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(body), stack, TRUE, TRUE, 0);

    GtkWidget* overview_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(overview_page, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(overview_page), 12);
    gtk_stack_add_titled(GTK_STACK(stack), overview_page, "overview", "Overview");
    gtk_box_pack_start(GTK_BOX(overview_page), make_label("Appliance status, profile status, and current bench assignments.", "device-detail"), FALSE, FALSE, 0);
    GtkWidget* overview_grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(overview_page), overview_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(overview_grid), make_status_card("DEVICE POLICY", "Source-only and destination-only roles are enforced by the profile."), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(overview_grid), make_status_card("IMAGE STORAGE", state->profile.image_storage_text.empty() ? "No storage roots configured" : split_lines(state->profile.image_storage_text).front()), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(overview_grid), make_status_card("INSTALLER", "Available from the selected destination disk."), TRUE, TRUE, 0);

    GtkWidget* branding_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(branding_page, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(branding_page), 12);
    gtk_stack_add_titled(GTK_STACK(stack), branding_page, "branding", "Branding");
    gtk_box_pack_start(GTK_BOX(branding_page), make_label("Workplace identity, colors, logo, and report text. Changes apply after saving the profile.", "device-detail"), FALSE, FALSE, 0);
    GtkWidget* branding_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(branding_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(branding_grid), 12);
    gtk_widget_set_hexpand(branding_grid, TRUE);
    GtkWidget* branding_profile = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(branding_profile), "lazarus-default", "Lazarus Default Theme");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(branding_profile), "acs", "ACS Computer Services");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(branding_profile), "custom", "Custom Theme");
    if (state->profile.branding_theme == "Lazarus Default Theme") {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(branding_profile), "lazarus-default");
    } else if (state->profile.branding_theme == "ACS Computer Services") {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(branding_profile), "acs");
    } else {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(branding_profile), "custom");
    }
    gtk_widget_set_hexpand(branding_profile, TRUE);
    gtk_grid_attach(GTK_GRID(branding_grid), make_label("Theme profile", "dialog-label"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(branding_grid), branding_profile, 1, 0, 1, 1);
    GtkWidget* branding_theme = nullptr;
    GtkWidget* branding_product_name = nullptr;
    GtkWidget* branding_subtitle = nullptr;
    GtkWidget* branding_accent = nullptr;
    GtkWidget* branding_background = nullptr;
    GtkWidget* branding_surface = nullptr;
    GtkWidget* branding_text = nullptr;
    GtkWidget* branding_icon = nullptr;
    GtkWidget* branding_logo = nullptr;
    GtkWidget* branding_report_footer = nullptr;
    const auto add_branding_field = [&](const char* label, const std::string& value, int row) {
        GtkWidget* label_widget = make_label(label, "dialog-label");
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
        gtk_widget_set_hexpand(entry, TRUE);
        gtk_grid_attach(GTK_GRID(branding_grid), label_widget, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(branding_grid), entry, 1, row, 1, 1);
        return entry;
    };
    branding_theme = add_branding_field("Theme", state->profile.branding_theme, 1);
    branding_product_name = add_branding_field("Product name", state->profile.branding_product_name, 2);
    branding_subtitle = add_branding_field("Subtitle", state->profile.branding_subtitle, 3);
    branding_accent = add_branding_field("Accent color", state->profile.branding_accent, 4);
    branding_background = add_branding_field("Background color", state->profile.branding_background, 5);
    branding_surface = add_branding_field("Surface color", state->profile.branding_surface, 6);
    branding_text = add_branding_field("Text color", state->profile.branding_text, 7);
    branding_icon = add_branding_field("Icon color", state->profile.branding_icon, 8);
    branding_logo = add_branding_field("Logo path", state->profile.branding_logo, 9);
    branding_report_footer = add_branding_field("Report footer", state->profile.branding_report_footer, 10);
    gtk_box_pack_start(GTK_BOX(branding_page), branding_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(branding_page), make_label("Colors use #RRGGBB or #RRGGBBAA. Leave the logo path empty to use the built-in Lazarus emblem.", "device-detail"), FALSE, FALSE, 0);

    GtkWidget* devices_page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(devices_page), 0);
    gtk_stack_add_titled(GTK_STACK(stack), devices_page, "devices", "Devices & Roles");

    GtkWidget* device_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(device_panel, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(device_panel), 12);
    gtk_widget_set_hexpand(device_panel, TRUE);
    gtk_widget_set_vexpand(device_panel, TRUE);
    gtk_box_pack_start(GTK_BOX(devices_page), device_panel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(device_panel), make_label("Assign exactly one role to each detected device. The running system disk stays blocked.", "device-detail"), FALSE, FALSE, 0);

    GtkWidget* device_panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(device_panel), device_panes, TRUE, TRUE, 0);
    GtkWidget* device_list = gtk_list_box_new();
    add_class(device_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(device_list), GTK_SELECTION_SINGLE);
    for (const auto& device : state->devices) {
        GtkWidget* device_row = make_row(device.title, device.detail + "\n" + device.identity);
        g_object_set_data_full(G_OBJECT(device_row), "profile-identity", g_strdup(device.identity.c_str()), g_free);
        g_object_set_data(G_OBJECT(device_row), "system-disk", GINT_TO_POINTER(device.system_disk ? 1 : 0));
        gtk_list_box_insert(GTK_LIST_BOX(device_list), device_row, -1);
    }
    GtkWidget* device_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(device_scroll, "dark-list");
    gtk_widget_set_size_request(device_scroll, 300, 280);
    gtk_container_add(GTK_CONTAINER(device_scroll), device_list);
    gtk_paned_pack1(GTK_PANED(device_panes), device_scroll, FALSE, FALSE);

    GtkWidget* selection_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(selection_panel, "admin-section");
    gtk_container_set_border_width(GTK_CONTAINER(selection_panel), 12);
    gtk_paned_pack2(GTK_PANED(device_panes), selection_panel, TRUE, FALSE);

    GtkWidget* selection_head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(selection_panel), selection_head, FALSE, FALSE, 0);
    GtkWidget* selected_title = make_label("Select a device", "panel-title");
    GtkWidget* selected_badge = make_label("UNSELECTED", "role-badge");
    add_class(selected_badge, "role-system");
    gtk_box_pack_start(GTK_BOX(selection_head), selected_title, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(selection_head), selected_badge, FALSE, FALSE, 0);
    GtkWidget* selected_detail = make_label("Select a device from the list to inspect its transport, size, and role.", "device-detail");
    gtk_box_pack_start(GTK_BOX(selection_panel), selected_detail, FALSE, FALSE, 0);
    GtkWidget* selected_label = make_label("Friendly label: None", "device-detail");
    gtk_box_pack_start(GTK_BOX(selection_panel), selected_label, FALSE, FALSE, 0);
    GtkWidget* selected_note = make_label("The running system disk is always protected.", "device-detail");
    add_class(selected_note, "admin-warning");
    gtk_box_pack_start(GTK_BOX(selection_panel), selected_note, FALSE, FALSE, 0);

    GtkWidget* role_grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(selection_panel), role_grid, FALSE, FALSE, 0);
    GtkWidget* source_button = gtk_button_new_with_label("Source Only");
    GtkWidget* destination_button = gtk_button_new_with_label("Destination Only");
    GtkWidget* storage_role_button = gtk_button_new_with_label("Image Storage");
    GtkWidget* ignored_button = gtk_button_new_with_label("Ignored");
    GtkWidget* unconfigured_button = gtk_button_new_with_label("Unconfigured");
    add_class(source_button, "admin-button");
    add_class(destination_button, "admin-button");
    add_class(storage_role_button, "admin-button");
    add_class(ignored_button, "admin-button");
    add_class(unconfigured_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(role_grid), source_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(role_grid), destination_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(role_grid), storage_role_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(role_grid), ignored_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(role_grid), unconfigured_button, TRUE, TRUE, 0);

    GtkWidget* smart_button = gtk_button_new_with_label("Run SMART Diagnostics");
    add_class(smart_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(selection_panel), smart_button, FALSE, FALSE, 0);

    GtkWidget* label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(selection_panel), label_box, FALSE, FALSE, 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(label_entry), "Left USB3, Rear USB-C, SSD dock...");
    GtkWidget* label_button = gtk_button_new_with_label("Apply Label");
    add_class(label_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(label_box), label_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(label_box), label_button, FALSE, FALSE, 0);

    GtkWidget* current_role_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(selection_panel), current_role_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(current_role_box), make_status_card("SOURCE ONLY", "Read-only imaging source"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(current_role_box), make_status_card("DESTINATION ONLY", "Writable restore target"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(current_role_box), make_status_card("IMAGE STORAGE", "Central Lazarus storage root"), TRUE, TRUE, 0);

    GtkWidget* storage_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(storage_page, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(storage_page), 12);
    gtk_stack_add_titled(GTK_STACK(stack), storage_page, "storage", "Storage");
    gtk_box_pack_start(GTK_BOX(storage_page), make_label("Browse only mounted Lazarus storage. Add one or more folders as approved image destinations.", "device-detail"), FALSE, FALSE, 0);
    GtkWidget* storage_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* browse_storage_button = gtk_button_new_with_label("Browse Folders");
    GtkWidget* add_storage_button = gtk_button_new_with_label("Add Storage");
    add_class(browse_storage_button, "admin-button");
    add_class(add_storage_button, "admin-button");
    gtk_entry_set_placeholder_text(GTK_ENTRY(storage_entry), "/path/to/lazarus-images");
    gtk_box_pack_start(GTK_BOX(storage_box), storage_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(storage_box), browse_storage_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(storage_box), add_storage_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(storage_page), storage_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(storage_page), make_scrolled_text_view(storage), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(storage_page), make_label("Assigned image-storage device (set from Devices & Roles)", "panel-title"), FALSE, FALSE, 0);
    gtk_widget_set_size_request(storage_device, -1, 44);
    gtk_box_pack_start(GTK_BOX(storage_page), storage_device, FALSE, FALSE, 0);

    GtkWidget* labels_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(labels_page, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(labels_page), 12);
    gtk_stack_add_titled(GTK_STACK(stack), labels_page, "labels", "Port Labels");
    gtk_box_pack_start(GTK_BOX(labels_page), make_label("Select a detected device, assign the physical bench label, then Save the profile.", "device-detail"), FALSE, FALSE, 0);
    GtkWidget* label_hint_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(port_label_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_label_entry), "Left USB3, Rear USB-C, SSD dock...");
    GtkWidget* apply_label_button = gtk_button_new_with_label("Apply Label To Selected Device");
    add_class(apply_label_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(label_hint_box), port_label_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(label_hint_box), apply_label_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_page), label_hint_box, FALSE, FALSE, 0);

    GtkWidget* label_list = gtk_list_box_new();
    add_class(label_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(label_list), GTK_SELECTION_SINGLE);
    GtkWidget* label_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(label_scroll, "dark-list");
    gtk_widget_set_vexpand(label_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(label_scroll), label_list);
    gtk_box_pack_start(GTK_BOX(labels_page), make_label("Detected devices and assigned labels", "panel-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_page), label_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(labels_page), make_label("Labels are stored against persistent device identities, not /dev/sdX names.", "device-detail"), FALSE, FALSE, 0);

    GtkWidget* install_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    add_class(install_page, "admin-panel");
    gtk_container_set_border_width(GTK_CONTAINER(install_page), 12);
    gtk_stack_add_titled(GTK_STACK(stack), install_page, "install", "Install Lazarus OS");
    gtk_box_pack_start(GTK_BOX(install_page), make_label("This installs the live Lazarus OS image to a selected writable destination disk.", "device-detail"), FALSE, FALSE, 0);

    GtkWidget* install_list = gtk_list_box_new();
    add_class(install_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(install_list), GTK_SELECTION_SINGLE);
    for (const auto& device : state->devices) {
        if (!is_install_target_device(device)) {
            continue;
        }
        GtkWidget* install_row = make_row(device_display_name(device), device.detail + "\n" + device.identity);
        g_object_set_data_full(G_OBJECT(install_row), "profile-identity", g_strdup(device.identity.c_str()), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(install_list), install_row, -1);
    }
    GtkWidget* install_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(install_scroll, "dark-list");
    gtk_widget_set_size_request(install_scroll, -1, 150);
    gtk_container_add(GTK_CONTAINER(install_scroll), install_list);
    gtk_box_pack_start(GTK_BOX(install_page), make_label("Available installation targets", "panel-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(install_page), install_scroll, FALSE, FALSE, 0);

    GtkWidget* install_warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(install_warning, "admin-warning");
    gtk_box_pack_start(GTK_BOX(install_page), install_warning, FALSE, FALSE, 0);
    GtkWidget* install_badge = make_label("READY", "role-badge");
    add_class(install_badge, "role-destination");
    GtkWidget* install_title = make_label("Select a destination disk", "panel-title");
    GtkWidget* install_detail = make_label("Use Devices & Roles to pick the disk first.", "device-detail");
    GtkWidget* install_note = make_label("Installation requires an exact ERASE confirmation and a writable destination.", "device-detail");
    gtk_box_pack_start(GTK_BOX(install_warning), install_badge, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(install_warning), install_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(install_warning), install_detail, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(install_warning), install_note, FALSE, FALSE, 0);

    GtkWidget* install_button = gtk_button_new_with_label("Install To Disk");
    add_class(install_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(install_page), install_button, FALSE, FALSE, 0);

    GtkWidget* overview_button = gtk_button_new_with_label("Return to Overview");
    add_class(overview_button, "admin-button");
    gtk_box_pack_start(GTK_BOX(install_page), overview_button, FALSE, FALSE, 0);

    ProfileEditorState editor;
    editor.app = state;
    editor.dialog = dialog;
    editor.device_list = device_list;
    editor.install_list = install_list;
    editor.storage = storage;
    editor.storage_device = storage_device;
    editor.source = source;
    editor.destination = destination;
    editor.ignored = ignored;
    editor.labels = labels;
    editor.label_list = label_list;
    editor.label_entry = label_entry;
    editor.port_label_entry = port_label_entry;
    editor.storage_entry = storage_entry;
    editor.pending_status = pending_status;
    editor.selected_title = selected_title;
    editor.selected_detail = selected_detail;
    editor.selected_label = selected_label;
    editor.selected_badge = selected_badge;
    editor.selected_note = selected_note;
    editor.install_title = install_title;
    editor.install_detail = install_detail;
    editor.install_badge = install_badge;
    editor.install_note = install_note;
    editor.branding_profile = branding_profile;
    editor.branding_theme = branding_theme;
    editor.branding_product_name = branding_product_name;
    editor.branding_subtitle = branding_subtitle;
    editor.branding_accent = branding_accent;
    editor.branding_background = branding_background;
    editor.branding_surface = branding_surface;
    editor.branding_text = branding_text;
    editor.branding_icon = branding_icon;
    editor.branding_logo = branding_logo;
    editor.branding_report_footer = branding_report_footer;

    g_signal_connect_swapped(branding_profile, "changed", G_CALLBACK(apply_branding_profile), &editor);

    g_signal_connect(device_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow*, gpointer data) {
        refresh_editor_selection(static_cast<ProfileEditorState*>(data));
    }), &editor);
    g_signal_connect(label_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* editor = static_cast<ProfileEditorState*>(data);
        if (row == nullptr) {
            return;
        }
        const char* identity = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "profile-identity"));
        if (identity == nullptr) {
            return;
        }
        GList* children = gtk_container_get_children(GTK_CONTAINER(editor->device_list));
        for (GList* child = children; child != nullptr; child = child->next) {
            GtkWidget* device_row = GTK_WIDGET(child->data);
            const char* device_identity = static_cast<const char*>(g_object_get_data(G_OBJECT(device_row), "profile-identity"));
            if (device_identity != nullptr && identity == std::string(device_identity)) {
                gtk_list_box_select_row(GTK_LIST_BOX(editor->device_list), GTK_LIST_BOX_ROW(device_row));
                break;
            }
        }
        g_list_free(children);
    }), &editor);
    g_signal_connect(install_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* editor = static_cast<ProfileEditorState*>(data);
        const char* identity = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "profile-identity"));
        auto device = identity == nullptr ? std::nullopt : find_device_by_identity(editor->app, identity);
        if (!device) {
            set_label_text(editor->install_title, "Select a destination disk");
            set_label_text(editor->install_detail, "Choose a writable destination device before attempting installation.");
            set_label_text(editor->install_badge, "READY");
            set_label_text(editor->install_note, "The install action only accepts a writable destination that is not the running system disk.");
            return;
        }
        set_label_text(editor->install_title, device_display_name(*device));
        set_label_text(editor->install_detail, device_technical_detail(*device));
        set_label_text(editor->install_badge, role_badge_text(*device));
        set_label_text(editor->install_note, "This disk will be partitioned and erased only after the exact ERASE confirmation.");
    }), &editor);
    g_signal_connect_swapped(add_storage_button, "clicked", G_CALLBACK(add_editor_storage), &editor);
    g_signal_connect_swapped(browse_storage_button, "clicked", G_CALLBACK(browse_editor_storage), &editor);
    g_signal_connect_swapped(source_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        assign_editor_role(e, EditorRole::SourceOnly);
    }), &editor);
    g_signal_connect_swapped(destination_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        assign_editor_role(e, EditorRole::DestinationOnly);
    }), &editor);
    g_signal_connect_swapped(storage_role_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        assign_editor_role(e, EditorRole::ImageStorage);
    }), &editor);
    g_signal_connect_swapped(ignored_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        assign_editor_role(e, EditorRole::Ignored);
    }), &editor);
    g_signal_connect_swapped(unconfigured_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        assign_editor_role(e, EditorRole::Unknown);
    }), &editor);
    g_signal_connect_swapped(smart_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        show_smart_diagnostics(e->app, e);
    }), &editor);
    g_signal_connect_swapped(label_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        apply_editor_label(e, e->label_entry);
    }), &editor);
    g_signal_connect_swapped(apply_label_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        apply_editor_label(e, e->port_label_entry);
    }), &editor);
    g_signal_connect_swapped(label_entry, "activate", G_CALLBACK(+[](ProfileEditorState* e) {
        apply_editor_label(e, e->label_entry);
    }), &editor);
    g_signal_connect_swapped(port_label_entry, "activate", G_CALLBACK(+[](ProfileEditorState* e) {
        apply_editor_label(e, e->port_label_entry);
    }), &editor);
    g_signal_connect_swapped(install_button, "clicked", G_CALLBACK(+[](ProfileEditorState* e) {
        install_to_disk(e->app, e);
    }), &editor);
    g_signal_connect_swapped(overview_button, "clicked", G_CALLBACK(+[](GtkWidget* stack_widget) {
        gtk_stack_set_visible_child_name(GTK_STACK(stack_widget), "overview");
    }), stack);

    gtk_stack_set_visible_child_name(GTK_STACK(stack), "devices");
    refresh_label_list(&editor);
    refresh_editor_selection(&editor);
    if (GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(device_list), 0)) {
        gtk_list_box_select_row(GTK_LIST_BOX(device_list), first);
    }
    if (GtkListBoxRow* first_install = gtk_list_box_get_row_at_index(GTK_LIST_BOX(install_list), 0)) {
        gtk_list_box_select_row(GTK_LIST_BOX(install_list), first_install);
    }

    gtk_widget_show_all(dialog);
    // GTK may expand a non-resizable dialog to satisfy a child's natural
    // size. Resize after realization so the Admin Center remains inside the
    // live appliance viewport.
    gtk_window_resize(GTK_WINDOW(dialog), dialog_width, dialog_height);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const auto request = "{\"command\":\"save_profile\",\"name\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(name))) +
                             ",\"branding_theme\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_theme))) +
                             ",\"branding_product_name\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_product_name))) +
                             ",\"branding_subtitle\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_subtitle))) +
                             ",\"branding_accent\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_accent))) +
                             ",\"branding_background\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_background))) +
                             ",\"branding_surface\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_surface))) +
                             ",\"branding_text\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_text))) +
                             ",\"branding_icon\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_icon))) +
                             ",\"branding_logo\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_logo))) +
                             ",\"branding_report_footer\":" + quote_json(gtk_entry_get_text(GTK_ENTRY(editor.branding_report_footer))) +
                             ",\"image_storage_device\":" + quote_json(trim(text_view_text(storage_device))) +
                             ",\"image_storage_text\":" + quote_json(text_view_text(storage)) +
                             ",\"source_text\":" + quote_json(text_view_text(source)) +
                             ",\"destination_text\":" + quote_json(text_view_text(destination)) +
                             ",\"ignored_text\":" + quote_json(text_view_text(ignored)) +
                             ",\"labels_text\":" + quote_json(text_view_text(labels)) + "}";
        std::string response;
        std::string error;
        if (service_request(state, request, response, error) && service_response_ok(response)) {
            load_profile_from_service(state, error);
            refresh_devices(state);
            refresh_backups(state);
            set_status(state, "Profile saved and reloaded.");
        } else {
            if (error.empty()) {
                error = extract_json_string(response, "error");
            }
            show_message(GTK_WINDOW(state->window), GTK_MESSAGE_ERROR, "Profile was not saved: " + error);
        }
    }
    gtk_widget_destroy(dialog);
}

GtkWidget* make_operation_page(AppState* state) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 18);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    state->operation_title = make_label("Operation", "app-title");
    state->operation_detail = make_label("", "device-detail");
    gtk_box_pack_start(GTK_BOX(header), state->operation_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), state->operation_detail, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

    GtkWidget* progress_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    add_class(progress_panel, "surface");
    gtk_container_set_border_width(GTK_CONTAINER(progress_panel), 12);
    state->operation_stage = make_label("Waiting for operation.", "panel-title");
    state->operation_bytes = make_label("", "device-detail");
    gtk_box_pack_start(GTK_BOX(progress_panel), state->operation_stage, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_panel), state->operation_bytes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_panel), state->progress_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), progress_panel, FALSE, FALSE, 0);

    state->operation_events = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(state->operation_events), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(state->operation_events), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->operation_events), GTK_WRAP_WORD_CHAR);
    add_class(state->operation_events, "dark-list");
    GtkWidget* event_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(event_scroll, "dark-list");
    gtk_container_add(GTK_CONTAINER(event_scroll), state->operation_events);
    gtk_box_pack_start(GTK_BOX(page), make_section("Event Stream", event_scroll), TRUE, TRUE, 0);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->operation_done_button = gtk_button_new_with_label("Return Home");
    gtk_widget_set_sensitive(state->operation_done_button, FALSE);
    add_class(state->operation_done_button, "admin-button");
    gtk_box_pack_end(GTK_BOX(controls), state->operation_done_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), controls, FALSE, FALSE, 0);
    g_signal_connect_swapped(state->operation_done_button, "clicked", G_CALLBACK(+[](AppState* s) {
        gtk_stack_set_visible_child_name(GTK_STACK(s->main_stack), "home");
    }), state);

    return page;
}

void build_home_shell(AppState* state) {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(state->window), root);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    add_class(header, "app-header");
    gtk_container_set_border_width(GTK_CONTAINER(header), 12);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    state->main_logo = make_logo(88, state->profile.branding_logo);
    gtk_box_pack_start(GTK_BOX(brand), state->main_logo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), brand, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(header), make_status_card("BENCH", "Lazarus Appliance"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), make_status_card("IMAGE STORAGE", "Configured"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), make_status_card("SERVICE", "Running"), FALSE, FALSE, 0);
    GtkWidget* edit_profile_button = gtk_button_new_with_label("Admin");
    add_class(edit_profile_button, "admin-button");
    gtk_widget_set_tooltip_text(edit_profile_button, "Edit bench profile and port roles");
    gtk_box_pack_start(GTK_BOX(header), edit_profile_button, FALSE, FALSE, 0);

    state->main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(state->main_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_box_pack_start(GTK_BOX(root), state->main_stack, TRUE, TRUE, 0);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(content), 14);
    gtk_stack_add_named(GTK_STACK(state->main_stack), content, "home");

    state->storage_label = make_label("");
    state->profile.summary_text = "Lazarus UI is starting. Backend refresh will begin after the window opens.";
    refresh_bench_summary(state);
    add_class(state->storage_label, "device-detail");
    gtk_box_pack_start(GTK_BOX(content), state->storage_label, FALSE, FALSE, 0);

    GtkWidget* actions = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(actions), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(actions), 12);
    GtkWidget* backup_button = make_action_card("CREATE BACKUP", "Image the selected source device to configured storage.", "image", true);
    GtkWidget* verify_button = make_action_card("VERIFY BACKUP", "Reopen and verify the selected Lazarus image.", "shield");
    GtkWidget* restore_button = make_action_card("RESTORE BACKUP", "Restore the selected backup to a destination device.", "restore");
    GtkWidget* diagnostics_button = make_action_card("RUN DIAGS", "Read SMART health data and produce a printable report.", "diagnostics");
    state->backup_action = backup_button;
    state->verify_action = verify_button;
    state->restore_action = restore_button;
    state->diagnostics_action = diagnostics_button;
    gtk_grid_attach(GTK_GRID(actions), backup_button, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), verify_button, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), restore_button, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(actions), diagnostics_button, 3, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(content), actions, FALSE, FALSE, 0);

    GtkWidget* smart_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(smart_bar), make_label("Device tools", "panel-title"), FALSE, FALSE, 0);
    GtkWidget* refresh_button = gtk_button_new_with_label("Refresh Devices");
    add_class(refresh_button, "admin-button");
    gtk_widget_set_tooltip_text(refresh_button, "Rescan connected drives and reload bench assignments");
    gtk_box_pack_end(GTK_BOX(smart_bar), refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), smart_bar, FALSE, FALSE, 0);

    GtkWidget* panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(panes), 350);
    gtk_box_pack_start(GTK_BOX(content), panes, TRUE, TRUE, 0);

    state->device_list = gtk_list_box_new();
    add_class(state->device_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->device_list), GTK_SELECTION_SINGLE);
    GtkWidget* device_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(device_scroll, "dark-list");
    gtk_container_add(GTK_CONTAINER(device_scroll), state->device_list);
    gtk_paned_pack1(GTK_PANED(panes), make_section("Devices", device_scroll), TRUE, FALSE);

    GtkWidget* backup_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    state->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search_entry), "Search ticket, customer, technician, purpose, or path");
    gtk_box_pack_start(GTK_BOX(backup_box), state->search_entry, FALSE, FALSE, 0);
    state->backup_list = gtk_list_box_new();
    add_class(state->backup_list, "dark-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->backup_list), GTK_SELECTION_SINGLE);
    GtkWidget* backup_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    add_class(backup_scroll, "dark-list");
    gtk_container_add(GTK_CONTAINER(backup_scroll), state->backup_list);
    gtk_box_pack_start(GTK_BOX(backup_box), backup_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(panes), make_section("Backups", backup_box), TRUE, FALSE);

    state->progress_bar = gtk_progress_bar_new();
    gtk_stack_add_named(GTK_STACK(state->main_stack), make_operation_page(state), "operation");
    gtk_stack_set_visible_child_name(GTK_STACK(state->main_stack), "home");
    state->status_label = make_label("Ready.");
    GtkWidget* bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    add_class(bottom_bar, "bottom-bar");
    gtk_box_pack_start(GTK_BOX(bottom_bar), state->status_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(bottom_bar), make_label("Bench Mode: Protected"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), bottom_bar, FALSE, FALSE, 0);

    g_signal_connect_swapped(backup_button, "clicked", G_CALLBACK(create_backup), state);
    g_signal_connect_swapped(verify_button, "clicked", G_CALLBACK(verify_selected_backup), state);
    g_signal_connect_swapped(restore_button, "clicked", G_CALLBACK(restore_selected_backup), state);
    g_signal_connect_swapped(diagnostics_button, "clicked", G_CALLBACK(+[](AppState* s) {
        const auto device = selected_device(s);
        if (!device) {
            show_message(GTK_WINDOW(s->window), GTK_MESSAGE_WARNING, "Select a device before running diagnostics.");
            return;
        }
        show_smart_report(s, *device);
    }), state);
    g_signal_connect_swapped(edit_profile_button, "clicked", G_CALLBACK(edit_profile), state);
    g_signal_connect_swapped(refresh_button, "clicked", G_CALLBACK(+[](AppState* s) {
        set_status(s, "Refreshing devices and backups from Lazarus service...");
        refresh_devices(s);
        refresh_backups(s);
        set_status(s, "Devices and backups refreshed.");
    }), state);
    g_signal_connect_swapped(state->search_entry, "changed", G_CALLBACK(refresh_backups), state);
    g_signal_connect(state->device_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow*, gpointer data) {
        auto* state = static_cast<AppState*>(data);
        refresh_device_card_expansion(state);
        refresh_action_availability(state);
    }), state);
    g_signal_connect(state->backup_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow*, gpointer data) {
        refresh_action_availability(static_cast<AppState*>(data));
    }), state);

    // GTK4 applications retain their window through presentation. The legacy
    // GTK3 show-all path can complete without acquiring an application hold.
    gtk_window_present(GTK_WINDOW(state->window));

    g_idle_add(
        +[](gpointer data) -> gboolean {
            auto* s = static_cast<AppState*>(data);
            set_status(s, "Loading devices and backups from Lazarus service...");
            refresh_devices(s);
            refresh_backups(s);
            set_status(s, "Ready.");
            return G_SOURCE_REMOVE;
        },
        state);
}

void activate(GtkApplication* app, gpointer user_data) {
    auto* state = static_cast<AppState*>(user_data);
    std::fprintf(stderr, "lazarus-gui: GTK4 activate\n");
    install_theme();
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "Arcology Lazarus");
    add_class(state->window, "lazarus-window");
    if (std::getenv("LAZARUS_KIOSK_FULLSCREEN") != nullptr) {
        int width = 1100;
        int height = 720;
        gtk_window_set_default_size(GTK_WINDOW(state->window), width, height);
        gtk_window_set_decorated(GTK_WINDOW(state->window), FALSE);
        gtk_window_move(GTK_WINDOW(state->window), 0, 0);
    } else {
        gtk_window_set_default_size(GTK_WINDOW(state->window), 1100, 720);
    }

    install_lazarus_pointer(state->window);

    // Load branding before constructing the shell so the initial icon set and
    // logo use the configured workplace theme instead of flashing defaults.
    std::string startup_profile_error;
    load_profile_from_service(state, startup_profile_error);
    build_home_shell(state);
    std::fprintf(stderr, "lazarus-gui: GTK4 window presented\n");
}

}  // namespace

int main(int argc, char** argv) {
    AppState state;
    state.bench_path = argc >= 2 ? argv[1] : "lazarus/examples/bench-alpha.profile";

    GtkApplication* app = gtk_application_new("org.arcology.lazarus", G_APPLICATION_DEFAULT_FLAGS);
    // Lazarus is a kiosk appliance. Retain the process before entering the
    // application lifecycle so X11/session-manager behavior cannot make a
    // cleanly-started appliance exit immediately.
    g_application_hold(G_APPLICATION(app));
    g_signal_connect(app, "activate", G_CALLBACK(activate), &state);
    std::fprintf(stderr, "lazarus-gui: entering GTK4 application loop\n");
    const int gtk_argc = 1;
    const int status = g_application_run(G_APPLICATION(app), gtk_argc, argv);
    g_object_unref(app);
    return status;
}
