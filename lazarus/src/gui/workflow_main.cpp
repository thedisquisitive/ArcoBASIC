#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "widgets.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

void gui_trace(const char* message) {
    if (FILE* stream = std::fopen("/var/log/arcology-lazarus/gui-trace.log", "a")) {
        std::fprintf(stream, "%s\n", message);
        std::fclose(stream);
    }
}

struct Device {
    std::string path;
    std::string name;
    std::string detail;
    std::string identity;
    bool system = false;
    std::string label;
    std::string model;
    std::string role;
    std::uint64_t size_bytes = 0;
    std::string transport;
    std::uint64_t link_speed_mbps = 0;
    std::string serial_ending;
    std::string disconnect_state;
    std::string disconnect_message;
    std::string physical_path;
    std::string port_identity;
    std::string filesystem;
    std::string mountpoint;
};

struct Port {
    std::string identity;
    std::string label;
    std::string role;
    bool online = false;
    bool system = false;
    std::string device_path;
    std::string model;
    std::uint64_t size_bytes = 0;
    std::string transport;
    std::string disk_identity;
};

struct Backup {
    std::string directory;
    std::string title;
    std::string status;
    std::string source_selector;
    std::string imaging_mode;
    std::string compression;
    std::string ticket;
    std::string customer;
    std::string technician;
    std::string purpose;
    std::string created_date;
};

struct TicketRecord {
    std::string ticket;
    std::string customer;
    std::string technician;
    std::string purpose;
    std::string created_date;
    std::string status;
    std::uint64_t backup_count = 0;
    std::string image_directory;
    std::string report_path;
    std::string report_kind;
    std::string latest_timestamp;
    std::string latest_technician;
    std::string latest_activity;
};

struct ActivityRecord {
    std::string timestamp;
    std::string technician;
    std::string verb;
    std::string ticket;
    std::string customer;
};

// One row per image storage location (the primary, id "primary", plus each configured extra).
struct StorageLocationRow {
    std::string id;
    std::string name;
    std::string type;
    bool is_default = false;
    bool online = false;
    std::string path;
    std::string detail;
};

struct App {
    GtkWidget* window = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* backup_list = nullptr;
    GtkWidget* network_status_label = nullptr;
    GtkWidget* network_status_indicator = nullptr;
    std::vector<Device> devices;
    std::vector<Port> ports;
    std::vector<Backup> backups;
    std::vector<StorageLocationRow> storage_locations;
    // Set just before navigating to the local/network storage sub-pages to switch them from
    // "replace the primary location" (default) into "add another location" mode.
    bool adding_extra_storage_location = false;
    std::vector<TicketRecord> tickets;
    std::vector<ActivityRecord> recent_activity;
    std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> drive_analysis_cache;
    std::string selected_device;
    std::string selected_backup;
    std::string bench_name;
    std::string source_text;
    std::string destination_text;
    std::string removable_text;
    std::string storage_text;
    std::string image_storage_device;
    std::string image_storage_volume;
    std::string image_storage_port_text;
    std::string storage_mount_source;
    std::string nas_storage_protocol;
    std::string nas_storage_server;
    std::string nas_storage_share;
    std::string nas_storage_username;
    std::string nas_storage_domain;
    std::string labels_text;
    std::string ignored_text;
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
    std::string admin_token;
    std::string device_generation;
    std::string storage_error;
    std::string page_after_refresh;
    std::string network_address;
    bool service_running = false;
    bool bench_protected = false;
    bool storage_online = false;
    bool network_online = false;
    std::uint64_t source_port_count = 0;
    std::uint64_t destination_port_count = 0;
    unsigned int active_operations = 0;
    bool operational_refresh_pending = false;
};

struct SelectionBinding {
    App* app;
    GtkWidget* button;
};

struct InstallBinding {
    App* app;
    GtkWidget* button;
    GtkWidget* confirmation;
    GtkWidget* status;
    GtkWidget* progress;
};

struct InstallJob {
    InstallBinding* binding = nullptr;
    std::string selector;
    std::string response;
    bool transport_ok = false;
    std::atomic_bool done{false};
    guint pulse_source = 0;
};

struct OperationUi {
    App* app = nullptr;
    GtkWidget* button = nullptr;
    GtkWidget* panel = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* result = nullptr;
    GtkWidget* print = nullptr;
    GtkWidget* safety = nullptr;
};

struct ServiceJob {
    OperationUi* ui = nullptr;
    std::string request;
    std::string success_message;
    std::string result_kind;
    std::string final_response;
    std::string transport_error;
    bool transport_ok = false;
    std::function<void(bool, const std::string&)> on_complete;
};

struct NavigationRequest {
    App* app = nullptr;
    std::string target;
};

struct PortLabelEntry {
    std::string identity;
    GtkWidget* input = nullptr;
};

struct PortLabelsBinding {
    App* app = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* save = nullptr;
    std::vector<PortLabelEntry> entries;
    std::unordered_set<std::string> replaced_identities;
};

void set_status(App* app, const std::string& value);
void enter_admin(App* app);

std::string socket_path() {
    const char* configured = std::getenv("LAZARUS_SERVICE_SOCKET");
    return configured != nullptr && *configured != '\0' ? configured : "/run/arcology-lazarus/service.sock";
}

bool request(const std::string& json, std::string& response) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    const timeval timeout{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path().c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) { ::close(fd); return false; }
    const std::string line = json + "\n";
    if (::send(fd, line.data(), line.size(), 0) < 0) { ::close(fd); return false; }
    char buffer[4096];
    for (;;) {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        response.append(buffer, static_cast<std::size_t>(count));
        if (response.find('\n') != std::string::npos) break;
    }
    ::close(fd);
    const auto newline = response.find('\n');
    if (newline != std::string::npos) response.resize(newline);
    return !response.empty();
}

bool request_complete(const std::string& json, std::string& response) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path().c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) { ::close(fd); return false; }
    const std::string line = json + "\n";
    if (::send(fd, line.data(), line.size(), 0) < 0) { ::close(fd); return false; }
    std::string buffered;
    char buffer[4096];
    for (;;) {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        buffered.append(buffer, static_cast<std::size_t>(count));
        if (buffered.size() > 1024 * 1024) {
            ::close(fd);
            return false;
        }
        std::size_t newline = std::string::npos;
        while ((newline = buffered.find('\n')) != std::string::npos) {
            auto response_line = buffered.substr(0, newline);
            buffered.erase(0, newline + 1);
            if (response_line.find("\"type\":\"final\"") != std::string::npos) {
                response = std::move(response_line);
                ::close(fd);
                return true;
            }
        }
    }
    ::close(fd);
    return false;
}

bool request_stream(const std::string& json, const std::function<void(const std::string&)>& on_progress,
                    std::string& final_response, std::string& error) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { error = "Could not create service socket."; return false; }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path().c_str(), sizeof(address.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "Could not connect to Lazarus service."; ::close(fd); return false;
    }
    const std::string request_line = json + "\n";
    if (::send(fd, request_line.data(), request_line.size(), 0) < 0) {
        error = "Could not send request to Lazarus service."; ::close(fd); return false;
    }
    std::string buffered;
    char chunk[4096];
    while (true) {
        const ssize_t count = ::recv(fd, chunk, sizeof(chunk), 0);
        if (count < 0) { error = "Could not read Lazarus service response."; ::close(fd); return false; }
        if (count == 0) break;
        buffered.append(chunk, static_cast<std::size_t>(count));
        if (buffered.size() > 1024 * 1024) {
            error = "The Lazarus service returned an overlong response line.";
            ::close(fd);
            return false;
        }
        std::size_t newline = std::string::npos;
        while ((newline = buffered.find('\n')) != std::string::npos) {
            std::string line = buffered.substr(0, newline);
            buffered.erase(0, newline + 1);
            if (line.find("\"type\":\"progress\"") != std::string::npos) on_progress(line);
            else if (line.find("\"type\":\"final\"") != std::string::npos) {
                final_response = std::move(line);
                ::close(fd);
                return true;
            }
        }
    }
    ::close(fd);
    if (final_response.empty()) { error = "Service closed without a final result."; return false; }
    return true;
}

std::string json_string(const std::string& text, const char* key) {
    const std::string marker = std::string{"\""} + key + "\":\"";
    const auto start = text.find(marker);
    if (start == std::string::npos) return {};
    std::string result;
    bool escaped = false;
    for (std::size_t index = start + marker.size(); index < text.size(); ++index) {
        const char value = text[index];
        if (escaped) {
            switch (value) {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                default: result += value; break;
            }
            escaped = false;
        }
        else if (value == '\\') escaped = true;
        else if (value == '"') break;
        else result += value;
    }
    return result;
}

std::uint64_t json_u64(const std::string& text, const char* key) {
    const std::string marker = std::string{"\""} + key + "\":";
    const auto start = text.find(marker);
    if (start == std::string::npos) return 0;
    const auto first = text.find_first_of("0123456789", start + marker.size());
    if (first == std::string::npos) return 0;
    const auto last = text.find_first_not_of("0123456789", first);
    try { return std::stoull(text.substr(first, last - first)); } catch (...) { return 0; }
}

bool json_bool(const std::string& text, const char* key) {
    const std::string marker = std::string{"\""} + key + "\":";
    const auto start = text.find(marker);
    return start != std::string::npos && text.compare(start + marker.size(), 4, "true") == 0;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> result; std::string item; std::istringstream input(text);
    while (std::getline(input, item, separator)) result.push_back(item);
    return result;
}

std::string quote_json(const std::string& value) {
    std::string result{"\""};
    static constexpr char digits[] = "0123456789abcdef";
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20) {
                    result += "\\u00";
                    result += digits[(character >> 4) & 0x0f];
                    result += digits[character & 0x0f];
                } else {
                    result += static_cast<char>(character);
                }
                break;
        }
    }
    return result + "\"";
}

std::string hex_encode_text(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        result.push_back(digits[character >> 4]);
        result.push_back(digits[character & 0x0f]);
    }
    return result;
}

std::string hex_decode_text(const std::string& value) {
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    if (value.size() % 2 != 0) return {};
    std::string result(value.size() / 2, '\0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = nibble(value[index * 2]);
        const int low = nibble(value[index * 2 + 1]);
        if (high < 0 || low < 0) return {};
        result[index] = static_cast<char>((high << 4) | low);
    }
    return result;
}

std::vector<std::string> nonempty_lines(const std::string& text) {
    std::vector<std::string> result;
    for (auto line : split(text, '\n')) {
        const auto first = line.find_first_not_of(" \t\r");
        const auto last = line.find_last_not_of(" \t\r");
        if (first != std::string::npos) result.push_back(line.substr(first, last - first + 1));
    }
    return result;
}

std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::pair<std::string, std::string>> parse_port_labels(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> labels;
    for (const auto& line : nonempty_lines(text)) {
        const auto separator = line.find('|');
        if (separator == std::string::npos) continue;
        const auto identity = trimmed(line.substr(0, separator));
        const auto friendly = trimmed(line.substr(separator + 1));
        if (!identity.empty() && !friendly.empty()) labels.emplace_back(identity, friendly);
    }
    return labels;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string safe_path_component(const std::string& value) {
    std::string result;
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character) || character == '-' || character == '_') result += static_cast<char>(character);
        else if (character == ' ' || character == '.' || character == '/') result += '_';
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "job" : result;
}

std::string image_output_directory(const std::string& root, const std::string& ticket, const std::string& customer) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H%M%S", &local);
    return (std::filesystem::path(root) / safe_path_component(ticket) /
            safe_path_component(customer) / timestamp).string();
}

void clear_children(GtkWidget* widget) {
    while (GtkWidget* child = gtk_widget_get_first_child(widget)) {
        if (GTK_IS_LIST_BOX(widget)) gtk_list_box_remove(GTK_LIST_BOX(widget), child);
        else gtk_widget_unparent(child);
    }
}

GtkWidget* label(const std::string& value, const char* css = nullptr) {
    return lazarus::gui::make_label(value, css);
}

void set_result_text(GtkWidget* view, const std::string& text) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text.c_str(), -1);
}

std::string human_bytes(std::uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buffer;
}

std::string human_duration(std::uint64_t total_seconds) {
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds % 3600) / 60;
    const auto seconds = total_seconds % 60;
    char buffer[32]{};
    if (hours > 0) std::snprintf(buffer, sizeof(buffer), "%lluh%02llum", (unsigned long long)hours, (unsigned long long)minutes);
    else if (minutes > 0) std::snprintf(buffer, sizeof(buffer), "%llum%02llus", (unsigned long long)minutes, (unsigned long long)seconds);
    else std::snprintf(buffer, sizeof(buffer), "%llus", (unsigned long long)seconds);
    return buffer;
}

std::string smart_attribute(const std::string& response, const char* key, const char* suffix = "") {
    const auto start = response.find(std::string{"\""} + key + "\":");
    if (start == std::string::npos) return "not reported";
    const auto end = response.find('}', start);
    const auto object = response.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
    if (!json_bool(object, "present")) return "not reported";
    return std::to_string(json_u64(object, "value")) + suffix;
}

OperationUi* add_operation_ui(App* app, GtkWidget* page, const char* button_text,
                              bool produces_printable_report = false) {
    auto* ui = new OperationUi;
    ui->app = app;
    ui->status = label("Preparing operation...", "operation-status");
    ui->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ui->progress), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), "Waiting");
    ui->result = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ui->result), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ui->result), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(ui->result), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui->result), GTK_WRAP_WORD_CHAR);
    GtkWidget* result_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(result_scroll, -1, 220);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(result_scroll), ui->result);
    ui->button = gtk_button_new_with_label(button_text);
    lazarus::gui::add_class(ui->button, "primary-command");
    gtk_widget_set_halign(ui->button, GTK_ALIGN_END);
    gtk_widget_set_size_request(ui->button, 240, -1);
    ui->print = gtk_button_new_with_label("Print Report");
    lazarus::gui::add_class(ui->print, "secondary-command");
    gtk_widget_set_sensitive(ui->print, FALSE);
    gtk_widget_set_visible(ui->print, produces_printable_report);
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    // add_page() lifts direct workflow actions out of the scrolling document and into a fixed
    // footer. Critical Start/Confirm controls must remain reachable on 768-line bench displays.
    g_object_set_data(G_OBJECT(actions), "lazarus-persistent-action", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(actions), ui->print);
    gtk_box_append(GTK_BOX(actions), ui->button);
    gtk_box_append(GTK_BOX(page), actions);
    ui->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(ui->panel, "operation-panel");
    ui->safety = label("NO ACTIVE DISK ACCESS", "disconnect-safe");
    gtk_box_append(GTK_BOX(ui->panel), ui->safety);
    gtk_box_append(GTK_BOX(ui->panel), ui->status);
    gtk_box_append(GTK_BOX(ui->panel), ui->progress);
    gtk_box_append(GTK_BOX(ui->panel), result_scroll);
    gtk_widget_set_visible(ui->panel, FALSE);
    gtk_box_append(GTK_BOX(page), ui->panel);
    g_signal_connect(ui->print, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* operation = static_cast<OperationUi*>(data);
        const char* report_path = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "report-path"));
        if (report_path == nullptr || *report_path == '\0') return;
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        gtk_label_set_text(GTK_LABEL(operation->status), "Queueing report on the default printer...");
        std::string response;
        if (!request("{\"command\":\"print_report\",\"report_path\":" + quote_json(report_path) + "}", response) ||
            response.find("\"ok\":true") == std::string::npos) {
            auto error = json_string(response, "error");
            if (error.empty()) error = "The Lazarus service did not queue the report.";
            gtk_label_set_text(GTK_LABEL(operation->status), error.c_str());
            gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
            return;
        }
        const auto message = json_string(response, "message");
        gtk_label_set_text(GTK_LABEL(operation->status), message.empty() ? "Report queued." : message.c_str());
        gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
    }), ui);
    return ui;
}

struct ProgressDelivery { OperationUi* ui; std::string line; };

void deliver_progress(OperationUi* ui, const std::string& line) {
    const auto operation = json_string(line, "operation");
    const auto bytes_done = json_u64(line, "bytes_done");
    const auto bytes_total = json_u64(line, "bytes_total");
    const auto chunks_done = json_u64(line, "chunks_done");
    const auto chunks_total = json_u64(line, "chunks_total");
    const auto bytes_per_second = json_u64(line, "bytes_per_second");
    const auto eta_seconds = json_u64(line, "eta_seconds");
    std::string stage = operation + ": " + json_string(line, "phase");
    const auto message = json_string(line, "message");
    const auto phase = json_string(line, "phase");
    gtk_label_set_text(GTK_LABEL(ui->safety),
        (phase == "flush" || phase == "finalize") ? "FLUSHING - DO NOT DISCONNECT" : "IN USE - DO NOT DISCONNECT");
    gtk_widget_remove_css_class(ui->safety, "disconnect-safe");
    lazarus::gui::add_class(ui->safety, "disconnect-unsafe");
    if (!message.empty()) stage += " - " + message;
    gtk_label_set_text(GTK_LABEL(ui->status), stage.c_str());

    std::ostringstream detail;
    detail << ((phase == "flush" || phase == "finalize")
                   ? "Disk access: FLUSHING - do not disconnect"
                   : "Disk access: ACTIVE - do not disconnect")
           << "\nOperation: " << (operation.empty() ? "working" : operation)
           << "\nPhase: " << (phase.empty() ? "working" : phase);
    if (!message.empty()) detail << "\nActivity: " << message;
    if (bytes_total > 0) {
        detail << "\nProgress: " << human_bytes(bytes_done) << " / " << human_bytes(bytes_total);
        if (bytes_per_second != 0) detail << " at " << human_bytes(bytes_per_second) << "/s";
        if (eta_seconds != 0) detail << "\nEstimated time remaining: " << human_duration(eta_seconds);
    } else if (chunks_total > 0) {
        detail << "\nProgress: chunk " << chunks_done << " / " << chunks_total;
    }
    set_result_text(ui->result, detail.str());

    if (bytes_total > 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress),
                                      std::min(1.0, static_cast<double>(bytes_done) / static_cast<double>(bytes_total)));
        auto text = human_bytes(bytes_done) + " / " + human_bytes(bytes_total);
        if (bytes_per_second != 0) text += "  " + human_bytes(bytes_per_second) + "/s";
        if (eta_seconds != 0) text += "  ETA " + human_duration(eta_seconds);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), text.c_str());
    } else if (chunks_total > 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress),
                                      std::min(1.0, static_cast<double>(chunks_done) / static_cast<double>(chunks_total)));
        const auto text = "Chunks " + std::to_string(chunks_done) + " / " + std::to_string(chunks_total);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), text.c_str());
    } else {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress));
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), "Working");
    }
}

void begin_service_job(OperationUi* ui, std::string request_json, std::string success_message,
                       std::string result_kind = {},
                       std::function<void(bool, const std::string&)> on_complete = {}) {
    ++ui->app->active_operations;
    gtk_widget_set_visible(ui->panel, TRUE);
    gtk_widget_set_sensitive(ui->button, FALSE);
    gtk_widget_set_sensitive(ui->print, FALSE);
    gtk_label_set_text(GTK_LABEL(ui->status), "Starting now.");
    gtk_label_set_text(GTK_LABEL(ui->safety), "STARTING OPERATION - DO NOT DISCONNECT");
    gtk_widget_remove_css_class(ui->safety, "disconnect-safe");
    lazarus::gui::add_class(ui->safety, "disconnect-unsafe");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui->progress), "Launching in 2 seconds");
    set_result_text(ui->result, "Request acknowledged. Lazarus has not started disk access yet.");
    auto* job = new ServiceJob{ui, std::move(request_json), std::move(success_message), std::move(result_kind),
                               {}, {}, false, std::move(on_complete)};
    g_timeout_add(2000, +[](gpointer data) {
        auto* current = static_cast<ServiceJob*>(data);
        gtk_label_set_text(GTK_LABEL(current->ui->status), "Connecting to Lazarus service...");
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(current->ui->progress));
        std::thread([current] {
            current->transport_ok = request_stream(current->request, [current](const std::string& line) {
                auto* delivery = new ProgressDelivery{current->ui, line};
                g_idle_add(+[](gpointer data) {
                    std::unique_ptr<ProgressDelivery> update(static_cast<ProgressDelivery*>(data));
                    deliver_progress(update->ui, update->line);
                    return G_SOURCE_REMOVE;
                }, delivery);
            }, current->final_response, current->transport_error);
            g_idle_add(+[](gpointer data) {
                std::unique_ptr<ServiceJob> finished(static_cast<ServiceJob*>(data));
                const bool ok = finished->transport_ok && finished->final_response.find("\"ok\":true") != std::string::npos;
                std::string summary;
                if (!finished->transport_ok) summary = finished->transport_error;
                else if (!ok) summary = json_string(finished->final_response, "error");
                if (summary.empty()) summary = ok ? finished->success_message : "The Lazarus service rejected the operation.";
                if (ok && finished->result_kind == "analysis") {
                    auto report = json_string(finished->final_response, "analysis_text");
                    if (report.empty()) report = "Drive analysis completed, but the service returned no readable summary.";
                    set_result_text(finished->ui->result, report);
                } else if (ok && finished->result_kind == "smart") {
                    std::ostringstream report;
                    report << "Health: " << json_string(finished->final_response, "health") << "\n"
                           << "Model: " << json_string(finished->final_response, "model") << "\n"
                           << "Serial: " << json_string(finished->final_response, "serial") << "\n"
                           << "Power-on hours: " << smart_attribute(finished->final_response, "power_on_hours") << "\n"
                           << "Temperature: " << smart_attribute(finished->final_response, "temperature_celsius", " C") << "\n"
                           << "Reallocated sectors: " << smart_attribute(finished->final_response, "reallocated_sectors") << "\n"
                           << "Pending sectors: " << smart_attribute(finished->final_response, "pending_sectors") << "\n"
                           << "Uncorrectable errors: " << smart_attribute(finished->final_response, "uncorrectable_errors");
                    set_result_text(finished->ui->result, report.str());
                } else if (ok && finished->result_kind == "backup") {
                    std::ostringstream report;
                    const bool escalation = json_bool(finished->final_response, "escalation_required");
                    report << (escalation
                                   ? "Raw backup preserved and verified. Specialist escalation is required; do not repair the source drive."
                                   : finished->success_message) << "\n"
                           << "Image: " << json_string(finished->final_response, "output_directory") << "\n"
                           << "Source bytes read: " << human_bytes(json_u64(finished->final_response, "bytes_written")) << "\n"
                           << "Stored bytes: " << human_bytes(json_u64(finished->final_response, "bytes_stored")) << "\n"
                           << "Zero-filled space skipped: " << human_bytes(json_u64(finished->final_response, "zero_bytes_elided")) << "\n"
                           << "Chunks written: " << json_u64(finished->final_response, "chunks_written") << "\n"
                           << "Resumed: " << (json_bool(finished->final_response, "resumed") ? "Yes" : "No") << "\n"
                           << "Full verification: " << (json_bool(finished->final_response, "verified") ? "Passed" : "Not completed") << "\n"
                           << (escalation ? "Escalation report: " : "Report: ") << json_string(finished->final_response, "report_path");
                    set_result_text(finished->ui->result, report.str());
                } else if (ok && finished->result_kind == "verify") {
                    std::ostringstream report;
                    report << finished->success_message << "\n"
                           << "Image: " << json_string(finished->final_response, "image_directory") << "\n"
                           << "Verified bytes: " << human_bytes(json_u64(finished->final_response, "actual_bytes")) << "\n"
                           << "Chunks verified: " << json_u64(finished->final_response, "chunks_verified") << "\n"
                           << "Report: " << json_string(finished->final_response, "report_path");
                    set_result_text(finished->ui->result, report.str());
                } else if (ok && finished->result_kind == "restore") {
                    std::ostringstream report;
                    report << finished->success_message << "\n"
                           << "Image: " << json_string(finished->final_response, "image_directory") << "\n"
                           << "Bytes restored: " << human_bytes(json_u64(finished->final_response, "bytes_written")) << "\n"
                           << "Chunks restored: " << json_u64(finished->final_response, "chunks_written") << "\n"
                           << "Report: " << json_string(finished->final_response, "report_path");
                    set_result_text(finished->ui->result, report.str());
                } else if (ok && finished->result_kind == "driver-job") {
                    std::ostringstream report;
                    report << finished->success_message << "\n"
                           << "Job: " << json_string(finished->final_response, "job_id") << "\n"
                           << "Windows partition: " << json_string(finished->final_response, "target_partition") << "\n"
                           << "Servicing runtime: Arcology Lazarus OS\n"
                           << "Replacement disk: Safe to disconnect";
                    set_result_text(finished->ui->result, report.str());
                } else if (finished->transport_ok && finished->result_kind == "boot-repair-plan") {
                    std::ostringstream report;
                    report << finished->success_message << "\n"
                           << "EFI System Partition: " << (json_bool(finished->final_response, "esp_present") ? "Present" : "Missing") << "\n"
                           << "Windows partition: " << (json_bool(finished->final_response, "windows_partition_present") ? "Present" : "Missing") << "\n"
                           << "bootmgfw.efi: " << (json_bool(finished->final_response, "bootmgfw_present") ? "Present" : "Missing") << "\n"
                           << "EFI/Boot/BOOTX64.EFI: " << (!json_bool(finished->final_response, "fallback_loader_present") ? "Missing" :
                                (json_bool(finished->final_response, "fallback_loader_matches_bootmgfw") ? "Present and current" : "Present but out of date")) << "\n"
                           << "BCD store: " << (!json_bool(finished->final_response, "bcd_present") ? "Missing" :
                                (json_bool(finished->final_response, "bcd_readable") ? "Present and readable" : "Present but unreadable")) << "\n"
                           << "Ready to apply repair: " << (json_bool(finished->final_response, "ready_for_servicing") ? "Yes" : "No");
                    const auto findings_text = json_string(finished->final_response, "findings_text");
                    if (!findings_text.empty()) report << "\n\n" << findings_text;
                    set_result_text(finished->ui->result, report.str());
                } else if (ok && finished->result_kind == "boot-repair-apply") {
                    std::ostringstream report;
                    report << finished->success_message << "\n"
                           << "Job: " << json_string(finished->final_response, "job_id") << "\n"
                           << "EFI System Partition: " << json_string(finished->final_response, "target_partition") << "\n"
                           << "Replacement disk: Safe to disconnect";
                    set_result_text(finished->ui->result, report.str());
                } else {
                    set_result_text(finished->ui->result, summary);
                }
                gtk_label_set_text(GTK_LABEL(finished->ui->status), summary.c_str());
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(finished->ui->progress), ok ? 1.0 : 0.0);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(finished->ui->progress), ok ? "Complete" : "Failed");
                gtk_label_set_text(GTK_LABEL(finished->ui->safety),
                    "OPERATION RELEASED - DEVICE STATE WILL REFRESH AUTOMATICALLY");
                gtk_widget_remove_css_class(finished->ui->safety, "disconnect-unsafe");
                lazarus::gui::add_class(finished->ui->safety, "disconnect-safe");
                gtk_widget_set_sensitive(finished->ui->button, TRUE);
                const auto report_path = json_string(finished->final_response, "report_path");
                if (ok && !report_path.empty() && json_bool(finished->final_response, "report_written")) {
                    g_object_set_data_full(G_OBJECT(finished->ui->print), "report-path", g_strdup(report_path.c_str()), g_free);
                    gtk_widget_set_sensitive(finished->ui->print, TRUE);
                }
                if (finished->ui->app->active_operations > 0) --finished->ui->app->active_operations;
                set_status(finished->ui->app, summary);
                if (finished->on_complete) finished->on_complete(ok, finished->final_response);
                return G_SOURCE_REMOVE;
            }, current);
        }).detach();
        return G_SOURCE_REMOVE;
    }, job);
}

void set_status(App* app, const std::string& value) {
    gtk_label_set_text(GTK_LABEL(app->status), value.c_str());
}

void refresh_operational_pages(App* app);
GtkWidget* make_home(App* app);

void show_page(App* app, const char* name) {
    if (std::strcmp(name, "home") == 0 && !app->admin_token.empty()) {
        std::string response;
        request("{\"command\":\"admin_logout\",\"admin_token\":" + quote_json(app->admin_token) + "}", response);
        app->admin_token.clear();
    }
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), name);
}

GtkWidget* back_button(App* app, const char* target) {
    GtkWidget* button = gtk_button_new_with_label(std::strcmp(target, "home") == 0 ? "<  Home" : "<  Administration");
    lazarus::gui::add_class(button, "secondary-command");
    lazarus::gui::add_class(button, "compact-back");
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(button), "target", const_cast<char*>(target));
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* source, gpointer data) {
        show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(source), "target")));
    }), app);
    return button;
}

GtkWidget* page_shell(App* app, const std::string& eyebrow, const std::string& title, const std::string& detail,
                      const char* back_target = "home") {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 48); gtk_widget_set_margin_end(page, 48);
    gtk_widget_set_margin_top(page, 26); gtk_widget_set_margin_bottom(page, 26);
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    lazarus::gui::add_class(top, "workflow-header");
    gtk_box_append(GTK_BOX(top), back_button(app, back_target));
    GtkWidget* headings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(headings, TRUE);
    gtk_box_append(GTK_BOX(headings), label("ARCOLOGY LAZARUS", "page-brand"));
    gtk_box_append(GTK_BOX(headings), label(eyebrow, "eyebrow"));
    gtk_box_append(GTK_BOX(headings), label(title, "workflow-title"));
    gtk_box_append(GTK_BOX(headings), label(detail, "workflow-detail"));
    gtk_box_append(GTK_BOX(top), headings);
    gtk_box_append(GTK_BOX(page), top);
    return page;
}

void load_data(App* app) {
    std::string response;
    if (!request("{\"command\":\"profile\"}", response)) {
        app->service_running = false;
        set_status(app, "Lazarus service is not available. Reconnecting automatically...");
        return;
    }
    const auto rows = json_string(response, "devices_rows");
    app->devices.clear();
    for (const auto& row : split(rows, '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() < 9) continue;
        Device device;
        device.path = fields[0]; device.name = fields[1]; device.detail = fields[2]; device.identity = fields[3];
        device.system = fields[4] == "1"; device.label = fields[5]; device.model = fields[6]; device.role = fields[8];
        if (fields.size() > 9) device.transport = fields[9];
        if (fields.size() > 10) device.serial_ending = fields[10];
        if (fields.size() > 11) device.disconnect_state = fields[11];
        if (fields.size() > 12) device.disconnect_message = fields[12];
        if (fields.size() > 13) device.physical_path = fields[13];
        if (fields.size() > 14) device.port_identity = fields[14];
        if (fields.size() > 15) device.filesystem = fields[15];
        if (fields.size() > 16) device.mountpoint = fields[16];
        if (fields.size() > 17) {
            try { device.link_speed_mbps = std::stoull(fields[17]); } catch (...) { device.link_speed_mbps = 0; }
        }
        try { device.size_bytes = std::stoull(fields[7]); } catch (...) { device.size_bytes = 0; }
        app->devices.push_back(std::move(device));
    }
    app->ports.clear();
    for (const auto& row : split(json_string(response, "ports_rows"), '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() < 5) continue;
        Port port;
        port.identity = fields[0]; port.label = fields[1]; port.role = fields[2];
        port.online = fields[3] == "1"; port.system = fields[4] == "1";
        if (fields.size() > 5) port.device_path = fields[5];
        if (fields.size() > 6) port.model = fields[6];
        if (fields.size() > 7) { try { port.size_bytes = std::stoull(fields[7]); } catch (...) {} }
        if (fields.size() > 8) port.transport = fields[8];
        if (fields.size() > 9) port.disk_identity = fields[9];
        app->ports.push_back(std::move(port));
    }
    app->backups.clear();
    for (const auto& row : split(json_string(response, "backups_rows"), '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() >= 2) {
            Backup backup;
            backup.directory = fields[0]; backup.title = fields[1];
            if (fields.size() > 3) backup.status = fields[3];
            if (fields.size() > 4) backup.source_selector = fields[4];
            if (fields.size() > 5) backup.imaging_mode = fields[5];
            if (fields.size() > 6) backup.compression = fields[6];
            if (fields.size() > 7) backup.ticket = fields[7];
            if (fields.size() > 8) backup.customer = fields[8];
            if (fields.size() > 9) backup.technician = fields[9];
            if (fields.size() > 10) backup.purpose = fields[10];
            if (fields.size() > 11) backup.created_date = fields[11];
            app->backups.push_back(std::move(backup));
        }
    }
    app->tickets.clear();
    for (const auto& row : split(json_string(response, "tickets_rows"), '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() < 10) continue;
        TicketRecord ticket;
        ticket.ticket = fields[0]; ticket.customer = fields[1]; ticket.technician = fields[2];
        ticket.purpose = fields[3]; ticket.created_date = fields[4]; ticket.status = fields[5];
        try { ticket.backup_count = std::stoull(fields[6]); } catch (...) {}
        ticket.image_directory = fields[7]; ticket.report_path = fields[8]; ticket.report_kind = fields[9];
        if (fields.size() > 10) ticket.latest_timestamp = fields[10];
        if (fields.size() > 11) ticket.latest_technician = fields[11];
        if (fields.size() > 12) ticket.latest_activity = fields[12];
        app->tickets.push_back(std::move(ticket));
    }
    app->recent_activity.clear();
    for (const auto& row : split(json_string(response, "activity_rows"), '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() < 5) continue;
        app->recent_activity.push_back({fields[0], fields[1], fields[2], fields[3], fields[4]});
    }
    app->storage_locations.clear();
    for (const auto& row : split(json_string(response, "storage_locations_rows"), '\n')) {
        const auto fields = split(row, '\t');
        if (fields.size() < 7) continue;
        StorageLocationRow location;
        location.id = fields[0]; location.name = fields[1]; location.type = fields[2];
        location.is_default = fields[3] == "1"; location.online = fields[4] == "1";
        location.path = fields[5]; location.detail = fields[6];
        app->storage_locations.push_back(std::move(location));
    }
    app->bench_name = json_string(response, "name");
    app->source_text = json_string(response, "source_text");
    app->destination_text = json_string(response, "destination_text");
    app->removable_text = json_string(response, "removable_text");
    app->storage_text = json_string(response, "image_storage_text");
    app->image_storage_device = json_string(response, "image_storage_device");
    app->image_storage_volume = json_string(response, "image_storage_volume");
    app->image_storage_port_text = json_string(response, "image_storage_port_text");
    app->storage_mount_source = json_string(response, "storage_mount_source");
    app->nas_storage_protocol = json_string(response, "nas_storage_protocol");
    app->nas_storage_server = json_string(response, "nas_storage_server");
    app->nas_storage_share = json_string(response, "nas_storage_share");
    app->nas_storage_username = json_string(response, "nas_storage_username");
    app->nas_storage_domain = json_string(response, "nas_storage_domain");
    app->labels_text = json_string(response, "labels_text");
    app->ignored_text = json_string(response, "ignored_text");
    app->branding_theme = json_string(response, "branding_theme");
    app->branding_product_name = json_string(response, "branding_product_name");
    app->branding_subtitle = json_string(response, "branding_subtitle");
    app->branding_accent = json_string(response, "branding_accent");
    app->branding_background = json_string(response, "branding_background");
    app->branding_surface = json_string(response, "branding_surface");
    app->branding_text = json_string(response, "branding_text");
    app->branding_icon = json_string(response, "branding_icon");
    app->branding_logo = json_string(response, "branding_logo");
    app->branding_report_footer = json_string(response, "branding_report_footer");
    app->service_running = json_bool(response, "service_running");
    app->bench_protected = json_bool(response, "bench_protected");
    app->storage_online = json_bool(response, "storage_online");
    app->storage_error = json_string(response, "storage_error");
    app->network_online = json_bool(response, "network_online");
    app->network_address = json_string(response, "network_ipv4");
    app->source_port_count = json_u64(response, "source_port_count");
    app->destination_port_count = json_u64(response, "destination_port_count");
    const auto device_generation = json_string(response, "device_generation");
    if (device_generation != app->device_generation) app->drive_analysis_cache.clear();
    app->device_generation = device_generation;
    set_status(app, "Ready. Select a workflow.");
}

std::string profile_save_request(App* app, const std::string& sources, const std::string& destinations,
                                 const std::string& storage, const std::string& labels,
                                 const std::string& product_name = {}, const std::string& bench_name = {}) {
    return "{\"command\":\"save_profile\",\"admin_token\":" + quote_json(app->admin_token) +
        ",\"name\":" + quote_json(bench_name.empty() ? app->bench_name : bench_name) +
        ",\"branding_theme\":" + quote_json(app->branding_theme) +
        ",\"branding_product_name\":" + quote_json(product_name.empty() ? app->branding_product_name : product_name) +
        ",\"branding_subtitle\":" + quote_json(app->branding_subtitle) +
        ",\"branding_accent\":" + quote_json(app->branding_accent) +
        ",\"branding_background\":" + quote_json(app->branding_background) +
        ",\"branding_surface\":" + quote_json(app->branding_surface) +
        ",\"branding_text\":" + quote_json(app->branding_text) +
        ",\"branding_icon\":" + quote_json(app->branding_icon) +
        ",\"branding_logo\":" + quote_json(app->branding_logo) +
        ",\"branding_report_footer\":" + quote_json(app->branding_report_footer) +
        ",\"image_storage_device\":" + quote_json(app->image_storage_device) + ",\"image_storage_text\":" + quote_json(storage) +
        ",\"image_storage_volume\":" + quote_json(app->image_storage_volume) +
        ",\"image_storage_port_text\":" + quote_json(app->image_storage_port_text) +
        ",\"nas_storage_protocol\":" + quote_json(app->nas_storage_protocol) +
        ",\"nas_storage_server\":" + quote_json(app->nas_storage_server) +
        ",\"nas_storage_share\":" + quote_json(app->nas_storage_share) +
        ",\"nas_storage_username\":" + quote_json(app->nas_storage_username) +
        ",\"nas_storage_domain\":" + quote_json(app->nas_storage_domain) +
        ",\"source_text\":" + quote_json(sources) + ",\"destination_text\":" + quote_json(destinations) +
        ",\"removable_text\":" + quote_json(app->removable_text) +
        ",\"ignored_text\":" + quote_json(app->ignored_text) + ",\"labels_text\":" + quote_json(labels) + "}";
}

bool save_profile(App* app, const std::string& sources, const std::string& destinations,
                  const std::string& storage, const std::string& labels,
                  const std::string& product_name = {}, const std::string& bench_name = {}) {
    const std::string request_json = profile_save_request(app, sources, destinations, storage, labels, product_name, bench_name);
    std::string response;
    if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        if (error.find("authentication") != std::string::npos || error.find("expired") != std::string::npos) app->admin_token.clear();
        set_status(app, error.empty() ? "Profile could not be saved. The service returned no successful confirmation." : error);
        return false;
    }
    load_data(app);
    set_status(app, "Bench profile saved and reloaded.");
    g_idle_add(+[](gpointer data) {
        refresh_operational_pages(static_cast<App*>(data));
        return G_SOURCE_REMOVE;
    }, app);
    return true;
}

struct DeviceMountRowBinding {
    App* app = nullptr;
    std::string selector;
    GtkWidget* status_label = nullptr;
    GtkWidget* button = nullptr;
};

// Shared mount/disconnect control used on every device row across the app (device_choice) and
// the Home page drive-status panel. Calls the generic, non-admin-gated mount_device/
// unmount_device service commands directly -- staff handle drives constantly and should not
// need an administrator password for routine mount/disconnect, unlike assigning image storage.
GtkWidget* device_mount_controls(App* app, const Device& device) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(row, 4);
    const bool mounted = !device.mountpoint.empty();
    GtkWidget* status = label(mounted ? ("Mounted at " + device.mountpoint) : "Not mounted", "choice-detail");
    gtk_widget_set_hexpand(status, TRUE);
    gtk_box_append(GTK_BOX(row), status);
    GtkWidget* button = gtk_button_new_with_label(mounted ? "Safely Unmount" : "Mount");
    lazarus::gui::add_class(button, "secondary-command");
    gtk_widget_set_sensitive(button, !device.system);
    gtk_box_append(GTK_BOX(row), button);

    auto* binding = new DeviceMountRowBinding{app, device.path, status, button};
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
        auto* values = static_cast<DeviceMountRowBinding*>(data);
        const bool disconnecting = std::strcmp(gtk_button_get_label(self), "Safely Unmount") == 0;
        gtk_widget_set_sensitive(GTK_WIDGET(self), FALSE);
        gtk_label_set_text(GTK_LABEL(values->status_label), disconnecting ? "Disconnecting..." : "Mounting...");
        const auto command = std::string(disconnecting ? "unmount_device" : "mount_device");
        std::string response;
        const auto request_json = "{\"command\":" + quote_json(command) +
            ",\"selector\":" + quote_json(values->selector) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status_label), error.empty() ? "The operation failed." : error.c_str());
            gtk_widget_set_sensitive(GTK_WIDGET(self), TRUE);
            return;
        }
        if (disconnecting) {
            gtk_label_set_text(GTK_LABEL(values->status_label), "Not mounted");
            gtk_button_set_label(GTK_BUTTON(self), "Mount");
        } else {
            const auto mount_path = json_string(response, "mount_path");
            gtk_label_set_text(GTK_LABEL(values->status_label), ("Mounted at " + mount_path).c_str());
            gtk_button_set_label(GTK_BUTTON(self), "Safely Unmount");
        }
        gtk_widget_set_sensitive(GTK_WIDGET(self), TRUE);
        set_status(values->app, json_string(response, "message"));
        load_data(values->app);
        refresh_operational_pages(values->app);
    }), binding);
    return row;
}

GtkWidget* device_choice(App* app, const Device& device, const char* required_role,
                         bool show_mount_controls = true) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(card, 14); gtk_widget_set_margin_end(card, 14);
    gtk_widget_set_margin_top(card, 12); gtk_widget_set_margin_bottom(card, 12);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), card);
    const bool allowed = device.role == required_role && !device.system;
    gtk_widget_set_sensitive(row, allowed);
    const auto title = !device.label.empty() ? device.label : (!device.model.empty() ? device.model : device.path);
    std::string detail = device.model;
    if (device.size_bytes != 0) detail += " | " + human_bytes(device.size_bytes);
    if (!device.transport.empty()) detail += " | " + device.transport;
    if (device.link_speed_mbps != 0) detail += " " + std::to_string(device.link_speed_mbps) + " Mb/s";
    if (!device.serial_ending.empty()) detail += " | serial ending " + device.serial_ending;
    if (device.transport == "usb" && device.link_speed_mbps != 0 && device.link_speed_mbps <= 480) {
        detail += "\nPerformance warning: this drive negotiated a USB 2.0-speed link. Try another port or cable.";
    }
    if (!device.label.empty()) detail += "\nConnected to: " + device.label;
    else if (!device.physical_path.empty()) detail += "\nPhysical connection: " + device.physical_path;
    detail += "\n" + (device.disconnect_message.empty()
        ? (allowed ? "Available" : "Unavailable for this workflow") : device.disconnect_message);
    gtk_box_append(GTK_BOX(card), label(title, "choice-title"));
    gtk_box_append(GTK_BOX(card), label(detail, "choice-detail"));
    GtkWidget* safety = label(device.disconnect_message.empty() ? "DISCONNECT STATE UNKNOWN" : device.disconnect_message,
                              device.disconnect_state == "safe" ? "disconnect-safe" : "disconnect-unsafe");
    gtk_box_append(GTK_BOX(card), safety);
    if (!device.system && show_mount_controls) {
        gtk_box_append(GTK_BOX(card), device_mount_controls(app, device));
    }
    g_object_set_data_full(G_OBJECT(row), "selector", g_strdup(device.path.c_str()), g_free);
    lazarus::gui::add_class(row, "choice-row");
    return row;
}

GtkWidget* backup_choice(const Backup& backup) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(card, 14); gtk_widget_set_margin_end(card, 14);
    gtk_widget_set_margin_top(card, 12); gtk_widget_set_margin_bottom(card, 12);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), card);
    gtk_box_append(GTK_BOX(card), label(backup.title.empty() ? "Lazarus backup" : backup.title, "choice-title"));
    if (backup.status == "interrupted") {
        gtk_box_append(GTK_BOX(card), label("INTERRUPTED - READY TO RESUME WHEN THE ORIGINAL SOURCE IS CONNECTED", "disconnect-unsafe"));
    }
    gtk_box_append(GTK_BOX(card), label(backup.directory, "choice-detail"));
    g_object_set_data_full(G_OBJECT(row), "backup", g_strdup(backup.directory.c_str()), g_free);
    lazarus::gui::add_class(row, "choice-row");
    return row;
}

bool select_backup_path(GtkWidget* list, const std::string& path) {
    if (path.empty()) return false;
    for (GtkWidget* child = gtk_widget_get_first_child(list); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        const auto* candidate = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "backup"));
        if (candidate != nullptr && path == candidate) {
            gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(child));
            return true;
        }
    }
    return false;
}

GtkWidget* choice_scroll(GtkWidget* choices, int height = 145) {
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroll, -1, height);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), choices);
    return scroll;
}

struct TicketReviewBinding {
    App* app;
    GtkWidget* search;
    GtkWidget* list;
    GtkWidget* detail;
    GtkWidget* resume;
    GtkWidget* recover;
    GtkWidget* verify;
    GtkWidget* restore;
    GtkWidget* print;
    GtkWidget* status;
};

const char* row_text(GtkListBoxRow* row, const char* key) {
    const auto* value = static_cast<const char*>(g_object_get_data(G_OBJECT(row), key));
    return value == nullptr ? "" : value;
}

GtkWidget* make_tickets_page(App* app) {
    GtkWidget* page = page_shell(app, "TICKET REVIEW", "Tickets and Job History",
        "Search completed and interrupted work, review the latest job state, and open the next workflow from one place.");

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* search = gtk_search_entry_new();
    gtk_widget_set_hexpand(search, TRUE);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),
                                          "Search ticket, customer, technician, purpose, or status");
    GtkWidget* refresh = gtk_button_new_with_label("Refresh Tickets");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_box_append(GTK_BOX(toolbar), search);
    gtk_box_append(GTK_BOX(toolbar), refresh);
    gtk_box_append(GTK_BOX(page), toolbar);

    GtkWidget* summary = label(
        app->tickets.empty() ? "No ticket history is available." :
            std::to_string(app->tickets.size()) + (app->tickets.size() == 1 ? " ticket" : " tickets"),
        app->tickets.empty() ? "empty-state" : "instruction");
    gtk_box_append(GTK_BOX(page), summary);
    if (!app->storage_online) {
        gtk_box_append(GTK_BOX(page), label(
            "Image storage is offline. Activity-only tickets are shown, but stored backup metadata and reports may be unavailable.",
            "warning-panel"));
    }

    GtkWidget* columns = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(columns), 14);
    gtk_widget_set_vexpand(columns, TRUE);
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(list, "choice-list");
    for (const auto& ticket : app->tickets) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(card, 12); gtk_widget_set_margin_end(card, 12);
        gtk_widget_set_margin_top(card, 9); gtk_widget_set_margin_bottom(card, 9);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), card);
        const auto title = (ticket.ticket.empty() ? "No ticket" : ticket.ticket) + " | " +
            (ticket.customer.empty() ? "Unknown customer" : ticket.customer);
        gtk_box_append(GTK_BOX(card), label(title, "choice-title"));
        const auto when = !ticket.latest_timestamp.empty() ? ticket.latest_timestamp : ticket.created_date;
        gtk_box_append(GTK_BOX(card), label(ticket.status + (when.empty() ? "" : " | " + when),
            ticket.status == "Escalation required" || ticket.status == "Interrupted"
                ? "disconnect-unsafe" : "choice-detail"));
        const auto search_text = lower_ascii(ticket.ticket + " " + ticket.customer + " " + ticket.technician + " " +
            ticket.purpose + " " + ticket.status + " " + ticket.latest_technician + " " + ticket.latest_activity);
        const auto count = std::to_string(ticket.backup_count);
        const std::vector<std::pair<const char*, std::string>> data{
            {"search", search_text}, {"ticket", ticket.ticket}, {"customer", ticket.customer},
            {"technician", ticket.technician}, {"purpose", ticket.purpose}, {"created", ticket.created_date},
            {"status", ticket.status}, {"backup-count", count}, {"image", ticket.image_directory},
            {"report", ticket.report_path}, {"report-kind", ticket.report_kind},
            {"latest-time", ticket.latest_timestamp}, {"latest-tech", ticket.latest_technician},
            {"latest-activity", ticket.latest_activity},
        };
        for (const auto& [key, value] : data) {
            g_object_set_data_full(G_OBJECT(row), key, g_strdup(value.c_str()), g_free);
        }
        lazarus::gui::add_class(row, "choice-row");
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    GtkWidget* list_scroll = choice_scroll(list, 430);
    gtk_widget_set_size_request(list_scroll, 390, 430);
    gtk_grid_attach(GTK_GRID(columns), list_scroll, 0, 0, 1, 1);

    GtkWidget* review = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(review, "workflow-section");
    gtk_widget_set_hexpand(review, TRUE);
    gtk_box_append(GTK_BOX(review), label("Ticket details", "section-title"));
    GtkWidget* detail = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(detail), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(detail), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(detail), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(detail), TRUE);
    set_result_text(detail, "Select a ticket to review its job history and stored backup.");
    GtkWidget* detail_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(detail_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(detail_scroll), detail);
    gtk_box_append(GTK_BOX(review), detail_scroll);
    GtkWidget* ticket_status = label("No ticket selected.", "section-detail");
    gtk_box_append(GTK_BOX(review), ticket_status);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget* resume = gtk_button_new_with_label("Resume Imaging");
    GtkWidget* recover = gtk_button_new_with_label("Recover Files");
    GtkWidget* verify = gtk_button_new_with_label("Verify Backup");
    GtkWidget* restore = gtk_button_new_with_label("Restore Backup");
    GtkWidget* print = gtk_button_new_with_label("Print Latest Report");
    for (auto* button : {resume, recover, verify, restore, print}) lazarus::gui::add_class(button, "secondary-command");
    gtk_widget_set_visible(resume, FALSE);
    gtk_widget_set_sensitive(resume, FALSE);
    gtk_widget_set_sensitive(recover, FALSE); gtk_widget_set_sensitive(verify, FALSE);
    gtk_widget_set_sensitive(restore, FALSE); gtk_widget_set_sensitive(print, FALSE);
    gtk_box_append(GTK_BOX(actions), resume); gtk_box_append(GTK_BOX(actions), recover); gtk_box_append(GTK_BOX(actions), verify);
    gtk_box_append(GTK_BOX(actions), restore); gtk_box_append(GTK_BOX(actions), print);
    gtk_box_append(GTK_BOX(review), actions);
    gtk_grid_attach(GTK_GRID(columns), review, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), columns);

    auto* binding = new TicketReviewBinding{app, search, list, detail, resume, recover, verify, restore, print, ticket_status};
    g_signal_connect(search, "search-changed", G_CALLBACK(+[](GtkSearchEntry*, gpointer data) {
        auto* values = static_cast<TicketReviewBinding*>(data);
        const auto query = lower_ascii(gtk_editable_get_text(GTK_EDITABLE(values->search)));
        for (GtkWidget* child = gtk_widget_get_first_child(values->list); child != nullptr;
             child = gtk_widget_get_next_sibling(child)) {
            const auto* haystack = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "search"));
            gtk_widget_set_visible(child, query.empty() || (haystack != nullptr && std::string{haystack}.find(query) != std::string::npos));
        }
    }), binding);
    g_signal_connect(list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<TicketReviewBinding*>(data);
        if (row == nullptr) return;
        std::ostringstream text;
        text << "Ticket: " << row_text(row, "ticket") << "\n"
             << "Customer: " << row_text(row, "customer") << "\n"
             << "Status: " << row_text(row, "status") << "\n"
             << "Purpose: " << row_text(row, "purpose") << "\n"
             << "Imaging technician: " << row_text(row, "technician") << "\n"
             << "Created: " << row_text(row, "created") << "\n"
             << "Stored backups: " << row_text(row, "backup-count") << "\n"
             << "Latest activity: " << row_text(row, "latest-activity") << "\n"
             << "Latest activity time: " << row_text(row, "latest-time") << "\n"
             << "Latest activity technician: " << row_text(row, "latest-tech") << "\n"
             << "Image: " << (std::strlen(row_text(row, "image")) == 0 ? "Not currently available" : row_text(row, "image")) << "\n"
             << "Report: " << (std::strlen(row_text(row, "report")) == 0 ? "Not available" : row_text(row, "report-kind"));
        set_result_text(values->detail, text.str());
        const bool has_image = std::strlen(row_text(row, "image")) != 0;
        const bool has_report = std::strlen(row_text(row, "report")) != 0;
        const auto ticket_status = std::string{row_text(row, "status")};
        const bool resume_required = has_image && ticket_status == "Interrupted";
        const bool escalation_required = ticket_status == "Escalation required";
        const bool stored_workflows_available = has_image && !resume_required && !escalation_required;
        gtk_widget_set_visible(values->resume, resume_required);
        gtk_widget_set_sensitive(values->resume, resume_required);
        gtk_widget_set_sensitive(values->recover, stored_workflows_available);
        gtk_widget_set_sensitive(values->verify, stored_workflows_available);
        gtk_widget_set_sensitive(values->restore, stored_workflows_available);
        gtk_widget_set_sensitive(values->print, has_report);
        g_object_set_data_full(G_OBJECT(values->print), "report-path", g_strdup(row_text(row, "report")), g_free);
        gtk_label_set_text(GTK_LABEL(values->status), resume_required
            ? "This image is interrupted. Resume imaging before verification, recovery, or restore."
            : (escalation_required
                ? "This job requires escalation. Review or print the escalation report; do not restore this image."
                : (has_image ? "Stored image workflows are available for this ticket."
                             : "This ticket currently has activity history only.")));
    }), binding);
    g_signal_connect(print, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* values = static_cast<TicketReviewBinding*>(data);
        const auto* report = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "report-path"));
        if (report == nullptr || *report == '\0') return;
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), "Queueing the ticket report...");
        std::string response;
        if (!request("{\"command\":\"print_report\",\"report_path\":" + quote_json(report) + "}", response) ||
            response.find("\"ok\":true") == std::string::npos) {
            auto error = json_string(response, "error");
            if (error.empty()) error = "The report could not be queued.";
            gtk_label_set_text(GTK_LABEL(values->status), error.c_str());
            gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
            return;
        }
        gtk_label_set_text(GTK_LABEL(values->status), "Report queued on the default printer.");
        gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
    }), binding);
    for (const auto& action : std::vector<std::pair<GtkWidget*, const char*>>{
             {resume, "backup"}, {recover, "recover-files"}, {verify, "verify"}, {restore, "restore"}}) {
        g_object_set_data(G_OBJECT(action.first), "page", const_cast<char*>(action.second));
        g_signal_connect(action.first, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
            auto* values = static_cast<TicketReviewBinding*>(data);
            auto* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(values->list));
            if (row == nullptr) return;
            values->app->selected_backup = row_text(row, "image");
            const auto* target = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "page"));
            refresh_operational_pages(values->app);
            values->app->page_after_refresh = target == nullptr ? "tickets" : target;
        }), binding);
    }
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        set_status(state, "Refreshing ticket history...");
        load_data(state);
        refresh_operational_pages(state);
    }), app);
    if (!app->tickets.empty()) {
        if (auto* first = GTK_LIST_BOX_ROW(gtk_widget_get_first_child(list))) gtk_list_box_select_row(GTK_LIST_BOX(list), first);
    }
    return page;
}

// Every job-record-producing page (Backup, Restore, Verify) signs the operator in against the
// admin-configured technician roster instead of taking a free-typed name, so one employee cannot
// log work under a coworker's name. An empty or unavailable roster blocks the workflow until an
// administrator configures it; silently falling back to a typed name would bypass accountability.
struct TechnicianField {
    GtkWidget* roster_dropdown = nullptr;
    GtkWidget* roster_pin = nullptr;
};

TechnicianField build_technician_field(GtkWidget* section) {
    std::string response;
    const bool roster_loaded = request("{\"command\":\"technicians_list\"}", response) &&
        response.find("\"ok\":true") != std::string::npos;
    auto names = split(json_string(response, "names_text"), '\n');
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& name) { return name.empty(); }), names.end());
    TechnicianField field;
    gtk_box_append(GTK_BOX(section), label("Technician", "form-label"));
    GtkStringList* model = gtk_string_list_new(nullptr);
    for (const auto& name : names) gtk_string_list_append(model, name.c_str());
    GtkWidget* dropdown = gtk_drop_down_new(G_LIST_MODEL(model), nullptr);
    gtk_widget_set_hexpand(dropdown, TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), GTK_INVALID_LIST_POSITION);
    gtk_box_append(GTK_BOX(section), dropdown);
    gtk_box_append(GTK_BOX(section), label("Technician PIN", "form-label"));
    GtkWidget* pin = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pin), "4-digit PIN");
    gtk_entry_set_visibility(GTK_ENTRY(pin), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(pin), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_max_length(GTK_ENTRY(pin), 4);
    gtk_box_append(GTK_BOX(section), pin);
    if (names.empty()) {
        gtk_widget_set_sensitive(dropdown, FALSE);
        gtk_widget_set_sensitive(pin, FALSE);
        const auto message = roster_loaded
            ? "No technicians are configured. An administrator must add a technician and PIN before starting a job."
            : "The technician roster could not be loaded. Return Home and retry before starting a job.";
        GtkWidget* warning = label(message, "warning-text");
        gtk_label_set_wrap(GTK_LABEL(warning), TRUE);
        gtk_box_append(GTK_BOX(section), warning);
    }
    field.roster_dropdown = dropdown;
    field.roster_pin = pin;
    return field;
}

bool technician_field_complete(const TechnicianField& field) {
    const std::string pin = gtk_editable_get_text(GTK_EDITABLE(field.roster_pin));
    return gtk_drop_down_get_selected(GTK_DROP_DOWN(field.roster_dropdown)) != GTK_INVALID_LIST_POSITION &&
           pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](unsigned char character) {
               return std::isdigit(character) != 0;
           });
}

std::string technician_field_json(const TechnicianField& field) {
    const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(field.roster_dropdown));
    auto* model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(field.roster_dropdown)));
    const std::string name = selected == GTK_INVALID_LIST_POSITION ? "" : gtk_string_list_get_string(model, selected);
    const std::string pin = gtk_editable_get_text(GTK_EDITABLE(field.roster_pin));
    return ",\"technician_name\":" + quote_json(name) + ",\"technician_pin\":" + quote_json(pin);
}

template <typename Binding, void (*Update)(Binding*)>
void connect_technician_field(const TechnicianField& field, Binding* binding) {
    g_signal_connect(field.roster_dropdown, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        Update(static_cast<Binding*>(data));
    }), binding);
    g_signal_connect(field.roster_pin, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        Update(static_cast<Binding*>(data));
    }), binding);
}

struct BackupBinding {
    App* app;
    GtkWidget* page;
    OperationUi* operation;
    GtkWidget* ticket;
    GtkWidget* customer;
    TechnicianField technician;
    GtkWidget* purpose;
    GtkWidget* preset;
    GtkWidget* storage;
    GtkWidget* imaging_mode;
    GtkWidget* scan_drives;
    GtkWidget* analysis_panel;
    GtkWidget* analysis_status;
    GtkWidget* analysis_spinner;
    GtkWidget* analysis_summary;
    GtkWidget* analysis_details;
    GtkWidget* analysis_expander;
    std::string selector;
    std::string selected_identity;
    std::uint64_t selected_size = 0;
    std::uint64_t analysis_sequence = 0;
    bool analysis_running = false;
    bool analysis_recommends_rescue = false;
};

struct BackupAnalysisJob {
    BackupBinding* binding = nullptr;
    std::string selector;
    std::string expected_identity;
    std::uint64_t expected_size = 0;
    std::uint64_t sequence = 0;
    std::string response;
    bool transport_ok = false;
};

std::string port_prompt(const App* app, const std::string& role, const std::string& fallback) {
    std::vector<std::string> names;
    for (const auto& port : app->ports) {
        if (port.role != role) continue;
        names.push_back(port.label.empty() ? "configured " + fallback : port.label);
    }
    if (names.empty()) return fallback;
    std::ostringstream result;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) result << (index + 1 == names.size() ? " or " : ", ");
        result << names[index];
    }
    return result.str();
}

GtkWidget* workflow_readiness(const App* app) {
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    lazarus::gui::add_class(panel, app->storage_online ? "success-panel" : "warning-panel");
    gtk_box_append(GTK_BOX(panel), label(app->storage_online ? "Image storage ready" : "Image storage unavailable",
                                         app->storage_online ? "success-text" : "warning-text"));
    gtk_box_append(GTK_BOX(panel), label(app->storage_online
        ? "Lazarus mounted the configured storage automatically. No storage setup is required for this job."
        : (app->storage_error.empty() ? "Connect the configured image-storage disk to its assigned port."
                                      : app->storage_error),
        "section-detail"));
    return panel;
}

void update_backup_button(BackupBinding* values) {
    const auto complete = [values](GtkWidget* entry) {
        return *gtk_editable_get_text(GTK_EDITABLE(entry)) != '\0';
    };
    const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->storage));
    gtk_button_set_label(GTK_BUTTON(values->operation->button), values->analysis_running
        ? "Analyzing Source..."
        : (values->analysis_recommends_rescue ? "Create Rescue Backup" : "Create Backup"));
    gtk_widget_set_sensitive(values->operation->button,
        !values->analysis_running && values->app->storage_online && !values->selector.empty() &&
        complete(values->ticket) && complete(values->customer) &&
        technician_field_complete(values->technician) && complete(values->purpose) && selected != GTK_INVALID_LIST_POSITION);
}

std::string backup_analysis_cache_key(const std::string& identity, std::uint64_t size) {
    return identity + "|" + std::to_string(size);
}

void style_backup_analysis_panel(BackupBinding* values, bool warning) {
    gtk_widget_remove_css_class(values->analysis_panel, "success-panel");
    gtk_widget_remove_css_class(values->analysis_panel, "warning-panel");
    lazarus::gui::add_class(values->analysis_panel, warning ? "warning-panel" : "success-panel");
}

void render_backup_analysis(BackupBinding* values, bool transport_ok, const std::string& response) {
    values->analysis_running = false;
    gtk_spinner_stop(GTK_SPINNER(values->analysis_spinner));
    gtk_widget_set_visible(values->analysis_spinner, FALSE);
    gtk_widget_set_sensitive(values->scan_drives, TRUE);

    const bool ok = transport_ok && response.find("\"ok\":true") != std::string::npos;
    if (!ok) {
        auto error = json_string(response, "error");
        if (error.empty()) error = "Lazarus could not complete the read-only source analysis.";
        values->analysis_recommends_rescue = true;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(values->imaging_mode), 1);
        gtk_widget_set_sensitive(values->imaging_mode, FALSE);
        style_backup_analysis_panel(values, true);
        gtk_label_set_text(GTK_LABEL(values->analysis_status), "Analysis unavailable - backup remains available");
        set_result_text(values->analysis_details, error);
        gtk_label_set_text(GTK_LABEL(values->analysis_summary),
            (error + "\nLazarus will use Rescue imaging and will not repair or write to the customer drive.").c_str());
        gtk_widget_set_visible(values->analysis_expander, TRUE);
        update_backup_button(values);
        return;
    }

    const auto os_name = json_string(response, "detected_os_name");
    const auto os_version = json_string(response, "detected_os_version");
    const auto confidence_value = json_string(response, "os_detection_confidence");
    const auto confidence = confidence_value == "confirmed" ? "confirmed" :
        (confidence_value == "filesystem-evidence" ? "installation files found" :
         (confidence_value == "layout-only" ? "layout candidate" :
          (confidence_value == "multiple" ? "multiple installations" : "not confirmed")));
    const auto boot_mode = json_string(response, "boot_mode");
    const auto encryption = json_string(response, "encryption_type");
    const auto health = json_string(response, "health");
    const bool layout_valid = json_bool(response, "layout_valid");
    values->analysis_recommends_rescue = !layout_valid || health == "failed";
    const bool analysis_warning = values->analysis_recommends_rescue || health.empty() || health == "unknown";
    if (values->analysis_recommends_rescue) gtk_drop_down_set_selected(GTK_DROP_DOWN(values->imaging_mode), 1);
    const guint preset = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->preset));
    gtk_widget_set_sensitive(values->imaging_mode, preset == 4 && !values->analysis_recommends_rescue);

    std::ostringstream summary;
    summary << "Detected OS: " << (os_name.empty() ? "Not confirmed" : os_name);
    if (!os_version.empty() && os_name.find(os_version) == std::string::npos) summary << " " << os_version;
    summary << " (" << confidence << ")\n"
            << "Boot: " << (boot_mode.empty() ? "Unknown" : boot_mode)
            << "  |  Encryption: " << (encryption.empty() ? "None detected" : encryption) << "\n"
            << "SMART health: " << (health.empty() ? "Not reported" : health) << "  |  Layout: "
            << (layout_valid ? "Parsed successfully" : "Damaged or unsupported - raw capture still available");
    if (values->analysis_recommends_rescue)
        summary << "\nRescue imaging selected. Lazarus will preserve evidence without attempting repair.";

    style_backup_analysis_panel(values, analysis_warning);
    gtk_label_set_text(GTK_LABEL(values->analysis_status), values->analysis_recommends_rescue
        ? "Source warnings found - rescue backup is ready"
        : (health.empty() || health == "unknown"
            ? "Source analysis complete - SMART health was not reported"
            : "Source analysis complete - backup is ready"));
    gtk_label_set_text(GTK_LABEL(values->analysis_summary), summary.str().c_str());
    set_result_text(values->analysis_details, json_string(response, "analysis_text"));
    gtk_widget_set_visible(values->analysis_expander, TRUE);
    update_backup_button(values);
}

void start_backup_analysis(BackupBinding* values) {
    ++values->analysis_sequence;
    values->analysis_running = !values->selector.empty();
    values->analysis_recommends_rescue = false;
    gtk_expander_set_expanded(GTK_EXPANDER(values->analysis_expander), FALSE);
    gtk_widget_set_visible(values->analysis_expander, FALSE);
    gtk_widget_remove_css_class(values->analysis_panel, "success-panel");
    gtk_widget_remove_css_class(values->analysis_panel, "warning-panel");

    if (values->selector.empty()) {
        gtk_spinner_stop(GTK_SPINNER(values->analysis_spinner));
        gtk_widget_set_visible(values->analysis_spinner, FALSE);
        gtk_label_set_text(GTK_LABEL(values->analysis_status), "Select a source drive to analyze it");
        gtk_label_set_text(GTK_LABEL(values->analysis_summary),
            "Lazarus will detect the OS, boot layout, encryption, and drive health before imaging.");
        update_backup_button(values);
        return;
    }

    values->selected_identity.clear();
    values->selected_size = 0;
    for (const auto& device : values->app->devices) {
        if (device.path != values->selector) continue;
        values->selected_identity = device.identity;
        values->selected_size = device.size_bytes;
        break;
    }
    const auto cache_key = backup_analysis_cache_key(values->selected_identity, values->selected_size);
    const auto cached = values->app->drive_analysis_cache.find(cache_key);
    if (cached != values->app->drive_analysis_cache.end() &&
        std::chrono::steady_clock::now() - cached->second.first < std::chrono::minutes(5)) {
        render_backup_analysis(values, true, cached->second.second);
        return;
    }
    if (cached != values->app->drive_analysis_cache.end()) values->app->drive_analysis_cache.erase(cached);

    gtk_spinner_start(GTK_SPINNER(values->analysis_spinner));
    gtk_widget_set_visible(values->analysis_spinner, TRUE);
    gtk_widget_set_sensitive(values->scan_drives, FALSE);
    gtk_label_set_text(GTK_LABEL(values->analysis_status), "Analyzing selected source read-only...");
    gtk_label_set_text(GTK_LABEL(values->analysis_summary),
        "You can enter the ticket information while Lazarus checks this drive. Imaging will unlock when analysis releases it.");
    update_backup_button(values);

    ++values->app->active_operations;
    auto* job = new BackupAnalysisJob{
        values, values->selector, values->selected_identity, values->selected_size,
        values->analysis_sequence, {}, false};
    std::thread([job]() {
        job->transport_ok = request_complete(
            "{\"command\":\"drive_analysis\",\"selector\":" + quote_json(job->selector) + "}", job->response);
        g_idle_add(+[](gpointer data) -> gboolean {
            std::unique_ptr<BackupAnalysisJob> completed(static_cast<BackupAnalysisJob*>(data));
            auto* values = completed->binding;
            if (values->app->active_operations > 0) --values->app->active_operations;
            if (values->page == nullptr || completed->sequence != values->analysis_sequence ||
                completed->selector != values->selector) return G_SOURCE_REMOVE;

            const auto returned_identity = json_string(completed->response, "identity");
            const auto returned_size = json_u64(completed->response, "size_bytes");
            const bool response_ok = completed->transport_ok &&
                completed->response.find("\"ok\":true") != std::string::npos;
            if (response_ok &&
                ((!completed->expected_identity.empty() && returned_identity != completed->expected_identity) ||
                 (completed->expected_size != 0 && returned_size != completed->expected_size))) {
                values->analysis_running = false;
                values->analysis_recommends_rescue = true;
                gtk_spinner_stop(GTK_SPINNER(values->analysis_spinner));
                gtk_widget_set_visible(values->analysis_spinner, FALSE);
                gtk_widget_set_sensitive(values->scan_drives, TRUE);
                style_backup_analysis_panel(values, true);
                gtk_label_set_text(GTK_LABEL(values->analysis_status), "Source identity changed");
                gtk_label_set_text(GTK_LABEL(values->analysis_summary),
                    "The selected connection no longer contains the same drive. Scan for drives and select the source again.");
                values->selector.clear();
                update_backup_button(values);
                return G_SOURCE_REMOVE;
            }
            if (response_ok) {
                values->app->drive_analysis_cache[backup_analysis_cache_key(
                    completed->expected_identity, completed->expected_size)] = {
                        std::chrono::steady_clock::now(), completed->response};
            }
            render_backup_analysis(values, completed->transport_ok, completed->response);
            return G_SOURCE_REMOVE;
        }, job);
    }).detach();
}

struct ResumeBinding {
    OperationUi* operation;
    std::string image_directory;
    std::string source_selector;
};

GtkWidget* make_backup_page(App* app) {
    GtkWidget* page = page_shell(app, "CREATE BACKUP", "Create a Verified Image",
                                 "Select the customer drive and record the required job information.");
    std::vector<const Backup*> interrupted;
    for (const auto& backup : app->backups) {
        if (backup.status == "interrupted") interrupted.push_back(&backup);
    }
    if (!interrupted.empty()) {
        GtkWidget* interrupted_list = gtk_list_box_new();
        lazarus::gui::add_class(interrupted_list, "choice-list");
        for (const auto* backup : interrupted) gtk_list_box_append(GTK_LIST_BOX(interrupted_list), backup_choice(*backup));
        GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        lazarus::gui::add_class(section, "warning-panel");
        gtk_box_append(GTK_BOX(section), label("Interrupted jobs are available", "warning-text"));
        gtk_box_append(GTK_BOX(section), label(
            "Reconnect the exact original source drive. Lazarus validates its persistent identity and existing chunk hashes before appending data.",
            "section-detail"));
        gtk_box_append(GTK_BOX(section), choice_scroll(interrupted_list, 118));
        gtk_box_append(GTK_BOX(page), section);
        auto* resume_operation = add_operation_ui(app, page, "Resume Selected Job", true);
        gtk_widget_set_sensitive(resume_operation->button, FALSE);
        auto* resume = new ResumeBinding{resume_operation, {}, {}};
        g_signal_connect(interrupted_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
            auto* values = static_cast<ResumeBinding*>(data);
            const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "backup"));
            values->image_directory = path == nullptr ? "" : path;
            gtk_widget_set_sensitive(values->operation->button, !values->image_directory.empty());
        }), resume);
        g_signal_connect(resume_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* values = static_cast<ResumeBinding*>(data);
            if (values->image_directory.empty()) return;
            begin_service_job(values->operation,
                "{\"command\":\"resume_image\",\"image_directory\":" + quote_json(values->image_directory) + "}",
                "Interrupted image resumed, completed, and finalized.", "backup");
        }), resume);
        select_backup_path(interrupted_list, app->selected_backup);
    }
    gtk_box_append(GTK_BOX(page), workflow_readiness(app));
    GtkWidget* choices = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(choices), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(choices, "choice-list");
    std::size_t sources = 0;
    GtkListBoxRow* sole_source = nullptr;
    for (const auto& device : app->devices) if (!device.system &&
             (device.role == "source-only" || device.role == "unknown" || device.role.empty())) {
        GtkWidget* row = device_choice(app, device, device.role.c_str(), false);
        gtk_list_box_append(GTK_LIST_BOX(choices), row);
        sole_source = GTK_LIST_BOX_ROW(row);
        ++sources;
    }
    GtkWidget* source_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(source_section, "workflow-section");
    GtkWidget* source_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* source_title = label("1. Source drive", "section-title");
    gtk_widget_set_hexpand(source_title, TRUE);
    gtk_box_append(GTK_BOX(source_heading), source_title);
    GtkWidget* scan_drives = gtk_button_new_with_label("Scan for Drives");
    lazarus::gui::add_class(scan_drives, "secondary-command");
    gtk_box_append(GTK_BOX(source_heading), scan_drives);
    gtk_box_append(GTK_BOX(source_section), source_heading);
    gtk_box_append(GTK_BOX(source_section), label(
        "Configured source ports remain read-only. A drive on an unassigned connection can also be selected and is opened read-only for this backup.",
        "section-detail"));
    g_signal_connect(scan_drives, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        set_status(state, "Scanning connected drives...");
        load_data(state);
        refresh_operational_pages(state);
    }), app);
    if (sources == 0) {
        const auto message = app->source_port_count == 0
            ? "No customer drive is detected. Plug one in, wait a moment, then press Scan for Drives."
            : "Waiting for a customer drive. Connect it to " + port_prompt(app, "source-only", "a SOURCE port") +
                ", or use an unassigned connection, then press Scan for Drives.";
        gtk_box_append(GTK_BOX(source_section), label(message, "empty-state"));
    }
    else gtk_box_append(GTK_BOX(source_section), choice_scroll(choices, 104));

    GtkWidget* analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    lazarus::gui::add_class(analysis_panel, "analysis-panel");
    GtkWidget* analysis_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* analysis_status = label("Select a source drive to analyze it", "choice-title");
    gtk_widget_set_hexpand(analysis_status, TRUE);
    GtkWidget* analysis_spinner = gtk_spinner_new();
    gtk_widget_set_visible(analysis_spinner, FALSE);
    gtk_box_append(GTK_BOX(analysis_heading), analysis_status);
    gtk_box_append(GTK_BOX(analysis_heading), analysis_spinner);
    gtk_box_append(GTK_BOX(analysis_panel), analysis_heading);
    GtkWidget* analysis_summary = label(
        "Lazarus will detect the OS, boot layout, encryption, and drive health before imaging.",
        "section-detail");
    gtk_box_append(GTK_BOX(analysis_panel), analysis_summary);
    GtkWidget* analysis_details = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(analysis_details), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(analysis_details), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(analysis_details), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(analysis_details), GTK_WRAP_WORD_CHAR);
    GtkWidget* analysis_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(analysis_scroll, -1, 190);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(analysis_scroll), analysis_details);
    GtkWidget* analysis_expander = gtk_expander_new("Full drive analysis");
    gtk_expander_set_child(GTK_EXPANDER(analysis_expander), analysis_scroll);
    gtk_widget_set_visible(analysis_expander, FALSE);
    gtk_box_append(GTK_BOX(analysis_panel), analysis_expander);
    gtk_box_append(GTK_BOX(source_section), analysis_panel);
    gtk_box_append(GTK_BOX(page), source_section);

    GtkWidget* form = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(form), 8); gtk_grid_set_column_spacing(GTK_GRID(form), 12);
    auto add_entry = [&](const char* title, int row) {
        GtkWidget* entry = gtk_entry_new();
        gtk_widget_set_hexpand(entry, TRUE);
        gtk_grid_attach(GTK_GRID(form), label(title, "form-label"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(form), entry, 1, row, 1, 1);
        return entry;
    };
    GtkWidget* ticket = add_entry("Ticket number", 0);
    GtkWidget* customer = add_entry("Customer name", 1);
    GtkWidget* purpose = add_entry("Purpose", 2);
    const char* preset_names[] = {"Backup Before Repair", "SSD Upgrade", "Data Recovery", "Hardware Migration", "Custom", nullptr};
    GtkWidget* preset = gtk_drop_down_new_from_strings(preset_names);
    gtk_grid_attach(GTK_GRID(form), label("Job preset", "form-label"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(form), preset, 1, 3, 1, 1);
    GtkStringList* storage_model = gtk_string_list_new(nullptr);
    for (const auto& root : nonempty_lines(app->storage_text)) gtk_string_list_append(storage_model, root.c_str());
    // gtk_drop_down_new() consumes the caller's model reference.
    GtkWidget* storage = gtk_drop_down_new(G_LIST_MODEL(storage_model), nullptr);
    gtk_widget_set_hexpand(storage, TRUE);
    // The model's own strings stay the literal storage paths (that is what gets submitted below
    // unchanged); this factory only changes how each row is *displayed*, looking up a friendly
    // name/default badge from app->storage_locations by matching path.
    GtkListItemFactory* storage_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(storage_factory, "setup", G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item) {
        GtkWidget* item_label = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(item_label), 0);
        gtk_list_item_set_child(item, item_label);
    }), nullptr);
    g_signal_connect(storage_factory, "bind", G_CALLBACK(+[](GtkListItemFactory*, GtkListItem* item, gpointer data) {
        auto* state = static_cast<App*>(data);
        auto* string_object = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
        const std::string path = gtk_string_object_get_string(string_object);
        std::string display = path;
        for (const auto& location : state->storage_locations) {
            if (location.path != path) continue;
            display = location.name.empty() ? path : location.name;
            if (location.is_default) display += " (Default)";
            if (!location.detail.empty()) display += "\n" + location.detail;
            break;
        }
        gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), display.c_str());
    }), app);
    gtk_drop_down_set_factory(GTK_DROP_DOWN(storage), storage_factory);
    g_object_unref(storage_factory);
    gtk_grid_attach(GTK_GRID(form), label("Image storage", "form-label"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(form), storage, 1, 4, 1, 1);
    const char* imaging_modes[] = {"Standard raw imaging", "Rescue imaging for unstable drives", nullptr};
    GtkWidget* imaging_mode = gtk_drop_down_new_from_strings(imaging_modes);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(imaging_mode), 1);
    gtk_grid_attach(GTK_GRID(form), label("Imaging mode", "form-label"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(form), imaging_mode, 1, 5, 1, 1);
    gtk_editable_set_text(GTK_EDITABLE(purpose), "Backup Before Repair");
    GtkWidget* job_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(job_section, "workflow-section");
    gtk_box_append(GTK_BOX(job_section), label("2. Job information", "section-title"));
    gtk_box_append(GTK_BOX(job_section), form);
    TechnicianField technician = build_technician_field(job_section);
    gtk_box_append(GTK_BOX(page), job_section);
    auto* operation = add_operation_ui(app, page, "Create Backup", true);
    auto* binding = new BackupBinding;
    binding->app = app;
    binding->page = page;
    binding->operation = operation;
    binding->ticket = ticket;
    binding->customer = customer;
    binding->technician = technician;
    binding->purpose = purpose;
    binding->preset = preset;
    binding->storage = storage;
    binding->imaging_mode = imaging_mode;
    binding->scan_drives = scan_drives;
    binding->analysis_panel = analysis_panel;
    binding->analysis_status = analysis_status;
    binding->analysis_spinner = analysis_spinner;
    binding->analysis_summary = analysis_summary;
    binding->analysis_details = analysis_details;
    binding->analysis_expander = analysis_expander;
    g_object_add_weak_pointer(G_OBJECT(page), reinterpret_cast<gpointer*>(&binding->page));
    gtk_widget_set_sensitive(operation->button, FALSE);
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<BackupBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->selector = selector == nullptr ? "" : selector;
        start_backup_analysis(values);
    }), binding);
    GtkWidget* required_entries[] = {ticket, customer, purpose};
    for (auto* entry : required_entries) {
        g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            update_backup_button(static_cast<BackupBinding*>(data));
        }), binding);
    }
    connect_technician_field<BackupBinding, update_backup_button>(binding->technician, binding);
    g_signal_connect(storage, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        update_backup_button(static_cast<BackupBinding*>(data));
    }), binding);
    g_signal_connect(preset, "notify::selected", G_CALLBACK((+[](GObject*, GParamSpec*, gpointer data) {
        auto* values = static_cast<BackupBinding*>(data);
        static constexpr const char* purposes[] = {"Backup Before Repair", "SSD Upgrade", "Data Recovery", "Hardware Migration", ""};
        const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->preset));
        if (selected < 4) gtk_editable_set_text(GTK_EDITABLE(values->purpose), purposes[selected]);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(values->imaging_mode),
                                   values->analysis_recommends_rescue || selected == 0 || selected == 2 ? 1 : 0);
        gtk_widget_set_sensitive(values->imaging_mode, selected == 4 && !values->analysis_recommends_rescue);
        update_backup_button(values);
    })), binding);
    {
        const guint storage_count = g_list_model_get_n_items(G_LIST_MODEL(storage_model));
        guint default_index = GTK_INVALID_LIST_POSITION;
        for (guint i = 0; i < storage_count; ++i) {
            const std::string path = gtk_string_list_get_string(storage_model, i);
            const bool is_default = std::any_of(app->storage_locations.begin(), app->storage_locations.end(),
                [&](const StorageLocationRow& location) { return location.path == path && location.is_default; });
            if (is_default) { default_index = i; break; }
        }
        if (default_index == GTK_INVALID_LIST_POSITION && storage_count > 0) default_index = 0;
        if (default_index != GTK_INVALID_LIST_POSITION) gtk_drop_down_set_selected(GTK_DROP_DOWN(storage), default_index);
    }
    if (sources == 1 && sole_source != nullptr) gtk_list_box_select_row(GTK_LIST_BOX(choices), sole_source);
    gtk_widget_set_sensitive(imaging_mode, FALSE);
    g_signal_connect(operation->button, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* values = static_cast<BackupBinding*>(data);
        const std::string ticket = gtk_editable_get_text(GTK_EDITABLE(values->ticket));
        const std::string customer = gtk_editable_get_text(GTK_EDITABLE(values->customer));
        const std::string purpose = gtk_editable_get_text(GTK_EDITABLE(values->purpose));
        const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->storage));
        auto* model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(values->storage)));
        const char* storage = selected == GTK_INVALID_LIST_POSITION ? nullptr : gtk_string_list_get_string(model, selected);
        const bool rescue = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->imaging_mode)) == 1;
        const guint selected_preset = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->preset));
        static constexpr const char* preset_ids[] = {"backup-before-repair", "ssd-upgrade", "data-recovery", "hardware-migration", "custom"};
        const char* preset_id = selected_preset < 5 ? preset_ids[selected_preset] : "custom";
        if (values->selector.empty() || ticket.empty() || customer.empty() || !technician_field_complete(values->technician) ||
            purpose.empty() || storage == nullptr) {
            const std::string message = "Select a source and complete ticket, customer, technician sign-in, purpose, and image storage.";
            gtk_label_set_text(GTK_LABEL(values->operation->status), message.c_str());
            set_result_text(values->operation->result, message);
            return;
        }
        const auto output = image_output_directory(storage, ticket, customer);
        const auto request_json = "{\"command\":\"image_source\",\"selector\":" + quote_json(values->selector) +
            ",\"output_directory\":" + quote_json(output) + ",\"ticket_number\":" + quote_json(ticket) +
            ",\"customer_name\":" + quote_json(customer) + technician_field_json(values->technician) +
            ",\"purpose\":" + quote_json(purpose) + ",\"compression\":\"zstd\",\"imaging_mode\":" +
            quote_json(rescue ? "rescue" : "raw") + ",\"preset\":" + quote_json(preset_id) + "}";
        begin_service_job(values->operation, request_json, "Image creation and verification completed.", "backup");
    })), binding);
    return page;
}

struct VerifyBinding { OperationUi* operation; TechnicianField technician; std::string backup; };

void update_verify_button(VerifyBinding* values) {
    gtk_widget_set_sensitive(values->operation->button,
                             !values->backup.empty() && technician_field_complete(values->technician));
}

GtkWidget* make_verify_page(App* app) {
    GtkWidget* page = page_shell(app, "VERIFY BACKUP", "Verify Recoverability",
                                 "Reopen an existing Lazarus image and verify every stored chunk.");
    GtkWidget* choices = gtk_list_box_new(); lazarus::gui::add_class(choices, "choice-list");
    for (const auto& backup : app->backups) gtk_list_box_append(GTK_LIST_BOX(choices), backup_choice(backup));
    GtkWidget* selection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(selection, "workflow-section");
    gtk_box_append(GTK_BOX(selection), label("Select an image", "section-title"));
    gtk_box_append(GTK_BOX(selection), label("Verification reopens the image and reads every stored chunk without modifying it.", "section-detail"));
    if (app->backups.empty()) gtk_box_append(GTK_BOX(selection), label("No backups are present in configured image storage.", "empty-state"));
    else gtk_box_append(GTK_BOX(selection), choice_scroll(choices, 204));
    gtk_box_append(GTK_BOX(page), selection);
    GtkWidget* technician_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(technician_section, "workflow-section");
    gtk_box_append(GTK_BOX(technician_section), label("Performed by", "section-title"));
    TechnicianField technician = build_technician_field(technician_section);
    gtk_box_append(GTK_BOX(page), technician_section);
    auto* operation = add_operation_ui(app, page, "Verify Selected Backup", true);
    gtk_widget_set_sensitive(operation->button, FALSE);
    auto* binding = new VerifyBinding{operation, technician, {}};
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<VerifyBinding*>(data);
        const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "backup"));
        values->backup = path == nullptr ? "" : path;
        update_verify_button(values);
    }), binding);
    connect_technician_field<VerifyBinding, update_verify_button>(binding->technician, binding);
    select_backup_path(choices, app->selected_backup);
    g_signal_connect(operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<VerifyBinding*>(data);
        if (values->backup.empty() || !technician_field_complete(values->technician)) return;
        begin_service_job(values->operation, "{\"command\":\"verify_image\",\"image_directory\":" +
            quote_json(values->backup) + technician_field_json(values->technician) + "}", "Image verification passed.", "verify");
    }), binding);
    return page;
}

struct RestoreBinding {
    App* app;
    OperationUi* operation;
    GtkWidget* confirmation;
    TechnicianField technician;
    std::string destination;
    std::string backup;
};

void update_restore_button(RestoreBinding* values) {
    const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(values->confirmation));
    gtk_widget_set_sensitive(values->operation->button,
                             values->app->storage_online && !values->destination.empty() &&
                                 !values->backup.empty() && confirmation == "ERASE" &&
                                 technician_field_complete(values->technician));
}

GtkWidget* make_restore_page(App* app) {
    GtkWidget* page = page_shell(app, "RESTORE BACKUP", "Restore an Image",
                                 "Select a completed backup and the replacement disk that Lazarus may erase.");
    gtk_box_append(GTK_BOX(page), workflow_readiness(app));
    GtkWidget* columns = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(columns), TRUE); gtk_grid_set_column_spacing(GTK_GRID(columns), 16);
    GtkWidget* backups = gtk_list_box_new(); GtkWidget* destinations = gtk_list_box_new();
    lazarus::gui::add_class(backups, "choice-list"); lazarus::gui::add_class(destinations, "choice-list");
    GtkListBoxRow* sole_backup = nullptr;
    GtkListBoxRow* sole_destination = nullptr;
    std::size_t backup_count = 0;
    std::size_t destination_count = 0;
    for (const auto& backup : app->backups) {
        if (backup.status == "interrupted") continue;
        GtkWidget* row = backup_choice(backup);
        gtk_list_box_append(GTK_LIST_BOX(backups), row);
        sole_backup = GTK_LIST_BOX_ROW(row);
        ++backup_count;
    }
    for (const auto& device : app->devices) if (!device.system &&
             (device.role == "destination-only" || device.role == "unknown" || device.role.empty())) {
        GtkWidget* row = device_choice(app, device, device.role.c_str());
        gtk_list_box_append(GTK_LIST_BOX(destinations), row);
        sole_destination = GTK_LIST_BOX_ROW(row);
        ++destination_count;
    }
    GtkWidget* backup_column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* destination_column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(backup_column, "workflow-section");
    lazarus::gui::add_class(destination_column, "workflow-section");
    gtk_box_append(GTK_BOX(backup_column), label("1. Backup", "section-title"));
    GtkWidget* destination_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* destination_title = label("2. Replacement disk to erase", "section-title");
    gtk_widget_set_hexpand(destination_title, TRUE);
    GtkWidget* scan_drives = gtk_button_new_with_label("Scan for Drives");
    lazarus::gui::add_class(scan_drives, "secondary-command");
    gtk_box_append(GTK_BOX(destination_heading), destination_title);
    gtk_box_append(GTK_BOX(destination_heading), scan_drives);
    gtk_box_append(GTK_BOX(destination_column), destination_heading);
    g_signal_connect(scan_drives, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        set_status(state, "Scanning connected drives...");
        load_data(state);
        refresh_operational_pages(state);
    }), app);
    gtk_box_append(GTK_BOX(destination_column), label(
        "A configured destination port is preferred. An unassigned non-system disk is also allowed after explicit ERASE confirmation.",
        "section-detail"));
    if (backup_count == 0) gtk_box_append(GTK_BOX(backup_column), label(
        app->storage_online ? "No Lazarus images were found in configured storage." : "Image storage must be connected before backups can be listed.",
        "empty-state"));
    else gtk_box_append(GTK_BOX(backup_column), choice_scroll(backups, 170));
    if (destination_count == 0) gtk_box_append(GTK_BOX(destination_column), label(
        "No replacement drive is detected. Plug one in, then return to this page to rescan drives.",
        "empty-state"));
    else gtk_box_append(GTK_BOX(destination_column), choice_scroll(destinations, 170));
    gtk_grid_attach(GTK_GRID(columns), backup_column, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(columns), destination_column, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), columns);
    GtkWidget* confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Type ERASE to authorize the selected destination");
    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(warning, "warning-panel");
    gtk_box_append(GTK_BOX(warning), label("The selected destination will be erased", "warning-text"));
    gtk_box_append(GTK_BOX(warning), label("Type ERASE only after confirming the model, capacity, and physical destination port.", "choice-detail"));
    gtk_box_append(GTK_BOX(warning), confirmation);
    gtk_box_append(GTK_BOX(page), warning);
    GtkWidget* technician_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(technician_section, "workflow-section");
    gtk_box_append(GTK_BOX(technician_section), label("Performed by", "section-title"));
    TechnicianField technician = build_technician_field(technician_section);
    gtk_box_append(GTK_BOX(page), technician_section);
    auto* operation = add_operation_ui(app, page, "Restore Selected Backup", true);
    gtk_widget_set_sensitive(operation->button, FALSE);
    auto* binding = new RestoreBinding{app, operation, confirmation, technician, {}, {}};
    g_signal_connect(backups, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<RestoreBinding*>(data);
        const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "backup"));
        values->backup = path == nullptr ? "" : path; update_restore_button(values);
    }), binding);
    g_signal_connect(destinations, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<RestoreBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->destination = selector == nullptr ? "" : selector; update_restore_button(values);
    }), binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) { update_restore_button(static_cast<RestoreBinding*>(data)); }), binding);
    connect_technician_field<RestoreBinding, update_restore_button>(binding->technician, binding);
    if (!select_backup_path(backups, app->selected_backup) && backup_count == 1 && sole_backup != nullptr) {
        gtk_list_box_select_row(GTK_LIST_BOX(backups), sole_backup);
    }
    if (destination_count == 1 && sole_destination != nullptr) gtk_list_box_select_row(GTK_LIST_BOX(destinations), sole_destination);
    g_signal_connect(operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<RestoreBinding*>(data);
        if (!technician_field_complete(values->technician)) return;
        const auto request_json = "{\"command\":\"restore_image\",\"image_directory\":" + quote_json(values->backup) +
            ",\"selector\":" + quote_json(values->destination) + ",\"confirmation\":\"ERASE\"" +
            technician_field_json(values->technician) + "}";
        begin_service_job(values->operation, request_json, "Restore completed and the destination was flushed.", "restore");
    }), binding);
    return page;
}

struct DiagnosticsBinding { OperationUi* operation; std::string selector; };

GtkWidget* make_diagnostics_page(App* app) {
    GtkWidget* page = page_shell(app, "DRIVE ANALYSIS", "Analyze a Drive",
                                 "Detect the operating system, boot layout, filesystems, encryption, and drive health without modifying the selected disk.");
    GtkWidget* choices = gtk_list_box_new(); lazarus::gui::add_class(choices, "choice-list");
    std::size_t count = 0;
    for (const auto& device : app->devices) if (!device.system) {
        GtkWidget* row = device_choice(app, device, device.role.c_str());
        gtk_widget_set_sensitive(row, TRUE); gtk_list_box_append(GTK_LIST_BOX(choices), row); ++count;
    }
    GtkWidget* selection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(selection, "workflow-section");
    gtk_box_append(GTK_BOX(selection), label("Select a drive", "section-title"));
    gtk_box_append(GTK_BOX(selection), label(
        "Lazarus uses protected read-only access. It does not repair filesystems, replay journals, or write to the selected drive.",
        "section-detail"));
    if (count == 0) gtk_box_append(GTK_BOX(selection), label("No diagnostic-capable drive is currently connected.", "empty-state"));
    else gtk_box_append(GTK_BOX(selection), choice_scroll(choices, 204));
    gtk_box_append(GTK_BOX(page), selection);
    auto* operation = add_operation_ui(app, page, "Analyze Selected Drive");
    gtk_widget_set_sensitive(operation->button, FALSE);
    auto* binding = new DiagnosticsBinding{operation, {}};
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<DiagnosticsBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->selector = selector == nullptr ? "" : selector;
        gtk_widget_set_sensitive(values->operation->button, !values->selector.empty());
    }), binding);
    g_signal_connect(operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<DiagnosticsBinding*>(data);
        if (values->selector.empty()) return;
        begin_service_job(values->operation, "{\"command\":\"drive_analysis\",\"selector\":" + quote_json(values->selector) + "}",
                          "Drive analysis completed.", "analysis");
    }), binding);
    return page;
}

struct UniversalRestoreBinding {
    App* app = nullptr;
    OperationUi* operation = nullptr;
    GtkWidget* confirmation = nullptr;
    TechnicianField technician;
    std::string backup;
    std::string destination;
};

void update_universal_restore_button(UniversalRestoreBinding* binding) {
    const auto confirmation = std::string(gtk_editable_get_text(GTK_EDITABLE(binding->confirmation)));
    gtk_widget_set_sensitive(binding->operation->button,
                             binding->app->storage_online && !binding->backup.empty() && !binding->destination.empty() &&
                             confirmation == "UNIVERSAL RESTORE" && technician_field_complete(binding->technician));
}

GtkWidget* make_driver_migration_page(App* app) {
    GtkWidget* page = page_shell(
        app, "WINDOWS RECOVERY", "Universal Restore",
        "Restore Windows to replacement hardware and prepare its required boot-storage driver automatically.");

    GtkWidget* choices = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(choices), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(choices), 12);
    GtkWidget* backups = gtk_list_box_new();
    GtkWidget* destinations = gtk_list_box_new();
    lazarus::gui::add_class(backups, "choice-list");
    lazarus::gui::add_class(destinations, "choice-list");
    std::size_t backup_count = 0;
    for (const auto& backup : app->backups) {
        if (backup.status == "interrupted") continue;
        gtk_list_box_append(GTK_LIST_BOX(backups), backup_choice(backup));
        ++backup_count;
    }
    std::size_t destination_count = 0;
    for (const auto& device : app->devices) {
        if (!device.system && (device.role == "destination-only" || device.role == "unknown" || device.role.empty())) {
            gtk_list_box_append(GTK_LIST_BOX(destinations), device_choice(app, device, device.role.c_str()));
            ++destination_count;
        }
    }
    GtkWidget* backup_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget* target_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(backup_section, "workflow-section");
    lazarus::gui::add_class(target_section, "workflow-section");
    gtk_box_append(GTK_BOX(backup_section), label("1. Windows backup", "section-title"));
    gtk_box_append(GTK_BOX(backup_section), backup_count == 0
        ? label("No Lazarus backups are available.", "empty-state") : choice_scroll(backups, 126));
    gtk_box_append(GTK_BOX(target_section), label("2. Replacement disk", "section-title"));
    gtk_box_append(GTK_BOX(target_section), label(
        "Must be installed through the replacement computer's boot-storage controller.", "section-detail"));
    gtk_box_append(GTK_BOX(target_section), destination_count == 0
        ? label("No destination-only or unassigned replacement disk is connected.", "empty-state") : choice_scroll(destinations, 126));
    gtk_grid_attach(GTK_GRID(choices), backup_section, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(choices), target_section, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), choices);

    GtkWidget* policy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    lazarus::gui::add_class(policy, "workflow-section");
    gtk_box_append(GTK_BOX(policy), label("Automatic compatibility preflight", "section-title"));
    gtk_box_append(GTK_BOX(policy), label(
        "Before erasing the destination, Lazarus identifies its PCI storage controller and requires a matching INF/CAT/SYS package in DriverVault. After restore, Lazarus updates offline Windows directly and preserves existing storage drivers as fallback boot paths.",
        "section-detail"));
    gtk_box_append(GTK_BOX(page), policy);

    GtkWidget* confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Type UNIVERSAL RESTORE to erase the replacement disk");
    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(warning, "warning-panel");
    gtk_box_append(GTK_BOX(warning), label("The selected replacement disk will be erased", "warning-text"));
    gtk_box_append(GTK_BOX(warning), confirmation);
    gtk_box_append(GTK_BOX(page), warning);

    GtkWidget* technician_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(technician_section, "workflow-section");
    gtk_box_append(GTK_BOX(technician_section), label("Performed by", "section-title"));
    TechnicianField technician = build_technician_field(technician_section);
    gtk_box_append(GTK_BOX(page), technician_section);

    auto* operation = add_operation_ui(app, page, "Begin Universal Restore");
    gtk_widget_set_sensitive(operation->button, FALSE);
    auto* binding = new UniversalRestoreBinding{app, operation, confirmation, technician, {}, {}};
    g_signal_connect(backups, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<UniversalRestoreBinding*>(data);
        const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "backup"));
        values->backup = path == nullptr ? "" : path;
        update_universal_restore_button(values);
    }), binding);
    g_signal_connect(destinations, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<UniversalRestoreBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->destination = selector == nullptr ? "" : selector;
        update_universal_restore_button(values);
    }), binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_universal_restore_button(static_cast<UniversalRestoreBinding*>(data));
    }), binding);
    connect_technician_field<UniversalRestoreBinding, update_universal_restore_button>(binding->technician, binding);
    g_signal_connect(operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<UniversalRestoreBinding*>(data);
        if (!technician_field_complete(values->technician)) return;
        auto* operation = values->operation;
        const auto backup = values->backup;
        const auto destination = values->destination;
        const auto technician_json = technician_field_json(values->technician);
        const auto plan = "{\"command\":\"driver_plan\",\"automatic\":true,\"image_directory\":" +
            quote_json(backup) + ",\"destination_selector\":" + quote_json(destination) + "}";
        begin_service_job(operation, plan, "Replacement storage driver matched.", {},
            [operation, backup, destination, technician_json](bool plan_ok, const std::string&) {
                if (!plan_ok) return;
                const auto restore = "{\"command\":\"restore_image\",\"image_directory\":" +
                    quote_json(backup) + ",\"selector\":" + quote_json(destination) +
                    ",\"confirmation\":\"ERASE\"" + technician_json + "}";
                begin_service_job(operation, restore,
                    "Windows image restored and verified. Preparing replacement-hardware servicing.", "restore",
                    [operation, backup, destination](bool restore_ok, const std::string&) {
                        if (!restore_ok) return;
                        const auto servicing = "{\"command\":\"driver_apply_offline\",\"automatic\":true,"
                            "\"image_directory\":" + quote_json(backup) +
                            ",\"destination_selector\":" + quote_json(destination) +
                            ",\"confirmation\":\"APPLY UNIVERSAL RESTORE\"}";
                        begin_service_job(operation, servicing,
                            "Universal Restore installed the replacement boot-storage driver inside offline Windows.", "driver-job",
                            [operation](bool servicing_ok, const std::string&) {
                                if (!servicing_ok) return;
                                set_status(operation->app,
                                    "Universal Restore finished. Run Boot Repair next to check and update this disk's "
                                    "EFI fallback boot path before removing it from the bench.");
                            });
                    });
            });
    }), binding);

    return page;
}

struct BootRepairBinding {
    App* app = nullptr;
    OperationUi* plan_operation = nullptr;
    OperationUi* apply_operation = nullptr;
    GtkWidget* confirmation = nullptr;
    std::string selector;
    bool ready_for_servicing = false;
};

void update_boot_repair_apply_button(BootRepairBinding* binding) {
    const auto confirmation = std::string(gtk_editable_get_text(GTK_EDITABLE(binding->confirmation)));
    gtk_widget_set_sensitive(binding->apply_operation->button,
                             !binding->selector.empty() && binding->ready_for_servicing &&
                             confirmation == "BOOT REPAIR");
}

GtkWidget* make_boot_repair_page(App* app) {
    GtkWidget* page = page_shell(
        app, "WINDOWS RECOVERY", "Boot Repair",
        "Diagnose and repair the UEFI firmware fallback boot path on a Windows disk.");

    GtkWidget* choices = gtk_list_box_new();
    lazarus::gui::add_class(choices, "choice-list");
    std::size_t count = 0;
    for (const auto& device : app->devices) {
        if (!device.system && (device.role == "destination-only" || device.role.empty())) {
            gtk_list_box_append(GTK_LIST_BOX(choices), device_choice(app, device, device.role.c_str()));
            ++count;
        }
    }
    GtkWidget* selection = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(selection, "workflow-section");
    gtk_box_append(GTK_BOX(selection), label("1. Select a disk", "section-title"));
    gtk_box_append(GTK_BOX(selection), label(
        "The diagnostic reads the EFI System Partition and Windows installation. It does not modify the disk.",
        "section-detail"));
    if (count == 0) gtk_box_append(GTK_BOX(selection), label("No destination-only or unassigned disk is currently connected.", "empty-state"));
    else gtk_box_append(GTK_BOX(selection), choice_scroll(choices, 145));
    gtk_box_append(GTK_BOX(page), selection);

    auto* plan_operation = add_operation_ui(app, page, "Run Boot Repair Diagnostic");
    gtk_widget_set_sensitive(plan_operation->button, FALSE);

    GtkWidget* apply_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(apply_section, "workflow-section");
    gtk_box_append(GTK_BOX(apply_section), label("2. Apply repair", "section-title"));
    gtk_box_append(GTK_BOX(apply_section), label(
        "Enabled only after the diagnostic finds a fixable condition. Copies the installed bootmgfw.efi to "
        "EFI/Boot/BOOTX64.EFI, the path UEFI firmware falls back to when it has no matching boot entry for this disk. "
        "The existing file, if any, is preserved before it is replaced.",
        "section-detail"));
    gtk_box_append(GTK_BOX(page), apply_section);

    GtkWidget* confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Type BOOT REPAIR to rebuild the EFI fallback boot path");
    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(warning, "warning-panel");
    gtk_box_append(GTK_BOX(warning), label("EFI/Boot/BOOTX64.EFI on the selected disk will be replaced", "warning-text"));
    gtk_box_append(GTK_BOX(warning), confirmation);
    gtk_box_append(GTK_BOX(page), warning);

    auto* apply_operation = add_operation_ui(app, page, "Apply Boot Repair");
    gtk_widget_set_sensitive(apply_operation->button, FALSE);

    auto* binding = new BootRepairBinding{app, plan_operation, apply_operation, confirmation, {}, false};
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<BootRepairBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->selector = selector == nullptr ? "" : selector;
        values->ready_for_servicing = false;
        gtk_widget_set_sensitive(values->plan_operation->button, !values->selector.empty());
        update_boot_repair_apply_button(values);
    }), binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_boot_repair_apply_button(static_cast<BootRepairBinding*>(data));
    }), binding);
    g_signal_connect(plan_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<BootRepairBinding*>(data);
        if (values->selector.empty()) return;
        values->ready_for_servicing = false;
        update_boot_repair_apply_button(values);
        const auto selector = values->selector;
        begin_service_job(values->plan_operation,
            "{\"command\":\"boot_repair_plan\",\"destination_selector\":" + quote_json(selector) + "}",
            "Boot repair diagnostic completed.", "boot-repair-plan",
            [values](bool ok, const std::string& response) {
                values->ready_for_servicing = ok && json_bool(response, "ready_for_servicing");
                update_boot_repair_apply_button(values);
            });
    }), binding);
    g_signal_connect(apply_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<BootRepairBinding*>(data);
        if (values->selector.empty() || !values->ready_for_servicing) return;
        begin_service_job(values->apply_operation,
            "{\"command\":\"boot_repair_apply\",\"destination_selector\":" + quote_json(values->selector) +
                ",\"confirmation\":\"APPLY BOOT REPAIR\"}",
            "Boot repair applied.", "boot-repair-apply");
    }), binding);

    return page;
}

struct RecoveryBinding {
    App* app = nullptr;
    OperationUi* open_operation = nullptr;
    OperationUi* export_operation = nullptr;
    GtkWidget* workspace = nullptr;
    GtkWidget* volume_list = nullptr;
    GtkWidget* entries = nullptr;
    GtkWidget* path_label = nullptr;
    GtkWidget* destinations = nullptr;
    GtkWidget* destination_folder = nullptr;
    GtkWidget* bitlocker_box = nullptr;
    GtkWidget* bitlocker_key_entry = nullptr;
    std::string image_directory;
    std::string session_id;
    std::string volume_token;
    std::string volume_filesystem;
    std::string relative_path;
    std::string destination_selector;
    std::string bitlocker_recovery_key;
};

void recovery_list_directory(RecoveryBinding* binding, const std::string& relative_path) {
    if (binding->session_id.empty() || binding->volume_token.empty()) return;
    std::string response;
    const auto payload = "{\"command\":\"browse_list\",\"session_id\":" + quote_json(binding->session_id) +
                         ",\"volume_token\":" + quote_json(binding->volume_token) +
                         ",\"relative_path\":" + quote_json(relative_path) +
                         (binding->bitlocker_recovery_key.empty() ? "" :
                            ",\"bitlocker_recovery_key\":" + quote_json(binding->bitlocker_recovery_key)) + "}";
    if (!request(payload, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        set_status(binding->app, error.empty() ? "The image folder could not be opened." : error);
        return;
    }
    if (binding->bitlocker_box != nullptr) gtk_widget_set_visible(binding->bitlocker_box, FALSE);
    binding->relative_path = json_string(response, "relative_path");
    gtk_label_set_text(GTK_LABEL(binding->path_label),
                       binding->relative_path.empty() ? "Image volume /" : ("Image volume /" + binding->relative_path).c_str());
    clear_children(binding->entries);
    for (const auto& row_text : split(json_string(response, "entries_rows"), '\n')) {
        const auto fields = split(row_text, '\t');
        if (fields.size() < 4) continue;
        const auto name = hex_decode_text(fields[0]);
        const auto child_relative = hex_decode_text(fields[1]);
        const auto type = fields[2];
        std::uint64_t size = 0;
        try { size = std::stoull(fields[3]); } catch (...) {}
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(content, 10); gtk_widget_set_margin_end(content, 10);
        gtk_widget_set_margin_top(content, 7); gtk_widget_set_margin_bottom(content, 7);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        GtkWidget* select = gtk_check_button_new();
        gtk_widget_set_sensitive(select, type == "file" || type == "directory");
        g_object_set_data_full(G_OBJECT(select), "relative", g_strdup(child_relative.c_str()), g_free);
        gtk_box_append(GTK_BOX(content), select);
        GtkWidget* facts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(facts, TRUE);
        gtk_box_append(GTK_BOX(facts), label(name, "choice-title"));
        gtk_box_append(GTK_BOX(facts), label(type == "file" ? human_bytes(size) :
                                                 (type == "directory" ? "Folder" : "Not exportable"), "choice-detail"));
        gtk_box_append(GTK_BOX(content), facts);
        if (type == "directory") {
            GtkWidget* open = gtk_button_new_with_label("Open");
            lazarus::gui::add_class(open, "secondary-command");
            g_object_set_data_full(G_OBJECT(open), "relative", g_strdup(child_relative.c_str()), g_free);
            g_signal_connect(open, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
                recovery_list_directory(static_cast<RecoveryBinding*>(data),
                                        static_cast<const char*>(g_object_get_data(G_OBJECT(button), "relative")));
            }), binding);
            gtk_box_append(GTK_BOX(content), open);
        }
        lazarus::gui::add_class(row, "choice-row");
        gtk_list_box_append(GTK_LIST_BOX(binding->entries), row);
    }
    gtk_widget_set_sensitive(binding->export_operation->button, !binding->destination_selector.empty());
    set_status(binding->app, "Browsing the verified image read-only.");
}

GtkWidget* make_recovery_page(App* app) {
    GtkWidget* page = page_shell(app, "RECOVER FILES", "Browse and Recover Files",
                                 "Open a verified image read-only, select files or folders, then copy them to removable media.");
    auto* binding = new RecoveryBinding;
    binding->app = app;
    GtkWidget* backup_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(backup_section, "workflow-section");
    gtk_box_append(GTK_BOX(backup_section), label("1. Select a Lazarus backup", "section-title"));
    GtkWidget* backups = gtk_list_box_new();
    lazarus::gui::add_class(backups, "choice-list");
    for (const auto& backup : app->backups) {
        if (backup.status != "interrupted") gtk_list_box_append(GTK_LIST_BOX(backups), backup_choice(backup));
    }
    gtk_box_append(GTK_BOX(backup_section), choice_scroll(backups, 130));
    gtk_box_append(GTK_BOX(page), backup_section);
    binding->open_operation = add_operation_ui(app, page, "Open Image Read Only");
    gtk_widget_set_visible(binding->open_operation->print, FALSE);
    gtk_widget_set_sensitive(binding->open_operation->button, FALSE);

    binding->workspace = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_visible(binding->workspace, FALSE);
    GtkWidget* volumes_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    lazarus::gui::add_class(volumes_section, "workflow-section");
    gtk_box_append(GTK_BOX(volumes_section), label("2. Select an image volume", "section-title"));
    binding->volume_list = gtk_list_box_new();
    lazarus::gui::add_class(binding->volume_list, "choice-list");
    gtk_box_append(GTK_BOX(volumes_section), choice_scroll(binding->volume_list, 112));
    gtk_box_append(GTK_BOX(binding->workspace), volumes_section);

    binding->bitlocker_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(binding->bitlocker_box, "workflow-section");
    gtk_widget_set_visible(binding->bitlocker_box, FALSE);
    gtk_box_append(GTK_BOX(binding->bitlocker_box), label(
        "This volume is protected by BitLocker. Enter the recovery key to unlock it read-only.", "section-detail"));
    binding->bitlocker_key_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(binding->bitlocker_key_entry), "111111-222222-333333-444444-555555-666666-777777-888888");
    gtk_entry_set_visibility(GTK_ENTRY(binding->bitlocker_key_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(binding->bitlocker_key_entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_append(GTK_BOX(binding->bitlocker_box), binding->bitlocker_key_entry);
    GtkWidget* unlock_button = gtk_button_new_with_label("Unlock Volume");
    lazarus::gui::add_class(unlock_button, "secondary-command");
    gtk_widget_set_halign(unlock_button, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(binding->bitlocker_box), unlock_button);
    gtk_box_append(GTK_BOX(binding->workspace), binding->bitlocker_box);

    GtkWidget* browser = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(browser, "workflow-section");
    GtkWidget* browser_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* up = gtk_button_new_with_label("Up");
    lazarus::gui::add_class(up, "secondary-command");
    binding->path_label = label("Image volume /", "section-title");
    gtk_widget_set_hexpand(binding->path_label, TRUE);
    gtk_box_append(GTK_BOX(browser_header), up);
    gtk_box_append(GTK_BOX(browser_header), binding->path_label);
    gtk_box_append(GTK_BOX(browser), browser_header);
    binding->entries = gtk_list_box_new();
    lazarus::gui::add_class(binding->entries, "choice-list");
    gtk_box_append(GTK_BOX(browser), choice_scroll(binding->entries, 210));
    gtk_box_append(GTK_BOX(binding->workspace), browser);

    GtkWidget* destination_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    lazarus::gui::add_class(destination_section, "workflow-section");
    gtk_box_append(GTK_BOX(destination_section), label("3. Copy selected items to removable media", "section-title"));
    binding->destinations = gtk_list_box_new();
    lazarus::gui::add_class(binding->destinations, "choice-list");
    std::size_t destinations = 0;
    for (const auto& device : app->devices) {
        if (!device.system && device.role == "removable-media") {
            gtk_list_box_append(GTK_LIST_BOX(binding->destinations), device_choice(app, device, "removable-media"));
            ++destinations;
        }
    }
    if (destinations == 0) {
        gtk_box_append(GTK_BOX(destination_section), label("No removable-media port is configured. Assign one in Administration > Physical Port Roles.", "empty-state"));
    } else {
        gtk_box_append(GTK_BOX(destination_section), choice_scroll(binding->destinations, 112));
    }
    binding->destination_folder = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(binding->destination_folder), "Optional folder on destination (example: Recovered Files)");
    gtk_box_append(GTK_BOX(destination_section), binding->destination_folder);
    gtk_box_append(GTK_BOX(binding->workspace), destination_section);
    gtk_box_append(GTK_BOX(page), binding->workspace);
    binding->export_operation = add_operation_ui(app, page, "Copy Selected Files");
    gtk_widget_set_visible(binding->export_operation->print, FALSE);
    gtk_widget_set_sensitive(binding->export_operation->button, FALSE);

    g_signal_connect(backups, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        const char* path = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "backup"));
        values->image_directory = path == nullptr ? "" : path;
        gtk_widget_set_sensitive(values->open_operation->button, !values->image_directory.empty());
    }), binding);
    select_backup_path(backups, app->selected_backup);
    g_signal_connect(binding->open_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        if (!values->session_id.empty()) {
            std::string ignored;
            request("{\"command\":\"browse_close\",\"session_id\":" + quote_json(values->session_id) + "}", ignored);
            values->session_id.clear();
        }
        begin_service_job(values->open_operation,
                          "{\"command\":\"browse_open\",\"image_directory\":" + quote_json(values->image_directory) + "}",
                          "The verified image is open read-only.", "browse-open",
                          [values](bool ok, const std::string& response) {
            if (!ok) return;
            values->session_id = json_string(response, "session_id");
            clear_children(values->volume_list);
            for (const auto& row_text : split(json_string(response, "volumes_rows"), '\n')) {
                const auto fields = split(row_text, '\t');
                if (fields.size() < 5) continue;
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
                gtk_widget_set_margin_start(copy, 12); gtk_widget_set_margin_end(copy, 12);
                gtk_widget_set_margin_top(copy, 8); gtk_widget_set_margin_bottom(copy, 8);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), copy);
                gtk_box_append(GTK_BOX(copy), label(hex_decode_text(fields[1]), "choice-title"));
                std::uint64_t size = 0; try { size = std::stoull(fields[3]); } catch (...) {}
                const auto detail = (fields[2].empty() ? "Unknown filesystem" : fields[2]) + " | " + human_bytes(size) +
                                    (fields[2] == "BitLocker" ? " | Recovery key required" :
                                     (fields[4] == "1" ? " | Read-only browsing available" : " | Unsupported filesystem"));
                gtk_box_append(GTK_BOX(copy), label(detail, "choice-detail"));
                gtk_widget_set_sensitive(row, fields[4] == "1");
                g_object_set_data_full(G_OBJECT(row), "volume", g_strdup(fields[0].c_str()), g_free);
                g_object_set_data_full(G_OBJECT(row), "filesystem", g_strdup(fields[2].c_str()), g_free);
                gtk_list_box_append(GTK_LIST_BOX(values->volume_list), row);
            }
            gtk_widget_set_visible(values->workspace, TRUE);
        });
    }), binding);
    g_signal_connect(binding->volume_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        const char* token = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "volume"));
        const char* filesystem = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "filesystem"));
        values->volume_token = token == nullptr ? "" : token;
        values->volume_filesystem = filesystem == nullptr ? "" : filesystem;
        values->bitlocker_recovery_key.clear();
        gtk_editable_set_text(GTK_EDITABLE(values->bitlocker_key_entry), "");
        const bool needs_key = values->volume_filesystem == "BitLocker";
        gtk_widget_set_visible(values->bitlocker_box, needs_key);
        if (!values->volume_token.empty() && !needs_key) recovery_list_directory(values, "");
    }), binding);
    g_signal_connect(unlock_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        values->bitlocker_recovery_key = gtk_editable_get_text(GTK_EDITABLE(values->bitlocker_key_entry));
        if (values->volume_token.empty() || values->bitlocker_recovery_key.empty()) return;
        recovery_list_directory(values, "");
    }), binding);
    g_signal_connect(up, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        std::filesystem::path parent(values->relative_path);
        recovery_list_directory(values, parent.has_parent_path() ? parent.parent_path().string() : "");
    }), binding);
    g_signal_connect(binding->destinations, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->destination_selector = selector == nullptr ? "" : selector;
        gtk_widget_set_sensitive(values->export_operation->button,
                                 !values->destination_selector.empty() && !values->volume_token.empty());
    }), binding);
    g_signal_connect(binding->export_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<RecoveryBinding*>(data);
        std::string selections;
        for (GtkWidget* row = gtk_widget_get_first_child(values->entries); row != nullptr; row = gtk_widget_get_next_sibling(row)) {
            GtkWidget* content = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
            GtkWidget* check = content == nullptr ? nullptr : gtk_widget_get_first_child(content);
            if (check != nullptr && GTK_IS_CHECK_BUTTON(check) && gtk_check_button_get_active(GTK_CHECK_BUTTON(check))) {
                const char* relative = static_cast<const char*>(g_object_get_data(G_OBJECT(check), "relative"));
                if (relative != nullptr) selections += hex_encode_text(relative) + "\n";
            }
        }
        if (selections.empty()) {
            set_status(values->app, "Select at least one file or folder to recover.");
            return;
        }
        begin_service_job(values->export_operation,
                          "{\"command\":\"browse_export\",\"session_id\":" + quote_json(values->session_id) +
                              ",\"volume_token\":" + quote_json(values->volume_token) +
                              ",\"destination_selector\":" + quote_json(values->destination_selector) +
                              ",\"destination_folder\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->destination_folder))) +
                              ",\"source_paths_hex\":" + quote_json(selections) +
                              (values->bitlocker_recovery_key.empty() ? "" :
                                 ",\"bitlocker_recovery_key\":" + quote_json(values->bitlocker_recovery_key)) + "}",
                          "Recovered files were flushed and the destination is safe to disconnect.", "recover-files");
    }), binding);
    return page;
}

struct PortRoleEntry {
    std::string identity;
    GtkWidget* selector = nullptr;
};

struct PortRolesBinding {
    App* app = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* save = nullptr;
    std::vector<PortRoleEntry> entries;
};

void populate_port_role_rows(PortRolesBinding* binding) {
    clear_children(binding->list);
    binding->entries.clear();
    static const char* role_names[] = {
        "Unconfigured", "Source / Read Only", "Destination Only", "Image Storage", "Removable Media", "Ignored", nullptr,
    };
    const auto role_index = [](const std::string& role) -> guint {
        if (role == "source-only") return 1;
        if (role == "destination-only") return 2;
        if (role == "image-storage") return 3;
        if (role == "removable-media") return 4;
        if (role == "ignored") return 5;
        return 0;
    };
    for (const auto& port : binding->app->ports) {
        if (port.system || port.identity.empty()) continue;
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        lazarus::gui::add_class(row, "port-label-row");
        GtkWidget* facts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(facts, TRUE);
        const auto title = port.label.empty() ? port.identity : port.label;
        gtk_box_append(GTK_BOX(facts), label(title, "choice-title"));
        const auto attached = port.online
            ? "Connected: " + (port.model.empty() ? port.device_path : port.model) + " | " + human_bytes(port.size_bytes)
            : "Empty - no disk is currently connected";
        gtk_box_append(GTK_BOX(facts), label(attached, port.online ? "choice-detail" : "port-role-offline"));
        GtkWidget* identity = label("Physical port: " + port.identity, "choice-detail");
        gtk_label_set_selectable(GTK_LABEL(identity), TRUE);
        gtk_box_append(GTK_BOX(facts), identity);
        gtk_box_append(GTK_BOX(row), facts);
        GtkStringList* model = gtk_string_list_new(role_names);
        GtkWidget* selector = gtk_drop_down_new(G_LIST_MODEL(model), nullptr);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(selector), role_index(port.role));
        gtk_widget_set_size_request(selector, 230, -1);
        gtk_widget_set_valign(selector, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), selector);
        gtk_box_append(GTK_BOX(binding->list), row);
        binding->entries.push_back({port.identity, selector});
    }
    if (binding->entries.empty()) {
        gtk_box_append(GTK_BOX(binding->list),
                       label("No physical ports were discovered. Connect a calibration drive and select Re-scan Physical Ports.", "empty-state"));
    }
    if (binding->save != nullptr) gtk_widget_set_sensitive(binding->save, !binding->entries.empty());
}

GtkWidget* make_port_roles_page(App* app) {
    GtkWidget* page = page_shell(app, "BENCH POLICY", "Assign Physical Port Roles",
                                 "Assign each detected physical connection exactly one role. Assignments follow the USB port, not /dev/sdX.",
                                 "admin");
    auto* binding = new PortRolesBinding;
    binding->app = app;
    GtkWidget* refresh = gtk_button_new_with_label("Re-scan Physical Ports");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_widget_set_halign(refresh, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(page), refresh);
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(list, "workflow-section");
    binding->list = list;
    populate_port_role_rows(binding);
    gtk_box_append(GTK_BOX(page), list);
    binding->status = label("No unsaved role changes.", "section-detail");
    gtk_box_append(GTK_BOX(page), binding->status);
    GtkWidget* save = gtk_button_new_with_label("Save Physical Port Roles");
    binding->save = save;
    lazarus::gui::add_class(save, "primary-command");
    gtk_widget_set_halign(save, GTK_ALIGN_END);
    gtk_widget_set_sensitive(save, !binding->entries.empty());
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<PortRolesBinding*>(data);
        gtk_label_set_text(GTK_LABEL(values->status), "Scanning physical USB and storage topology...");
        load_data(values->app);
        populate_port_role_rows(values);
        gtk_label_set_text(GTK_LABEL(values->status), "Physical port inventory refreshed. Unsaved selections were reset.");
    }), binding);
    g_signal_connect(save, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<PortRolesBinding*>(data);
        std::string sources;
        std::string destinations;
        std::string removable;
        std::string ignored;
        std::string storage_ports;
        for (const auto& entry : values->entries) {
            switch (gtk_drop_down_get_selected(GTK_DROP_DOWN(entry.selector))) {
                case 1: sources += entry.identity + "\n"; break;
                case 2: destinations += entry.identity + "\n"; break;
                case 3:
                    if (!storage_ports.empty()) {
                        gtk_label_set_text(GTK_LABEL(values->status), "Only one physical connection may be assigned as image storage.");
                        return;
                    }
                    storage_ports = entry.identity + "\n";
                    break;
                case 4: removable += entry.identity + "\n"; break;
                case 5: ignored += entry.identity + "\n"; break;
                default: break;
            }
        }
        const auto old_removable = values->app->removable_text;
        const auto old_ignored = values->app->ignored_text;
        const auto old_storage_ports = values->app->image_storage_port_text;
        values->app->removable_text = removable;
        values->app->ignored_text = ignored;
        values->app->image_storage_port_text = storage_ports;
        if (!save_profile(values->app, sources, destinations, values->app->storage_text, values->app->labels_text)) {
            values->app->removable_text = old_removable;
            values->app->ignored_text = old_ignored;
            values->app->image_storage_port_text = old_storage_ports;
            gtk_label_set_text(GTK_LABEL(values->status), "The role policy was not saved. Review the status bar for details.");
            return;
        }
        gtk_label_set_text(GTK_LABEL(values->status), "Physical port roles saved and enforced.");
    }), binding);
    gtk_box_append(GTK_BOX(page), save);
    return page;
}

GtkWidget* make_port_policy_page(App* app, bool source) {
    const std::string existing = source ? app->source_text : app->destination_text;
    GtkWidget* page = page_shell(app, "BENCH POLICY", source ? "Configure Source Ports" : "Configure Destination Ports",
                                 "Select the physical paths allowed for this role. System disks are excluded.", "admin");
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    std::size_t available = 0;
    for (const auto& port : app->ports) {
        if (port.system) continue;
        const auto title = port.label.empty() ? port.identity : port.label;
        GtkWidget* check = gtk_check_button_new_with_label(title.c_str());
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), existing.find(port.identity) != std::string::npos);
        g_object_set_data_full(G_OBJECT(check), "identity", g_strdup(port.identity.c_str()), g_free);
        gtk_box_append(GTK_BOX(list), check);
        ++available;
    }
    GtkWidget* save = gtk_button_new_with_label(source ? "Save Source Port Policy" : "Save Destination Port Policy");
    lazarus::gui::add_class(save, "primary-command");
    g_signal_connect(save, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data); std::string selected;
        GtkWidget* list = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "list"));
        for (GtkWidget* child = gtk_widget_get_first_child(list); child != nullptr; child = gtk_widget_get_next_sibling(child)) {
            if (gtk_check_button_get_active(GTK_CHECK_BUTTON(child))) selected += static_cast<const char*>(g_object_get_data(G_OBJECT(child), "identity")) + std::string("\n");
        }
        const bool is_source = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "source"));
        save_profile(state, is_source ? selected : state->source_text, is_source ? state->destination_text : selected, state->storage_text, state->labels_text);
    })), app);
    g_object_set_data(G_OBJECT(save), "list", list); g_object_set_data(G_OBJECT(save), "source", GINT_TO_POINTER(source));
    if (available == 0) {
        gtk_box_append(GTK_BOX(page), label("No configurable drives were reported by the Lazarus service.", "empty-state"));
        gtk_widget_set_sensitive(save, FALSE);
    } else {
        gtk_box_append(GTK_BOX(page), list);
    }
    gtk_box_append(GTK_BOX(page), save); return page;
}

struct StorageBinding {
    App* app = nullptr;
    OperationUi* operation = nullptr;
    GtkWidget* mode = nullptr;
    GtkWidget* confirmation = nullptr;
    GtkWidget* selected_facts = nullptr;
    GtkWidget* destructive_warning = nullptr;
    GtkWidget* folder = nullptr;
    GtkWidget* browse = nullptr;
    GtkWidget* browser_panel = nullptr;
    GtkWidget* browser_path = nullptr;
    GtkWidget* browser_list = nullptr;
    GtkWidget* browser_up = nullptr;
    GtkWidget* browser_open = nullptr;
    GtkWidget* browser_use = nullptr;
    GtkWidget* new_folder = nullptr;
    GtkWidget* create_folder = nullptr;
    std::string selector;
    std::string browser_relative;
    // Non-null only when this page was reached via "Add Another Location" rather than the
    // primary type-card flow -- see App::adding_extra_storage_location.
    GtkWidget* location_name = nullptr;
    GtkWidget* set_default = nullptr;
    bool adding_extra = false;
};

struct StorageMountBinding {
    App* app = nullptr;
    OperationUi* operation = nullptr;
    GtkWidget* state = nullptr;
};

struct NasStorageBinding {
    App* app = nullptr;
    OperationUi* operation = nullptr;
    GtkWidget* protocol = nullptr;
    GtkWidget* server = nullptr;
    GtkWidget* share = nullptr;
    GtkWidget* folder = nullptr;
    GtkWidget* username = nullptr;
    GtkWidget* password = nullptr;
    GtkWidget* domain = nullptr;
    // Non-null only when this page was reached via "Add Another Location" rather than the
    // primary type-card flow -- see App::adding_extra_storage_location.
    GtkWidget* location_name = nullptr;
    GtkWidget* set_default = nullptr;
    bool adding_extra = false;
};

void update_nas_storage_action(NasStorageBinding* binding) {
    const bool smb = gtk_drop_down_get_selected(GTK_DROP_DOWN(binding->protocol)) == 0;
    const auto present = [](GtkWidget* entry) {
        return *gtk_editable_get_text(GTK_EDITABLE(entry)) != '\0';
    };
    gtk_entry_set_placeholder_text(GTK_ENTRY(binding->share), smb ? "Backups" : "/volume1/backups");
    gtk_widget_set_sensitive(binding->username, smb);
    gtk_widget_set_sensitive(binding->password, smb);
    gtk_widget_set_sensitive(binding->domain, smb);
    gtk_widget_set_sensitive(binding->operation->button,
        present(binding->server) && present(binding->share) && present(binding->folder) &&
        (!smb || (present(binding->username) && present(binding->password))));
}

void refresh_storage_mount_controls(StorageMountBinding* binding) {
    const auto assigned = !binding->app->nas_storage_protocol.empty()
        ? "Assigned NAS: " + binding->app->nas_storage_protocol + "://" +
            binding->app->nas_storage_server + "/" + binding->app->nas_storage_share
        : (binding->app->image_storage_device.empty()
            ? std::string{"No image storage is configured."}
            : "Assigned disk: " + binding->app->image_storage_device);
    const auto volume = !binding->app->nas_storage_protocol.empty()
        ? "Network storage protocol: " + binding->app->nas_storage_protocol
        : (binding->app->image_storage_volume.empty()
        ? std::string{"Filesystem: detected from the assigned disk"}
        : "Filesystem: " + binding->app->image_storage_volume);
    auto configured_folder = binding->app->storage_text;
    if (const auto newline = configured_folder.find('\n'); newline != std::string::npos) configured_folder.resize(newline);
    if (configured_folder.empty()) configured_folder = "/mnt/lazarus-storage/images";
    const auto mounted = binding->app->storage_online
        ? "Mounted read-write at " + configured_folder + "\nMounted source: " + binding->app->storage_mount_source
        : "Not mounted. Backups cannot use this storage until it is mounted." +
            (binding->app->storage_error.empty() ? std::string{} : "\nReason: " + binding->app->storage_error);
    gtk_label_set_text(GTK_LABEL(binding->state), (assigned + "\n" + volume + "\n" + mounted).c_str());
    gtk_button_set_label(GTK_BUTTON(binding->operation->button),
                         binding->app->storage_online ? "Safely Unmount Image Storage" : "Mount Assigned Image Storage");
    gtk_widget_remove_css_class(binding->operation->button,
                                binding->app->storage_online ? "primary-command" : "danger-command");
    lazarus::gui::add_class(binding->operation->button,
                            binding->app->storage_online ? "danger-command" : "primary-command");
    gtk_widget_set_sensitive(binding->operation->button,
                             binding->app->storage_online || !binding->app->image_storage_device.empty() ||
                             !binding->app->nas_storage_protocol.empty());
}

void add_storage_mount_status(App* app, GtkWidget* page) {
    GtkWidget* status_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(status_panel, "workflow-section");
    gtk_box_append(GTK_BOX(status_panel), label("Current Status", "section-title"));
    GtkWidget* status_state = label("Reading image-storage mount state...", "instruction");
    gtk_label_set_selectable(GTK_LABEL(status_state), TRUE);
    gtk_box_append(GTK_BOX(status_panel), status_state);
    auto* mount_operation = add_operation_ui(app, status_panel, "Mount Assigned Image Storage");
    auto* mount_binding = new StorageMountBinding{app, mount_operation, status_state};
    refresh_storage_mount_controls(mount_binding);
    g_signal_connect(mount_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageMountBinding*>(data);
        const bool unmount = values->app->storage_online;
        begin_service_job(values->operation,
                          "{\"command\":\"" + std::string(unmount ? "unmount_image_storage" : "mount_image_storage") +
                              "\",\"admin_token\":" + quote_json(values->app->admin_token) + "}",
                          unmount ? "Image storage was safely unmounted." : "Image storage is mounted and ready.", {},
                          [values](bool, const std::string&) {
                              load_data(values->app);
                              refresh_storage_mount_controls(values);
                              refresh_operational_pages(values->app);
                          });
    }), mount_binding);
    gtk_box_append(GTK_BOX(page), status_panel);
}

void update_storage_action(StorageBinding* binding) {
    const bool erase = gtk_drop_down_get_selected(GTK_DROP_DOWN(binding->mode)) == 1;
    const auto confirmation = std::string{gtk_editable_get_text(GTK_EDITABLE(binding->confirmation))};
    const auto folder = std::filesystem::path(gtk_editable_get_text(GTK_EDITABLE(binding->folder))).lexically_normal();
    const bool folder_valid = !folder.empty() && folder != "." && !folder.is_absolute() &&
        std::none_of(folder.begin(), folder.end(), [](const auto& component) { return component == ".."; });
    gtk_widget_set_visible(binding->confirmation, erase);
    gtk_widget_set_visible(binding->destructive_warning, erase);
    gtk_widget_set_sensitive(binding->browse, !erase && !binding->selector.empty());
    if (erase) gtk_widget_set_visible(binding->browser_panel, FALSE);
    gtk_button_set_label(GTK_BUTTON(binding->operation->button),
                         erase ? "Format Entire Drive for Image Storage" : "Use Existing Filesystem");
    gtk_widget_remove_css_class(binding->operation->button, erase ? "primary-command" : "danger-command");
    lazarus::gui::add_class(binding->operation->button, erase ? "danger-command" : "primary-command");
    gtk_widget_set_sensitive(binding->operation->button,
        !binding->selector.empty() && folder_valid && (!erase || confirmation == "ERASE"));
}

void storage_list_directory(StorageBinding* binding, const std::string& relative_path) {
    if (binding->selector.empty()) return;
    gtk_widget_set_visible(binding->browser_panel, TRUE);
    std::string response;
    const auto payload = "{\"command\":\"storage_folders\",\"admin_token\":" +
        quote_json(binding->app->admin_token) + ",\"selector\":" + quote_json(binding->selector) +
        ",\"relative_path\":" + quote_json(relative_path) + "}";
    if (!request(payload, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        set_status(binding->app, error.empty() ? "The selected disk could not be opened for folder browsing." : error);
        return;
    }
    binding->browser_relative = json_string(response, "relative_path");
    const auto shown_path = binding->browser_relative.empty()
        ? std::string{"Selected disk /"}
        : "Selected disk /" + binding->browser_relative;
    gtk_label_set_text(GTK_LABEL(binding->browser_path), shown_path.c_str());
    clear_children(binding->browser_list);
    std::size_t folder_count = 0;
    for (const auto& row_text : split(json_string(response, "directories_rows"), '\n')) {
        const auto fields = split(row_text, '\t');
        if (fields.size() < 2) continue;
        const auto name = hex_decode_text(fields[0]);
        const auto child_relative = hex_decode_text(fields[1]);
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(content, 10); gtk_widget_set_margin_end(content, 10);
        gtk_widget_set_margin_top(content, 7); gtk_widget_set_margin_bottom(content, 7);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        GtkWidget* title = label(name, "choice-title");
        gtk_widget_set_hexpand(title, TRUE);
        gtk_box_append(GTK_BOX(content), title);
        gtk_box_append(GTK_BOX(content), label("Folder", "choice-detail"));
        g_object_set_data_full(G_OBJECT(row), "relative", g_strdup(child_relative.c_str()), g_free);
        lazarus::gui::add_class(row, "choice-row");
        gtk_list_box_append(GTK_LIST_BOX(binding->browser_list), row);
        ++folder_count;
    }
    if (folder_count == 0) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                                   label("No subfolders here. Use this folder or create a new one below.", "empty-state"));
        gtk_list_box_append(GTK_LIST_BOX(binding->browser_list), row);
    }
    gtk_widget_set_sensitive(binding->browser_up, !binding->browser_relative.empty());
    gtk_widget_set_sensitive(binding->browser_open, FALSE);
    gtk_widget_set_sensitive(binding->browser_use, TRUE);
    set_status(binding->app, folder_count == 1 ? "1 subfolder found." : std::to_string(folder_count) + " subfolders found.");
}

GtkWidget* build_storage_type_card(App* app, const char* icon, const char* title, const char* detail,
                                    const std::vector<const char*>& bullets, const char* button_label,
                                    const char* target_page, bool selected) {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    lazarus::gui::add_class(card, "storage-type-card");
    if (selected) lazarus::gui::add_class(card, "storage-type-card-selected");
    gtk_widget_set_hexpand(card, TRUE);

    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* icon_badge = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    lazarus::gui::add_class(icon_badge, "storage-type-icon-badge");
    gtk_widget_set_valign(icon_badge, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(icon_badge, TRUE);
    gtk_widget_set_halign(icon_badge, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(icon_badge), lazarus::gui::make_icon(icon, 32));
    gtk_box_append(GTK_BOX(top_row), icon_badge);
    GtkWidget* radio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    lazarus::gui::add_class(radio, selected ? "storage-type-radio-selected" : "storage-type-radio");
    gtk_widget_set_valign(radio, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_row), radio);
    gtk_box_append(GTK_BOX(card), top_row);

    gtk_box_append(GTK_BOX(card), label(title, "choice-title"));
    gtk_box_append(GTK_BOX(card), label(detail, "choice-detail"));
    for (const char* item : bullets) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(row), lazarus::gui::make_icon("check", 16));
        gtk_box_append(GTK_BOX(row), label(item, "choice-detail"));
        gtk_box_append(GTK_BOX(card), row);
    }

    GtkWidget* button = gtk_button_new_with_label(button_label);
    lazarus::gui::add_class(button, selected ? "primary-command" : "secondary-command");
    g_object_set_data_full(G_OBJECT(button), "target-page", g_strdup(target_page), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
        const auto* target = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "target-page"));
        auto* state = static_cast<App*>(data);
        state->adding_extra_storage_location = false;
        show_page(state, target);
    }), app);
    gtk_box_append(GTK_BOX(card), button);
    return card;
}

GtkWidget* build_storage_location_row(App* app, const StorageLocationRow& location) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    lazarus::gui::add_class(row, "workflow-section");
    gtk_box_append(GTK_BOX(row), lazarus::gui::make_icon(location.type == "nas" ? "network-storage" : "hdd", 24));

    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(title_row), label(location.name, "choice-title"));
    if (location.is_default) gtk_box_append(GTK_BOX(title_row), label("Default", "port-role"));
    GtkWidget* dot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    lazarus::gui::add_class(dot, location.online ? "heartbeat-ok" : "heartbeat-warning");
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(title_row), dot);
    gtk_box_append(GTK_BOX(title_row), label(location.online ? "Mounted" : "Not mounted", "choice-detail"));
    gtk_box_append(GTK_BOX(text), title_row);
    gtk_box_append(GTK_BOX(text), label(location.detail, "choice-detail"));
    gtk_box_append(GTK_BOX(row), text);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const auto set_location_id = [](GtkWidget* button, const std::string& id) {
        g_object_set_data_full(G_OBJECT(button), "location-id", g_strdup(id.c_str()), g_free);
    };
    if (location.id != "primary") {
        GtkWidget* toggle = gtk_button_new_with_label(location.online ? "Unmount" : "Mount");
        lazarus::gui::add_class(toggle, "secondary-command");
        set_location_id(toggle, location.id);
        g_object_set_data(G_OBJECT(toggle), "location-unmount", GINT_TO_POINTER(location.online ? 1 : 0));
        g_signal_connect(toggle, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
            auto* state = static_cast<App*>(data);
            const auto* id = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "location-id"));
            const bool unmount = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "location-unmount")) != 0;
            std::string response;
            request("{\"command\":\"" + std::string(unmount ? "unmount_storage_location" : "mount_storage_location") +
                     "\",\"admin_token\":" + quote_json(state->admin_token) + ",\"id\":" + quote_json(id) + "}", response);
            load_data(state);
            refresh_operational_pages(state);
        }), app);
        gtk_box_append(GTK_BOX(actions), toggle);
    }
    if (!location.is_default) {
        GtkWidget* set_default = gtk_button_new_with_label("Set Default");
        lazarus::gui::add_class(set_default, "secondary-command");
        set_location_id(set_default, location.id == "primary" ? "" : location.id);
        g_signal_connect(set_default, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
            auto* state = static_cast<App*>(data);
            const auto* id = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "location-id"));
            std::string response;
            request("{\"command\":\"set_default_storage_location\",\"admin_token\":" + quote_json(state->admin_token) +
                     ",\"id\":" + quote_json(id) + "}", response);
            load_data(state);
            refresh_operational_pages(state);
        }), app);
        gtk_box_append(GTK_BOX(actions), set_default);
    }
    if (location.id != "primary") {
        GtkWidget* remove = gtk_button_new_with_label("Remove");
        lazarus::gui::add_class(remove, "danger-command");
        set_location_id(remove, location.id);
        g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
            auto* state = static_cast<App*>(data);
            const auto* id = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "location-id"));
            std::string response;
            request("{\"command\":\"remove_storage_location\",\"admin_token\":" + quote_json(state->admin_token) +
                     ",\"id\":" + quote_json(id) + "}", response);
            load_data(state);
            refresh_operational_pages(state);
        }), app);
        gtk_box_append(GTK_BOX(actions), remove);
    }
    gtk_box_append(GTK_BOX(row), actions);
    return row;
}

GtkWidget* make_storage_page(App* app) {
    GtkWidget* page = page_shell(app, "BENCH STORAGE", "Configure Image Storage",
                                 "Choose where Lazarus should store image backups.", "admin");

    const bool network_selected = !app->nas_storage_protocol.empty();
    const bool local_selected = !network_selected && !app->image_storage_device.empty();
    const bool configured = app->storage_online || local_selected || network_selected;

    if (!configured) {
        GtkWidget* banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        lazarus::gui::add_class(banner, "alert-banner");
        gtk_box_append(GTK_BOX(banner), lazarus::gui::make_icon("alert-triangle", 26));
        GtkWidget* banner_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(banner_text, TRUE);
        gtk_box_append(GTK_BOX(banner_text), label("No image storage is configured.", "warning-text"));
        gtk_box_append(GTK_BOX(banner_text), label(
            "Backups cannot be created until a storage location is set.", "choice-detail"));
        gtk_box_append(GTK_BOX(banner), banner_text);
        gtk_box_append(GTK_BOX(page), banner);
    }

    gtk_box_append(GTK_BOX(page), label(
        "Select how and where you want to store Lazarus image backups.", "section-detail"));
    GtkWidget* cards = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(cards), TRUE);
    gtk_box_append(GTK_BOX(cards), build_storage_type_card(
        app, "hdd", "Local Drive", "Use an internal or external drive connected to this bench.",
        {"Best for portable or dedicated drives", "No network required", "Highest performance"},
        "Configure Local Storage", "admin-storage-local", local_selected));
    gtk_box_append(GTK_BOX(cards), build_storage_type_card(
        app, "network-storage", "Network Storage", "Store images on an SMB (Windows) or NFS (Linux/Unix) server.",
        {"Centralized storage", "Access from multiple benches", "Easy to expand"},
        "Configure Network Storage", "admin-storage-network", network_selected));
    gtk_box_append(GTK_BOX(page), cards);

    gtk_box_append(GTK_BOX(page), label("Additional Storage Locations", "section-title"));
    gtk_box_append(GTK_BOX(page), label(
        "Configure more than one location so a technician can choose where to write each backup. "
        "All configured locations are mounted at once.", "section-detail"));
    if (app->storage_locations.empty()) {
        gtk_box_append(GTK_BOX(page), label(
            "No storage locations are configured yet.", "empty-state"));
    } else {
        for (const auto& location : app->storage_locations) {
            gtk_box_append(GTK_BOX(page), build_storage_location_row(app, location));
        }
    }
    GtkWidget* add_extra_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* add_local = gtk_button_new_with_label("Add Local Drive");
    lazarus::gui::add_class(add_local, "secondary-command");
    g_object_set_data_full(G_OBJECT(add_local), "target-page", g_strdup("admin-storage-local"), g_free);
    GtkWidget* add_network = gtk_button_new_with_label("Add Network Storage");
    lazarus::gui::add_class(add_network, "secondary-command");
    g_object_set_data_full(G_OBJECT(add_network), "target-page", g_strdup("admin-storage-network"), g_free);
    for (GtkWidget* button : {add_local, add_network}) {
        g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* self, gpointer data) {
            auto* state = static_cast<App*>(data);
            const auto* target = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "target-page"));
            state->adding_extra_storage_location = true;
            show_page(state, target);
        }), app);
        gtk_box_append(GTK_BOX(add_extra_row), button);
    }
    gtk_box_append(GTK_BOX(page), add_extra_row);

    return page;
}

GtkWidget* make_storage_network_page(App* app) {
    const bool adding_extra = app->adding_extra_storage_location;
    GtkWidget* page = page_shell(app, "BENCH STORAGE", "Network Storage",
                                 "Connect an SMB share or NFS export. Lazarus tests the repository read-write before saving it and keeps SMB credentials in a root-only file.",
                                 "admin-storage");
    GtkWidget* location_name = nullptr;
    GtkWidget* set_default = nullptr;
    if (adding_extra) {
        gtk_box_append(GTK_BOX(page), label("Additional location details", "section-title"));
        gtk_box_append(GTK_BOX(page), label("Location name", "form-label"));
        location_name = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(location_name), "e.g. Office NAS");
        gtk_box_append(GTK_BOX(page), location_name);
        set_default = gtk_check_button_new_with_label("Set as the default location for new backups");
        gtk_box_append(GTK_BOX(page), set_default);
    } else {
        add_storage_mount_status(app, page);
    }
    GtkWidget* nas = page;
    const char* protocols[] = {"SMB / Windows Share", "NFS Export", nullptr};
    GtkWidget* nas_protocol = gtk_drop_down_new_from_strings(protocols);
    GtkWidget* nas_server = gtk_entry_new();
    GtkWidget* nas_share = gtk_entry_new();
    GtkWidget* nas_folder = gtk_entry_new();
    GtkWidget* nas_username = gtk_entry_new();
    GtkWidget* nas_password = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(nas_password), TRUE);
    GtkWidget* nas_domain = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(nas_server), "nas.example.local or 192.168.1.20");
    gtk_entry_set_placeholder_text(GTK_ENTRY(nas_folder), "Lazarus Images");
    gtk_entry_set_placeholder_text(GTK_ENTRY(nas_username), "Backup service account");
    gtk_entry_set_placeholder_text(GTK_ENTRY(nas_domain), "Optional SMB domain or workgroup");
    gtk_editable_set_text(GTK_EDITABLE(nas_folder), "images");
    if (!adding_extra) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(nas_protocol), app->nas_storage_protocol == "nfs" ? 1 : 0);
        g_object_set(nas_password, "placeholder-text", app->nas_storage_protocol == "smb"
            ? "Re-enter password to test or change settings" : "SMB password", nullptr);
        gtk_editable_set_text(GTK_EDITABLE(nas_server), app->nas_storage_server.c_str());
        gtk_editable_set_text(GTK_EDITABLE(nas_share), app->nas_storage_share.c_str());
        gtk_editable_set_text(GTK_EDITABLE(nas_username), app->nas_storage_username.c_str());
        gtk_editable_set_text(GTK_EDITABLE(nas_domain), app->nas_storage_domain.c_str());
        auto nas_relative = std::string{"images"};
        const auto nas_prefix = std::string{"/mnt/lazarus-storage/"};
        if (!app->nas_storage_protocol.empty() && app->storage_text.rfind(nas_prefix, 0) == 0) {
            nas_relative = app->storage_text.substr(nas_prefix.size());
            if (const auto newline = nas_relative.find('\n'); newline != std::string::npos) nas_relative.resize(newline);
        }
        gtk_editable_set_text(GTK_EDITABLE(nas_folder), nas_relative.c_str());
    } else {
        gtk_entry_set_placeholder_text(GTK_ENTRY(nas_password), "SMB password");
    }
    for (const auto& field : std::vector<std::pair<const char*, GtkWidget*>>{
             {"Protocol", nas_protocol}, {"NAS hostname or IP", nas_server},
             {"Share name / export path", nas_share}, {"Lazarus image folder inside the share", nas_folder},
             {"SMB username", nas_username}, {"SMB password", nas_password}, {"SMB domain (optional)", nas_domain}}) {
        gtk_box_append(GTK_BOX(nas), label(field.first, "form-label"));
        gtk_box_append(GTK_BOX(nas), field.second);
    }
    auto* nas_operation = add_operation_ui(app, nas, adding_extra ? "Add NAS Storage Location" : "Test and Use NAS Storage");
    auto* nas_binding = new NasStorageBinding{
        app, nas_operation, nas_protocol, nas_server, nas_share, nas_folder,
        nas_username, nas_password, nas_domain, location_name, set_default, adding_extra};
    for (auto* entry : {nas_server, nas_share, nas_folder, nas_username, nas_password, nas_domain}) {
        g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            update_nas_storage_action(static_cast<NasStorageBinding*>(data));
        }), nas_binding);
    }
    g_signal_connect(nas_protocol, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        update_nas_storage_action(static_cast<NasStorageBinding*>(data));
    }), nas_binding);
    g_signal_connect(nas_operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<NasStorageBinding*>(data);
        const bool smb = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->protocol)) == 0;
        auto request_json = "{\"command\":\"" + std::string(values->adding_extra ? "add_nas_storage_location" : "configure_nas_storage") +
            "\",\"admin_token\":" + quote_json(values->app->admin_token) + ",\"protocol\":" + quote_json(smb ? "smb" : "nfs") +
            ",\"server\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->server))) +
            ",\"share\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->share))) +
            ",\"directory\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->folder))) +
            ",\"username\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->username))) +
            ",\"password\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->password))) +
            ",\"domain\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->domain)));
        if (values->adding_extra) {
            request_json += ",\"name\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->location_name))) +
                ",\"set_default\":" + quote_json(gtk_check_button_get_active(GTK_CHECK_BUTTON(values->set_default)) ? "1" : "0");
        }
        request_json += "}";
        begin_service_job(values->operation, request_json,
                          values->adding_extra ? "The additional NAS location was tested and mounted read-write."
                                                : "NAS storage is mounted read-write and ready for backups.", {},
                          [values](bool ok, const std::string&) {
                              if (!ok) return;
                              gtk_editable_set_text(GTK_EDITABLE(values->password), "");
                              load_data(values->app);
                              if (values->adding_extra) {
                                  values->app->adding_extra_storage_location = false;
                                  show_page(values->app, "admin-storage");
                              }
                              refresh_operational_pages(values->app);
                          });
    }), nas_binding);
    update_nas_storage_action(nas_binding);
    return page;
}

GtkWidget* make_storage_local_page(App* app) {
    const bool adding_extra = app->adding_extra_storage_location;
    GtkWidget* page = page_shell(app, "BENCH STORAGE", "Local Drive Storage",
                                 "Select a physical disk. Lazarus mounts supported existing storage without erasing it, or prepares dedicated ext4 storage after explicit confirmation.",
                                 "admin-storage");
    GtkWidget* location_name = nullptr;
    GtkWidget* set_default = nullptr;
    if (adding_extra) {
        gtk_box_append(GTK_BOX(page), label("Additional location details", "section-title"));
        gtk_box_append(GTK_BOX(page), label("Location name", "form-label"));
        location_name = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(location_name), "e.g. Portable SSD");
        gtk_box_append(GTK_BOX(page), location_name);
        set_default = gtk_check_button_new_with_label("Set as the default location for new backups");
        gtk_box_append(GTK_BOX(page), set_default);
    } else {
        add_storage_mount_status(app, page);
    }
    gtk_box_append(GTK_BOX(page), label("1. Select the physical storage disk", "section-title"));
    gtk_box_append(GTK_BOX(page), label(
        "The Lazarus system disk and source-only customer drives are never eligible.", "section-detail"));
    GtkWidget* choices = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(choices), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(choices, "choice-list");
    std::size_t available = 0;
    for (const auto& device : app->devices) {
        if (device.system || device.role == "source-only" || device.role == "ignored") continue;
        GtkWidget* row = device_choice(app, device, device.role.c_str());
        const auto title = !device.label.empty() ? device.label : (!device.model.empty() ? device.model : device.path);
        std::string facts = title + "\n" + human_bytes(device.size_bytes);
        if (!device.transport.empty()) facts += " | " + device.transport;
        if (!device.serial_ending.empty()) facts += " | serial ending " + device.serial_ending;
        facts += "\nFilesystem: " + (device.filesystem.empty() ? std::string{"not detected"} : device.filesystem);
        facts += " | Mount: " + (device.mountpoint.empty() ? std::string{"not mounted"} : device.mountpoint);
        facts += "\nRole: " + (device.role.empty() ? std::string{"unconfigured"} : device.role);
        facts += "\nPhysical connection: " + (!device.label.empty() ? device.label : device.physical_path);
        g_object_set_data_full(G_OBJECT(row), "storage-facts", g_strdup(facts.c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(choices), row);
        ++available;
    }
    if (available == 0) {
        gtk_box_append(GTK_BOX(page), label(
            "No eligible storage disk is visible. Lazarus now checks physical storage controllers during boot; restart with the updated live image if this remains empty.",
            "empty-state"));
    } else {
        gtk_box_append(GTK_BOX(page), choice_scroll(choices, 210));
    }

    GtkWidget* selected_facts = label("No storage disk selected.", "instruction");
    gtk_box_append(GTK_BOX(page), selected_facts);

    const char* modes[] = {
        "Use Existing Filesystem - Keep Current Data",
        "Format Entire Drive as Lazarus Storage - Erase All Data",
        nullptr
    };
    GtkWidget* mode = gtk_drop_down_new_from_strings(modes);
    gtk_box_append(GTK_BOX(page), label("2. Choose how Lazarus should prepare it", "section-title"));
    gtk_box_append(GTK_BOX(page), mode);

    gtk_box_append(GTK_BOX(page), label("3. Choose the image folder", "section-title"));
    gtk_box_append(GTK_BOX(page), label(
        "Enter a folder relative to the selected disk, or browse its existing folders. Lazarus creates the folder when needed.",
        "section-detail"));
    GtkWidget* folder_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* folder = gtk_entry_new();
    gtk_widget_set_hexpand(folder, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(folder), "images or Backups/Lazarus");
    gtk_editable_set_text(GTK_EDITABLE(folder), "images");
    GtkWidget* browse = gtk_button_new_with_label("Browse Folders...");
    lazarus::gui::add_class(browse, "secondary-command");
    gtk_widget_set_sensitive(browse, FALSE);
    gtk_box_append(GTK_BOX(folder_row), folder);
    gtk_box_append(GTK_BOX(folder_row), browse);
    gtk_box_append(GTK_BOX(page), folder_row);

    GtkWidget* browser_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(browser_panel, "workflow-section");
    gtk_widget_set_visible(browser_panel, FALSE);
    GtkWidget* browser_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* browser_up = gtk_button_new_with_label("Up");
    lazarus::gui::add_class(browser_up, "secondary-command");
    gtk_widget_set_sensitive(browser_up, FALSE);
    GtkWidget* browser_path = label("Selected disk /", "section-title");
    gtk_widget_set_hexpand(browser_path, TRUE);
    GtkWidget* browser_close = gtk_button_new_with_label("Close");
    lazarus::gui::add_class(browser_close, "secondary-command");
    gtk_box_append(GTK_BOX(browser_header), browser_up);
    gtk_box_append(GTK_BOX(browser_header), browser_path);
    gtk_box_append(GTK_BOX(browser_header), browser_close);
    gtk_box_append(GTK_BOX(browser_panel), browser_header);
    GtkWidget* browser_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(browser_list), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(browser_list, "choice-list");
    gtk_box_append(GTK_BOX(browser_panel), choice_scroll(browser_list, 190));
    GtkWidget* create_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* new_folder = gtk_entry_new();
    gtk_widget_set_hexpand(new_folder, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(new_folder), "New folder name");
    GtkWidget* create_folder = gtk_button_new_with_label("Create and Open");
    lazarus::gui::add_class(create_folder, "secondary-command");
    gtk_widget_set_sensitive(create_folder, FALSE);
    gtk_box_append(GTK_BOX(create_row), new_folder);
    gtk_box_append(GTK_BOX(create_row), create_folder);
    gtk_box_append(GTK_BOX(browser_panel), create_row);
    GtkWidget* browser_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* browser_open = gtk_button_new_with_label("Open Selected Folder");
    lazarus::gui::add_class(browser_open, "secondary-command");
    gtk_widget_set_sensitive(browser_open, FALSE);
    GtkWidget* browser_use = gtk_button_new_with_label("Use Current Folder");
    lazarus::gui::add_class(browser_use, "primary-command");
    gtk_widget_set_sensitive(browser_use, FALSE);
    gtk_box_append(GTK_BOX(browser_actions), browser_open);
    gtk_box_append(GTK_BOX(browser_actions), browser_use);
    gtk_box_append(GTK_BOX(browser_panel), browser_actions);
    gtk_box_append(GTK_BOX(page), browser_panel);

    GtkWidget* destructive_warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    lazarus::gui::add_class(destructive_warning, "warning-panel");
    gtk_box_append(GTK_BOX(destructive_warning), label("FORMATTING ERASES THE ENTIRE SELECTED PHYSICAL DISK", "warning-text"));
    gtk_box_append(GTK_BOX(destructive_warning), label(
        "Every existing partition, filesystem, and file on that disk will be removed. Other connected disks are not modified.",
        "choice-detail"));
    gtk_widget_set_visible(destructive_warning, FALSE);
    gtk_box_append(GTK_BOX(page), destructive_warning);
    GtkWidget* confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Type ERASE to format the selected physical disk");
    gtk_widget_set_visible(confirmation, FALSE);
    gtk_box_append(GTK_BOX(page), confirmation);

    auto* operation = add_operation_ui(app, page, adding_extra ? "Add Storage Location" : "Configure Image Storage");
    gtk_widget_set_sensitive(operation->button, FALSE);
    auto* binding = new StorageBinding;
    binding->app = app;
    binding->operation = operation;
    binding->mode = mode;
    binding->confirmation = confirmation;
    binding->selected_facts = selected_facts;
    binding->destructive_warning = destructive_warning;
    binding->folder = folder;
    binding->browse = browse;
    binding->browser_panel = browser_panel;
    binding->browser_path = browser_path;
    binding->browser_list = browser_list;
    binding->browser_up = browser_up;
    binding->browser_open = browser_open;
    binding->browser_use = browser_use;
    binding->new_folder = new_folder;
    binding->create_folder = create_folder;
    binding->location_name = location_name;
    binding->set_default = set_default;
    binding->adding_extra = adding_extra;
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const char* selector = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        values->selector = selector == nullptr ? "" : selector;
        values->browser_relative.clear();
        gtk_widget_set_visible(values->browser_panel, FALSE);
        const char* facts = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "storage-facts"));
        gtk_label_set_text(GTK_LABEL(values->selected_facts), facts == nullptr ? "No storage disk selected." : facts);
        update_storage_action(values);
    }), binding);
    g_signal_connect(mode, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        update_storage_action(static_cast<StorageBinding*>(data));
    }), binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_storage_action(static_cast<StorageBinding*>(data));
    }), binding);
    g_signal_connect(folder, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_storage_action(static_cast<StorageBinding*>(data));
    }), binding);
    g_signal_connect(browse, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        storage_list_directory(values, "");
    })), binding);
    g_signal_connect(browser_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        gtk_widget_set_sensitive(values->browser_open,
            row != nullptr && g_object_get_data(G_OBJECT(row), "relative") != nullptr);
    }), binding);
    g_signal_connect(browser_open, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        auto* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(values->browser_list));
        if (row == nullptr) return;
        const auto* relative = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "relative"));
        if (relative != nullptr) storage_list_directory(values, relative);
    }), binding);
    g_signal_connect(browser_list, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        const auto* relative = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "relative"));
        if (relative != nullptr) storage_list_directory(static_cast<StorageBinding*>(data), relative);
    }), binding);
    g_signal_connect(browser_up, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const auto parent = std::filesystem::path(values->browser_relative).parent_path().generic_string();
        storage_list_directory(values, parent);
    }), binding);
    g_signal_connect(new_folder, "changed", G_CALLBACK(+[](GtkEditable* entry, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const std::string name = gtk_editable_get_text(entry);
        gtk_widget_set_sensitive(values->create_folder,
            !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos);
    }), binding);
    g_signal_connect(create_folder, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const std::string name = gtk_editable_get_text(GTK_EDITABLE(values->new_folder));
        if (name.empty()) return;
        std::string response;
        const auto payload = "{\"command\":\"storage_create_folder\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"selector\":" + quote_json(values->selector) +
            ",\"relative_path\":" + quote_json(values->browser_relative) +
            ",\"name\":" + quote_json(name) + "}";
        if (!request(payload, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            set_status(values->app, error.empty() ? "The folder could not be created." : error);
            return;
        }
        gtk_editable_set_text(GTK_EDITABLE(values->new_folder), "");
        set_status(values->app, json_string(response, "message"));
        storage_list_directory(values, json_string(response, "relative_path"));
    }), binding);
    g_signal_connect(browser_use, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const auto chosen = values->browser_relative.empty() ? std::string{"images"} : values->browser_relative;
        gtk_editable_set_text(GTK_EDITABLE(values->folder), chosen.c_str());
        gtk_widget_set_visible(values->browser_panel, FALSE);
        set_status(values->app, values->browser_relative.empty()
            ? "The disk root was selected; Lazarus will use its images folder."
            : "Image folder selected: " + values->browser_relative);
    }), binding);
    g_signal_connect(browser_close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        gtk_widget_set_visible(static_cast<StorageBinding*>(data)->browser_panel, FALSE);
    }), binding);
    g_signal_connect(operation->button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<StorageBinding*>(data);
        const bool erase = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->mode)) == 1;
        const std::string directory = gtk_editable_get_text(GTK_EDITABLE(values->folder));
        auto request_json = "{\"command\":\"" + std::string(values->adding_extra ? "add_local_storage_location" : "configure_image_storage") +
            "\",\"admin_token\":" + quote_json(values->app->admin_token) + ",\"selector\":" + quote_json(values->selector) +
            ",\"mode\":" + quote_json(erase ? "format" : "existing") +
            ",\"directory\":" + quote_json(directory) +
            ",\"confirmation\":" + quote_json(erase ? "ERASE" : "");
        if (values->adding_extra) {
            request_json += ",\"name\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(values->location_name))) +
                ",\"set_default\":" + quote_json(gtk_check_button_get_active(GTK_CHECK_BUTTON(values->set_default)) ? "1" : "0");
        }
        request_json += "}";
        begin_service_job(values->operation, request_json,
                          values->adding_extra ? "The additional storage location was prepared and mounted."
                                                : "Image storage is mounted and persistent.", {},
                          [values](bool ok, const std::string&) {
                              if (!ok) return;
                              load_data(values->app);
                              if (values->adding_extra) {
                                  values->app->adding_extra_storage_location = false;
                                  show_page(values->app, "admin-storage");
                              }
                              refresh_operational_pages(values->app);
                          });
    }), binding);
    return page;
}

void populate_port_label_rows(PortLabelsBinding* binding) {
    clear_children(binding->list);
    binding->entries.clear();
    binding->replaced_identities.clear();

    std::unordered_map<std::string, std::string> configured;
    for (const auto& [identity, friendly] : parse_port_labels(binding->app->labels_text)) {
        configured.insert_or_assign(identity, friendly);
    }

    std::unordered_set<std::string> connected;
    std::size_t displayed = 0;
    const auto add_row = [binding, &displayed](const std::string& identity, const std::string& friendly,
                                               const std::string& title, const std::string& detail,
                                               const std::string& role, bool online) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        lazarus::gui::add_class(row, "port-label-row");
        gtk_widget_set_margin_start(row, 14); gtk_widget_set_margin_end(row, 14);
        gtk_widget_set_margin_top(row, 12); gtk_widget_set_margin_bottom(row, 12);

        GtkWidget* icon = lazarus::gui::make_icon(online ? "usb" : "hdd", 34);
        gtk_widget_set_valign(icon, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row), icon);

        GtkWidget* facts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(facts, TRUE);
        gtk_box_append(GTK_BOX(facts), label(title, "choice-title"));
        gtk_box_append(GTK_BOX(facts), label(role, online ? "port-role" : "port-role-offline"));
        GtkWidget* detail_label = label(detail, "choice-detail");
        gtk_label_set_selectable(GTK_LABEL(detail_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(detail_label), 58);
        gtk_box_append(GTK_BOX(facts), detail_label);
        gtk_box_append(GTK_BOX(row), facts);

        GtkWidget* editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_size_request(editor, 290, -1);
        gtk_widget_set_valign(editor, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(editor), label("Technician-facing port name", "form-label"));
        GtkWidget* input = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(input), "Example: Left USB 3 Source");
        gtk_editable_set_text(GTK_EDITABLE(input), friendly.c_str());
        gtk_widget_set_tooltip_text(input, "Use the physical bench name printed beside this connection.");
        gtk_box_append(GTK_BOX(editor), input);
        gtk_box_append(GTK_BOX(row), editor);
        gtk_box_append(GTK_BOX(binding->list), row);

        binding->entries.push_back({identity, input});
        g_signal_connect(input, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* current = static_cast<PortLabelsBinding*>(data);
            gtk_label_set_text(GTK_LABEL(current->status), "Unsaved label changes.");
            gtk_widget_set_sensitive(current->save, TRUE);
        }), binding);
        ++displayed;
    };

    for (const auto& port : binding->app->ports) {
        if (port.system || port.identity.empty()) continue;
        connected.insert(port.identity);
        const auto found = configured.find(port.identity);
        const auto friendly = found == configured.end() ? port.label : found->second;
        const auto title = friendly.empty() ? port.identity : friendly;
        const auto role = port.role.empty() || port.role == "unknown"
            ? "Role: Unconfigured" : "Role: " + port.role;
        const auto detail = port.online
            ? "Connected disk: " + (port.model.empty() ? port.device_path : port.model) +
                " (" + human_bytes(port.size_bytes) + ")\nPhysical port: " + port.identity
            : "No disk connected\nPhysical port: " + port.identity;
        add_row(port.identity, friendly, title, detail, role, port.online);
    }

    for (const auto& [identity, friendly] : configured) {
        if (connected.contains(identity)) continue;
        add_row(identity, friendly, friendly, "Persistent connection: " + identity,
                "Not currently connected", false);
    }

    if (displayed == 0) {
        gtk_box_append(GTK_BOX(binding->list),
                       label("No configurable connection is detected. Connect a test drive, then select Re-scan Connections.",
                             "empty-state"));
        gtk_widget_set_sensitive(binding->save, FALSE);
    }
}

GtkWidget* make_labels_page(App* app) {
    GtkWidget* page = page_shell(app, "BENCH CALIBRATION", "Name Physical Connections",
                                 "Give each dock or USB connection the same short name printed on the bench.", "admin");

    GtkWidget* guide = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    lazarus::gui::add_class(guide, "calibration-guide");
    const char* steps[] = {
        "1  Connect a test drive to the port",
        "2  Re-scan and identify the new connection",
        "3  Enter its bench name and save"
    };
    for (const char* step : steps) {
        GtkWidget* item = label(step, "calibration-step");
        gtk_widget_set_hexpand(item, TRUE);
        gtk_box_append(GTK_BOX(guide), item);
    }
    gtk_box_append(GTK_BOX(page), guide);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* toolbar_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(toolbar_copy, TRUE);
    gtk_box_append(GTK_BOX(toolbar_copy), label("Detected bench connections", "section-title"));
    gtk_box_append(GTK_BOX(toolbar_copy), label("The Linux device name may change. Lazarus saves the persistent hardware path shown below.", "section-detail"));
    GtkWidget* refresh = gtk_button_new_with_label("Re-scan Connections");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_widget_set_valign(refresh, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(toolbar), toolbar_copy);
    gtk_box_append(GTK_BOX(toolbar), refresh);
    gtk_box_append(GTK_BOX(page), toolbar);

    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    lazarus::gui::add_class(list, "port-label-list");
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(page), scroll);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* status = label("Labels match the saved bench profile.", "instruction");
    gtk_widget_set_hexpand(status, TRUE);
    GtkWidget* save = gtk_button_new_with_label("Save Port Names");
    lazarus::gui::add_class(save, "primary-command");
    gtk_widget_set_sensitive(save, FALSE);
    gtk_box_append(GTK_BOX(actions), status);
    gtk_box_append(GTK_BOX(actions), save);
    gtk_box_append(GTK_BOX(page), actions);

    auto* binding = new PortLabelsBinding{app, list, status, save, {}, {}};
    populate_port_label_rows(binding);

    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* current = static_cast<PortLabelsBinding*>(data);
        gtk_label_set_text(GTK_LABEL(current->status), "Scanning connected storage devices...");
        load_data(current->app);
        populate_port_label_rows(current);
        gtk_label_set_text(GTK_LABEL(current->status), "Connection list refreshed.");
        gtk_widget_set_sensitive(current->save, FALSE);
    }), binding);

    g_signal_connect(save, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* current = static_cast<PortLabelsBinding*>(data);
        std::unordered_set<std::string> visible_identities;
        std::unordered_map<std::string, std::string> used_names;
        std::string serialized;

        for (const auto& entry : current->entries) {
            visible_identities.insert(entry.identity);
            const auto friendly = trimmed(gtk_editable_get_text(GTK_EDITABLE(entry.input)));
            if (friendly.empty()) continue;
            if (friendly.find('|') != std::string::npos) {
                gtk_label_set_text(GTK_LABEL(current->status), "Port names cannot contain the | character.");
                gtk_widget_grab_focus(entry.input);
                return;
            }
            const auto normalized = lower_ascii(friendly);
            if (const auto duplicate = used_names.find(normalized); duplicate != used_names.end()) {
                gtk_label_set_text(GTK_LABEL(current->status), "Every physical connection needs a unique port name.");
                gtk_widget_grab_focus(entry.input);
                return;
            }
            used_names.emplace(normalized, entry.identity);
            serialized += entry.identity + "|" + friendly + "\n";
        }

        for (const auto& [identity, friendly] : parse_port_labels(current->app->labels_text)) {
            if (!visible_identities.contains(identity) && !current->replaced_identities.contains(identity)) {
                serialized += identity + "|" + friendly + "\n";
            }
        }

        gtk_label_set_text(GTK_LABEL(current->status), "Saving physical port names...");
        if (save_profile(current->app, current->app->source_text, current->app->destination_text,
                         current->app->storage_text, serialized)) {
            gtk_label_set_text(GTK_LABEL(current->status), "Physical port names saved and active.");
            gtk_widget_set_sensitive(current->save, FALSE);
        }
    })), binding);
    return page;
}

struct BrandingSaveBinding {
    App* app = nullptr;
    GtkWidget* product_name = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* save = nullptr;
    bool saving = false;
};

struct BrandingSaveJob {
    BrandingSaveBinding* binding = nullptr;
    std::string product_name;
    std::string request_json;
    std::string response;
    bool transport_ok = false;
};

struct BenchNameSaveBinding {
    App* app = nullptr;
    GtkWidget* bench_name = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* save = nullptr;
    bool saving = false;
};

struct BenchNameSaveJob {
    BenchNameSaveBinding* binding = nullptr;
    std::string bench_name;
    std::string request_json;
    std::string response;
    bool transport_ok = false;
};

GtkWidget* make_branding_page(App* app) {
    GtkWidget* page = page_shell(app, "BRANDING", "Branding and Reports",
                                 "Set the product name used in the kiosk header and technician reports.", "admin");

    GtkWidget* identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(identity, "workflow-section");
    gtk_box_append(GTK_BOX(identity), label("Bench identity", "section-title"));
    gtk_box_append(GTK_BOX(identity), label(
        "Name this physical bench so technicians can tell appliances apart. This name is required before storage, "
        "port, or label changes can be saved.", "section-detail"));

    GtkWidget* bench_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(bench_entry), "Bench 3");
    gtk_entry_set_max_length(GTK_ENTRY(bench_entry), 120);
    gtk_editable_set_text(GTK_EDITABLE(bench_entry), app->bench_name.c_str());
    GtkWidget* bench_save_status = label(
        trimmed(app->bench_name).empty() ? "A bench name is required." : "No unsaved bench name changes.",
        "instruction");
    GtkWidget* bench_save = gtk_button_new_with_label("Save Bench Name");
    lazarus::gui::add_class(bench_save, "primary-command");
    gtk_widget_set_sensitive(bench_save, !trimmed(app->bench_name).empty());

    gtk_box_append(GTK_BOX(identity), label("Bench name", "instruction"));
    gtk_box_append(GTK_BOX(identity), bench_entry);
    gtk_box_append(GTK_BOX(identity), bench_save_status);
    gtk_box_append(GTK_BOX(identity), bench_save);
    gtk_box_append(GTK_BOX(page), identity);

    auto* bench_binding = new BenchNameSaveBinding{app, bench_entry, bench_save_status, bench_save, false};
    g_signal_connect(bench_entry, "changed", G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
        auto* values = static_cast<BenchNameSaveBinding*>(data);
        if (values->saving) return;
        const auto bench_name = trimmed(gtk_editable_get_text(editable));
        gtk_widget_set_sensitive(values->save, !bench_name.empty() && bench_name.size() <= 120);
        gtk_label_set_text(GTK_LABEL(values->status), bench_name.empty()
            ? "A bench name is required."
            : "Bench name has unsaved changes.");
    }), bench_binding);
    g_signal_connect(bench_save, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* values = static_cast<BenchNameSaveBinding*>(data);
        if (values->saving) return;
        const auto bench_name = trimmed(gtk_editable_get_text(GTK_EDITABLE(values->bench_name)));
        if (bench_name.empty() || bench_name.size() > 120 ||
            bench_name.find_first_of("\r\n") != std::string::npos) {
            gtk_label_set_text(GTK_LABEL(values->status), "Enter a bench name between 1 and 120 characters on one line.");
            return;
        }

        values->saving = true;
        gtk_widget_set_sensitive(values->save, FALSE);
        gtk_widget_set_sensitive(values->bench_name, FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), "Saving bench name to persistent appliance storage...");
        set_status(values->app, "Saving bench name. The kiosk remains available.");

        auto* job = new BenchNameSaveJob{
            values,
            bench_name,
            profile_save_request(values->app, values->app->source_text, values->app->destination_text,
                                 values->app->storage_text, values->app->labels_text, {}, bench_name),
            {}, false};
        std::thread([job]() {
            job->transport_ok = request(job->request_json, job->response);
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* result = static_cast<BenchNameSaveJob*>(data);
                auto* current = result->binding;
                const bool saved = result->transport_ok && result->response.find("\"ok\":true") != std::string::npos;
                current->saving = false;
                gtk_widget_set_sensitive(current->bench_name, TRUE);
                gtk_widget_set_sensitive(current->save, TRUE);
                if (!saved) {
                    const auto error = json_string(result->response, "error");
                    const auto message = error.empty()
                        ? "Bench name was not saved because the Lazarus service did not return a complete response."
                        : "Bench name was not saved: " + error;
                    gtk_label_set_text(GTK_LABEL(current->status), message.c_str());
                    set_status(current->app, message);
                    if (error.find("authentication") != std::string::npos || error.find("expired") != std::string::npos) {
                        current->app->admin_token.clear();
                    }
                    delete result;
                    return G_SOURCE_REMOVE;
                }

                current->app->bench_name = result->bench_name;
                gtk_label_set_text(GTK_LABEL(current->status), "Bench name saved to persistent appliance storage.");
                set_status(current->app, "Bench name saved.");
                delete result;
                return G_SOURCE_REMOVE;
            }, job);
        }).detach();
    })), bench_binding);

    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(panel, "workflow-section");
    gtk_box_append(GTK_BOX(panel), label("Product identity", "section-title"));
    gtk_box_append(GTK_BOX(panel), label("Use a short workplace or appliance name. Saving never interrupts imaging or restarts the kiosk.", "section-detail"));

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Arcology Lazarus");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 80);
    gtk_editable_set_text(GTK_EDITABLE(entry), app->branding_product_name.c_str());
    GtkWidget* save_status = label("No unsaved branding changes.", "instruction");
    GtkWidget* save = gtk_button_new_with_label("Save Branding");
    lazarus::gui::add_class(save, "primary-command");
    gtk_widget_set_sensitive(save, !trimmed(app->branding_product_name).empty());

    gtk_box_append(GTK_BOX(panel), label("Product name", "instruction"));
    gtk_box_append(GTK_BOX(panel), entry);
    gtk_box_append(GTK_BOX(panel), save_status);
    gtk_box_append(GTK_BOX(panel), save);
    gtk_box_append(GTK_BOX(page), panel);

    auto* binding = new BrandingSaveBinding{app, entry, save_status, save, false};
    g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
        auto* values = static_cast<BrandingSaveBinding*>(data);
        if (values->saving) return;
        const auto product_name = trimmed(gtk_editable_get_text(editable));
        gtk_widget_set_sensitive(values->save, !product_name.empty() && product_name.size() <= 80);
        gtk_label_set_text(GTK_LABEL(values->status), product_name.empty()
            ? "A product name is required."
            : "Branding has unsaved changes.");
    }), binding);
    g_signal_connect(save, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* values = static_cast<BrandingSaveBinding*>(data);
        if (values->saving) return;
        const auto product_name = trimmed(gtk_editable_get_text(GTK_EDITABLE(values->product_name)));
        if (product_name.empty() || product_name.size() > 80 ||
            product_name.find_first_of("\r\n") != std::string::npos) {
            gtk_label_set_text(GTK_LABEL(values->status), "Enter a product name between 1 and 80 characters on one line.");
            return;
        }

        values->saving = true;
        gtk_widget_set_sensitive(values->save, FALSE);
        gtk_widget_set_sensitive(values->product_name, FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), "Saving branding to persistent appliance storage...");
        set_status(values->app, "Saving branding. The kiosk remains available.");

        auto* job = new BrandingSaveJob{
            values,
            product_name,
            profile_save_request(values->app, values->app->source_text, values->app->destination_text,
                                 values->app->storage_text, values->app->labels_text, product_name),
            {}, false};
        std::thread([job]() {
            job->transport_ok = request(job->request_json, job->response);
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* result = static_cast<BrandingSaveJob*>(data);
                auto* current = result->binding;
                const bool saved = result->transport_ok && result->response.find("\"ok\":true") != std::string::npos;
                current->saving = false;
                gtk_widget_set_sensitive(current->product_name, TRUE);
                gtk_widget_set_sensitive(current->save, TRUE);
                if (!saved) {
                    const auto error = json_string(result->response, "error");
                    const auto message = error.empty()
                        ? "Branding was not saved because the Lazarus service did not return a complete response."
                        : "Branding was not saved: " + error;
                    gtk_label_set_text(GTK_LABEL(current->status), message.c_str());
                    set_status(current->app, message);
                    if (error.find("authentication") != std::string::npos || error.find("expired") != std::string::npos) {
                        current->app->admin_token.clear();
                    }
                    delete result;
                    return G_SOURCE_REMOVE;
                }

                current->app->branding_product_name = result->product_name;
                gtk_label_set_text(GTK_LABEL(current->status), "Branding saved to persistent appliance storage.");
                set_status(current->app, "Branding saved. New reports will use the updated product name.");
                delete result;
                return G_SOURCE_REMOVE;
            }, job);
        }).detach();
    })), binding);
    return page;
}

void update_install_button(InstallBinding* binding) {
    const bool confirmed = std::string{gtk_editable_get_text(GTK_EDITABLE(binding->confirmation))} == "ERASE";
    gtk_widget_set_sensitive(binding->button, confirmed && !binding->app->selected_device.empty());
}

GtkWidget* make_install_page(App* app) {
    GtkWidget* page = page_shell(app, "APPLIANCE INSTALLER", "Install Lazarus OS",
                                 "Install the live system to one writable disk, then reboot into the persistent kiosk.", "admin");
    gtk_box_append(GTK_BOX(page), label("Select the appliance system disk. Source-only, image-storage, ignored, and running system disks are blocked.", "instruction"));

    GtkWidget* choices = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(choices), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(choices, "choice-list");
    std::size_t eligible = 0;
    for (const auto& device : app->devices) {
        if (device.system || device.role == "source-only" || device.role == "image-storage" ||
            device.role == "removable-media" || device.role == "ignored") continue;
        GtkWidget* row = device_choice(app, device, device.role.c_str());
        gtk_widget_set_sensitive(row, TRUE);
        gtk_list_box_append(GTK_LIST_BOX(choices), row);
        ++eligible;
    }
    if (eligible == 0) {
        gtk_box_append(GTK_BOX(page), label("No writable installation target is connected. Attach a blank disk or assign one as a destination in Administration.", "empty-state"));
        return page;
    }
    gtk_box_append(GTK_BOX(page), choices);

    GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    lazarus::gui::add_class(warning, "warning-panel");
    gtk_box_append(GTK_BOX(warning), label("The selected disk will be completely erased", "choice-title"));
    gtk_box_append(GTK_BOX(warning), label("The installer creates UEFI, system, and persistent-state partitions. No other disk is modified.", "choice-detail"));
    gtk_box_append(GTK_BOX(page), warning);

    GtkWidget* confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirmation), "Type ERASE to enable installation");
    gtk_box_append(GTK_BOX(page), confirmation);
    GtkWidget* progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), "Waiting for target selection");
    gtk_box_append(GTK_BOX(page), progress);
    GtkWidget* install_status = label("Installation has not started.", "instruction");
    gtk_box_append(GTK_BOX(page), install_status);
    GtkWidget* install = gtk_button_new_with_label("Erase Disk and Install Lazarus OS");
    lazarus::gui::add_class(install, "primary-command");
    gtk_widget_set_sensitive(install, FALSE);
    gtk_box_append(GTK_BOX(page), install);

    auto* binding = new InstallBinding{app, install, confirmation, install_status, progress};
    g_signal_connect(choices, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<InstallBinding*>(data);
        values->app->selected_device = row == nullptr ? "" : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "selector"));
        update_install_button(values);
    }), binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_install_button(static_cast<InstallBinding*>(data));
    }), binding);
    g_signal_connect(install, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
        auto* values = static_cast<InstallBinding*>(data);
        gtk_widget_set_sensitive(values->button, FALSE);
        gtk_widget_set_sensitive(values->confirmation, FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), "Starting now. The service is preparing the selected disk.");
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(values->progress), "Starting installer");
        auto* job = new InstallJob{};
        job->binding = values;
        job->selector = values->app->selected_device;
        job->pulse_source = g_timeout_add(120, +[](gpointer data) -> gboolean {
            auto* current = static_cast<InstallJob*>(data);
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(current->binding->progress));
            return G_SOURCE_CONTINUE;
        }, job);
        g_timeout_add_once(350, +[](gpointer data) {
            auto* current = static_cast<InstallJob*>(data);
            std::thread([current]() {
                const std::string payload = "{\"command\":\"install_os\",\"admin_token\":" +
                    quote_json(current->binding->app->admin_token) + ",\"selector\":" + quote_json(current->selector) +
                    ",\"confirmation\":\"ERASE\"}";
                current->transport_ok = request_complete(payload, current->response);
                current->done.store(true);
                g_idle_add(+[](gpointer result_data) -> gboolean {
                    auto* result = static_cast<InstallJob*>(result_data);
                    if (result->pulse_source != 0) {
                        g_source_remove(result->pulse_source);
                        result->pulse_source = 0;
                    }
                    const bool installed = result->transport_ok && result->response.find("\"ok\":true") != std::string::npos;
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(result->binding->progress), installed ? 1.0 : 0.0);
                    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(result->binding->progress), installed ? "Installation complete" : "Installation failed");
                    gtk_label_set_text(GTK_LABEL(result->binding->status), installed
                        ? "Lazarus OS was installed. Shut down, remove the live USB, and boot from the installed disk."
                        : "Installation did not complete. The selected disk must not be used until the failure is reviewed.");
                    if (!installed) {
                        gtk_widget_set_sensitive(result->binding->confirmation, TRUE);
                        update_install_button(result->binding);
                    }
                    delete result;
                    return G_SOURCE_REMOVE;
                }, current);
            }).detach();
        }, job);
    })), binding);
    return page;
}

GtkWidget* password_entry(const char* placeholder) {
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
    return entry;
}

struct AdminLoginBinding {
    App* app;
    GtkWidget* credential;
    GtkWidget* recovery_mode;
    GtkWidget* status;
    GtkWidget* button;
};

struct AdminSetupBinding {
    App* app;
    GtkWidget* password;
    GtkWidget* confirmation;
    GtkWidget* status;
    GtkWidget* setup_panel;
    GtkWidget* recovery_panel;
    GtkWidget* recovery_label;
    GtkWidget* acknowledgement;
    GtkWidget* continue_button;
};

GtkWidget* make_admin_lock_page(App* app) {
    GtkWidget* page = page_shell(app, "SECURE AREA", "Administration Locked",
                                 "Bench policy, branding, storage, installation, and security settings require administrator authentication.");
    std::string status_response;
    const bool status_ok = request("{\"command\":\"admin_status\"}", status_response);
    const bool configured = status_ok && json_bool(status_response, "configured");
    const bool healthy = status_ok && json_bool(status_response, "healthy");

    if (!status_ok) {
        gtk_box_append(GTK_BOX(page), label("The Lazarus service did not answer the administration status request.", "empty-state"));
        return page;
    }
    if (configured && !healthy) {
        GtkWidget* warning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        lazarus::gui::add_class(warning, "warning-panel");
        gtk_box_append(GTK_BOX(warning), label("Administrator credentials are unreadable", "warning-text"));
        gtk_box_append(GTK_BOX(warning), label("Lazarus will not replace a damaged credential file automatically. Recover the appliance state from trusted media or service the credential file offline.", "choice-detail"));
        gtk_box_append(GTK_BOX(page), warning);
        return page;
    }

    if (!configured) {
        GtkWidget* setup = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        lazarus::gui::add_class(setup, "workflow-section");
        gtk_box_append(GTK_BOX(setup), label("Create the appliance administrator password", "section-title"));
        gtk_box_append(GTK_BOX(setup), label("Enter any non-empty password. Lazarus will generate a unique recovery key after setup.", "section-detail"));
        GtkWidget* password = password_entry("New administrator password");
        GtkWidget* confirmation = password_entry("Confirm administrator password");
        GtkWidget* setup_status = label("No administrator credentials exist on this appliance yet.", "instruction");
        GtkWidget* create = gtk_button_new_with_label("Create Administrator Credentials");
        lazarus::gui::add_class(create, "primary-command");
        gtk_widget_set_sensitive(create, FALSE);
        gtk_box_append(GTK_BOX(setup), password);
        gtk_box_append(GTK_BOX(setup), confirmation);
        gtk_box_append(GTK_BOX(setup), setup_status);
        gtk_box_append(GTK_BOX(setup), create);
        gtk_box_append(GTK_BOX(page), setup);

        GtkWidget* recovery_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        lazarus::gui::add_class(recovery_panel, "recovery-panel");
        gtk_box_append(GTK_BOX(recovery_panel), label("Record the appliance recovery key now", "section-title"));
        gtk_box_append(GTK_BOX(recovery_panel), label("This key is unique to this Lazarus appliance and will not be displayed again. Store it outside the appliance.", "section-detail"));
        GtkWidget* recovery_label = label("", "recovery-code");
        gtk_label_set_selectable(GTK_LABEL(recovery_label), TRUE);
        gtk_box_append(GTK_BOX(recovery_panel), recovery_label);
        GtkWidget* acknowledgement = gtk_check_button_new_with_label("I stored this recovery key securely");
        GtkWidget* continue_button = gtk_button_new_with_label("Continue to Administration");
        lazarus::gui::add_class(continue_button, "primary-command");
        gtk_widget_set_sensitive(continue_button, FALSE);
        gtk_box_append(GTK_BOX(recovery_panel), acknowledgement);
        gtk_box_append(GTK_BOX(recovery_panel), continue_button);
        gtk_widget_set_visible(recovery_panel, FALSE);
        gtk_box_append(GTK_BOX(page), recovery_panel);

        auto* binding = new AdminSetupBinding{app, password, confirmation, setup_status, setup,
                                               recovery_panel, recovery_label, acknowledgement, continue_button};
        const auto update_setup = +[](GtkEditable*, gpointer data) {
            auto* values = static_cast<AdminSetupBinding*>(data);
            const std::string password = gtk_editable_get_text(GTK_EDITABLE(values->password));
            const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(values->confirmation));
            gtk_widget_set_sensitive(static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(values->password), "setup-button")),
                                     !password.empty() && password == confirmation);
        };
        g_object_set_data(G_OBJECT(password), "setup-button", create);
        g_signal_connect(password, "changed", G_CALLBACK(update_setup), binding);
        g_signal_connect(confirmation, "changed", G_CALLBACK(update_setup), binding);
        g_signal_connect(create, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* values = static_cast<AdminSetupBinding*>(data);
            const std::string password = gtk_editable_get_text(GTK_EDITABLE(values->password));
            const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(values->confirmation));
            if (password.empty() || password != confirmation) {
                gtk_label_set_text(GTK_LABEL(values->status), "Passwords must match and cannot be empty.");
                return;
            }
            std::string response;
            if (!request("{\"command\":\"admin_setup\",\"new_password\":" + quote_json(password) + "}", response) ||
                response.find("\"ok\":true") == std::string::npos) {
                const auto error = json_string(response, "error");
                gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Administrator setup failed." : error.c_str());
                return;
            }
            values->app->admin_token = json_string(response, "token");
            gtk_label_set_text(GTK_LABEL(values->recovery_label), json_string(response, "recovery_key").c_str());
            gtk_widget_set_visible(values->setup_panel, FALSE);
            gtk_widget_set_visible(values->recovery_panel, TRUE);
            set_status(values->app, "Administrator credentials created. Record the recovery key before continuing.");
        }), binding);
        g_signal_connect(acknowledgement, "toggled", G_CALLBACK(+[](GtkCheckButton* check, gpointer data) {
            auto* values = static_cast<AdminSetupBinding*>(data);
            gtk_widget_set_sensitive(values->continue_button, gtk_check_button_get_active(check));
        }), binding);
        g_signal_connect(continue_button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* values = static_cast<AdminSetupBinding*>(data);
            show_page(values->app, "admin");
        }), binding);
        return page;
    }

    GtkWidget* login = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(login, "workflow-section");
    gtk_box_append(GTK_BOX(login), label("Unlock Administration", "section-title"));
    gtk_box_append(GTK_BOX(login), label("The service limits repeated failed attempts and expires unlocked sessions after 15 minutes.", "section-detail"));
    GtkWidget* credential = password_entry("Administrator password");
    GtkWidget* recovery_mode = gtk_check_button_new_with_label("Use the appliance recovery key");
    GtkWidget* login_status = label("Enter the administrator password.", "instruction");
    GtkWidget* unlock = gtk_button_new_with_label("Unlock Administration");
    lazarus::gui::add_class(unlock, "primary-command");
    gtk_widget_set_sensitive(unlock, FALSE);
    gtk_box_append(GTK_BOX(login), credential);
    gtk_box_append(GTK_BOX(login), recovery_mode);
    gtk_box_append(GTK_BOX(login), login_status);
    gtk_box_append(GTK_BOX(login), unlock);
    gtk_box_append(GTK_BOX(page), login);
    auto* binding = new AdminLoginBinding{app, credential, recovery_mode, login_status, unlock};
    g_signal_connect(credential, "changed", G_CALLBACK(+[](GtkEditable* entry, gpointer data) {
        auto* values = static_cast<AdminLoginBinding*>(data);
        gtk_widget_set_sensitive(values->button, *gtk_editable_get_text(entry) != '\0');
    }), binding);
    g_signal_connect(recovery_mode, "toggled", G_CALLBACK(+[](GtkCheckButton* check, gpointer data) {
        auto* values = static_cast<AdminLoginBinding*>(data);
        const bool recovery = gtk_check_button_get_active(check);
        gtk_entry_set_placeholder_text(GTK_ENTRY(values->credential), recovery ? "LAZ appliance recovery key" : "Administrator password");
        gtk_label_set_text(GTK_LABEL(values->status), recovery ? "Enter the recovery key recorded during setup." : "Enter the administrator password.");
        gtk_editable_set_text(GTK_EDITABLE(values->credential), "");
    }), binding);
    g_signal_connect(unlock, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<AdminLoginBinding*>(data);
        const bool recovery = gtk_check_button_get_active(GTK_CHECK_BUTTON(values->recovery_mode));
        const std::string credential = gtk_editable_get_text(GTK_EDITABLE(values->credential));
        const auto request_json = std::string{"{\"command\":\"admin_login\","} +
            (recovery ? "\"recovery_key\":" : "\"password\":") + quote_json(credential) + "}";
        std::string response;
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Administration could not be unlocked." : error.c_str());
            gtk_editable_set_text(GTK_EDITABLE(values->credential), "");
            return;
        }
        values->app->admin_token = json_string(response, "token");
        gtk_editable_set_text(GTK_EDITABLE(values->credential), "");
        set_status(values->app, recovery ? "Administration unlocked with the appliance recovery key." : "Administration unlocked.");
        show_page(values->app, "admin");
    }), binding);
    return page;
}

struct AdminSecurityBinding {
    App* app;
    GtkWidget* password;
    GtkWidget* confirmation;
    GtkWidget* status;
    GtkWidget* change_button;
};

GtkWidget* make_admin_security_page(App* app) {
    GtkWidget* page = page_shell(app, "ADMIN SECURITY", "Password and Recovery",
                                 "Change the administrator password or replace the appliance recovery key.", "admin");
    GtkWidget* password_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(password_panel, "workflow-section");
    gtk_box_append(GTK_BOX(password_panel), label("Change administrator password", "section-title"));
    gtk_box_append(GTK_BOX(password_panel), label("The new password can be any non-empty value.", "section-detail"));
    GtkWidget* password = password_entry("New administrator password");
    GtkWidget* confirmation = password_entry("Confirm new administrator password");
    GtkWidget* password_status = label("Password has not been changed.", "instruction");
    GtkWidget* change = gtk_button_new_with_label("Change Administrator Password");
    lazarus::gui::add_class(change, "primary-command");
    gtk_widget_set_sensitive(change, FALSE);
    gtk_box_append(GTK_BOX(password_panel), password);
    gtk_box_append(GTK_BOX(password_panel), confirmation);
    gtk_box_append(GTK_BOX(password_panel), password_status);
    gtk_box_append(GTK_BOX(password_panel), change);
    gtk_box_append(GTK_BOX(page), password_panel);
    auto* password_binding = new AdminSecurityBinding{app, password, confirmation, password_status, change};
    const auto update_change = +[](GtkEditable*, gpointer data) {
        auto* values = static_cast<AdminSecurityBinding*>(data);
        const std::string password = gtk_editable_get_text(GTK_EDITABLE(values->password));
        const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(values->confirmation));
        gtk_widget_set_sensitive(values->change_button, !password.empty() && password == confirmation);
    };
    g_signal_connect(password, "changed", G_CALLBACK(update_change), password_binding);
    g_signal_connect(confirmation, "changed", G_CALLBACK(update_change), password_binding);
    g_signal_connect(change, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<AdminSecurityBinding*>(data);
        const std::string password = gtk_editable_get_text(GTK_EDITABLE(values->password));
        std::string response;
        const auto request_json = "{\"command\":\"admin_change_password\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"new_password\":" + quote_json(password) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Password change failed." : error.c_str());
            return;
        }
        gtk_editable_set_text(GTK_EDITABLE(values->password), "");
        gtk_editable_set_text(GTK_EDITABLE(values->confirmation), "");
        gtk_label_set_text(GTK_LABEL(values->status), "Administrator password changed.");
        set_status(values->app, "Administrator password changed.");
    }), password_binding);

    GtkWidget* recovery_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(recovery_panel, "warning-panel");
    gtk_box_append(GTK_BOX(recovery_panel), label("Replace the recovery key", "warning-text"));
    gtk_box_append(GTK_BOX(recovery_panel), label("Rotating the key immediately invalidates the previous copy. Type ROTATE to continue.", "choice-detail"));
    GtkWidget* rotate_confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(rotate_confirmation), "Type ROTATE");
    GtkWidget* rotate = gtk_button_new_with_label("Generate New Recovery Key");
    lazarus::gui::add_class(rotate, "secondary-command");
    gtk_widget_set_sensitive(rotate, FALSE);
    GtkWidget* rotated_key = label("", "recovery-code");
    gtk_label_set_selectable(GTK_LABEL(rotated_key), TRUE);
    gtk_widget_set_visible(rotated_key, FALSE);
    gtk_box_append(GTK_BOX(recovery_panel), rotate_confirmation);
    gtk_box_append(GTK_BOX(recovery_panel), rotate);
    gtk_box_append(GTK_BOX(recovery_panel), rotated_key);
    gtk_box_append(GTK_BOX(page), recovery_panel);
    g_object_set_data(G_OBJECT(rotate), "confirmation", rotate_confirmation);
    g_object_set_data(G_OBJECT(rotate), "result", rotated_key);
    g_signal_connect(rotate_confirmation, "changed", G_CALLBACK(+[](GtkEditable* entry, gpointer data) {
        gtk_widget_set_sensitive(static_cast<GtkWidget*>(data), std::string{gtk_editable_get_text(entry)} == "ROTATE");
    }), rotate);
    g_signal_connect(rotate, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data);
        std::string response;
        if (!request("{\"command\":\"admin_rotate_recovery\",\"admin_token\":" + quote_json(state->admin_token) + "}", response) ||
            response.find("\"ok\":true") == std::string::npos) {
            set_status(state, json_string(response, "error"));
            return;
        }
        auto* result = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "result"));
        gtk_label_set_text(GTK_LABEL(result), json_string(response, "recovery_key").c_str());
        gtk_widget_set_visible(result, TRUE);
        gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(button), "confirmation")), "");
        set_status(state, "Recovery key rotated. Record the new key now; the previous key is invalid.");
    }), app);
    return page;
}

struct TechnicianBinding {
    App* app;
    GtkWidget* list;
    GtkWidget* status;
    GtkWidget* regenerate;
    GtkWidget* remove_confirmation;
    GtkWidget* remove;
    GtkWidget* pin_reveal;
    GtkWidget* assigned_pin;
    GtkWidget* assigned_pin_confirmation;
    GtkWidget* assign_pin;
    GtkWidget* pin_validation;
    std::string selected;
};

struct TechnicianAddBinding {
    GtkWidget* name = nullptr;
    GtkWidget* pin = nullptr;
    GtkWidget* confirmation = nullptr;
    GtkWidget* add = nullptr;
    GtkWidget* validation = nullptr;
};

bool valid_pin_entry(GtkWidget* entry) {
    const std::string pin = gtk_editable_get_text(GTK_EDITABLE(entry));
    return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

void update_technician_add(TechnicianAddBinding* binding) {
    const std::string name = gtk_editable_get_text(GTK_EDITABLE(binding->name));
    const std::string pin = gtk_editable_get_text(GTK_EDITABLE(binding->pin));
    const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(binding->confirmation));
    const bool ready = !name.empty() && valid_pin_entry(binding->pin) && pin == confirmation;
    gtk_widget_set_sensitive(binding->add, ready);
    const char* message = ready ? "Ready to add technician."
        : (name.empty() ? "Enter the technician's name."
        : (!valid_pin_entry(binding->pin) ? "PIN must be exactly four digits."
        : "PIN entries do not match."));
    gtk_label_set_text(GTK_LABEL(binding->validation), message);
}

void update_technician_actions(TechnicianBinding* binding) {
    const bool selected = !binding->selected.empty();
    gtk_widget_set_sensitive(binding->regenerate, selected);
    const std::string pin = gtk_editable_get_text(GTK_EDITABLE(binding->assigned_pin));
    const std::string confirmation = gtk_editable_get_text(GTK_EDITABLE(binding->assigned_pin_confirmation));
    gtk_widget_set_sensitive(binding->assign_pin,
                             selected && valid_pin_entry(binding->assigned_pin) && pin == confirmation);
    const char* pin_message = !selected ? "Select a technician first."
        : (pin.empty() && confirmation.empty() ? "Enter the designated PIN twice."
        : (!valid_pin_entry(binding->assigned_pin) ? "PIN must be exactly four digits."
        : (pin != confirmation ? "PIN entries do not match." : "Ready to save the designated PIN.")));
    gtk_label_set_text(GTK_LABEL(binding->pin_validation), pin_message);
    const bool confirmed = std::string{gtk_editable_get_text(GTK_EDITABLE(binding->remove_confirmation))} == "REMOVE";
    gtk_widget_set_sensitive(binding->remove, selected && confirmed);
}

void refresh_technicians(TechnicianBinding* binding) {
    std::string response;
    if (!request("{\"command\":\"technicians_list\"}", response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        gtk_label_set_text(GTK_LABEL(binding->status), error.empty() ? "Could not read the technician list." : error.c_str());
        return;
    }
    clear_children(binding->list);
    binding->selected.clear();
    for (const auto& name : split(json_string(response, "names_text"), '\n')) {
        if (name.empty()) continue;
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(content, 14); gtk_widget_set_margin_end(content, 14);
        gtk_widget_set_margin_top(content, 10); gtk_widget_set_margin_bottom(content, 10);
        gtk_box_append(GTK_BOX(content), label(name, "choice-title"));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        lazarus::gui::add_class(row, "choice-row");
        g_object_set_data_full(G_OBJECT(row), "technician-name", g_strdup(name.c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(binding->list), row);
    }
    if (gtk_widget_get_first_child(binding->list) == nullptr) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label("No technicians are configured yet.", "empty-state"));
        gtk_list_box_append(GTK_LIST_BOX(binding->list), row);
    }
    gtk_widget_set_visible(binding->pin_reveal, FALSE);
    gtk_label_set_text(GTK_LABEL(binding->status), "Select a technician to assign a known PIN, generate a random PIN, or remove them.");
    update_technician_actions(binding);
}

GtkWidget* make_technicians_page(App* app) {
    GtkWidget* page = page_shell(app, "TECHNICIANS", "Technicians",
                                 "Manage the names and PINs technicians use to sign in to Backup, Restore, and Verify jobs.", "admin");

    GtkWidget* add_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(add_panel, "workflow-section");
    gtk_box_append(GTK_BOX(add_panel), label("Add a technician", "section-title"));
    gtk_box_append(GTK_BOX(add_panel), label(
        "Assign the same 4-digit PIN on each appliance when a technician needs consistent bench access. Only a salted hash is stored.",
        "section-detail"));
    GtkWidget* name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Technician name");
    GtkWidget* add_pin_entry = password_entry("Assigned 4-digit PIN");
    GtkWidget* add_pin_confirmation = password_entry("Confirm assigned PIN");
    gtk_entry_set_max_length(GTK_ENTRY(add_pin_entry), 4);
    gtk_entry_set_max_length(GTK_ENTRY(add_pin_confirmation), 4);
    gtk_entry_set_input_purpose(GTK_ENTRY(add_pin_entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_input_purpose(GTK_ENTRY(add_pin_confirmation), GTK_INPUT_PURPOSE_DIGITS);
    GtkWidget* add = gtk_button_new_with_label("Add Technician");
    lazarus::gui::add_class(add, "primary-command");
    gtk_widget_set_sensitive(add, FALSE);
    GtkWidget* add_pin = label("", "recovery-code");
    gtk_label_set_selectable(GTK_LABEL(add_pin), TRUE);
    gtk_widget_set_visible(add_pin, FALSE);
    GtkWidget* show_add_pin = gtk_check_button_new_with_label("Show PIN while entering");
    GtkWidget* add_validation = label("Enter the technician's name.", "instruction");
    gtk_box_append(GTK_BOX(add_panel), name_entry);
    gtk_box_append(GTK_BOX(add_panel), add_pin_entry);
    gtk_box_append(GTK_BOX(add_panel), add_pin_confirmation);
    gtk_box_append(GTK_BOX(add_panel), show_add_pin);
    gtk_box_append(GTK_BOX(add_panel), add_validation);
    gtk_box_append(GTK_BOX(add_panel), add);
    gtk_box_append(GTK_BOX(add_panel), add_pin);
    gtk_box_append(GTK_BOX(page), add_panel);

    GtkWidget* configured = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(configured, "workflow-section");
    gtk_box_append(GTK_BOX(configured), label("Configured technicians", "section-title"));
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(list, "choice-list");
    gtk_box_append(GTK_BOX(configured), choice_scroll(list, 160));
    GtkWidget* status = label("Loading technicians...", "instruction");
    gtk_box_append(GTK_BOX(configured), status);

    GtkWidget* assign_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* assigned_pin = password_entry("New assigned 4-digit PIN");
    GtkWidget* assigned_pin_confirmation = password_entry("Confirm assigned PIN");
    gtk_entry_set_max_length(GTK_ENTRY(assigned_pin), 4);
    gtk_entry_set_max_length(GTK_ENTRY(assigned_pin_confirmation), 4);
    gtk_entry_set_input_purpose(GTK_ENTRY(assigned_pin), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_input_purpose(GTK_ENTRY(assigned_pin_confirmation), GTK_INPUT_PURPOSE_DIGITS);
    gtk_widget_set_hexpand(assigned_pin, TRUE);
    gtk_widget_set_hexpand(assigned_pin_confirmation, TRUE);
    GtkWidget* assign_pin = gtk_button_new_with_label("Set Selected PIN");
    lazarus::gui::add_class(assign_pin, "primary-command");
    gtk_widget_set_sensitive(assign_pin, FALSE);
    gtk_box_append(GTK_BOX(assign_panel), assigned_pin);
    gtk_box_append(GTK_BOX(assign_panel), assigned_pin_confirmation);
    gtk_box_append(GTK_BOX(assign_panel), assign_pin);
    gtk_box_append(GTK_BOX(configured), assign_panel);
    GtkWidget* show_assigned_pin = gtk_check_button_new_with_label("Show PIN while entering");
    GtkWidget* pin_validation = label("Select a technician first.", "instruction");
    gtk_box_append(GTK_BOX(configured), show_assigned_pin);
    gtk_box_append(GTK_BOX(configured), pin_validation);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* regenerate = gtk_button_new_with_label("Generate New PIN");
    GtkWidget* remove_confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(remove_confirmation), "Type REMOVE");
    gtk_widget_set_hexpand(remove_confirmation, TRUE);
    GtkWidget* remove = gtk_button_new_with_label("Remove Technician");
    lazarus::gui::add_class(regenerate, "secondary-command");
    lazarus::gui::add_class(remove, "danger-command");
    gtk_widget_set_sensitive(regenerate, FALSE);
    gtk_widget_set_sensitive(remove, FALSE);
    gtk_box_append(GTK_BOX(actions), regenerate);
    gtk_box_append(GTK_BOX(actions), remove_confirmation);
    gtk_box_append(GTK_BOX(actions), remove);
    gtk_box_append(GTK_BOX(configured), actions);
    GtkWidget* regenerated_pin = label("", "recovery-code");
    gtk_label_set_selectable(GTK_LABEL(regenerated_pin), TRUE);
    gtk_widget_set_visible(regenerated_pin, FALSE);
    gtk_box_append(GTK_BOX(configured), regenerated_pin);
    gtk_box_append(GTK_BOX(page), configured);

    auto* binding = new TechnicianBinding{app, list, status, regenerate, remove_confirmation, remove, regenerated_pin,
                                          assigned_pin, assigned_pin_confirmation, assign_pin, pin_validation, {}};
    auto* add_binding = new TechnicianAddBinding{name_entry, add_pin_entry, add_pin_confirmation, add, add_validation};
    g_object_set_data(G_OBJECT(add), "name-entry", name_entry);
    g_object_set_data(G_OBJECT(add), "pin-label", add_pin);

    for (GtkWidget* entry : {name_entry, add_pin_entry, add_pin_confirmation}) {
        g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            update_technician_add(static_cast<TechnicianAddBinding*>(data));
        }), add_binding);
    }
    g_signal_connect(show_add_pin, "toggled", G_CALLBACK(+[](GtkCheckButton* check, gpointer data) {
        const bool visible = gtk_check_button_get_active(check);
        auto* values = static_cast<TechnicianAddBinding*>(data);
        gtk_entry_set_visibility(GTK_ENTRY(values->pin), visible);
        gtk_entry_set_visibility(GTK_ENTRY(values->confirmation), visible);
    }), add_binding);
    g_signal_connect(show_assigned_pin, "toggled", G_CALLBACK(+[](GtkCheckButton* check, gpointer data) {
        const bool visible = gtk_check_button_get_active(check);
        auto* values = static_cast<TechnicianBinding*>(data);
        gtk_entry_set_visibility(GTK_ENTRY(values->assigned_pin), visible);
        gtk_entry_set_visibility(GTK_ENTRY(values->assigned_pin_confirmation), visible);
    }), binding);
    g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* values = static_cast<TechnicianBinding*>(data);
        auto* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "name-entry"));
        auto* pin_label = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "pin-label"));
        const std::string name = gtk_editable_get_text(GTK_EDITABLE(entry));
        auto* pin_entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "pin-entry"));
        auto* pin_confirmation = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "pin-confirmation"));
        const std::string pin = gtk_editable_get_text(GTK_EDITABLE(pin_entry));
        if (name.empty() || !valid_pin_entry(pin_entry) ||
            pin != gtk_editable_get_text(GTK_EDITABLE(pin_confirmation))) return;
        std::string response;
        const auto request_json = "{\"command\":\"technician_add\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"name\":" + quote_json(name) +
            ",\"pin\":" + quote_json(pin) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Could not add the technician." : error.c_str());
            if (error.find("expired") != std::string::npos) values->app->admin_token.clear();
            return;
        }
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
        gtk_editable_set_text(GTK_EDITABLE(pin_entry), "");
        gtk_editable_set_text(GTK_EDITABLE(pin_confirmation), "");
        gtk_widget_set_visible(pin_label, FALSE);
        refresh_technicians(values);
        refresh_operational_pages(values->app);
        gtk_label_set_text(GTK_LABEL(values->status), "Technician added with the designated PIN.");
    }), binding);
    g_object_set_data(G_OBJECT(add), "pin-entry", add_pin_entry);
    g_object_set_data(G_OBJECT(add), "pin-confirmation", add_pin_confirmation);

    g_signal_connect(list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<TechnicianBinding*>(data);
        const auto* name = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "technician-name"));
        values->selected = name == nullptr ? "" : name;
        gtk_editable_set_text(GTK_EDITABLE(values->remove_confirmation), "");
        gtk_editable_set_text(GTK_EDITABLE(values->assigned_pin), "");
        gtk_editable_set_text(GTK_EDITABLE(values->assigned_pin_confirmation), "");
        gtk_widget_set_visible(values->pin_reveal, FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), values->selected.empty()
            ? "Select a technician to manage."
            : ("Selected technician: " + values->selected).c_str());
        update_technician_actions(values);
    }), binding);
    g_signal_connect(remove_confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_technician_actions(static_cast<TechnicianBinding*>(data));
    }), binding);
    g_signal_connect(assigned_pin, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_technician_actions(static_cast<TechnicianBinding*>(data));
    }), binding);
    g_signal_connect(assigned_pin_confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_technician_actions(static_cast<TechnicianBinding*>(data));
    }), binding);
    g_signal_connect(assign_pin, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<TechnicianBinding*>(data);
        const std::string pin = gtk_editable_get_text(GTK_EDITABLE(values->assigned_pin));
        if (values->selected.empty() || !valid_pin_entry(values->assigned_pin) ||
            pin != gtk_editable_get_text(GTK_EDITABLE(values->assigned_pin_confirmation))) return;
        std::string response;
        const auto request_json = "{\"command\":\"technician_set_pin\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"name\":" + quote_json(values->selected) +
            ",\"pin\":" + quote_json(pin) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Could not assign the PIN." : error.c_str());
            if (error.find("expired") != std::string::npos) values->app->admin_token.clear();
            return;
        }
        gtk_editable_set_text(GTK_EDITABLE(values->assigned_pin), "");
        gtk_editable_set_text(GTK_EDITABLE(values->assigned_pin_confirmation), "");
        gtk_label_set_text(GTK_LABEL(values->status), "Designated PIN saved. Use the same PIN assignment on the other appliances.");
    }), binding);
    g_signal_connect(regenerate, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<TechnicianBinding*>(data);
        if (values->selected.empty()) return;
        std::string response;
        const auto request_json = "{\"command\":\"technician_regenerate_pin\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"name\":" + quote_json(values->selected) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Could not regenerate the PIN." : error.c_str());
            if (error.find("expired") != std::string::npos) values->app->admin_token.clear();
            return;
        }
        gtk_label_set_text(GTK_LABEL(values->pin_reveal), json_string(response, "pin").c_str());
        gtk_widget_set_visible(values->pin_reveal, TRUE);
        gtk_label_set_text(GTK_LABEL(values->status), "New PIN generated. Record it now; it cannot be shown again.");
    }), binding);
    g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<TechnicianBinding*>(data);
        if (values->selected.empty()) return;
        std::string response;
        const auto request_json = "{\"command\":\"technician_remove\",\"admin_token\":" +
            quote_json(values->app->admin_token) + ",\"name\":" + quote_json(values->selected) + "}";
        if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty() ? "Could not remove the technician." : error.c_str());
            if (error.find("expired") != std::string::npos) values->app->admin_token.clear();
            return;
        }
        refresh_technicians(values);
        refresh_operational_pages(values->app);
        gtk_label_set_text(GTK_LABEL(values->status), "Technician removed.");
    }), binding);

    refresh_technicians(binding);
    return page;
}

struct PrinterBinding {
    App* app;
    GtkWidget* list;
    GtkWidget* status;
    GtkWidget* set_default;
    GtkWidget* test_page;
    GtkWidget* remove_confirmation;
    GtkWidget* remove;
    std::string selected;
};

void update_printer_actions(PrinterBinding* binding) {
    const bool selected = !binding->selected.empty();
    gtk_widget_set_sensitive(binding->set_default, selected);
    gtk_widget_set_sensitive(binding->test_page, selected);
    const bool confirmed = std::string{gtk_editable_get_text(GTK_EDITABLE(binding->remove_confirmation))} == "REMOVE";
    gtk_widget_set_sensitive(binding->remove, selected && confirmed);
}

void refresh_printers(PrinterBinding* binding) {
    std::string response;
    const auto request_json = "{\"command\":\"printers\",\"admin_token\":" +
        quote_json(binding->app->admin_token) + "}";
    if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        gtk_label_set_text(GTK_LABEL(binding->status), error.empty() ? "Could not read CUPS printer configuration." : error.c_str());
        if (error.find("expired") != std::string::npos) binding->app->admin_token.clear();
        return;
    }

    clear_children(binding->list);
    binding->selected.clear();
    for (const auto& row_text : split(json_string(response, "printers_rows"), '\n')) {
        const auto fields = split(row_text, '\t');
        if (fields.size() < 4 || fields[0].empty()) continue;
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(content, 14); gtk_widget_set_margin_end(content, 14);
        gtk_widget_set_margin_top(content, 10); gtk_widget_set_margin_bottom(content, 10);
        const std::string title = fields[0] + (fields[1] == "default" ? "  |  DEFAULT" : "");
        gtk_box_append(GTK_BOX(content), label(title, "choice-title"));
        gtk_box_append(GTK_BOX(content), label(fields[2] + (fields[3].empty() ? "" : "\n" + fields[3]), "choice-detail"));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        lazarus::gui::add_class(row, "choice-row");
        g_object_set_data_full(G_OBJECT(row), "printer-name", g_strdup(fields[0].c_str()), g_free);
        gtk_list_box_append(GTK_LIST_BOX(binding->list), row);
    }
    if (gtk_widget_get_first_child(binding->list) == nullptr) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label("No printers are configured. Add a driverless printer below.", "empty-state"));
        gtk_list_box_append(GTK_LIST_BOX(binding->list), row);
    }
    const auto default_printer = json_string(response, "default_printer");
    gtk_label_set_text(GTK_LABEL(binding->status), default_printer.empty()
        ? "CUPS is running. No default printer is configured."
        : ("CUPS is running. Default printer: " + default_printer).c_str());
    update_printer_actions(binding);
}

void run_printer_action(PrinterBinding* binding, const std::string& command, const std::string& extra = {}) {
    if (binding->selected.empty()) return;
    std::string response;
    auto request_json = "{\"command\":" + quote_json(command) + ",\"admin_token\":" +
        quote_json(binding->app->admin_token) + ",\"name\":" + quote_json(binding->selected);
    if (!extra.empty()) request_json += ",\"confirmation\":" + quote_json(extra);
    request_json += "}";
    if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        gtk_label_set_text(GTK_LABEL(binding->status), error.empty() ? "The print service rejected the request." : error.c_str());
        if (error.find("expired") != std::string::npos) binding->app->admin_token.clear();
        return;
    }
    if (command == "printer_test_page") {
        const auto output = json_string(response, "service_output");
        gtk_label_set_text(GTK_LABEL(binding->status), output.empty() ? "Test page submitted to CUPS." : output.c_str());
        return;
    }
    gtk_editable_set_text(GTK_EDITABLE(binding->remove_confirmation), "");
    refresh_printers(binding);
}

struct PrinterAddBinding {
    PrinterBinding* printers;
    GtkWidget* name;
    GtkWidget* address;
    GtkWidget* connection;
    GtkWidget* add;
};

struct PrinterDiscoveryBinding {
    PrinterBinding* printers;
    GtkWidget* list;
    GtkWidget* status;
    GtkWidget* scan;
    GtkWidget* install;
    std::string selected_name;
    std::string selected_queue;
    std::string selected_uri;
    bool scanning = false;
};

struct PrinterDiscoveryJob {
    PrinterDiscoveryBinding* binding;
    std::string request_json;
    std::string response;
    bool transport_ok = false;
};

struct PrinterAddJob {
    PrinterBinding* printers;
    GtkWidget* button;
    GtkWidget* name_entry;
    GtkWidget* address_entry;
    std::string request_json;
    std::string response;
    bool transport_ok = false;
};

void begin_printer_add(PrinterBinding* printers, GtkWidget* button, std::string request_json,
                       GtkWidget* name_entry = nullptr, GtkWidget* address_entry = nullptr) {
    gtk_widget_set_sensitive(button, FALSE);
    gtk_label_set_text(GTK_LABEL(printers->status),
                       "Contacting the printer and reading its IPP capabilities...");
    auto* job = new PrinterAddJob{printers, button, name_entry, address_entry,
                                  std::move(request_json), {}, false};
    std::thread([job]() {
        job->transport_ok = request(job->request_json, job->response);
        g_idle_add(+[](gpointer data) -> gboolean {
            std::unique_ptr<PrinterAddJob> result(static_cast<PrinterAddJob*>(data));
            const bool added = result->transport_ok && result->response.find("\"ok\":true") != std::string::npos;
            if (!added) {
                auto error = json_string(result->response, "error");
                if (error.empty()) error = result->transport_ok
                    ? "CUPS rejected the printer without returning diagnostic details."
                    : "The Lazarus service did not answer the printer request.";
                gtk_label_set_text(GTK_LABEL(result->printers->status), error.c_str());
                gtk_widget_set_sensitive(result->button, TRUE);
                return G_SOURCE_REMOVE;
            }
            if (result->name_entry != nullptr) gtk_editable_set_text(GTK_EDITABLE(result->name_entry), "");
            if (result->address_entry != nullptr) gtk_editable_set_text(GTK_EDITABLE(result->address_entry), "");
            gtk_label_set_text(GTK_LABEL(result->printers->status), json_string(result->response, "message").c_str());
            refresh_printers(result->printers);
            gtk_widget_set_sensitive(result->button, TRUE);
            return G_SOURCE_REMOVE;
        }, job);
    }).detach();
}

void begin_printer_discovery(PrinterDiscoveryBinding* binding) {
    if (binding->scanning) return;
    binding->scanning = true;
    binding->selected_name.clear();
    binding->selected_queue.clear();
    binding->selected_uri.clear();
    gtk_widget_set_sensitive(binding->scan, FALSE);
    gtk_widget_set_sensitive(binding->install, FALSE);
    gtk_label_set_text(GTK_LABEL(binding->status), "Scanning the local network for IPP and AirPrint printers...");
    clear_children(binding->list);
    auto* job = new PrinterDiscoveryJob{
        binding,
        "{\"command\":\"printer_discover\",\"admin_token\":" +
            quote_json(binding->printers->app->admin_token) + "}",
        {}, false
    };
    std::thread([job]() {
        job->transport_ok = request(job->request_json, job->response);
        g_idle_add(+[](gpointer data) -> gboolean {
            std::unique_ptr<PrinterDiscoveryJob> result(static_cast<PrinterDiscoveryJob*>(data));
            auto* values = result->binding;
            values->scanning = false;
            gtk_widget_set_sensitive(values->scan, TRUE);
            const bool discovered = result->transport_ok && result->response.find("\"ok\":true") != std::string::npos;
            if (!discovered) {
                auto error = json_string(result->response, "error");
                if (error.empty()) error = "Printer discovery did not receive a complete response from the Lazarus service.";
                gtk_label_set_text(GTK_LABEL(values->status), error.c_str());
            } else {
                for (const auto& row_text : split(json_string(result->response, "discovered_rows"), '\n')) {
                    const auto fields = split(row_text, '\t');
                    if (fields.size() < 3 || fields[0].empty() || fields[2].empty()) continue;
                    GtkWidget* row = gtk_list_box_row_new();
                    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                    gtk_widget_set_margin_start(content, 14); gtk_widget_set_margin_end(content, 14);
                    gtk_widget_set_margin_top(content, 10); gtk_widget_set_margin_bottom(content, 10);
                    gtk_box_append(GTK_BOX(content), label(fields[0], "choice-title"));
                    gtk_box_append(GTK_BOX(content), label("Driverless IPP  |  " + fields[2], "choice-detail"));
                    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
                    lazarus::gui::add_class(row, "choice-row");
                    g_object_set_data_full(G_OBJECT(row), "discovered-name", g_strdup(fields[0].c_str()), g_free);
                    g_object_set_data_full(G_OBJECT(row), "discovered-queue", g_strdup(fields[1].c_str()), g_free);
                    g_object_set_data_full(G_OBJECT(row), "discovered-uri", g_strdup(fields[2].c_str()), g_free);
                    gtk_list_box_append(GTK_LIST_BOX(values->list), row);
                }
                gtk_label_set_text(GTK_LABEL(values->status), json_string(result->response, "message").c_str());
            }
            if (gtk_widget_get_first_child(values->list) == nullptr) {
                GtkWidget* row = gtk_list_box_row_new();
                gtk_widget_set_sensitive(row, FALSE);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                    label("No driverless printer answered. Confirm that the printer is awake and on this network, or add it by IP address below.", "empty-state"));
                gtk_list_box_append(GTK_LIST_BOX(values->list), row);
            }
            refresh_printers(values->printers);
            return G_SOURCE_REMOVE;
        }, job);
    }).detach();
}

GtkWidget* make_printers_page(App* app) {
    GtkWidget* page = page_shell(app, "ADMIN PRINTERS", "Printer Management",
                                 "Discover local printers, configure a queue, and choose where Lazarus reports print.", "admin");

    GtkWidget* discovery = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(discovery, "workflow-section");
    GtkWidget* discovery_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* discovery_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(discovery_copy, TRUE);
    gtk_box_append(GTK_BOX(discovery_copy), label("Printers on this network", "section-title"));
    gtk_box_append(GTK_BOX(discovery_copy), label("Lazarus detects IPP Everywhere and AirPrint printers announced on the local network.", "section-detail"));
    gtk_box_append(GTK_BOX(discovery_heading), discovery_copy);
    GtkWidget* scan = gtk_button_new_with_label("Scan Again");
    lazarus::gui::add_class(scan, "secondary-command");
    gtk_box_append(GTK_BOX(discovery_heading), scan);
    gtk_box_append(GTK_BOX(discovery), discovery_heading);
    GtkWidget* discovered_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(discovered_list), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(discovered_list, "choice-list");
    gtk_box_append(GTK_BOX(discovery), choice_scroll(discovered_list, 160));
    GtkWidget* discovery_status = label("Discovery begins automatically when this page opens.", "instruction");
    gtk_label_set_wrap(GTK_LABEL(discovery_status), TRUE);
    gtk_box_append(GTK_BOX(discovery), discovery_status);
    GtkWidget* install_discovered = gtk_button_new_with_label("Install Selected Printer");
    lazarus::gui::add_class(install_discovered, "primary-command");
    gtk_widget_set_sensitive(install_discovered, FALSE);
    gtk_box_append(GTK_BOX(discovery), install_discovered);
    gtk_box_append(GTK_BOX(page), discovery);

    GtkWidget* configured = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(configured, "workflow-section");
    GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* heading_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(heading_copy, TRUE);
    gtk_box_append(GTK_BOX(heading_copy), label("Configured printers", "section-title"));
    gtk_box_append(GTK_BOX(heading_copy), label("Select one printer to manage it.", "section-detail"));
    gtk_box_append(GTK_BOX(heading), heading_copy);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh Printers");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_box_append(GTK_BOX(heading), refresh);
    gtk_box_append(GTK_BOX(configured), heading);
    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    lazarus::gui::add_class(list, "choice-list");
    gtk_box_append(GTK_BOX(configured), choice_scroll(list, 160));
    GtkWidget* printer_status = label("Select Refresh Printers to read the CUPS configuration.", "instruction");
    gtk_box_append(GTK_BOX(configured), printer_status);
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* set_default = gtk_button_new_with_label("Set Default");
    GtkWidget* test_page = gtk_button_new_with_label("Print Test Page");
    GtkWidget* remove_confirmation = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(remove_confirmation), "Type REMOVE");
    gtk_widget_set_hexpand(remove_confirmation, TRUE);
    GtkWidget* remove = gtk_button_new_with_label("Remove Printer");
    lazarus::gui::add_class(set_default, "secondary-command");
    lazarus::gui::add_class(test_page, "secondary-command");
    lazarus::gui::add_class(remove, "danger-command");
    gtk_widget_set_sensitive(set_default, FALSE);
    gtk_widget_set_sensitive(test_page, FALSE);
    gtk_widget_set_sensitive(remove, FALSE);
    gtk_box_append(GTK_BOX(actions), set_default);
    gtk_box_append(GTK_BOX(actions), test_page);
    gtk_box_append(GTK_BOX(actions), remove_confirmation);
    gtk_box_append(GTK_BOX(actions), remove);
    gtk_box_append(GTK_BOX(configured), actions);
    gtk_box_append(GTK_BOX(page), configured);

    auto* binding = new PrinterBinding{app, list, printer_status, set_default, test_page,
                                       remove_confirmation, remove, {}};
    auto* discovery_binding = new PrinterDiscoveryBinding{
        binding, discovered_list, discovery_status, scan, install_discovered, {}, {}, {}, false
    };
    g_signal_connect(scan, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        begin_printer_discovery(static_cast<PrinterDiscoveryBinding*>(data));
    }), discovery_binding);
    g_signal_connect(discovered_list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<PrinterDiscoveryBinding*>(data);
        const auto read = [row](const char* key) -> std::string {
            if (row == nullptr) return {};
            const auto* value = static_cast<const char*>(g_object_get_data(G_OBJECT(row), key));
            return value == nullptr ? "" : value;
        };
        values->selected_name = read("discovered-name");
        values->selected_queue = read("discovered-queue");
        values->selected_uri = read("discovered-uri");
        gtk_widget_set_sensitive(values->install, !values->selected_uri.empty());
    }), discovery_binding);
    g_signal_connect(install_discovered, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<PrinterDiscoveryBinding*>(data);
        if (values->selected_uri.empty()) return;
        const auto request_json = "{\"command\":\"printer_add\",\"admin_token\":" +
            quote_json(values->printers->app->admin_token) + ",\"name\":" + quote_json(values->selected_queue) +
            ",\"display_name\":" + quote_json(values->selected_name) +
            ",\"uri\":" + quote_json(values->selected_uri) + "}";
        begin_printer_add(values->printers, values->install, request_json);
    }), discovery_binding);
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        refresh_printers(static_cast<PrinterBinding*>(data));
    }), binding);
    g_signal_connect(list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* values = static_cast<PrinterBinding*>(data);
        const auto* name = row == nullptr ? nullptr : static_cast<const char*>(g_object_get_data(G_OBJECT(row), "printer-name"));
        values->selected = name == nullptr ? "" : name;
        gtk_editable_set_text(GTK_EDITABLE(values->remove_confirmation), "");
        update_printer_actions(values);
    }), binding);
    g_signal_connect(remove_confirmation, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
        update_printer_actions(static_cast<PrinterBinding*>(data));
    }), binding);
    g_signal_connect(set_default, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        run_printer_action(static_cast<PrinterBinding*>(data), "printer_set_default");
    }), binding);
    g_signal_connect(test_page, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        run_printer_action(static_cast<PrinterBinding*>(data), "printer_test_page");
    }), binding);
    g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        run_printer_action(static_cast<PrinterBinding*>(data), "printer_remove", "REMOVE");
    }), binding);

    GtkWidget* add_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(add_panel, "workflow-section");
    gtk_box_append(GTK_BOX(add_panel), label("Add by address", "section-title"));
    gtk_box_append(GTK_BOX(add_panel), label("Use an IP address, hostname, or full ipp:// or ipps:// printer URI. Automatic setup tries driverless IPP before the legacy JetDirect fallback.", "section-detail"));
    GtkWidget* add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Queue name (optional)");
    GtkWidget* address = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(address), "192.168.1.50 or ipp://192.168.1.50/ipp/print");
    gtk_widget_set_hexpand(address, TRUE);
    const char* connection_names[] = {"Automatic", "IPP Everywhere", "Secure IPP", "JetDirect / Port 9100", nullptr};
    GtkWidget* connection = gtk_drop_down_new_from_strings(connection_names);
    GtkWidget* add = gtk_button_new_with_label("Add Printer");
    lazarus::gui::add_class(add, "primary-command");
    gtk_widget_set_sensitive(add, FALSE);
    gtk_box_append(GTK_BOX(add_row), name);
    gtk_box_append(GTK_BOX(add_row), address);
    gtk_box_append(GTK_BOX(add_row), connection);
    gtk_box_append(GTK_BOX(add_row), add);
    gtk_box_append(GTK_BOX(add_panel), add_row);
    gtk_box_append(GTK_BOX(page), add_panel);
    auto* add_binding = new PrinterAddBinding{binding, name, address, connection, add};
    const auto update_add = +[](GtkEditable*, gpointer data) {
        auto* values = static_cast<PrinterAddBinding*>(data);
        gtk_widget_set_sensitive(values->add,
            *gtk_editable_get_text(GTK_EDITABLE(values->address)) != '\0');
    };
    g_signal_connect(name, "changed", G_CALLBACK(update_add), add_binding);
    g_signal_connect(address, "changed", G_CALLBACK(update_add), add_binding);
    g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<PrinterAddBinding*>(data);
        const std::string name = gtk_editable_get_text(GTK_EDITABLE(values->name));
        const std::string address = gtk_editable_get_text(GTK_EDITABLE(values->address));
        const auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(values->connection));
        std::string connection = "auto";
        if (selected == 1) connection = "ipp";
        else if (selected == 2) connection = "ipps";
        else if (selected == 3) connection = "socket";
        auto request_json = "{\"command\":\"printer_add\",\"admin_token\":" +
            quote_json(values->printers->app->admin_token) + ",\"name\":" + quote_json(name) +
            ",\"display_name\":" + quote_json(address);
        if (address.find("://") != std::string::npos) {
            request_json += ",\"uri\":" + quote_json(address);
        } else {
            request_json += ",\"address\":" + quote_json(address) +
                ",\"connection\":" + quote_json(connection);
        }
        request_json += "}";
        begin_printer_add(values->printers, values->add, request_json, values->name, values->address);
    }), add_binding);

    g_timeout_add(250, +[](gpointer data) -> gboolean {
        auto* values = static_cast<PrinterDiscoveryBinding*>(data);
        if (values->printers->app->admin_token.empty()) return G_SOURCE_CONTINUE;
        refresh_printers(values->printers);
        begin_printer_discovery(values);
        return G_SOURCE_REMOVE;
    }, discovery_binding);
    return page;
}

struct NetworkAdminBinding {
    App* app;
    GtkWidget* interfaces;
    GtkWidget* interface_selector;
    GtkWidget* mode;
    GtkWidget* address;
    GtkWidget* prefix;
    GtkWidget* gateway;
    GtkWidget* dns;
    GtkWidget* status;
    GtkWidget* log;
    GtkWidget* apply;
};

void update_network_fields(NetworkAdminBinding* binding) {
    const bool static_mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(binding->mode)) == 1;
    gtk_widget_set_sensitive(binding->address, static_mode);
    gtk_widget_set_sensitive(binding->prefix, static_mode);
    gtk_widget_set_sensitive(binding->gateway, static_mode);
    gtk_widget_set_sensitive(binding->dns, static_mode);
}

void refresh_network_admin(NetworkAdminBinding* binding) {
    gui_trace("network page refresh: begin");
    std::string response;
    if (!request("{\"command\":\"network_config\"}", response) ||
        response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        gtk_label_set_text(GTK_LABEL(binding->status), error.empty()
            ? "Could not read network configuration from the Lazarus service." : error.c_str());
        return;
    }
    gui_trace("network page refresh: service response received");

    clear_children(binding->interfaces);
    gui_trace("network page refresh: old interface rows cleared");
    GtkStringList* model = gtk_string_list_new(nullptr);
    gtk_string_list_append(model, "Automatic (all wired interfaces)");
    const auto configured_interface = json_string(response, "interface");
    guint selected_interface = 0;
    guint model_index = 1;
    bool have_interfaces = false;
    for (const auto& row_text : split(json_string(response, "interfaces_rows"), '\n')) {
        const auto fields = split(row_text, '\t');
        if (fields.size() < 6 || fields[0].empty()) continue;
        gtk_string_list_append(model, fields[0].c_str());
        if (fields[0] == configured_interface) selected_interface = model_index;
        ++model_index;

        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(content, 14); gtk_widget_set_margin_end(content, 14);
        gtk_widget_set_margin_top(content, 10); gtk_widget_set_margin_bottom(content, 10);
        gtk_box_append(GTK_BOX(content), label(fields[0] + "  |  " + fields[1], "choice-title"));
        const std::string address = fields[4].empty() ? "No IPv4 address" : fields[4];
        gtk_box_append(GTK_BOX(content), label("Link: " + fields[2] + " / " + fields[3] +
                                                   "  |  IPv4: " + address + "  |  MAC: " + fields[5],
                                               "choice-detail"));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
        lazarus::gui::add_class(row, "choice-row");
        gtk_list_box_append(GTK_LIST_BOX(binding->interfaces), row);
        have_interfaces = true;
    }
    gui_trace("network page refresh: interface rows built");
    if (!have_interfaces) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_sensitive(row, FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
            label("No non-loopback network interfaces were detected. Check the cable, adapter, and driver support.", "empty-state"));
        gtk_list_box_append(GTK_LIST_BOX(binding->interfaces), row);
    }
    gui_trace("network page refresh: empty state resolved");
    gtk_drop_down_set_model(GTK_DROP_DOWN(binding->interface_selector), G_LIST_MODEL(model));
    gui_trace("network page refresh: interface model assigned");
    gtk_drop_down_set_selected(GTK_DROP_DOWN(binding->interface_selector), selected_interface);
    g_object_unref(model);
    gui_trace("network page refresh: interface selected");

    gtk_drop_down_set_selected(GTK_DROP_DOWN(binding->mode), json_string(response, "mode") == "static" ? 1 : 0);
    gui_trace("network page refresh: mode selected");
    gtk_editable_set_text(GTK_EDITABLE(binding->address), json_string(response, "address").c_str());
    gtk_editable_set_text(GTK_EDITABLE(binding->prefix), json_string(response, "prefix").c_str());
    gtk_editable_set_text(GTK_EDITABLE(binding->gateway), json_string(response, "gateway").c_str());
    gtk_editable_set_text(GTK_EDITABLE(binding->dns), json_string(response, "dns").c_str());
    gtk_label_set_text(GTK_LABEL(binding->log), json_string(response, "log").empty()
        ? "No DHCP client events have been recorded yet." : json_string(response, "log").c_str());
    gtk_label_set_text(GTK_LABEL(binding->status), "Network configuration loaded.");
    update_network_fields(binding);
    gui_trace("network page refresh: complete");
}

void apply_network_admin(NetworkAdminBinding* binding, bool force_dhcp) {
    const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(binding->interface_selector));
    auto* model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(binding->interface_selector)));
    const char* selected_name = selected == GTK_INVALID_LIST_POSITION ? nullptr : gtk_string_list_get_string(model, selected);
    const std::string interface = selected == 0 || selected_name == nullptr ? "auto" : selected_name;
    const std::string mode = force_dhcp || gtk_drop_down_get_selected(GTK_DROP_DOWN(binding->mode)) == 0
        ? "dhcp" : "static";
    gtk_widget_set_sensitive(binding->apply, FALSE);
    gtk_label_set_text(GTK_LABEL(binding->status), mode == "dhcp"
        ? "Restarting DHCP discovery. Waiting for the network to answer..."
        : "Applying the static IPv4 configuration...");
    while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, FALSE);

    std::string response;
    const auto request_json = "{\"command\":\"network_apply\",\"admin_token\":" +
        quote_json(binding->app->admin_token) + ",\"mode\":" + quote_json(mode) +
        ",\"interface\":" + quote_json(interface) +
        ",\"address\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(binding->address))) +
        ",\"prefix\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(binding->prefix))) +
        ",\"gateway\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(binding->gateway))) +
        ",\"dns\":" + quote_json(gtk_editable_get_text(GTK_EDITABLE(binding->dns))) + "}";
    if (!request(request_json, response) || response.find("\"ok\":true") == std::string::npos) {
        const auto error = json_string(response, "error");
        gtk_label_set_text(GTK_LABEL(binding->status), error.empty()
            ? "The Lazarus service could not apply the network configuration." : error.c_str());
        gtk_widget_set_sensitive(binding->apply, TRUE);
        return;
    }
    gtk_label_set_text(GTK_LABEL(binding->status), json_string(response, "message").c_str());
    gtk_widget_set_sensitive(binding->apply, TRUE);
    g_timeout_add_seconds(2, +[](gpointer data) -> gboolean {
        refresh_network_admin(static_cast<NetworkAdminBinding*>(data));
        return G_SOURCE_REMOVE;
    }, binding);
}

GtkWidget* make_network_page(App* app) {
    gui_trace("network page: begin");
    GtkWidget* page = page_shell(app, "ADMIN NETWORK", "Network Configuration",
                                 "Inspect wired interfaces and configure DHCP or a static IPv4 address.", "admin");

    GtkWidget* interface_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(interface_panel, "workflow-section");
    GtkWidget* interface_heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* interface_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(interface_copy, TRUE);
    gtk_box_append(GTK_BOX(interface_copy), label("Detected interfaces", "section-title"));
    gtk_box_append(GTK_BOX(interface_copy), label("Link state is read directly from Linux. Wireless setup is not supported yet.", "section-detail"));
    gtk_box_append(GTK_BOX(interface_heading), interface_copy);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh Status");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_box_append(GTK_BOX(interface_heading), refresh);
    gtk_box_append(GTK_BOX(interface_panel), interface_heading);
    GtkWidget* interfaces = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(interfaces), GTK_SELECTION_NONE);
    lazarus::gui::add_class(interfaces, "choice-list");
    gtk_box_append(GTK_BOX(interface_panel), choice_scroll(interfaces, 160));
    gtk_box_append(GTK_BOX(page), interface_panel);

    GtkWidget* settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    lazarus::gui::add_class(settings, "workflow-section");
    gtk_box_append(GTK_BOX(settings), label("Address configuration", "section-title"));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10); gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    GtkStringList* empty_interfaces = gtk_string_list_new(nullptr);
    // gtk_drop_down_new() consumes the caller's model reference. A replacement
    // model assigned by refresh_network_admin() uses transfer-none semantics.
    GtkWidget* interface_selector = gtk_drop_down_new(G_LIST_MODEL(empty_interfaces), nullptr);
    const char* modes[] = {"DHCP (automatic)", "Static IPv4", nullptr};
    GtkWidget* mode = gtk_drop_down_new_from_strings(modes);
    GtkWidget* address = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(address), "192.168.1.50");
    GtkWidget* prefix = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(prefix), "24");
    GtkWidget* gateway = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(gateway), "192.168.1.1");
    GtkWidget* dns = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(dns), "192.168.1.1, 1.1.1.1");
    const std::pair<const char*, GtkWidget*> fields[] = {
        {"Interface", interface_selector}, {"Mode", mode}, {"IPv4 address", address},
        {"Prefix length", prefix}, {"Gateway", gateway}, {"DNS servers", dns},
    };
    for (std::size_t index = 0; index < std::size(fields); ++index) {
        GtkWidget* caption = label(fields[index].first, "form-label");
        gtk_widget_set_halign(caption, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), caption, 0, static_cast<int>(index), 1, 1);
        gtk_widget_set_hexpand(fields[index].second, TRUE);
        gtk_grid_attach(GTK_GRID(grid), fields[index].second, 1, static_cast<int>(index), 1, 1);
    }
    gtk_box_append(GTK_BOX(settings), grid);
    GtkWidget* status = label("Reading network configuration...", "instruction");
    gtk_box_append(GTK_BOX(settings), status);
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* retry = gtk_button_new_with_label("Retry DHCP");
    GtkWidget* apply = gtk_button_new_with_label("Save and Apply");
    lazarus::gui::add_class(retry, "secondary-command");
    lazarus::gui::add_class(apply, "primary-command");
    gtk_box_append(GTK_BOX(actions), retry); gtk_box_append(GTK_BOX(actions), apply);
    gtk_box_append(GTK_BOX(settings), actions);
    gtk_box_append(GTK_BOX(page), settings);

    GtkWidget* log_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(log_panel, "workflow-section");
    gtk_box_append(GTK_BOX(log_panel), label("DHCP client events", "section-title"));
    GtkWidget* log = label("No DHCP client events have been recorded yet.", "technical-details");
    gtk_label_set_selectable(GTK_LABEL(log), TRUE);
    gtk_box_append(GTK_BOX(log_panel), log);
    gtk_box_append(GTK_BOX(page), log_panel);

    auto* binding = new NetworkAdminBinding{app, interfaces, interface_selector, mode, address,
                                            prefix, gateway, dns, status, log, apply};
    gui_trace("network page: widgets and binding built");
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        refresh_network_admin(static_cast<NetworkAdminBinding*>(data));
    }), binding);
    g_signal_connect(mode, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        update_network_fields(static_cast<NetworkAdminBinding*>(data));
    }), binding);
    g_signal_connect(retry, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<NetworkAdminBinding*>(data);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(values->mode), 0);
        apply_network_admin(values, true);
    }), binding);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        apply_network_admin(static_cast<NetworkAdminBinding*>(data), false);
    }), binding);
    gui_trace("network page: signals connected");
    refresh_network_admin(binding);
    gui_trace("network page: complete");
    return page;
}

GtkWidget* make_admin_page(App* app) {
    GtkWidget* page = page_shell(app, "ADMINISTRATION", "Bench Configuration", "Configure one bench concern at a time. Operational work remains on Home.");
    struct Task { const char* title; const char* detail; const char* icon; const char* target; };
    const Task tasks[] = {
        {"Password and Recovery", "Manage administrator access and recovery.", "shield", "admin-security"},
        {"Technicians", "Manage technician sign-in names and PINs.", "shield", "admin-technicians"},
        {"Printers and Reports", "Configure report printing and test output.", "printer", "admin-printers"},
        {"Network", "Configure wired DHCP or static IPv4 addressing.", "diagnostics", "admin-network"},
        {"Physical Port Roles", "Assign source, destination, storage, and removable-media connections.", "usb", "admin-port-roles"},
        {"Image Storage", "Choose persistent backup locations.", "image", "admin-storage"},
        {"Physical Port Labels", "Give bench connections clear names.", "usb", "admin-labels"},
        {"Branding and Reports", "Configure workplace identity and reports.", "image", "admin-branding"},
        {"Install Lazarus OS", "Install this appliance to a dedicated disk.", "ssd", "admin-install"},
    };
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    for (std::size_t index = 0; index < std::size(tasks); ++index) {
        const auto& task = tasks[index];
        GtkWidget* button = gtk_button_new(); GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        gtk_widget_set_margin_start(content, 12); gtk_widget_set_margin_end(content, 12); gtk_widget_set_margin_top(content, 10); gtk_widget_set_margin_bottom(content, 10);
        GtkWidget* copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_valign(copy, GTK_ALIGN_CENTER);
        gtk_button_set_child(GTK_BUTTON(button), content);
        gtk_box_append(GTK_BOX(content), lazarus::gui::make_icon(task.icon, 32));
        gtk_box_append(GTK_BOX(copy), label(task.title, "admin-task-title"));
        gtk_box_append(GTK_BOX(copy), label(task.detail, "admin-task-detail"));
        gtk_box_append(GTK_BOX(content), copy);
        lazarus::gui::add_class(button, "admin-task"); g_object_set_data(G_OBJECT(button), "page", const_cast<char*>(task.target));
        g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) { show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(button), "page"))); }), app);
        gtk_grid_attach(GTK_GRID(grid), button, static_cast<int>(index % 2), static_cast<int>(index / 2), 1, 1);
    }
    gtk_box_append(GTK_BOX(page), grid);
    return page;
}

void add_page(App* app, GtkWidget* page, const char* name, const char* title) {
    GtkWidget* action_bar = nullptr;
    for (GtkWidget* item = gtk_widget_get_first_child(page); item != nullptr;) {
        GtkWidget* next = gtk_widget_get_next_sibling(item);
        if (g_object_get_data(G_OBJECT(item), "lazarus-persistent-action") != nullptr) {
            if (action_bar == nullptr) {
                action_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                gtk_widget_set_halign(action_bar, GTK_ALIGN_FILL);
                gtk_widget_set_hexpand(action_bar, TRUE);
                lazarus::gui::add_class(action_bar, "workflow-action-bar");
                GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
                gtk_widget_set_hexpand(spacer, TRUE);
                gtk_box_append(GTK_BOX(action_bar), spacer);
            }
            g_object_ref(item);
            gtk_box_remove(GTK_BOX(page), item);
            gtk_box_append(GTK_BOX(action_bar), item);
            g_object_unref(item);
        }
        item = next;
    }

    // Every page, including Home, is constrained to the physical viewport. Without this wrapper,
    // Home's natural height can enlarge the homogeneous GtkStack beyond a 1366x768 framebuffer and
    // clip the bottom of every workflow in an unmanaged kiosk session.
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);

    GtkWidget* child = scroll;
    if (action_bar != nullptr) {
        GtkWidget* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(shell, TRUE);
        gtk_widget_set_vexpand(shell, TRUE);
        gtk_box_append(GTK_BOX(shell), scroll);
        gtk_box_append(GTK_BOX(shell), action_bar);
        child = shell;
    }
    gtk_stack_add_titled(GTK_STACK(app->stack), child, name, title);
}

void enter_admin(App* app) {
    if (!app->admin_token.empty()) {
        show_page(app, "admin");
        return;
    }

    if (GtkWidget* existing = gtk_stack_get_child_by_name(GTK_STACK(app->stack), "admin-lock")) {
        gtk_stack_remove(GTK_STACK(app->stack), existing);
    }
    add_page(app, make_admin_lock_page(app), "admin-lock", "Administration Locked");
    show_page(app, "admin-lock");
}

void refresh_operational_pages(App* app) {
    if (app->operational_refresh_pending) return;
    app->operational_refresh_pending = true;
    const char* visible = gtk_stack_get_visible_child_name(GTK_STACK(app->stack));
    app->page_after_refresh = visible == nullptr ? "home" : visible;
    const char* pages[] = {
        "home", "tickets", "backup", "restore", "verify", "recover-files", "driver-migration", "boot-repair", "diagnostics",
        "admin-port-roles", "admin-source", "admin-destination", "admin-storage", "admin-storage-local", "admin-storage-network",
        "admin-labels", "admin-install"
    };
    for (const char* name : pages) {
        if (GtkWidget* child = gtk_stack_get_child_by_name(GTK_STACK(app->stack), name)) {
            gtk_stack_remove(GTK_STACK(app->stack), child);
        }
    }
    g_idle_add(+[](gpointer data) {
        auto* state = static_cast<App*>(data);
        add_page(state, make_home(state), "home", "Home");
        add_page(state, make_tickets_page(state), "tickets", "Review Tickets");
        add_page(state, make_backup_page(state), "backup", "Create Backup");
        add_page(state, make_restore_page(state), "restore", "Restore Backup");
        add_page(state, make_verify_page(state), "verify", "Verify Backup");
        add_page(state, make_recovery_page(state), "recover-files", "Recover Files");
        add_page(state, make_driver_migration_page(state), "driver-migration", "Universal Restore");
        add_page(state, make_boot_repair_page(state), "boot-repair", "Boot Repair");
        add_page(state, make_diagnostics_page(state), "diagnostics", "Run Diagnostics");
        add_page(state, make_port_roles_page(state), "admin-port-roles", "Physical Port Roles");
        add_page(state, make_port_policy_page(state, true), "admin-source", "Source Ports");
        add_page(state, make_port_policy_page(state, false), "admin-destination", "Destination Ports");
        add_page(state, make_storage_page(state), "admin-storage", "Image Storage");
        add_page(state, make_storage_local_page(state), "admin-storage-local", "Local Drive Storage");
        add_page(state, make_storage_network_page(state), "admin-storage-network", "Network Storage");
        add_page(state, make_labels_page(state), "admin-labels", "Port Labels");
        add_page(state, make_install_page(state), "admin-install", "Install Lazarus OS");
        if (!state->page_after_refresh.empty() &&
            gtk_stack_get_child_by_name(GTK_STACK(state->stack), state->page_after_refresh.c_str()) != nullptr) {
            show_page(state, state->page_after_refresh.c_str());
        }
        state->page_after_refresh.clear();
        state->operational_refresh_pending = false;
        return G_SOURCE_REMOVE;
    }, app);
}

gboolean complete_action_navigation(gpointer data) {
    std::unique_ptr<NavigationRequest> request(static_cast<NavigationRequest*>(data));
    show_page(request->app, request->target.c_str());
    return G_SOURCE_REMOVE;
}

void activate_action_button(GtkButton* source, gpointer data) {
    auto* navigation = new NavigationRequest;
    navigation->app = static_cast<App*>(data);
    const char* target = static_cast<const char*>(g_object_get_data(G_OBJECT(source), "page"));
    navigation->target = target == nullptr ? "home" : target;
    g_idle_add(complete_action_navigation, navigation);
}

GtkWidget* action_button(App* app, const char* title, const char* detail, const char* icon, const char* page) {
    GtkWidget* button = gtk_button_new(); GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 18); gtk_widget_set_margin_end(box, 18); gtk_widget_set_margin_top(box, 18); gtk_widget_set_margin_bottom(box, 18);
    gtk_button_set_child(GTK_BUTTON(button), box); gtk_box_append(GTK_BOX(box), lazarus::gui::make_icon(icon, 42));
    gtk_box_append(GTK_BOX(box), label(title, "action-title")); gtk_box_append(GTK_BOX(box), label(detail, "action-detail"));
    lazarus::gui::add_class(button, "workflow-card");
    g_signal_connect(button, "clicked", G_CALLBACK(activate_action_button), app);
    g_object_set_data(G_OBJECT(button), "page", const_cast<char*>(page)); return button;
}

struct PowerBinding {
    App* app;
    GtkWidget* confirm;
    GtkWidget* cancel;
    GtkWidget* status;
    bool restart;
};

GtkWidget* make_power_page(App* app, bool restart) {
    const char* action = restart ? "Restart" : "Shut Down";
    GtkWidget* page = page_shell(app, "APPLIANCE POWER", std::string{action} + " Lazarus",
                                 restart
                                     ? "Stop Lazarus services cleanly and restart the appliance."
                                     : "Stop Lazarus services cleanly and power off the appliance.");
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    lazarus::gui::add_class(panel, restart ? "workflow-section" : "warning-panel");
    gtk_widget_set_halign(panel, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(panel, 560, -1);
    GtkWidget* icon = lazarus::gui::make_icon(restart ? "restart" : "power", 58);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(panel), icon);
    gtk_box_append(GTK_BOX(panel), label(restart ? "Restart this appliance?" : "Shut down this appliance?", "choice-title"));
    gtk_box_append(GTK_BOX(panel), label(
        "Lazarus waits for the current service operation to finish before this request can be processed.",
        "choice-detail"));
    GtkWidget* status = label("No power action has been requested.", "instruction");
    gtk_box_append(GTK_BOX(panel), status);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    lazarus::gui::add_class(cancel, "secondary-command");
    GtkWidget* confirm = gtk_button_new_with_label(restart ? "Restart Lazarus" : "Shut Down Lazarus");
    lazarus::gui::add_class(confirm, restart ? "primary-command" : "danger-command");
    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), confirm);
    gtk_box_append(GTK_BOX(panel), actions);
    gtk_box_append(GTK_BOX(page), panel);

    auto* binding = new PowerBinding{app, confirm, cancel, status, restart};
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        show_page(static_cast<PowerBinding*>(data)->app, "home");
    }), binding);
    g_signal_connect(confirm, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* values = static_cast<PowerBinding*>(data);
        gtk_widget_set_sensitive(values->confirm, FALSE);
        gtk_widget_set_sensitive(values->cancel, FALSE);
        gtk_label_set_text(GTK_LABEL(values->status), values->restart
            ? "Requesting a clean restart..." : "Requesting a clean shutdown...");
        const std::string action = values->restart ? "restart" : "shutdown";
        const std::string confirmation = values->restart ? "RESTART" : "SHUT DOWN";
        std::string response;
        if (!request("{\"command\":\"system_power\",\"action\":" + quote_json(action) +
                         ",\"confirmation\":" + quote_json(confirmation) + "}", response) ||
            response.find("\"ok\":true") == std::string::npos) {
            const auto error = json_string(response, "error");
            gtk_label_set_text(GTK_LABEL(values->status), error.empty()
                ? "The Lazarus service did not accept the power request." : error.c_str());
            gtk_widget_set_sensitive(values->confirm, TRUE);
            gtk_widget_set_sensitive(values->cancel, TRUE);
            return;
        }
        gtk_label_set_text(GTK_LABEL(values->status), values->restart
            ? "Restart accepted. Lazarus is restarting now."
            : "Shutdown accepted. Lazarus is powering off now.");
    }), binding);
    return page;
}

GtkWidget* heartbeat_item(const char* title, const char* value, bool healthy,
                          GtkWidget** value_widget = nullptr, GtkWidget** indicator_widget = nullptr) {
    GtkWidget* item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    lazarus::gui::add_class(item, "heartbeat-item");

    GtkWidget* indicator = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(indicator, 10, 10);
    gtk_widget_set_valign(indicator, GTK_ALIGN_CENTER);
    lazarus::gui::add_class(indicator, healthy ? "heartbeat-ok" : "heartbeat-warning");
    if (indicator_widget != nullptr) {
        *indicator_widget = indicator;
        g_object_add_weak_pointer(G_OBJECT(indicator), reinterpret_cast<gpointer*>(indicator_widget));
    }
    gtk_box_append(GTK_BOX(item), indicator);

    GtkWidget* copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(copy, TRUE);
    gtk_box_append(GTK_BOX(copy), label(title, "heartbeat-title"));
    GtkWidget* value_label = label(value, "heartbeat-value");
    if (value_widget != nullptr) {
        *value_widget = value_label;
        g_object_add_weak_pointer(G_OBJECT(value_label), reinterpret_cast<gpointer*>(value_widget));
    }
    gtk_box_append(GTK_BOX(copy), value_label);
    gtk_box_append(GTK_BOX(item), copy);
    return item;
}

GtkWidget* make_drive_status_aside(App* app) {
    GtkWidget* aside = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    lazarus::gui::add_class(aside, "workflow-section");
    gtk_widget_set_size_request(aside, 320, -1);
    GtkWidget* drive_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* drive_title = label("Connected Drives", "section-title");
    gtk_widget_set_hexpand(drive_title, TRUE);
    GtkWidget* refresh = gtk_button_new_with_label("Refresh");
    lazarus::gui::add_class(refresh, "secondary-command");
    gtk_widget_set_tooltip_text(refresh, "Re-scan connected disks and refresh Lazarus drive screens now.");
    g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
        auto* state = static_cast<App*>(data);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        set_status(state, "Refreshing connected drives...");
        load_data(state);
        refresh_operational_pages(state);
        set_status(state, "Connected drives refreshed.");
    }), app);
    gtk_box_append(GTK_BOX(drive_header), drive_title);
    gtk_box_append(GTK_BOX(drive_header), refresh);
    gtk_box_append(GTK_BOX(aside), drive_header);
    gtk_box_append(GTK_BOX(aside), label("Mount or safely disconnect any connected disk.", "section-detail"));
    GtkWidget* list = gtk_list_box_new();
    lazarus::gui::add_class(list, "choice-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    std::size_t count = 0;
    for (const auto& device : app->devices) {
        if (device.system) continue;
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_start(card, 10); gtk_widget_set_margin_end(card, 10);
        gtk_widget_set_margin_top(card, 8); gtk_widget_set_margin_bottom(card, 8);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), card);
        const auto title = !device.label.empty() ? device.label : (!device.model.empty() ? device.model : device.path);
        gtk_box_append(GTK_BOX(card), label(title, "choice-title"));
        auto detail = device.model.empty() ? human_bytes(device.size_bytes)
                                           : device.model + " | " + human_bytes(device.size_bytes);
        if (!device.transport.empty()) detail += " | " + device.transport;
        if (!device.serial_ending.empty()) detail += "\nSerial ending: " + device.serial_ending;
        detail += " | Role: " + (device.role.empty() ? std::string{"unconfigured"} : device.role);
        if (!device.label.empty()) detail += "\nConnection: " + device.label;
        else if (!device.physical_path.empty()) detail += "\nConnection: " + device.physical_path;
        gtk_box_append(GTK_BOX(card), label(detail, "choice-detail"));
        gtk_box_append(GTK_BOX(card), device_mount_controls(app, device));
        lazarus::gui::add_class(row, "choice-row");
        gtk_list_box_append(GTK_LIST_BOX(list), row);
        ++count;
    }
    if (count == 0) {
        gtk_box_append(GTK_BOX(aside), label("No drives are currently connected.", "empty-state"));
    } else {
        gtk_box_append(GTK_BOX(aside), choice_scroll(list, 460));
    }

    gtk_box_append(GTK_BOX(aside), label("Recent Activity", "section-title"));
    if (app->recent_activity.empty()) {
        gtk_box_append(GTK_BOX(aside), label("No backup, restore, or verify jobs have completed yet.", "empty-state"));
    } else {
        GtkWidget* activity_list = gtk_list_box_new();
        lazarus::gui::add_class(activity_list, "choice-list");
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(activity_list), GTK_SELECTION_NONE);
        for (const auto& entry : app->recent_activity) {
            GtkWidget* row = gtk_list_box_row_new();
            GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_set_margin_start(card, 10); gtk_widget_set_margin_end(card, 10);
            gtk_widget_set_margin_top(card, 6); gtk_widget_set_margin_bottom(card, 6);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), card);
            const auto who = entry.technician.empty() ? "Unknown technician" : entry.technician;
            gtk_box_append(GTK_BOX(card), label(who + " " + entry.verb, "choice-title"));
            gtk_box_append(GTK_BOX(card), label(entry.ticket + "  |  " + entry.customer + "  |  " + entry.timestamp, "choice-detail"));
            lazarus::gui::add_class(row, "choice-row");
            gtk_list_box_append(GTK_LIST_BOX(activity_list), row);
        }
        gtk_box_append(GTK_BOX(aside), choice_scroll(activity_list, 190));
    }
    return aside;
}

GtkWidget* make_home(App* app) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign(page, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(page, 32); gtk_widget_set_margin_end(page, 32); gtk_widget_set_margin_top(page, 16); gtk_widget_set_margin_bottom(page, 16);
    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_box_append(GTK_BOX(brand), lazarus::gui::make_logo(180, app->branding_logo));
    GtkWidget* intro = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_valign(intro, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(intro), label("Choose a task", "workflow-title"));
    gtk_box_append(GTK_BOX(intro), label("Each workflow guides one safe operation at a time.", "workflow-detail"));
    gtk_box_append(GTK_BOX(brand), intro);
    gtk_box_append(GTK_BOX(page), brand);

    GtkWidget* heartbeat = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(heartbeat), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(heartbeat), 4);
    lazarus::gui::add_class(heartbeat, "heartbeat-strip");
    gtk_grid_attach(GTK_GRID(heartbeat), heartbeat_item(
        "SERVICE", app->service_running ? "Running" : "Unavailable", app->service_running), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(heartbeat), heartbeat_item(
        "BENCH", app->bench_protected ? "Protected" : "Needs configuration", app->bench_protected), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(heartbeat), heartbeat_item(
        "IMAGE STORAGE", app->storage_online ? "Connected" : "Offline", app->storage_online), 2, 0, 1, 1);
    const std::string network_value = !app->network_address.empty()
        ? app->network_address : (app->network_online ? "No IPv4 address" : "Offline");
    gtk_grid_attach(GTK_GRID(heartbeat), heartbeat_item(
        "NETWORK IP", network_value.c_str(), !app->network_address.empty(),
        &app->network_status_label, &app->network_status_indicator), 3, 0, 1, 1);
    gtk_box_append(GTK_BOX(page), heartbeat);

    std::ostringstream ribbon_text;
    ribbon_text << (app->bench_name.empty() ? "Lazarus Appliance" : app->bench_name)
                << "  |  Source ports locked: " << app->source_port_count
                << "  |  Destination ports locked: " << app->destination_port_count;
    gtk_box_append(GTK_BOX(page), label(ribbon_text.str(), "bench-ribbon"));

    GtkWidget* grid = gtk_grid_new(); gtk_grid_set_column_spacing(GTK_GRID(grid), 12); gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Create Backup", "Image a customer drive connected to a source port.", "image", "backup"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Restore Backup", "Restore a verified image to a destination drive.", "restore", "restore"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Recover Files", "Browse an image read-only and copy selected data.", "recover-files", "recover-files"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Universal Restore", "Restore Windows onto replacement hardware.", "windows-repair", "driver-migration"), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Boot Repair", "Diagnose and repair the EFI fallback boot path on a disk.", "windows-repair", "boot-repair"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), action_button(app, "Review Tickets", "Search job history, backup state, escalation, and reports.", "image", "tickets"), 1, 2, 1, 1);
    gtk_box_append(GTK_BOX(page), grid);

    GtkWidget* utility = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    lazarus::gui::add_class(utility, "home-utility");
    GtkWidget* utility_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(utility_copy, TRUE);
    gtk_box_append(GTK_BOX(utility_copy), label("Appliance", "section-title"));
    gtk_box_append(GTK_BOX(utility_copy), label("System power and protected bench configuration.", "section-detail"));
    gtk_box_append(GTK_BOX(utility), utility_copy);
    GtkWidget* restart = gtk_button_new_with_label("Restart");
    GtkWidget* shutdown = gtk_button_new_with_label("Shut Down");
    GtkWidget* diagnostics = gtk_button_new_with_label("Run Diagnostics");
    GtkWidget* verify = gtk_button_new_with_label("Verify Backup");
    GtkWidget* admin = gtk_button_new_with_label("Administration");
    lazarus::gui::add_class(restart, "secondary-command");
    lazarus::gui::add_class(shutdown, "secondary-command");
    lazarus::gui::add_class(diagnostics, "secondary-command");
    lazarus::gui::add_class(verify, "secondary-command");
    lazarus::gui::add_class(admin, "secondary-command");
    g_object_set_data(G_OBJECT(restart), "page", const_cast<char*>("power-restart"));
    g_object_set_data(G_OBJECT(shutdown), "page", const_cast<char*>("power-shutdown"));
    g_object_set_data(G_OBJECT(diagnostics), "page", const_cast<char*>("diagnostics"));
    g_object_set_data(G_OBJECT(verify), "page", const_cast<char*>("verify"));
    g_signal_connect(restart, "clicked", G_CALLBACK(+[](GtkButton* source, gpointer data) {
        show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(source), "page")));
    }), app);
    g_signal_connect(shutdown, "clicked", G_CALLBACK(+[](GtkButton* source, gpointer data) {
        show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(source), "page")));
    }), app);
    g_signal_connect(diagnostics, "clicked", G_CALLBACK(+[](GtkButton* source, gpointer data) {
        show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(source), "page")));
    }), app);
    g_signal_connect(verify, "clicked", G_CALLBACK(+[](GtkButton* source, gpointer data) {
        show_page(static_cast<App*>(data), static_cast<const char*>(g_object_get_data(G_OBJECT(source), "page")));
    }), app);
    g_signal_connect_swapped(admin, "clicked", G_CALLBACK(+[](App* state) { enter_admin(state); }), app);
    gtk_box_append(GTK_BOX(utility), restart);
    gtk_box_append(GTK_BOX(utility), shutdown);
    gtk_box_append(GTK_BOX(utility), diagnostics);
    gtk_box_append(GTK_BOX(utility), verify);
    gtk_box_append(GTK_BOX(utility), admin);
    gtk_box_append(GTK_BOX(page), utility);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_append(GTK_BOX(layout), page);
    gtk_box_append(GTK_BOX(layout), make_drive_status_aside(app));
    return layout;
}

void activate(GtkApplication* application, gpointer data) {
    auto* app = static_cast<App*>(data); lazarus::gui::install_theme();
    gui_trace("activate: begin");
    std::fprintf(stderr, "lazarus-gui: workflow activate\n");
    app->window = gtk_application_window_new(application); gtk_window_set_title(GTK_WINDOW(app->window), "Arcology Lazarus"); gtk_window_set_default_size(GTK_WINDOW(app->window), 1024, 768); gtk_window_set_resizable(GTK_WINDOW(app->window), TRUE); gtk_widget_set_cursor_from_name(app->window, "default"); lazarus::gui::add_class(app->window, "lazarus-window");
    gui_trace("activate: window created");
    // The appliance intentionally has no window manager. GTK's fullscreen
    // request therefore has nobody to resize the window, so use the active
    // monitor geometry as the initial kiosk size as well.
    if (GdkDisplay* display = gdk_display_get_default()) {
        if (GListModel* monitors = gdk_display_get_monitors(display); g_list_model_get_n_items(monitors) > 0) {
            GdkMonitor* monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
            GdkRectangle geometry{};
            gdk_monitor_get_geometry(monitor, &geometry);
            gtk_window_set_default_size(GTK_WINDOW(app->window), geometry.width, geometry.height);
            g_object_unref(monitor);
        }
    }
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_window_set_child(GTK_WINDOW(app->window), root);
    app->stack = gtk_stack_new(); gtk_stack_set_transition_type(GTK_STACK(app->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE); gtk_stack_set_hhomogeneous(GTK_STACK(app->stack), FALSE); gtk_stack_set_vhomogeneous(GTK_STACK(app->stack), FALSE); gtk_widget_set_hexpand(app->stack, TRUE); gtk_widget_set_vexpand(app->stack, TRUE); gtk_box_append(GTK_BOX(root), app->stack);
    app->status = gtk_label_new("Loading Lazarus service..."); gtk_label_set_xalign(GTK_LABEL(app->status), 0); lazarus::gui::add_class(app->status, "bottom-bar"); gtk_box_append(GTK_BOX(root), app->status);
    gui_trace("activate: root widgets created");
    load_data(app);
    gui_trace("activate: service data loaded");
    lazarus::gui::install_theme(app->branding_accent, app->branding_background, app->branding_surface,
                                app->branding_text, app->branding_icon);
    gui_trace("activate: theme installed");
    add_page(app, make_home(app), "home", "Home");
    gui_trace("activate: home page added");
    add_page(app, make_tickets_page(app), "tickets", "Review Tickets");
    gui_trace("activate: tickets page added");
    add_page(app, make_backup_page(app), "backup", "Create Backup");
    gui_trace("activate: backup page added");
    add_page(app, make_restore_page(app), "restore", "Restore Backup");
    gui_trace("activate: restore page added");
    add_page(app, make_verify_page(app), "verify", "Verify Backup");
    gui_trace("activate: verify page added");
    add_page(app, make_recovery_page(app), "recover-files", "Recover Files");
    gui_trace("activate: recovery page added");
    add_page(app, make_driver_migration_page(app), "driver-migration", "Universal Restore");
    gui_trace("activate: migration page added");
    add_page(app, make_boot_repair_page(app), "boot-repair", "Boot Repair");
    gui_trace("activate: boot repair page added");
    add_page(app, make_diagnostics_page(app), "diagnostics", "Run Diagnostics");
    gui_trace("activate: diagnostics page added");
    add_page(app, make_power_page(app, true), "power-restart", "Restart Lazarus");
    add_page(app, make_power_page(app, false), "power-shutdown", "Shut Down Lazarus");
    gui_trace("activate: power pages added");
    add_page(app, make_admin_page(app), "admin", "Administration");
    gui_trace("activate: admin page added");
    add_page(app, make_admin_security_page(app), "admin-security", "Password and Recovery");

    add_page(app, make_technicians_page(app), "admin-technicians", "Technicians");
    gui_trace("activate: security page added");
    add_page(app, make_printers_page(app), "admin-printers", "Printers and Reports");
    gui_trace("activate: printers page added");
    add_page(app, make_network_page(app), "admin-network", "Network");
    gui_trace("activate: network page added");
    add_page(app, make_port_roles_page(app), "admin-port-roles", "Physical Port Roles");
    gui_trace("activate: port roles page added");
    add_page(app, make_port_policy_page(app, true), "admin-source", "Source Ports");
    add_page(app, make_port_policy_page(app, false), "admin-destination", "Destination Ports");
    gui_trace("activate: port policy pages added");
    add_page(app, make_storage_page(app), "admin-storage", "Image Storage");
    add_page(app, make_storage_local_page(app), "admin-storage-local", "Local Drive Storage");
    add_page(app, make_storage_network_page(app), "admin-storage-network", "Network Storage");
    gui_trace("activate: storage page added");
    add_page(app, make_labels_page(app), "admin-labels", "Port Labels");
    gui_trace("activate: labels page added");
    add_page(app, make_branding_page(app), "admin-branding", "Branding");
    gui_trace("activate: branding page added");
    add_page(app, make_install_page(app), "admin-install", "Install Lazarus OS");
    gui_trace("activate: install page added");
    show_page(app, "home");
    // The recovery appliance has no window manager. The monitor-sized window
    // above already owns the display; requesting fullscreen as well causes GTK
    // to offset the unmanaged surface and clip its top edge.
    if (std::getenv("LAZARUS_KIOSK_FULLSCREEN") != nullptr) {
        gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);
    }
    gtk_window_present(GTK_WINDOW(app->window));
    g_idle_add(+[](gpointer data) -> gboolean {
        auto* state = static_cast<App*>(data);
        char geometry[96]{};
        std::snprintf(geometry, sizeof(geometry), "activate: allocated window %dx%d",
                      gtk_widget_get_width(state->window), gtk_widget_get_height(state->window));
        gui_trace(geometry);
        std::fprintf(stderr, "lazarus-gui: allocated window %dx%d\n",
                     gtk_widget_get_width(state->window), gtk_widget_get_height(state->window));
        return G_SOURCE_REMOVE;
    }, app);
    gui_trace("activate: window presented");
    g_timeout_add(1250, +[](gpointer data) -> gboolean {
        auto* state = static_cast<App*>(data);
        gui_trace("device poll: begin");
        std::string response;
        if (!request("{\"command\":\"device_generation\"}", response)) {
            gui_trace("device poll: request failed");
            if (state->service_running && state->active_operations == 0) {
                state->service_running = false;
                refresh_operational_pages(state);
                set_status(state, "Lazarus service is unavailable. Reconnecting automatically...");
            }
            return G_SOURCE_CONTINUE;
        }
        gui_trace("device poll: response received");
        const auto generation = json_string(response, "device_generation");
        if (!state->service_running) {
            gui_trace("device poll: service restored");
            load_data(state);
            if (state->service_running && state->active_operations == 0) {
                refresh_operational_pages(state);
                set_status(state, "Lazarus service restored. Workflows are ready.");
            }
            return G_SOURCE_CONTINUE;
        }
        if (generation.empty() || generation == state->device_generation) {
            gui_trace("device poll: unchanged");
            return G_SOURCE_CONTINUE;
        }
        if (state->active_operations != 0) {
            gui_trace("device poll: operation active");
            return G_SOURCE_CONTINUE;
        }
        gui_trace("device poll: rebuilding pages");
        state->device_generation = generation;
        load_data(state);
        refresh_operational_pages(state);
        set_status(state, "Connected drives changed. Lazarus refreshed this workflow automatically.");
        return G_SOURCE_CONTINUE;
    }, app);
    g_timeout_add_seconds(3, +[](gpointer data) -> gboolean {
        auto* state = static_cast<App*>(data);
        gui_trace("network poll: begin");
        std::string response;
        if (!request("{\"command\":\"network_status\"}", response) ||
            response.find("\"ok\":true") == std::string::npos) {
            gui_trace("network poll: request failed");
            return G_SOURCE_CONTINUE;
        }
        gui_trace("network poll: response received");
        state->network_online = json_bool(response, "online");
        state->network_address = json_string(response, "ipv4");
        const std::string value = !state->network_address.empty()
            ? state->network_address : (state->network_online ? "No IPv4 address" : "Offline");
        if (state->network_status_label != nullptr) {
            gui_trace("network poll: updating label");
            gtk_label_set_text(GTK_LABEL(state->network_status_label), value.c_str());
            gui_trace("network poll: label updated");
        }
        if (state->network_status_indicator != nullptr) {
            gui_trace("network poll: updating indicator");
            gtk_widget_remove_css_class(state->network_status_indicator, "heartbeat-ok");
            gtk_widget_remove_css_class(state->network_status_indicator, "heartbeat-warning");
            lazarus::gui::add_class(state->network_status_indicator,
                                    state->network_address.empty() ? "heartbeat-warning" : "heartbeat-ok");
            gui_trace("network poll: indicator updated");
        }
        gui_trace("network poll: complete");
        return G_SOURCE_CONTINUE;
    }, app);
    std::fprintf(stderr, "lazarus-gui: workflow window presented\n");
}

} // namespace

int main(int, char** argv) {
    App state; GtkApplication* app = gtk_application_new("org.arcology.lazarus", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &state);
    // The appliance must stay alive for the duration of the kiosk session.
    // Keep it independently of any transient X11 window-manager behavior.
    g_application_hold(G_APPLICATION(app));
    std::fprintf(stderr, "lazarus-gui: starting workflow application\n");
    // The kiosk passes a bench-profile path for the service, not as a document
    // for GtkApplication to open. Keep it out of GTK's command-line handling.
    const int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
