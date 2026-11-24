# 0002: Slide overrides stay a list parallel to the slides

**Status:** Accepted, 2026-11-23 (#95 architecture review).

## Context

A slide can override template properties for itself (plan 5.4). Since #92 a group stores its slides as `Group::slides`, a list of strings, and their overrides as `Group::slideOverrides`, a list of JSON objects of the same length. #95 is the first code that edits overrides, so it was the time to ask whether a slide should become a struct that holds its text and its overrides together.

The editor edits lyrics as one plain text (plan 5.4, text-first editing). That text has no slide identity: after an edit, nothing says which new slide was which old one. So the overrides have to be matched to the edited slides by their text whichever way they're stored. `applyLyricsText` (`core/song/songtext.h`) is the only code that changes an existing song's slides; the song editor's only slide edit goes through it.

## Decision

Keep the parallel list. `applyLyricsText` remaps it, so callers never see two lists to keep in step. For each group the edit keeps:

1. a slide whose text exactly equals an unused old slide in the group takes its override, in order, so reordered or repeated slides keep theirs;
2. then, if the group has as many slides as before, the rest match by position, so fixing a typo keeps the slide's style;
3. anything left is dropped, with one warning naming the group and how many were lost.

A group renamed in place, or matched by position, goes through the same rule.

## Consequences

- The struct was rejected because it would have touched about 34 references and 72 test constructions without moving the matching logic anywhere else.
- `SongStore` and `songjson` keep fitting `slideOverrides` to exactly as long as `slides` on every save and read.
- Code that adds, removes, or reorders slides outside `applyLyricsText` must keep the list in step. Today none does.
