# P3: Planning Center spike

Throwaway prototype for plan 3.1 (P3): sign in with a personal access token, list service types and upcoming plans, and dump one plan's items, songs, and arrangements to JSON, to learn what the API gives us (plan 6.4). Read-only. Python, standard library only.

## Setup

The token lives outside the repo in `~/.config/visualis/pco.env` (mode 600):

```
PCO_APP_ID=...
PCO_SECRET=...
```

Create one at api.planningcenteronline.com → Personal Access Tokens, **while switched to the organization whose data you want**: a token only sees the organization it was made in. `PCO_APP_ID` and `PCO_SECRET` in the environment override the file.

## Run

```sh
cd spikes/planning-center
./dump_plan.py --list          # service types and upcoming plans
./dump_plan.py                 # dump the next upcoming plan
./dump_plan.py --plan <id>     # dump a specific plan
```

Output goes to `data/plan-<id>/`: `raw/` has every response as returned, and `plan.json` has the fields Visualis would import. `data/` is git-ignored because plans can contain people's names. The printed report shows structure only.

## Findings so far

From a sandbox organization with one hand-made plan (2026-11-21). Not yet from the church's real data.

1. **Personal access tokens are per organization.** The first token was made in an empty personal organization and saw nothing. Operators who belong to several organizations need to pick the right one when making the token. That matters for the sign-in screen in the real app.
2. **Rate limit: 100 requests per 20 seconds**, reported on every response (`X-PCO-API-Request-Rate-*`). Dumping a plan takes about 6 requests plus 2 per song, so a typical service (5–7 songs) is about 20 requests.
3. **Items have a type** (`item_type`: `song`, `header`, `media`, or `item`), a position, and a `service_position` (pre, during, or post service).
4. **A service can change a song's section order for that day only** (`custom_arrangement_sequence` on the item). The import has to prefer it over the arrangement's default sequence.
5. **Planning Center has its own projector settings** per item (`selected_background`, `selected_layout`). Worth checking whether the church uses them.
6. **There's no scripture item type.** A reading is a plain item whose title is the reference ("Acts 2:38"). The P5 parser picks out the reference and rejects other titles, so the import can recognize scripture items. Real titles may wrap the reference in other words ("Scripture Reading: Acts 2:38"), so the parser will need to find a reference inside a title.
7. **Every plan has a link back to Planning Center** (`planning_center_url`), useful for an "open in Planning Center" button.

8. **Songs come with full credits**, as from a SongSelect import: CCLI number, authors, copyright (with stray leading spaces to trim), key, BPM, meter, and length.
9. **The arrangement sequence is the full song map**, with repeats and sections that have no lyrics: "Intro, Verse 1, Verse 2, Pre Chorus 1, Chorus, Turnaround, … Bridge 1, Bridge 1, … Interlude ×7, Refrain ×3, Chorus, Ending" (36 entries). Lyric-less sections (Intro, Turnaround, Interlude, Ending) need to become blank slides or be skipped.
10. **Lyrics usually aren't in Planning Center as text.** The arrangement's lyrics field held only placeholder text, and the real lyrics were a PDF attachment. Sections are derived from labels in that text, so without labelled lyrics everything is one "General" section. **Planning Center is the source of the order (songs, sequence, key), not the lyrics.** Lyrics and groups will come from the ProPresenter library (plan 7.4) and editing in Visualis. The import has to match Planning Center's sequence labels ("Pre Chorus 1") to the library's group names ("Pre-Chorus"), forgivingly, and show the operator what didn't match.
11. **Attachments list filename, type, and what they're attached to** (`Arrangement`, `Item`, and so on). Sermon decks will mostly come from a file the pastor sends, not from a plan attachment, so pulling them from Planning Center is a bonus path.

## Still to learn

- Real church data with a church token: several songs, item notes, and what a lower-permission token can't see.
- Downloading an attachment (needs a separate "open" request per file).
