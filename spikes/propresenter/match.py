"""Matches a Planning Center arrangement sequence ("Pre Chorus 1", "Bridge 2") to a library
song's group names ("PreChorus", "Bridge 1"). Forgiving about spelling, never guesses: anything
it can't match is returned as unmatched for the operator to resolve.
"""
import difflib
import re
from dataclasses import dataclass

# Section kinds, by the spellings seen in Planning Center and the church's ProPresenter library.
_KINDS = {
    "prechorus": ["prechorus", "pre chorus", "pre-chorus", "pre"],
    "postchorus": ["postchorus", "post chorus", "post-chorus"],
    "chorus": ["chorus"],
    "verse": ["verse"],
    "bridge": ["bridge"],
    "refrain": ["refrain"],
    "tag": ["tag"],
    "vamp": ["vamp"],
    "intro": ["intro"],
    "outro": ["outro"],
    "ending": ["ending", "end"],
    "interlude": ["interlude"],
    "instrumental": ["instrumental"],
    "turnaround": ["turnaround"],
    "breakdown": ["breakdown"],
    "blank": ["blank"],
}
_SPELLINGS = {sp.replace(" ", "").replace("-", ""): kind for kind, sps in _KINDS.items() for sp in sps}


@dataclass(frozen=True)
class Label:
    kind: str          # normalized kind, or the cleaned text if unknown
    number: int | None
    language: str      # "" or e.g. "spanish"


def parse_label(text):
    """'Spanish - Chorus 1' -> Label('chorus', 1, 'spanish'); 'Pre Chorus 1' -> ('prechorus', 1, '');
    'Refain 3' (typo) -> ('refrain', 3, '')."""
    s = text.lower().strip()
    language = ""
    m = re.match(r"^(spanish|tamil|english|french|korean|portuguese)\s*-\s*(.*)$", s) or \
        re.match(r"^(.*?)\s+(spanish|tamil|english|french|korean|portuguese)$", s)
    if m:
        language, s = (m.group(1), m.group(2)) if m.re.pattern.startswith("^(spanish") else (m.group(2), m.group(1))
    m = re.match(r"^(.*?)\s*(\d+)?$", s)
    word, number = m.group(1), int(m.group(2)) if m.group(2) else None
    key = re.sub(r"[\s\-_]+", "", word)
    kind = _SPELLINGS.get(key)
    if kind is None:
        close = difflib.get_close_matches(key, _SPELLINGS, n=1, cutoff=0.8)
        kind = _SPELLINGS[close[0]] if close else key
    return Label(kind, number, language)


def match_sequence(sequence, group_names, language=""):
    """Returns (steps, unmatched). steps: [(pco label, group name or None)].
    Rules, in order: same kind and number; a numbered label matches the only unnumbered
    group of its kind ("Pre Chorus 1" -> "PreChorus") and vice versa ("Chorus" -> "Chorus 1"
    when it's the only chorus). Language-tagged groups are used only for that language."""
    groups = [(name, parse_label(name)) for name in group_names]
    groups = [(n, l) for n, l in groups if l.language == language]
    steps, unmatched = [], []
    for label in sequence:
        want = parse_label(label)
        same_kind = [(n, l) for n, l in groups if l.kind == want.kind]
        exact = [n for n, l in same_kind if l.number == want.number]
        if not exact and len(same_kind) == 1:
            n, l = same_kind[0]
            if l.number is None or want.number is None or (l.number == 1 and want.number == 1):
                exact = [n]
        if not exact and want.number == 1:
            exact = [n for n, l in same_kind if l.number is None]
        if not exact and want.number is None:
            exact = [n for n, l in same_kind if l.number == 1]
        hit = exact[0] if len(exact) == 1 else None
        steps.append((label, hit))
        if hit is None and label not in unmatched:
            unmatched.append(label)
    return steps, unmatched
