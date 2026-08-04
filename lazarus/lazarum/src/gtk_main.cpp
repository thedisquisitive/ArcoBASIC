#include "lazarum/viewer.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ViewerApp {
    GtkApplication* application = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* mount_button = nullptr;
    GtkWidget* open_button = nullptr;
    GtkWidget* refresh_button = nullptr;
    GtkWidget* search = nullptr;
    GtkWidget* filter_button = nullptr;
    GtkWidget* storage_label = nullptr;
    GtkWidget* status_label = nullptr;
    GtkWidget* image_list = nullptr;
    GtkWidget* image_count = nullptr;
    GtkWidget* report_list = nullptr;
    GtkWidget* report_title = nullptr;
    GtkWidget* report_view = nullptr;
    GtkWidget* extract_report_button = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* detail_state = nullptr;
    GtkWidget* detail_ticket = nullptr;
    GtkWidget* detail_customer = nullptr;
    GtkWidget* detail_technician = nullptr;
    GtkWidget* detail_purpose = nullptr;
    GtkWidget* detail_created = nullptr;
    GtkWidget* detail_source = nullptr;
    GtkWidget* detail_size = nullptr;
    GtkWidget* detail_compression = nullptr;
    GtkWidget* warning_label = nullptr;
    GtkWidget* files_status = nullptr;
    GtkWidget* files_prepare_button = nullptr;
    GtkWidget* files_volume_dropdown = nullptr;
    GtkWidget* files_path_label = nullptr;
    GtkWidget* files_up_button = nullptr;
    GtkWidget* files_list = nullptr;
    GtkWidget* files_extract_button = nullptr;
    GtkWidget* details_view = nullptr;
    GtkWidget* drive_connection = nullptr;
    GtkWidget* drive_filesystem = nullptr;
    GtkWidget* drive_capacity = nullptr;
    GtkWidget* drive_used = nullptr;
    GtkWidget* drive_free = nullptr;
    GtkWidget* summary_integrity = nullptr;
    GtkWidget* summary_state = nullptr;
    GtkWidget* summary_size = nullptr;
    GtkWidget* summary_reports = nullptr;
    std::vector<lazarum::ImageSummary> images;
    std::vector<lazarum::ImageVolume> file_volumes;
    std::vector<lazarum::ImageFileEntry> file_entries;
    std::unique_ptr<lazarum::ImageDataProvider> data_provider;
    fs::path storage_root;
    fs::path files_image_directory;
    std::string files_relative_path;
    gulong files_volume_signal = 0;
    int selected_image = -1;
    std::string selected_report;
    std::string initial_path;
    std::thread worker;
};

struct ScanCompletion {
    ViewerApp* app = nullptr;
    lazarum::ScanResult result;
};

struct MountCompletion {
    ViewerApp* app = nullptr;
    lazarum::MountResult result;
};

struct ExtractDialogState {
    ViewerApp* app = nullptr;
    fs::path image_directory;
    std::string report_name;
};

enum class DataOperation {
    Volumes,
    Directory,
    Extract,
};

struct DataCompletion {
    ViewerApp* app = nullptr;
    DataOperation operation = DataOperation::Volumes;
    fs::path image_directory;
    std::string volume_id;
    std::string relative_path;
    lazarum::DataOperationResult result;
};

struct FileExtractDialogState {
    ViewerApp* app = nullptr;
    fs::path image_directory;
    std::string volume_id;
    std::vector<std::string> relative_paths;
};

struct ProviderProgress {
    ViewerApp* app = nullptr;
    std::string message;
};

std::string human_bytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output.setf(std::ios::fixed);
    output.precision(unit == 0 ? 0 : 1);
    output << value << ' ' << units[unit];
    return output.str();
}

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

std::string image_search_text(const lazarum::ImageSummary& image) {
    return lower_ascii(image.ticket_number + " " + image.customer_name + " " + image.technician + " " +
                       image.purpose + " " + image.created_at + " " + image.source_model + " " +
                       image.directory.string());
}

std::string html_to_text(const std::string& html) {
    std::string document = html;
    for (const std::string tag : {"head", "style", "script"}) {
        for (;;) {
            const auto start = document.find("<" + tag);
            if (start == std::string::npos) break;
            const auto end = document.find("</" + tag + ">", start);
            if (end == std::string::npos) {
                document.erase(start);
                break;
            }
            document.erase(start, end + tag.size() + 3 - start);
        }
    }
    std::string output;
    bool in_tag = false;
    for (std::size_t index = 0; index < document.size(); ++index) {
        const char character = document[index];
        if (character == '<') {
            in_tag = true;
            if (!output.empty() && output.back() != '\n') output.push_back('\n');
        } else if (character == '>') {
            in_tag = false;
        } else if (!in_tag) {
            if (document.compare(index, 6, "&nbsp;") == 0) {
                output.push_back(' ');
                index += 5;
            } else if (document.compare(index, 5, "&amp;") == 0) {
                output.push_back('&');
                index += 4;
            } else if (document.compare(index, 4, "&lt;") == 0) {
                output.push_back('<');
                index += 3;
            } else if (document.compare(index, 4, "&gt;") == 0) {
                output.push_back('>');
                index += 3;
            } else if (document.compare(index, 6, "&quot;") == 0) {
                output.push_back('"');
                index += 5;
            } else if (document.compare(index, 5, "&#39;") == 0) {
                output.push_back('\'');
                index += 4;
            } else {
                output.push_back(character);
            }
        }
    }
    std::string compact;
    int newlines = 0;
    for (const char character : output) {
        if (character == '\r') continue;
        if (character == '\n') {
            if (++newlines <= 2) compact.push_back(character);
        } else {
            newlines = 0;
            compact.push_back(character);
        }
    }
    return compact;
}

GtkWidget* label(const char* text, const char* css_class = nullptr) {
    GtkWidget* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0F);
    gtk_label_set_wrap(GTK_LABEL(widget), TRUE);
    if (css_class != nullptr) gtk_widget_add_css_class(widget, css_class);
    return widget;
}

GtkWidget* icon_button(const char* icon, const char* text) {
    GtkWidget* button = gtk_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget* image = gtk_image_new_from_icon_name(icon);
    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    gtk_button_set_child(GTK_BUTTON(button), box);
    return button;
}

fs::path lazarum_asset_path(const char* filename) {
    if (const char* configured = std::getenv("LAZARUM_ASSET_DIR"); configured != nullptr && *configured != '\0') {
        const auto candidate = fs::path(configured) / filename;
        if (fs::exists(candidate)) return candidate;
    }
#ifdef LAZARUM_SOURCE_DATA_DIR
    const auto data = fs::path(LAZARUM_SOURCE_DATA_DIR) / filename;
    if (fs::exists(data)) return data;
#endif
#ifdef LAZARUM_SOURCE_ASSET_DIR
    const auto source = fs::path(LAZARUM_SOURCE_ASSET_DIR) / filename;
    if (fs::exists(source)) return source;
#endif
    for (const auto& directory : {fs::path("/usr/share/lazarum"), fs::path("/usr/local/share/lazarum")}) {
        const auto candidate = directory / filename;
        if (fs::exists(candidate)) return candidate;
    }
    return {};
}

GtkWidget* asset_image(const char* filename, int width, int height, const char* fallback_icon) {
    const auto path = lazarum_asset_path(filename);
    GtkWidget* image = nullptr;
    if (!path.empty()) {
        GError* error = nullptr;
        GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(path.c_str(), width, height, TRUE, &error);
        if (pixbuf != nullptr) {
            GdkTexture* texture = gdk_texture_new_for_pixbuf(pixbuf);
            image = gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
            g_object_unref(texture);
            g_object_unref(pixbuf);
        }
        if (error != nullptr) g_error_free(error);
    }
    if (image == nullptr) {
        image = gtk_image_new_from_icon_name(fallback_icon);
        gtk_image_set_pixel_size(GTK_IMAGE(image), std::min(width, height));
    }
    gtk_widget_set_size_request(image, width, height);
    gtk_widget_set_hexpand(image, FALSE);
    gtk_widget_set_vexpand(image, FALSE);
    return image;
}

GtkWidget* tab_label(const char* icon, const char* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    return box;
}

void set_status(ViewerApp* app, const std::string& text, bool error = false) {
    gtk_label_set_text(GTK_LABEL(app->status_label), text.c_str());
    if (error) gtk_widget_add_css_class(app->status_label, "error-text");
    else gtk_widget_remove_css_class(app->status_label, "error-text");
}

void set_text_view(GtkWidget* view, const std::string& contents) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, contents.c_str(), static_cast<gint>(contents.size()));
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view), &start, 0.0, FALSE, 0.0, 0.0);
}

void clear_list(GtkWidget* list) {
    while (GtkWidget* child = gtk_widget_get_first_child(list)) {
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
    }
}

std::string selected_volume_id(const ViewerApp* app) {
    if (app->files_volume_dropdown == nullptr || app->file_volumes.empty()) return {};
    const auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->files_volume_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= app->file_volumes.size()) return {};
    return app->file_volumes[selected].id;
}

void reset_files_page(ViewerApp* app, const char* message) {
    app->file_volumes.clear();
    app->file_entries.clear();
    app->files_image_directory.clear();
    app->files_relative_path.clear();
    if (app->files_list != nullptr) clear_list(app->files_list);
    if (app->files_volume_dropdown != nullptr) {
        GtkStringList* empty = gtk_string_list_new(nullptr);
        gtk_drop_down_set_model(GTK_DROP_DOWN(app->files_volume_dropdown), G_LIST_MODEL(empty));
        g_object_unref(empty);
        gtk_widget_set_sensitive(app->files_volume_dropdown, FALSE);
    }
    if (app->files_path_label != nullptr) gtk_label_set_text(GTK_LABEL(app->files_path_label), "/");
    if (app->files_prepare_button != nullptr) gtk_widget_set_sensitive(app->files_prepare_button, FALSE);
    if (app->files_up_button != nullptr) gtk_widget_set_sensitive(app->files_up_button, FALSE);
    if (app->files_extract_button != nullptr) gtk_widget_set_sensitive(app->files_extract_button, FALSE);
    if (app->files_status != nullptr) gtk_label_set_text(GTK_LABEL(app->files_status), message);
}

void start_volume_discovery(ViewerApp* app);
void start_directory_listing(ViewerApp* app, std::string volume_id, std::string relative_path);
void start_file_extraction(ViewerApp* app, fs::path image_directory, std::string volume_id,
                           std::vector<std::string> relative_paths, fs::path destination);
void on_volume_selected(GObject*, GParamSpec*, gpointer data);

void set_detail(GtkWidget* widget, const std::string& value, const char* fallback = "—") {
    gtk_label_set_text(GTK_LABEL(widget), value.empty() ? fallback : value.c_str());
}

void clear_selection_details(ViewerApp* app) {
    app->selected_image = -1;
    app->selected_report.clear();
    for (GtkWidget* widget : {app->detail_state, app->detail_ticket, app->detail_customer,
                              app->detail_technician, app->detail_purpose, app->detail_created,
                              app->detail_source, app->detail_size, app->detail_compression}) {
        set_detail(widget, "");
    }
    gtk_label_set_text(GTK_LABEL(app->warning_label), "Select an image to review its job and integrity state.");
    reset_files_page(app, "Select an image, then open Files for immediate read-only access.");
    set_detail(app->summary_integrity, "Unknown");
    set_detail(app->summary_state, "—");
    set_detail(app->summary_size, "—");
    set_detail(app->summary_reports, "—");
    set_text_view(app->details_view, "Select an image to view its format, paths, reports, and recorded warnings.");
    gtk_label_set_text(GTK_LABEL(app->report_title), "No report selected");
    set_text_view(app->report_view, "");
    clear_list(app->report_list);
    gtk_widget_set_sensitive(app->extract_report_button, FALSE);
}

GtkWidget* make_empty_image_row() {
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(content, "empty-state");
    gtk_widget_set_size_request(content, -1, 286);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_widget_set_margin_start(content, 8);
    gtk_widget_set_margin_end(content, 8);
    GtkWidget* image = asset_image("lazarum-empty-drive.svg", 132, 108, "drive-harddisk-symbolic");
    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_opacity(image, 0.72);
    gtk_box_append(GTK_BOX(content), image);
    GtkWidget* title = label("No images found", "empty-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5F);
    gtk_box_append(GTK_BOX(content), title);
    GtkWidget* detail = label("Mount a Lazarus drive or open an existing mount to view images.", "muted");
    gtk_label_set_xalign(GTK_LABEL(detail), 0.5F);
    gtk_label_set_justify(GTK_LABEL(detail), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), detail);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

GtkWidget* make_image_row(const lazarum::ImageSummary& image, std::size_t index) {
    GtkWidget* row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "lazarum-index", GUINT_TO_POINTER(static_cast<guint>(index + 1)));
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(content, 10);
    gtk_widget_set_margin_bottom(content, 10);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);

    GtkWidget* first = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const bool ready = image.finalized && !image.incomplete;
    GtkWidget* state = label(ready ? "READY" : "INCOMPLETE", ready ? "ready-badge" : "warning-badge");
    gtk_box_append(GTK_BOX(first), state);
    const std::string title = image.ticket_number.empty() ? image.directory.filename().string()
                                                           : image.ticket_number + " — " + image.customer_name;
    GtkWidget* title_label = label(title.c_str(), "image-title");
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(first), title_label);
    gtk_box_append(GTK_BOX(content), first);

    const std::string detail = image.purpose + (image.created_at.empty() ? "" : "  •  " + image.created_at);
    gtk_box_append(GTK_BOX(content), label(detail.c_str(), "muted"));
    const std::string source = image.source_model + "  •  " + human_bytes(image.logical_bytes) +
                               " logical  •  " + image.compression;
    gtk_box_append(GTK_BOX(content), label(source.c_str(), "muted-small"));
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

void rebuild_image_list(ViewerApp* app) {
    clear_list(app->image_list);
    const char* entered = gtk_editable_get_text(GTK_EDITABLE(app->search));
    const std::string query = lower_ascii(entered == nullptr ? "" : entered);
    const bool ready_only = app->filter_button != nullptr &&
                            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->filter_button));
    std::size_t visible = 0;
    for (std::size_t index = 0; index < app->images.size(); ++index) {
        if (ready_only && (!app->images[index].finalized || app->images[index].incomplete)) continue;
        if (!query.empty() && image_search_text(app->images[index]).find(query) == std::string::npos) continue;
        gtk_list_box_append(GTK_LIST_BOX(app->image_list), make_image_row(app->images[index], index));
        ++visible;
    }
    if (visible == 0) gtk_list_box_append(GTK_LIST_BOX(app->image_list), make_empty_image_row());
    const std::string count = std::to_string(visible) + (visible == 1 ? " image" : " images");
    gtk_label_set_text(GTK_LABEL(app->image_count), count.c_str());
    clear_selection_details(app);
    if (visible != 0) {
        GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(app->image_list), 0);
        if (first != nullptr) gtk_list_box_select_row(GTK_LIST_BOX(app->image_list), first);
    }
}

GtkWidget* make_report_row(const lazarum::ReportInfo& report) {
    GtkWidget* row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(row), "lazarum-report", g_strdup(report.name.c_str()), g_free);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);
    gtk_box_append(GTK_BOX(content), gtk_image_new_from_icon_name("text-x-generic-symbolic"));
    GtkWidget* name = label(report.name.c_str());
    gtk_widget_set_hexpand(name, TRUE);
    gtk_box_append(GTK_BOX(content), name);
    gtk_box_append(GTK_BOX(content), label(human_bytes(report.size_bytes).c_str(), "muted-small"));
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

void populate_selected_image(ViewerApp* app, std::size_t index) {
    if (index >= app->images.size()) return;
    app->selected_image = static_cast<int>(index);
    app->selected_report.clear();
    const auto& image = app->images[index];
    set_detail(app->detail_state, image.finalized && !image.incomplete ? "Ready for review" : "Incomplete");
    set_detail(app->detail_ticket, image.ticket_number);
    set_detail(app->detail_customer, image.customer_name);
    set_detail(app->detail_technician, image.technician);
    set_detail(app->detail_purpose, image.purpose);
    set_detail(app->detail_created, image.created_at);
    set_detail(app->detail_source, image.source_model +
        (image.source_serial_ending.empty() ? "" : " (serial ending " + image.source_serial_ending + ")"));
    set_detail(app->detail_size, human_bytes(image.logical_bytes) + " logical / " +
        human_bytes(image.stored_bytes) + " stored");
    set_detail(app->detail_compression, image.compression);

    if (image.warnings.empty()) {
        gtk_label_set_text(GTK_LABEL(app->warning_label),
                           "Image structure recognized. Use the saved reports for the recorded verification result.");
        gtk_widget_remove_css_class(app->warning_label, "error-card");
    } else {
        std::string warnings;
        for (const auto& warning : image.warnings) warnings += (warnings.empty() ? "" : "\n") + warning;
        gtk_label_set_text(GTK_LABEL(app->warning_label), warnings.c_str());
        gtk_widget_add_css_class(app->warning_label, "error-card");
    }
    reset_files_page(app, "Open the Files tab to browse this image on demand.");
    const auto data_capability = app->data_provider->capability(image.directory);
    gtk_label_set_text(GTK_LABEL(app->files_status), data_capability.detail.c_str());
    gtk_widget_set_sensitive(app->files_prepare_button, data_capability.filesystem_explorer_available);
    if (data_capability.filesystem_explorer_available && app->notebook != nullptr &&
        gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) == 1) {
        start_volume_discovery(app);
    }
    set_detail(app->summary_integrity, image.reports.empty() ? "No report" : "Report saved");
    set_detail(app->summary_state, image.finalized && !image.incomplete ? "Ready" : "Incomplete");
    set_detail(app->summary_size, human_bytes(image.logical_bytes));
    set_detail(app->summary_reports, std::to_string(image.reports.size()));

    std::ostringstream details;
    details << "IMAGE DIRECTORY\n" << image.directory.string() << "\n\n"
            << "FORMAT\nLazarus directory image v" << image.format_version << "\n\n"
            << "STATE\n" << (image.finalized ? "FINALIZED present" : "FINALIZED missing") << "\n"
            << (image.incomplete ? "INCOMPLETE present" : "INCOMPLETE absent") << "\n"
            << (image.structurally_recognized ? "Required image files recognized" : "Image structure incomplete") << "\n\n"
            << "STORAGE\n" << human_bytes(image.logical_bytes) << " logical\n"
            << human_bytes(image.stored_bytes) << " stored\n"
            << image.compression << " compression\n\n"
            << "REPORTS\n";
    if (image.reports.empty()) details << "No recognized reports\n";
    for (const auto& report : image.reports) details << report.name << "  (" << human_bytes(report.size_bytes) << ")\n";
    if (!image.warnings.empty()) {
        details << "\nWARNINGS\n";
        for (const auto& warning : image.warnings) details << "• " << warning << "\n";
    }
    set_text_view(app->details_view, details.str());

    clear_list(app->report_list);
    for (const auto& report : image.reports) {
        gtk_list_box_append(GTK_LIST_BOX(app->report_list), make_report_row(report));
    }
    gtk_label_set_text(GTK_LABEL(app->report_title), image.reports.empty() ? "No saved reports" : "Select a report");
    set_text_view(app->report_view, image.reports.empty()
        ? "No recognized Lazarus job reports were found in this image directory." : "");
    gtk_widget_set_sensitive(app->extract_report_button, FALSE);
}

void on_image_selected(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    if (row == nullptr) {
        clear_selection_details(app);
        return;
    }
    const auto encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "lazarum-index"));
    if (encoded != 0) populate_selected_image(app, encoded - 1);
}

void on_report_selected(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    app->selected_report.clear();
    gtk_widget_set_sensitive(app->extract_report_button, FALSE);
    if (row == nullptr || app->selected_image < 0) return;
    const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "lazarum-report"));
    if (name == nullptr) return;
    app->selected_report = name;
    std::string error;
    auto contents = lazarum::read_report(app->images[static_cast<std::size_t>(app->selected_image)].directory,
                                         app->selected_report, error);
    gtk_label_set_text(GTK_LABEL(app->report_title), app->selected_report.c_str());
    if (!error.empty()) {
        set_text_view(app->report_view, error);
        set_status(app, error, true);
        return;
    }
    if (fs::path(app->selected_report).extension() == ".html") contents = html_to_text(contents);
    set_text_view(app->report_view, contents);
    gtk_widget_set_sensitive(app->extract_report_button, TRUE);
    set_status(app, "Report opened read-only.");
}

void set_busy(ViewerApp* app, bool busy) {
    gtk_widget_set_sensitive(app->mount_button, !busy);
    gtk_widget_set_sensitive(app->open_button, !busy);
    gtk_widget_set_sensitive(app->refresh_button, !busy && !app->storage_root.empty());
    gtk_widget_set_sensitive(app->search, !busy && !app->images.empty());
    if (app->filter_button != nullptr) {
        gtk_widget_set_sensitive(app->filter_button, !busy && !app->images.empty());
    }
    if (app->image_list != nullptr) gtk_widget_set_sensitive(app->image_list, !busy);
}

void start_scan(ViewerApp* app, fs::path root);

void update_drive_status(ViewerApp* app) {
    if (app->storage_root.empty()) {
        for (GtkWidget* widget : {app->drive_connection, app->drive_filesystem, app->drive_capacity,
                                  app->drive_used, app->drive_free}) {
            set_detail(widget, "—");
        }
        return;
    }
    set_detail(app->drive_connection, "Mounted folder open");
    set_detail(app->drive_filesystem, "Host-managed mount");
    std::error_code error;
    const auto space = fs::space(app->storage_root, error);
    if (error) {
        set_detail(app->drive_capacity, "Unavailable");
        set_detail(app->drive_used, "Unavailable");
        set_detail(app->drive_free, "Unavailable");
        return;
    }
    set_detail(app->drive_capacity, human_bytes(space.capacity));
    set_detail(app->drive_used, human_bytes(space.capacity >= space.available ? space.capacity - space.available : 0));
    set_detail(app->drive_free, human_bytes(space.available));
}

gboolean finish_scan(gpointer data) {
    std::unique_ptr<ScanCompletion> completion(static_cast<ScanCompletion*>(data));
    ViewerApp* app = completion->app;
    app->storage_root = completion->result.storage_root;
    app->images = std::move(completion->result.images);
    const std::string storage = app->storage_root.string();
    gtk_label_set_text(GTK_LABEL(app->storage_label), storage.c_str());
    update_drive_status(app);
    rebuild_image_list(app);
    set_busy(app, false);
    gtk_widget_set_sensitive(app->search, !app->images.empty());
    gtk_widget_set_sensitive(app->filter_button, !app->images.empty());
    if (completion->result.warnings.empty()) {
        set_status(app, "Drive opened read-only. " + std::to_string(app->images.size()) + " image(s) found.");
    } else {
        set_status(app, completion->result.warnings.front(), true);
    }
    return G_SOURCE_REMOVE;
}

void start_scan(ViewerApp* app, fs::path root) {
    if (app->worker.joinable()) app->worker.join();
    set_busy(app, true);
    set_status(app, "Scanning for Lazarus images…");
    app->worker = std::thread([app, root = std::move(root)] {
        auto* completion = new ScanCompletion{app, lazarum::scan_storage(root)};
        g_idle_add(finish_scan, completion);
    });
}

gboolean finish_mount(gpointer data) {
    std::unique_ptr<MountCompletion> completion(static_cast<MountCompletion*>(data));
    ViewerApp* app = completion->app;
    if (!completion->result.mounted || completion->result.mount_point.empty()) {
        set_busy(app, false);
        set_status(app, completion->result.error.empty() ? "The drive could not be mounted read-only."
                                                          : completion->result.error, true);
        return G_SOURCE_REMOVE;
    }
    set_status(app, "Drive mounted read-only. Scanning images…");
    start_scan(app, completion->result.mount_point);
    return G_SOURCE_REMOVE;
}

void on_mount_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    const auto device = lazarum::discover_lazarus_storage_device();
    if (!device) {
        set_status(app, "No drive labeled LAZARUS_STORAGE was found. Use Open Mounted Drive if it is already mounted.", true);
        return;
    }
    if (app->worker.joinable()) app->worker.join();
    set_busy(app, true);
    set_status(app, "Requesting a non-writing ext4 mount…");
    app->worker = std::thread([app, device = *device] {
        auto* completion = new MountCompletion{app, lazarum::mount_ext4_read_only(device)};
        g_idle_add(finish_mount, completion);
    });
}

void on_open_folder_ready(GObject* source, GAsyncResult* result, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    GError* error = nullptr;
    GFile* selected = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
    if (selected != nullptr) {
        char* path = g_file_get_path(selected);
        if (path != nullptr) {
            start_scan(app, path);
            g_free(path);
        } else {
            set_status(app, "Choose a local mounted filesystem folder.", true);
        }
        g_object_unref(selected);
    } else if (error != nullptr && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        set_status(app, error->message, true);
    }
    g_clear_error(&error);
}

void on_open_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open mounted Lazarus storage");
    gtk_file_dialog_set_accept_label(dialog, "Open Drive");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(app->window), nullptr, on_open_folder_ready, app);
    g_object_unref(dialog);
}

void on_extract_folder_ready(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<ExtractDialogState> state(static_cast<ExtractDialogState*>(data));
    GError* dialog_error = nullptr;
    GFile* selected = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &dialog_error);
    if (selected != nullptr) {
        char* path = g_file_get_path(selected);
        if (path != nullptr) {
            fs::path written;
            std::string error;
            if (lazarum::extract_report(state->image_directory, state->report_name, path, written, error)) {
                set_status(state->app, "Report extracted without modifying the image: " + written.string());
            } else {
                set_status(state->app, error, true);
            }
            g_free(path);
        }
        g_object_unref(selected);
    } else if (dialog_error != nullptr &&
               !g_error_matches(dialog_error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
        set_status(state->app, dialog_error->message, true);
    }
    g_clear_error(&dialog_error);
}

void on_extract_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    if (app->selected_image < 0 || app->selected_report.empty()) return;
    auto* state = new ExtractDialogState{
        app,
        app->images[static_cast<std::size_t>(app->selected_image)].directory,
        app->selected_report,
    };
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Extract report to folder");
    gtk_file_dialog_set_accept_label(dialog, "Extract Here");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(app->window), nullptr, on_extract_folder_ready, state);
    g_object_unref(dialog);
}

void on_search_changed(GtkEditable*, gpointer data) {
    rebuild_image_list(static_cast<ViewerApp*>(data));
}

void on_filter_toggled(GtkToggleButton*, gpointer data) {
    rebuild_image_list(static_cast<ViewerApp*>(data));
}

void on_refresh_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    if (!app->storage_root.empty()) start_scan(app, app->storage_root);
}

gboolean apply_provider_progress(gpointer data) {
    std::unique_ptr<ProviderProgress> progress(static_cast<ProviderProgress*>(data));
    if (progress->app->files_status != nullptr) {
        gtk_label_set_text(GTK_LABEL(progress->app->files_status), progress->message.c_str());
    }
    if (progress->app->status_label != nullptr) set_status(progress->app, progress->message);
    return G_SOURCE_REMOVE;
}

GtkWidget* make_file_row(const lazarum::ImageFileEntry& entry, std::size_t index) {
    GtkWidget* row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "lazarum-file-index",
                      GUINT_TO_POINTER(static_cast<guint>(index + 1)));
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), entry.extractable);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(content, 9);
    gtk_widget_set_margin_bottom(content, 9);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    const char* icon = entry.directory ? "folder-symbolic" :
                       (entry.type == "file" ? "text-x-generic-symbolic" : "action-unavailable-symbolic");
    GtkWidget* image = gtk_image_new_from_icon_name(icon);
    gtk_widget_add_css_class(image, entry.extractable ? "file-icon" : "muted-icon");
    gtk_box_append(GTK_BOX(content), image);
    GtkWidget* name = label(entry.name.c_str(), "file-name");
    gtk_widget_set_hexpand(name, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(content), name);
    const std::string type = entry.directory ? "Folder" :
                             (entry.type == "file" ? human_bytes(entry.size_bytes) : entry.type);
    gtk_box_append(GTK_BOX(content), label(type.c_str(), "muted-small"));
    if (entry.directory) gtk_box_append(GTK_BOX(content), gtk_image_new_from_icon_name("go-next-symbolic"));
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

void update_file_extract_sensitivity(ViewerApp* app) {
    if (app->files_extract_button == nullptr) return;
    GList* selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(app->files_list));
    bool extractable = selected != nullptr;
    for (GList* item = selected; item != nullptr && extractable; item = item->next) {
        auto* row = GTK_LIST_BOX_ROW(item->data);
        const auto encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "lazarum-file-index"));
        if (encoded == 0 || encoded > app->file_entries.size() || !app->file_entries[encoded - 1].extractable) {
            extractable = false;
        }
    }
    g_list_free(selected);
    gtk_widget_set_sensitive(app->files_extract_button, extractable);
}

gboolean finish_data_operation(gpointer data) {
    std::unique_ptr<DataCompletion> completion(static_cast<DataCompletion*>(data));
    ViewerApp* app = completion->app;
    set_busy(app, false);
    if (app->selected_image < 0 ||
        app->images[static_cast<std::size_t>(app->selected_image)].directory != completion->image_directory) {
        return G_SOURCE_REMOVE;
    }
    if (!completion->result.completed) {
        const std::string error = completion->result.error.empty()
            ? "The image data operation did not complete."
            : completion->result.error;
        gtk_label_set_text(GTK_LABEL(app->files_status), error.c_str());
        set_status(app, error, true);
        gtk_widget_set_sensitive(app->files_prepare_button, TRUE);
        return G_SOURCE_REMOVE;
    }
    if (completion->operation == DataOperation::Volumes) {
        app->files_image_directory = completion->image_directory;
        app->file_volumes = std::move(completion->result.volumes);
        GtkStringList* model = gtk_string_list_new(nullptr);
        for (const auto& volume : app->file_volumes) {
            std::string display = volume.label;
            if (!volume.filesystem.empty()) display += "  •  " + volume.filesystem;
            if (volume.size_bytes != 0) display += "  •  " + human_bytes(volume.size_bytes);
            gtk_string_list_append(model, display.c_str());
        }
        g_signal_handler_block(app->files_volume_dropdown, app->files_volume_signal);
        gtk_drop_down_set_model(GTK_DROP_DOWN(app->files_volume_dropdown), G_LIST_MODEL(model));
        g_object_unref(model);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->files_volume_dropdown), 0);
        g_signal_handler_unblock(app->files_volume_dropdown, app->files_volume_signal);
        gtk_widget_set_sensitive(app->files_volume_dropdown, !app->file_volumes.empty());
        if (app->file_volumes.empty()) {
            gtk_label_set_text(GTK_LABEL(app->files_status), "No volume candidates were found in this image.");
        } else {
            start_directory_listing(app, app->file_volumes.front().id, "");
        }
        return G_SOURCE_REMOVE;
    }
    if (completion->operation == DataOperation::Directory) {
        app->files_relative_path = completion->relative_path;
        app->file_entries = std::move(completion->result.entries);
        clear_list(app->files_list);
        for (std::size_t index = 0; index < app->file_entries.size(); ++index) {
            gtk_list_box_append(GTK_LIST_BOX(app->files_list), make_file_row(app->file_entries[index], index));
        }
        const std::string display_path = app->files_relative_path.empty() ? "/" : "/" + app->files_relative_path;
        gtk_label_set_text(GTK_LABEL(app->files_path_label), display_path.c_str());
        gtk_widget_set_sensitive(app->files_up_button, !app->files_relative_path.empty());
        gtk_widget_set_sensitive(app->files_volume_dropdown, true);
        gtk_widget_set_sensitive(app->files_prepare_button, true);
        const std::string message = std::to_string(app->file_entries.size()) +
                                    (app->file_entries.size() == 1 ? " item" : " items") +
                                    " — mounted read-only.";
        gtk_label_set_text(GTK_LABEL(app->files_status), message.c_str());
        set_status(app, "Image filesystem opened read-only.");
        return G_SOURCE_REMOVE;
    }
    gtk_widget_set_sensitive(app->files_volume_dropdown, true);
    gtk_widget_set_sensitive(app->files_prepare_button, true);
    gtk_widget_set_sensitive(app->files_up_button, !app->files_relative_path.empty());
    const std::string message = std::to_string(completion->result.extracted_paths.size()) +
                                (completion->result.extracted_paths.size() == 1
                                     ? " item extracted without overwrite."
                                     : " items extracted without overwrite.");
    gtk_label_set_text(GTK_LABEL(app->files_status), message.c_str());
    set_status(app, message);
    update_file_extract_sensitivity(app);
    return G_SOURCE_REMOVE;
}

void start_data_operation(ViewerApp* app, DataOperation operation, fs::path image_directory,
                          std::string volume_id, std::string relative_path,
                          std::vector<std::string> extract_paths = {}, fs::path destination = {}) {
    if (app->worker.joinable()) app->worker.join();
    set_busy(app, true);
    gtk_widget_set_sensitive(app->files_prepare_button, FALSE);
    gtk_widget_set_sensitive(app->files_volume_dropdown, FALSE);
    gtk_widget_set_sensitive(app->files_up_button, FALSE);
    gtk_widget_set_sensitive(app->files_extract_button, FALSE);
    app->worker = std::thread([app, operation, image_directory = std::move(image_directory),
                               volume_id = std::move(volume_id), relative_path = std::move(relative_path),
                               extract_paths = std::move(extract_paths), destination = std::move(destination)] {
        lazarum::DataOperationResult result;
        if (operation == DataOperation::Volumes) {
            result = app->data_provider->list_volumes(image_directory);
        } else if (operation == DataOperation::Directory) {
            result = app->data_provider->list_directory(image_directory, volume_id, relative_path);
        } else {
            result = app->data_provider->extract(image_directory, volume_id, extract_paths, destination);
        }
        auto* completion = new DataCompletion{
            app, operation, image_directory, volume_id, relative_path, std::move(result),
        };
        g_idle_add(finish_data_operation, completion);
    });
}

void start_volume_discovery(ViewerApp* app) {
    if (app->selected_image < 0) return;
    const auto image = app->images[static_cast<std::size_t>(app->selected_image)].directory;
    gtk_label_set_text(GTK_LABEL(app->files_status),
                       "Opening volumes with on-demand chunk verification…");
    start_data_operation(app, DataOperation::Volumes, image, "", "");
}

void start_directory_listing(ViewerApp* app, std::string volume_id, std::string relative_path) {
    if (app->files_image_directory.empty() || volume_id.empty()) return;
    gtk_label_set_text(GTK_LABEL(app->files_status), "Reading image directory…");
    start_data_operation(app, DataOperation::Directory, app->files_image_directory,
                         std::move(volume_id), std::move(relative_path));
}

void start_file_extraction(ViewerApp* app, fs::path image_directory, std::string volume_id,
                           std::vector<std::string> relative_paths, fs::path destination) {
    gtk_label_set_text(GTK_LABEL(app->files_status), "Extracting selected data without overwrite…");
    start_data_operation(app, DataOperation::Extract, std::move(image_directory), std::move(volume_id), "",
                         std::move(relative_paths), std::move(destination));
}

void on_prepare_files_clicked(GtkButton*, gpointer data) {
    start_volume_discovery(static_cast<ViewerApp*>(data));
}

void on_notebook_switch_page(GtkNotebook*, GtkWidget*, guint page, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    if (page != 1 || app->selected_image < 0) return;
    const auto& selected = app->images[static_cast<std::size_t>(app->selected_image)];
    if (app->files_image_directory != selected.directory) start_volume_discovery(app);
}

void on_volume_selected(GObject*, GParamSpec*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    const auto id = selected_volume_id(app);
    if (!id.empty() && !app->files_image_directory.empty()) start_directory_listing(app, id, "");
}

void on_files_up_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    const auto id = selected_volume_id(app);
    if (id.empty()) return;
    const auto parent = fs::path(app->files_relative_path).parent_path();
    start_directory_listing(app, id, parent == "." ? std::string{} : parent.string());
}

void on_file_row_activated(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    const auto encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "lazarum-file-index"));
    if (encoded == 0 || encoded > app->file_entries.size()) return;
    const auto& entry = app->file_entries[encoded - 1];
    if (entry.directory) start_directory_listing(app, selected_volume_id(app), entry.relative_path);
}

void on_file_selection_changed(GtkListBox*, gpointer data) {
    update_file_extract_sensitivity(static_cast<ViewerApp*>(data));
}

void on_file_extract_folder_ready(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<FileExtractDialogState> state(static_cast<FileExtractDialogState*>(data));
    GError* error = nullptr;
    GFile* folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
    if (folder == nullptr) {
        if (error != nullptr && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            set_status(state->app, error->message, true);
        }
        if (error != nullptr) g_error_free(error);
        return;
    }
    const char* path = g_file_peek_path(folder);
    if (path == nullptr) {
        set_status(state->app, "Choose a local destination folder.", true);
    } else {
        start_file_extraction(state->app, std::move(state->image_directory), std::move(state->volume_id),
                              std::move(state->relative_paths), path);
    }
    g_object_unref(folder);
}

void on_extract_files_clicked(GtkButton*, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    GList* selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(app->files_list));
    std::vector<std::string> paths;
    for (GList* item = selected; item != nullptr; item = item->next) {
        auto* row = GTK_LIST_BOX_ROW(item->data);
        const auto encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "lazarum-file-index"));
        if (encoded != 0 && encoded <= app->file_entries.size() && app->file_entries[encoded - 1].extractable) {
            paths.push_back(app->file_entries[encoded - 1].relative_path);
        }
    }
    g_list_free(selected);
    const auto volume_id = selected_volume_id(app);
    if (paths.empty() || volume_id.empty()) return;
    auto* state = new FileExtractDialogState{app, app->files_image_directory, volume_id, std::move(paths)};
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Extract selected image data");
    gtk_file_dialog_set_accept_label(dialog, "Extract Here");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(app->window), nullptr,
                                  on_file_extract_folder_ready, state);
    g_object_unref(dialog);
}

GtkWidget* detail_row(const char* key, GtkWidget** value) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(box), label(key, "detail-key"));
    *value = label("—", "detail-value");
    gtk_widget_set_hexpand(*value, TRUE);
    gtk_box_append(GTK_BOX(box), *value);
    return box;
}

GtkWidget* metric_card(const char* icon, const char* title, GtkWidget** value, const char* detail) {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(card, "metric-card");
    gtk_widget_set_size_request(card, -1, 138);
    GtkWidget* image = gtk_image_new_from_icon_name(icon);
    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(image, "metric-icon");
    gtk_box_append(GTK_BOX(card), image);
    GtkWidget* heading = label(title, "detail-key");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.5F);
    gtk_box_append(GTK_BOX(card), heading);
    *value = label("—", "metric-value");
    gtk_label_set_xalign(GTK_LABEL(*value), 0.5F);
    gtk_box_append(GTK_BOX(card), *value);
    GtkWidget* subtext = label(detail, "muted-small");
    gtk_label_set_xalign(GTK_LABEL(subtext), 0.5F);
    gtk_label_set_justify(GTK_LABEL(subtext), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(card), subtext);
    return card;
}

void on_quick_action(GtkButton* button, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    const auto page = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "lazarum-page"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), static_cast<int>(page));
}

GtkWidget* quick_action(ViewerApp* app, const char* icon, const char* title, const char* detail, guint page) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_add_css_class(button, "quick-action");
    g_object_set_data(G_OBJECT(button), "lazarum-page", GUINT_TO_POINTER(page));
    g_signal_connect(button, "clicked", G_CALLBACK(on_quick_action), app);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* image = gtk_image_new_from_icon_name(icon);
    gtk_widget_add_css_class(image, "accent-icon");
    gtk_box_append(GTK_BOX(row), image);
    GtkWidget* copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget* title_label = label(title, "quick-title");
    gtk_box_append(GTK_BOX(copy), title_label);
    gtk_box_append(GTK_BOX(copy), label(detail, "muted-small"));
    gtk_widget_set_hexpand(copy, TRUE);
    gtk_box_append(GTK_BOX(row), copy);
    gtk_box_append(GTK_BOX(row), gtk_image_new_from_icon_name("go-next-symbolic"));
    gtk_button_set_child(GTK_BUTTON(button), row);
    return button;
}

GtkWidget* build_overview(ViewerApp* app) {
    GtkWidget* scroll = gtk_scrolled_window_new();
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(content, "overview-page");
    gtk_widget_set_margin_top(content, 18);
    gtk_widget_set_margin_bottom(content, 18);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_box_append(GTK_BOX(content), label("Job and image overview", "section-title"));

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 34);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_size_request(grid, -1, 270);
    gtk_widget_add_css_class(grid, "hero-card");
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(left, TRUE);
    gtk_box_append(GTK_BOX(left), detail_row("STATE", &app->detail_state));
    gtk_box_append(GTK_BOX(left), detail_row("CUSTOMER", &app->detail_customer));
    gtk_box_append(GTK_BOX(left), detail_row("PURPOSE", &app->detail_purpose));
    gtk_box_append(GTK_BOX(left), detail_row("CREATED", &app->detail_created));
    gtk_box_append(GTK_BOX(left), detail_row("IMAGE SIZE", &app->detail_size));
    gtk_grid_attach(GTK_GRID(grid), left, 0, 0, 1, 1);

    GtkWidget* emblem = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(emblem, "brand-emblem");
    gtk_widget_set_halign(emblem, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(emblem, GTK_ALIGN_CENTER);
    GtkWidget* logo = asset_image("lazarum-phoenix-emblem.svg", 218, 218, "security-high-symbolic");
    gtk_widget_set_halign(logo, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(emblem), logo);
    gtk_grid_attach(GTK_GRID(grid), emblem, 1, 0, 1, 1);

    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_box_append(GTK_BOX(right), detail_row("TICKET", &app->detail_ticket));
    gtk_box_append(GTK_BOX(right), detail_row("TECHNICIAN", &app->detail_technician));
    gtk_box_append(GTK_BOX(right), detail_row("SOURCE", &app->detail_source));
    gtk_box_append(GTK_BOX(right), detail_row("COMPRESSION", &app->detail_compression));
    gtk_grid_attach(GTK_GRID(grid), right, 2, 0, 1, 1);
    gtk_box_append(GTK_BOX(content), grid);

    GtkWidget* status_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(status_card, "status-card");
    GtkWidget* status_icon = gtk_image_new_from_icon_name("dialog-information-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(status_icon), 24);
    gtk_widget_add_css_class(status_icon, "info-icon");
    gtk_box_append(GTK_BOX(status_card), status_icon);
    app->warning_label = label("Select an image to review its job and integrity state.", "status-message");
    gtk_widget_set_hexpand(app->warning_label, TRUE);
    gtk_box_append(GTK_BOX(status_card), app->warning_label);
    gtk_box_append(GTK_BOX(content), status_card);

    GtkWidget* lower = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* summary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(summary, "panel-card");
    gtk_widget_set_hexpand(summary, TRUE);
    gtk_box_append(GTK_BOX(summary), label("Backup summary", "section-title-small"));
    GtkWidget* metrics = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(metrics), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(metrics), TRUE);
    gtk_grid_attach(GTK_GRID(metrics), metric_card("security-high-symbolic", "INTEGRITY", &app->summary_integrity,
                                                   "Review saved report"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(metrics), metric_card("object-select-symbolic", "IMAGE STATE", &app->summary_state,
                                                   "Finalization markers"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(metrics), metric_card("drive-harddisk-symbolic", "LOGICAL SIZE", &app->summary_size,
                                                   "Captured source bytes"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(metrics), metric_card("text-x-generic-symbolic", "REPORTS", &app->summary_reports,
                                                   "Saved job records"), 3, 0, 1, 1);
    gtk_box_append(GTK_BOX(summary), metrics);
    gtk_box_append(GTK_BOX(lower), summary);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(actions, "panel-card");
    gtk_widget_set_size_request(actions, 315, -1);
    gtk_box_append(GTK_BOX(actions), label("Quick actions", "section-title-small"));
    gtk_box_append(GTK_BOX(actions), quick_action(app, "security-high-symbolic", "Verify Image Integrity",
                                                 "Review the saved integrity record.", 2));
    gtk_box_append(GTK_BOX(actions), quick_action(app, "folder-open-symbolic", "Browse Files",
                                                 "Review image file-access status.", 1));
    gtk_box_append(GTK_BOX(actions), quick_action(app, "document-save-symbolic", "Extract Reports",
                                                 "Open and safely copy job reports.", 2));
    gtk_box_append(GTK_BOX(actions), quick_action(app, "dialog-information-symbolic", "Image Details",
                                                 "Inspect format markers, paths, and warnings.", 3));
    gtk_box_append(GTK_BOX(lower), actions);
    gtk_box_append(GTK_BOX(content), lower);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
    return scroll;
}

GtkWidget* build_reports(ViewerApp* app) {
    GtkWidget* pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_add_css_class(pane, "workspace-page");
    gtk_paned_set_position(GTK_PANED(pane), 260);
    GtkWidget* reports_scroll = gtk_scrolled_window_new();
    app->report_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->report_list), GTK_SELECTION_SINGLE);
    g_signal_connect(app->report_list, "row-selected", G_CALLBACK(on_report_selected), app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(reports_scroll), app->report_list);
    gtk_paned_set_start_child(GTK_PANED(pane), reports_scroll);

    GtkWidget* viewer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(viewer, 12);
    gtk_widget_set_margin_bottom(viewer, 12);
    gtk_widget_set_margin_start(viewer, 12);
    gtk_widget_set_margin_end(viewer, 12);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->report_title = label("No report selected", "section-title");
    gtk_widget_set_hexpand(app->report_title, TRUE);
    gtk_box_append(GTK_BOX(header), app->report_title);
    app->extract_report_button = icon_button("document-save-symbolic", "Extract Report…");
    gtk_widget_set_sensitive(app->extract_report_button, FALSE);
    g_signal_connect(app->extract_report_button, "clicked", G_CALLBACK(on_extract_clicked), app);
    gtk_box_append(GTK_BOX(header), app->extract_report_button);
    gtk_box_append(GTK_BOX(viewer), header);
    GtkWidget* report_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(report_scroll, TRUE);
    app->report_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->report_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->report_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->report_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->report_view), 14);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->report_view), 14);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->report_view), 14);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->report_view), 14);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(report_scroll), app->report_view);
    gtk_box_append(GTK_BOX(viewer), report_scroll);
    gtk_paned_set_end_child(GTK_PANED(pane), viewer);
    return pane;
}

GtkWidget* build_files_page(ViewerApp* app) {
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(content, "workspace-page");
    gtk_widget_set_margin_top(content, 18);
    gtk_widget_set_margin_bottom(content, 18);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* heading = label("Files inside the image", "section-title");
    gtk_widget_set_hexpand(heading, TRUE);
    gtk_box_append(GTK_BOX(header), heading);
    app->files_prepare_button = icon_button("folder-open-symbolic", "Open Files");
    gtk_widget_add_css_class(app->files_prepare_button, "suggested-action");
    gtk_widget_set_sensitive(app->files_prepare_button, FALSE);
    g_signal_connect(app->files_prepare_button, "clicked", G_CALLBACK(on_prepare_files_clicked), app);
    gtk_box_append(GTK_BOX(header), app->files_prepare_button);
    gtk_box_append(GTK_BOX(content), header);

    app->files_status = label("Select an image, then open Files for immediate read-only access.", "status-card");
    gtk_box_append(GTK_BOX(content), app->files_status);

    GtkWidget* navigation = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(navigation, "file-toolbar");
    gtk_box_append(GTK_BOX(navigation), label("VOLUME", "detail-key"));
    app->files_volume_dropdown = gtk_drop_down_new(nullptr, nullptr);
    gtk_widget_set_size_request(app->files_volume_dropdown, 300, -1);
    gtk_widget_set_sensitive(app->files_volume_dropdown, FALSE);
    app->files_volume_signal = g_signal_connect(app->files_volume_dropdown, "notify::selected",
                                                G_CALLBACK(on_volume_selected), app);
    gtk_box_append(GTK_BOX(navigation), app->files_volume_dropdown);
    app->files_up_button = icon_button("go-up-symbolic", "Up");
    gtk_widget_set_sensitive(app->files_up_button, FALSE);
    g_signal_connect(app->files_up_button, "clicked", G_CALLBACK(on_files_up_clicked), app);
    gtk_box_append(GTK_BOX(navigation), app->files_up_button);
    app->files_path_label = label("/", "file-path");
    gtk_widget_set_hexpand(app->files_path_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(app->files_path_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(navigation), app->files_path_label);
    gtk_box_append(GTK_BOX(content), navigation);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "file-browser");
    app->files_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->files_list), GTK_SELECTION_MULTIPLE);
    gtk_widget_add_css_class(app->files_list, "file-list");
    g_signal_connect(app->files_list, "row-activated", G_CALLBACK(on_file_row_activated), app);
    g_signal_connect(app->files_list, "selected-rows-changed",
                     G_CALLBACK(on_file_selection_changed), app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->files_list);
    gtk_box_append(GTK_BOX(content), scroll);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* safety = label(
        "Verified image • read-only mount • links and special files blocked • no overwrite", "muted-small");
    gtk_widget_set_hexpand(safety, TRUE);
    gtk_box_append(GTK_BOX(actions), safety);
    app->files_extract_button = icon_button("document-save-symbolic", "Extract Selected…");
    gtk_widget_add_css_class(app->files_extract_button, "suggested-action");
    gtk_widget_set_sensitive(app->files_extract_button, FALSE);
    g_signal_connect(app->files_extract_button, "clicked", G_CALLBACK(on_extract_files_clicked), app);
    gtk_box_append(GTK_BOX(actions), app->files_extract_button);
    gtk_box_append(GTK_BOX(content), actions);
    return content;
}

GtkWidget* build_details_page(ViewerApp* app) {
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(content, "workspace-page");
    gtk_widget_set_margin_top(content, 18);
    gtk_widget_set_margin_bottom(content, 18);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_box_append(GTK_BOX(content), label("Technical image details", "section-title"));
    gtk_box_append(GTK_BOX(content), label(
        "Factual paths, format state, stored sizes, report inventory, and structural warnings from the selected image.",
        "muted"));
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    app->details_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->details_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->details_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->details_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->details_view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->details_view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->details_view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->details_view), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->details_view), 16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->details_view);
    gtk_box_append(GTK_BOX(content), scroll);
    return content;
}

GtkWidget* drive_status_row(const char* icon, const char* title, GtkWidget** value) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* image = gtk_image_new_from_icon_name(icon);
    gtk_widget_add_css_class(image, "drive-status-icon");
    gtk_box_append(GTK_BOX(row), image);
    GtkWidget* key = label(title, "drive-status-key");
    gtk_widget_set_hexpand(key, TRUE);
    gtk_box_append(GTK_BOX(row), key);
    *value = label("—", "drive-status-value");
    gtk_label_set_xalign(GTK_LABEL(*value), 1.0F);
    gtk_label_set_ellipsize(GTK_LABEL(*value), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(*value, 135, -1);
    gtk_box_append(GTK_BOX(row), *value);
    return row;
}

GtkWidget* build_drive_status(ViewerApp* app) {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "sidebar-card");
    gtk_box_append(GTK_BOX(card), label("DRIVE STATUS", "sidebar-heading"));
    gtk_box_append(GTK_BOX(card), drive_status_row("network-wired-symbolic", "Connection", &app->drive_connection));
    gtk_box_append(GTK_BOX(card), drive_status_row("drive-harddisk-symbolic", "File System", &app->drive_filesystem));
    gtk_box_append(GTK_BOX(card), drive_status_row("media-floppy-symbolic", "Total Capacity", &app->drive_capacity));
    gtk_box_append(GTK_BOX(card), drive_status_row("drive-harddisk-symbolic", "Used Space", &app->drive_used));
    gtk_box_append(GTK_BOX(card), drive_status_row("list-add-symbolic", "Free Space", &app->drive_free));
    return card;
}

GtkWidget* build_ready_card() {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(card, "ready-card");
    GtkWidget* image = gtk_image_new_from_icon_name("security-high-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(image), 44);
    gtk_widget_add_css_class(image, "ready-icon");
    gtk_box_append(GTK_BOX(card), image);
    GtkWidget* copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_append(GTK_BOX(copy), label("LAZARUM", "sidebar-heading"));
    gtk_box_append(GTK_BOX(copy), label("Ready", "ready-text"));
    gtk_box_append(GTK_BOX(copy), label("Ready to explore Lazarus drives without writing to them.", "muted-small"));
    gtk_widget_set_hexpand(copy, TRUE);
    gtk_box_append(GTK_BOX(card), copy);
    return card;
}

void install_css() {
    static constexpr const char* css = R"CSS(
        * { outline-color: #f39a22; }
        window { background: #07121c; color: #edf2f5; }
        headerbar { min-height: 64px; background: #091521; color: #edf2f5; border-bottom: 1px solid #293947; box-shadow: 0 3px 12px rgba(0,0,0,.28); }
        button { background: #101e2a; color: #edf2f5; border: 1px solid #304352; border-radius: 7px; padding: 9px 14px; }
        button:hover { background: #162735; border-color: #526777; }
        .suggested-action { background: #f39a22; color: #07121c; border-color: #ffad35; font-weight: 750; }
        .suggested-action:hover { background: #ffac32; color: #07121c; }
        .app-title { font-size: 24px; font-weight: 800; color: #ffffff; }
        .subtitle { color: #f39a22; font-size: 10px; font-weight: 800; }
        .storage-bar { min-height: 32px; background: #091521; border-bottom: 1px solid #253542; }
        .storage-path { color: #c3cdd4; }
        .sidebar { background: #091521; border: 1px solid #2b3e4b; border-radius: 8px; padding: 14px; box-shadow: 0 5px 18px rgba(0,0,0,.24); }
        .images-heading { color: #f2f5f7; font-size: 12px; font-weight: 800; letter-spacing: .5px; }
        .sidebar-heading { color: #f2f5f7; font-size: 11px; font-weight: 800; }
        .sidebar-card { background: #0d1a25; border: 1px solid #304550; border-radius: 7px; padding: 14px; box-shadow: 0 3px 10px rgba(0,0,0,.20); }
        .ready-card { background: #0d1a25; border: 1px solid #304550; border-radius: 7px; padding: 14px; box-shadow: 0 3px 10px rgba(0,0,0,.20); }
        .ready-icon, .ready-text { color: #43d17a; }
        .drive-status-icon { color: #8798a5; }
        .drive-status-key { color: #d8e0e5; font-size: 11px; }
        .drive-status-value { color: #9babb6; font-size: 10px; }
        .section-title { font-size: 18px; font-weight: 800; color: #ffffff; }
        .section-title-small { font-size: 14px; font-weight: 800; color: #ffffff; }
        .image-title { font-size: 13px; font-weight: 700; }
        .empty-title { color: #f4f7f8; font-size: 14px; font-weight: 750; }
        .empty-state { background: #0a1721; border: 1px dashed #3c5260; border-radius: 7px; padding: 22px 18px; }
        .muted { color: #aab7c0; }
        .muted-small { color: #8495a1; font-size: 10px; }
        .ready-badge { color: #62db8c; font-size: 9px; font-weight: 800; }
        .warning-badge { color: #ffc35b; font-size: 9px; font-weight: 800; }
        .hero-card { background: #0c1924; border: 1px solid #304651; border-radius: 7px; padding: 22px; box-shadow: 0 5px 16px rgba(0,0,0,.24); }
        .brand-emblem { padding: 4px; }
        .detail-key { color: #95a6b2; font-size: 10px; font-weight: 800; letter-spacing: .3px; }
        .detail-value { color: #f0f4f6; font-size: 13px; font-weight: 600; }
        .status-card { background: #0d1a25; border: 1px solid #304550; border-radius: 7px; padding: 14px 16px; }
        .status-message { color: #c8d2d8; }
        .info-icon { color: #39a5ff; }
        .panel-card { background: #0a1722; border: 1px solid #304550; border-radius: 7px; padding: 14px; box-shadow: 0 4px 14px rgba(0,0,0,.22); }
        .metric-card { background: #0d1b27; border: 1px solid #2c414d; border-radius: 7px; padding: 16px 9px; }
        .metric-icon { color: #9dabb5; -gtk-icon-size: 32px; }
        .metric-value { color: #e9eef1; font-size: 13px; font-weight: 650; }
        .quick-action { background: transparent; border-color: transparent; border-radius: 6px; padding: 8px; }
        .quick-action:hover { background: #10212e; border-color: #314754; }
        .quick-title { color: #edf2f5; font-size: 12px; font-weight: 650; }
        .accent-icon { color: #f39a22; -gtk-icon-size: 23px; }
        .file-toolbar { background: #0d1a25; border: 1px solid #304550; border-radius: 7px; padding: 9px; }
        .file-browser { background: #091620; border: 1px solid #304550; border-radius: 7px; }
        .file-list row { border-bottom: 1px solid #20313d; }
        .file-list row:selected { background: #173044; border-left: 3px solid #f39a22; }
        .file-icon { color: #f39a22; -gtk-icon-size: 22px; }
        .muted-icon { color: #657783; -gtk-icon-size: 22px; }
        .file-name { color: #eef3f5; font-size: 12px; font-weight: 600; }
        .file-path { color: #b9c5cc; font-family: monospace; }
        .error-card { background: #2b2210; border-color: #d49f28; }
        .safety-card { background: #10241b; border: 1px solid #336848; border-radius: 7px; padding: 13px; }
        .error-text { color: #ff9b82; }
        .footer { min-height: 40px; background: #08131d; border-top: 1px solid #293a47; padding: 4px 16px; }
        .footer-icon { color: #aab7c0; }
        searchentry { background: #0b1823; border: 1px solid #30424f; border-radius: 7px; }
        scrolledwindow, viewport, notebook > stack, .overview-page, .workspace-page { background: #07121c; }
        list { background: transparent; }
        list row { background: transparent; border-bottom: 1px solid #20313d; }
        list row:hover { background: #10212e; }
        list row:selected { background: #152936; border-left: 3px solid #f39a22; }
        textview, textview text { background: #091620; color: #e6ecef; }
        paned > separator { min-width: 0; min-height: 0; background: transparent; }
        notebook { background: #07121c; border: 1px solid #304550; border-radius: 8px; box-shadow: 0 5px 18px rgba(0,0,0,.25); }
        notebook > header { background: #0b1823; border-bottom: 1px solid #2a3d49; }
        notebook > header > tabs > tab { color: #aab7c0; padding: 10px 15px; border-bottom: 2px solid transparent; }
        notebook > header > tabs > tab:checked { color: #ffffff; border-bottom-color: #f39a22; }
    )CSS";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void on_activate(GtkApplication* application, gpointer data) {
    auto* app = static_cast<ViewerApp*>(data);
    if (app->window != nullptr) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }
    install_css();
    app->application = application;
    app->data_provider = lazarum::make_image_data_provider();
    app->data_provider->set_progress_callback([app](std::string message) {
        g_idle_add(apply_provider_progress, new ProviderProgress{app, std::move(message)});
    });
    app->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app->window), "Lazarum — Lazarus Drive Viewer");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1480, 900);

    GtkWidget* header = gtk_header_bar_new();
    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(brand), label("Lazarum", "app-title"));
    gtk_box_append(GTK_BOX(brand), label("LAZARUS DRIVE VIEWER", "subtitle"));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), brand);
    app->mount_button = icon_button("drive-harddisk-symbolic", "Mount Lazarus Drive");
    gtk_widget_add_css_class(app->mount_button, "suggested-action");
    g_signal_connect(app->mount_button, "clicked", G_CALLBACK(on_mount_clicked), app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->mount_button);
    app->open_button = icon_button("folder-open-symbolic", "Open Mounted Drive…");
    g_signal_connect(app->open_button, "clicked", G_CALLBACK(on_open_clicked), app);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->open_button);
    app->refresh_button = icon_button("view-refresh-symbolic", "Refresh");
    gtk_widget_set_sensitive(app->refresh_button, FALSE);
    g_signal_connect(app->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), app);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->refresh_button);
    gtk_window_set_titlebar(GTK_WINDOW(app->window), header);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* storage_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(storage_bar, "storage-bar");
    gtk_widget_set_margin_top(storage_bar, 8);
    gtk_widget_set_margin_bottom(storage_bar, 8);
    gtk_widget_set_margin_start(storage_bar, 14);
    gtk_widget_set_margin_end(storage_bar, 14);
    gtk_box_append(GTK_BOX(storage_bar), gtk_image_new_from_icon_name("drive-removable-media-symbolic"));
    app->storage_label = label("No drive selected", "storage-path");
    gtk_widget_set_hexpand(app->storage_label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(app->storage_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(storage_bar), app->storage_label);
    app->status_label = label("Connect a Lazarus image drive or open an existing read-only mount.", "muted");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 1.0F);
    gtk_box_append(GTK_BOX(storage_bar), app->status_label);
    gtk_box_append(GTK_BOX(root), storage_bar);

    GtkWidget* pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(pane), 385);
    gtk_widget_set_vexpand(pane, TRUE);
    GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sidebar, "sidebar");
    gtk_widget_set_size_request(sidebar, 350, -1);
    gtk_widget_set_margin_top(sidebar, 10);
    gtk_widget_set_margin_bottom(sidebar, 10);
    gtk_widget_set_margin_start(sidebar, 10);
    gtk_widget_set_margin_end(sidebar, 6);
    GtkWidget* image_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* images_title = label("IMAGES", "images-heading");
    gtk_widget_set_hexpand(images_title, TRUE);
    gtk_box_append(GTK_BOX(image_header), images_title);
    app->image_count = label("0 images", "muted-small");
    gtk_box_append(GTK_BOX(image_header), app->image_count);
    gtk_box_append(GTK_BOX(sidebar), image_header);
    GtkWidget* search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app->search = gtk_search_entry_new();
    gtk_widget_set_hexpand(app->search, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(app->search), "");
    gtk_widget_set_sensitive(app->search, FALSE);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(app->search), "Ticket, customer, technician…");
    g_signal_connect(app->search, "search-changed", G_CALLBACK(on_search_changed), app);
    gtk_box_append(GTK_BOX(search_row), app->search);
    app->filter_button = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(app->filter_button), "view-filter-symbolic");
    gtk_widget_set_tooltip_text(app->filter_button, "Show ready images only");
    gtk_widget_set_sensitive(app->filter_button, FALSE);
    g_signal_connect(app->filter_button, "toggled", G_CALLBACK(on_filter_toggled), app);
    gtk_box_append(GTK_BOX(search_row), app->filter_button);
    gtk_box_append(GTK_BOX(sidebar), search_row);
    GtkWidget* image_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(image_scroll, TRUE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(image_scroll), 230);
    app->image_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->image_list), GTK_SELECTION_SINGLE);
    g_signal_connect(app->image_list, "row-selected", G_CALLBACK(on_image_selected), app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(image_scroll), app->image_list);
    gtk_box_append(GTK_BOX(sidebar), image_scroll);
    gtk_box_append(GTK_BOX(sidebar), build_drive_status(app));
    gtk_box_append(GTK_BOX(sidebar), build_ready_card());
    gtk_paned_set_start_child(GTK_PANED(pane), sidebar);

    app->notebook = gtk_notebook_new();
    gtk_widget_set_margin_top(app->notebook, 10);
    gtk_widget_set_margin_bottom(app->notebook, 10);
    gtk_widget_set_margin_start(app->notebook, 6);
    gtk_widget_set_margin_end(app->notebook, 10);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), build_overview(app),
                             tab_label("security-high-symbolic", "Overview"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), build_files_page(app),
                             tab_label("folder-open-symbolic", "Files"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), build_reports(app),
                             tab_label("text-x-generic-symbolic", "Reports"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), build_details_page(app),
                             tab_label("dialog-information-symbolic", "Details"));
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), app);
    gtk_paned_set_end_child(GTK_PANED(pane), app->notebook);
    gtk_box_append(GTK_BOX(root), pane);

    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(footer, "footer");
    gtk_widget_set_size_request(footer, -1, 40);
    gtk_widget_set_vexpand(footer, FALSE);
    GtkWidget* lock = gtk_image_new_from_icon_name("changes-prevent-symbolic");
    gtk_widget_add_css_class(lock, "footer-icon");
    gtk_box_append(GTK_BOX(footer), lock);
    GtkWidget* read_only = label("Read-only viewer mode", "muted");
    gtk_widget_set_hexpand(read_only, TRUE);
    gtk_box_append(GTK_BOX(footer), read_only);
    GtkWidget* footer_version = label("Lazarum 0.1.0", "muted-small");
    gtk_label_set_wrap(GTK_LABEL(footer_version), FALSE);
    gtk_box_append(GTK_BOX(footer), footer_version);
    GtkWidget* footer_logo = asset_image("lazarum-phoenix-emblem.svg", 24, 24, "security-high-symbolic");
    gtk_box_append(GTK_BOX(footer), footer_logo);
    gtk_box_append(GTK_BOX(root), footer);
    gtk_window_set_child(GTK_WINDOW(app->window), root);
    rebuild_image_list(app);
    gtk_window_present(GTK_WINDOW(app->window));

    if (!app->initial_path.empty()) start_scan(app, app->initial_path);
}

}  // namespace

int main(int argc, char** argv) {
    ViewerApp app;
    int application_argc = argc;
    if (argc == 2 && argv[1][0] != '-') {
        app.initial_path = argv[1];
        application_argc = 1;
    }
    GtkApplication* application = gtk_application_new("com.arcology.Lazarum", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), &app);
    const int result = g_application_run(G_APPLICATION(application), application_argc, argv);
    if (app.worker.joinable()) app.worker.join();
    g_object_unref(application);
    return result;
}
