# Arcology v0.1a

`arcology-os/stdlib/arcology.abas` is the first domain module for The Arcology Commons. It
builds on `commons` and `arcodb`; it is not a web server yet.

The v0.1a module provides:

* users
* communities
* memberships
* posts
* events
* explainable user/community feeds
* reports
* moderation actions
* audit entries
* ArcoDB persistence
* static HTML export

Example:

```basic
#IMPORT "arcology"

app = Arcology.Open("arcology-os/var/local/arcology-v01a.arcodb")

ignored = Arcology.CreateUser(app, "ada", "Ada Lovelace")
ignored = Arcology.CreateCommunity(app, "photography", "Photography")
ignored = Arcology.JoinCommunity(app, "ada", "photography")
post = Arcology.Post(app, "photography", "ada", "Sunset walk", "Meet at the library")

feed = Arcology.FeedForUser(app, "ada")
FOR item IN feed.Items
    PRINT item.Title + " -- " + item.Reason
NEXT

Arcology.Save(app)
```

Static export:

```basic
result = Arcology.ExportSite(app, "arcology-os/dist/commons")
PRINT result.Path
```

Or from the included example:

```sh
arcosh arcology-os/examples/arcology_export_site.abas \
  arcology-os/var/local/arcology-v01a.arcodb arcology-os/dist/commons
```

This writes `index.html`, `style.css`, and one `community-<slug>.html` page per
community.

Serve the exported site directly from ArcoBASIC:

```sh
arcology-os/scripts/run/serve-arcology.sh
```

Or call the underlying example directly:

```sh
arcosh arcology-os/examples/arcology_serve_static.abas \
  arcology-os/var/local/arcology-v01a.arcodb arcology-os/dist/commons 8080
```

Then open `http://127.0.0.1:8080/`.

Feed items carry explicit `Reason` text. Moderation actions carry rule IDs,
action names, explanations, actor handles, timestamps, and appeal availability.

This module is deliberately small. The next layers should be:

* dynamic `Web.App` routing and request dispatch
* sessions and identity
* permission checks
* richer ArcoDB indexes/search
* uploads/media records
* background jobs
