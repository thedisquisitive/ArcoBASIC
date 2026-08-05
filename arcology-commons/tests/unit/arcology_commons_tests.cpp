#include "arco/runtime.hpp"
#include "arco/shell.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string run_shell_capture(const std::string& code) {
    arco::Runtime runtime;
    arco::shell::register_shell_builtins(runtime);
    std::ostringstream output;
    runtime.set_output(output);
    const auto result = runtime.run_string(code);
    require(result.ok, result.error);
    return output.str();
}

} // namespace

int main() {
    require(run_shell_capture(
        "#IMPORT \"commons\"\n"
        "router = Commons.Router()\n"
        "router = Commons.AddRoute(router, \"GET\", \"/communities/:id\", \"ShowCommunity\", \"Community page\")\n"
        "match = Commons.MatchRoute(router, \"get\", \"/communities/photo\")\n"
        "PRINT match.Ok\n"
        "PRINT match.Handler\n"
        "PRINT match.Params.id\n"
        "response = Commons.Text(200, \"ok\")\n"
        "PRINT Object.Get(response.Headers, \"Content-Type\")\n"
        "validation = Commons.Validation()\n"
        "validation = Commons.RequireField(validation, \"\", \"title\")\n"
        "validation = Commons.MaxLength(validation, \"abcdef\", \"body\", 3)\n"
        "PRINT validation.Ok\n"
        "PRINT LEN(validation.Errors)\n"
        "feed = Commons.Feed([Commons.FeedItem(\"post\", \"p1\", \"Hello\", \"From a community you joined\")], \"Local\", \"Chronological\")\n"
        "PRINT feed.CaughtUp\n"
        "PRINT Object.Get(feed.Items[0], \"Reason\")\n"
        "caught = Commons.CaughtUp()\n"
        "PRINT caught.Message\n"
        "report = Commons.Report(\"post:p1\", \"user:ada\", \"Spam\")\n"
        "action = Commons.ModerationAction(report, \"spam\", \"remove_content\", \"Matched spam rule\", \"mod:grace\")\n"
        "PRINT action.RuleId\n"
        "PRINT action.AppealAvailable\n"
        "audit = Commons.AuditEntry(\"mod:grace\", \"remove\", \"post:p1\", \"spam\")\n"
        "PRINT audit.Verb\n") == "TRUE\nShowCommunity\nphoto\ntext/plain; charset=utf-8\nFALSE\n2\nFALSE\nFrom a community you joined\nYou're caught up.\nspam\nTRUE\nremove\n", "runs Arcology Commons framework helpers");
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-v01a-runtime-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "PRINT Arcology.Version()\n"
            "manifest = Arcology.Manifest()\n"
            "PRINT manifest.Name\n"
            "ada = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\", \"Steward\", \"moderator\")\n"
            "grace = Arcology.CreateUser(app, \"Grace\", \"Grace Hopper\")\n"
            "photo = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local photo walks\")\n"
            "PRINT ada.Ok\n"
            "PRINT grace.Value.Handle\n"
            "PRINT photo.Value.Slug\n"
            "joined = Arcology.JoinCommunity(app, \"Grace\", \"photography\")\n"
            "PRINT joined.Ok\n"
            "post = Arcology.Post(app, \"photography\", \"Grace\", \"Sunset walk\", \"Meet by the library\", [\"local\"])\n"
            "event = Arcology.Event(app, \"photography\", \"Open studio\", \"2026-08-12 18:00\", \"Community center\")\n"
            "feed = Arcology.FeedForUser(app, \"grace\")\n"
            "PRINT feed.Title\n"
            "PRINT feed.Explanation CONTAINS \"No hidden\"\n"
            "PRINT LEN(feed.Items)\n"
            "PRINT Object.Get(feed.Items[0], \"Reason\") CONTAINS \"photography\"\n"
            "report = Arcology.ReportContent(app, \"post:\" + STRING(post.Value.__id), \"Ada\", \"Spam\", \"test report\")\n"
            "PRINT report.Value.Status\n"
            "action = Arcology.Moderate(app, \"post:\" + STRING(post.Value.__id), \"spam\", \"remove_content\", \"transparent removal\", \"Ada\")\n"
            "PRINT action.Value.Action\n"
            "after = Arcology.CommunityFeed(app, \"photography\")\n"
            "PRINT LEN(after.Items)\n"
            "info = Arcology.Inspect(app)\n"
            "PRINT info.Users\n"
            "PRINT info.Communities\n"
            "PRINT info.Posts\n"
            "PRINT info.Reports\n"
            "PRINT Arcology.Save(app)\n"
            "reopened = Arcology.Open(\"" + db_file.string() + "\")\n"
            "loaded = Arcology.User(reopened, \"grace\")\n"
            "PRINT loaded.DisplayName\n") == "ARCOLOGY_0.1A\nThe Arcology Commons\nTRUE\ngrace\nphotography\nTRUE\nYour Commons\nTRUE\n2\nTRUE\nopen\nremove_content\n1\n2\n1\n1\n1\nTRUE\nGrace Hopper\n", "runs Arcology Commons users, feeds, moderation, and persistence");
        require(std::filesystem::exists(db_file), "writes Arcology Commons ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-static-runtime-test.arcodb";
        const auto site_dir = std::filesystem::temp_directory_path() / "arcology-static-runtime-test-site";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        std::filesystem::remove_all(site_dir);
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "ignored = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\")\n"
            "ignored = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local <photos> & walks\")\n"
            "ignored = Arcology.JoinCommunity(app, \"Ada\", \"photography\")\n"
            "ignored = Arcology.Post(app, \"photography\", \"ada\", \"Sunset & shadows\", \"Meet <outside>\")\n"
            "result = Arcology.ExportSite(app, \"" + site_dir.string() + "\")\n"
            "PRINT result.Ok\n"
            "PRINT result.Files\n"
            "index = File.ReadText(Arcology.SitePath(result.Path, \"index.html\"))\n"
            "community = File.ReadText(Arcology.SitePath(result.Path, \"community-photography.html\"))\n"
            "style = File.ReadText(Arcology.SitePath(result.Path, \"style.css\"))\n"
            "PRINT index CONTAINS \"The Arcology Commons\"\n"
            "PRINT community CONTAINS \"Local &lt;photos&gt; &amp; walks\"\n"
            "PRINT community CONTAINS \"Sunset &amp; shadows\"\n"
            "PRINT style CONTAINS \"topbar\"\n") == "TRUE\n3\nTRUE\nTRUE\nTRUE\nTRUE\n", "exports the Arcology Commons static site");
        require(std::filesystem::exists(site_dir / "index.html"), "writes Arcology Commons static index");
        std::filesystem::remove_all(site_dir);
    }
    return 0;
}
