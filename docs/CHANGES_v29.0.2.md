# v29.0.2 — the slash, one layer deeper (and armed by v29.0.1)

*Same evening, same household, next dialog: Share & compare said "No
account with that name — check the spelling" about an account that could
log in, sync, and share in the other direction.*

## The mechanics — a postmortem of my own patch

v29.0.1 normalized the server URL **inside AuthClient**. That made login
tolerate a trailing slash — and thereby quietly SAVE the raw
slash-bearing value into settings, arming every consumer the patch
didn't touch. ShareClient was one: `base + "/share"` → `//share` →
route-level 404 — whose classifier collapsed both 404s into NotFound,
with a comment defending the collapse ("both mean the thing you named
doesn't exist"). The app's own URL bug, printed as the owner's typo. The
asymmetry (he→her worked, she→him didn't) decodes as: whichever
machine's saved URL carries the slash can't share out.

## The fixes

- **`LoginDialog::serverUrl()` normalizes at the BIRTH of the value** —
  every save and every consumer, present and future, inherits it. This
  is the fix shape v29.0.1 should have had; consumer-side normalization
  (AuthClient, SyncClient, now ShareClient too) stays as defense in
  depth, demoted from being the fix.
- **ShareClient's 404s split**: the body was already parsed — the
  distinction was one comparison away. `no_such_user` → NotFound (the
  spelling message, now truthful); any other 404 →
  `Outcome::UnexpectedReply` → "The server answered unexpectedly —
  check the server address (just http://host:port) and that app and
  server versions match."

## Numbers

**371 tests** (ui +1: the dialog's URL is born clean; login_live +1: the
slash-bearing share succeeds, a genuine typo still says NotFound, and a
wrong path names itself). One test-writing lesson kept honest in the
comments: `loginForToken` REGISTERS — its own comment says "registering
IS logging in" — and the first draft registered explicitly first,
turning the helper's mint into UsernameTaken. Read the helper before
calling the helper. Both version files at 29.0.2, checked.
