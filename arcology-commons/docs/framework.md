# Commons Framework Alpha

`stdlib/commons.abas` is the first application-framework layer for community
software in ArcoBASIC. It is intentionally generic: Arcology-specific social
network features should be built as a separate module on top of it.

The module currently provides:

* request and response records
* route declarations and path-parameter matching
* validation result helpers
* feed records with explicit explanations for why an item appears
* moderation report/action records
* audit entries

Example:

```basic
#IMPORT "commons"

router = Commons.Router()
router = Commons.AddRoute(router, "GET", "/communities/:id", "ShowCommunity", "Community page")

match = Commons.MatchRoute(router, "GET", "/communities/photo")
PRINT match.Handler
PRINT match.Params.id

feed = Commons.Feed([
    Commons.FeedItem("post", "p1", "Open Studio Night", "From a community you joined")
], "Local", "Chronological posts from your communities")

PRINT feed.Items[0]
```

For now, handlers are stored by name rather than invoked dynamically. That keeps
the framework compatible with the current language while leaving room for a
future dispatch layer once ArcoBASIC grows first-class function references or a
server runtime.
