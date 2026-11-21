"""Reads ProPresenter 7 .pro files (protobuf) into Visualis's song shape: groups, arrangements,
slides, and credits. Standard library only.

Field numbers come from the MIT-licensed reverse-engineered schema at
github.com/greyshirtguy/ProPresenter7-Proto (proto/presentation.proto and friends).
ProPresenter's format is undocumented, so treat every field as "observed", not guaranteed.
"""
import re
import struct
from dataclasses import dataclass, field


# --- protobuf wire format ------------------------------------------------------------------

def _varint(buf, i):
    shift = result = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, i
        shift += 7


def decode(buf):
    """Schema-less decode: {field number: [values]}. Length-delimited values stay bytes
    (decode them again if they're messages); fixed32/64 stay raw bytes."""
    out = {}
    i, n = 0, len(buf)
    while i < n:
        key, i = _varint(buf, i)
        fno, wtype = key >> 3, key & 7
        if wtype == 0:
            val, i = _varint(buf, i)
        elif wtype == 1:
            val, i = buf[i:i + 8], i + 8
        elif wtype == 2:
            ln, i = _varint(buf, i)
            val, i = buf[i:i + ln], i + ln
        elif wtype == 5:
            val, i = buf[i:i + 4], i + 4
        else:
            raise ValueError(f"unsupported wire type {wtype} at byte {i}")
        out.setdefault(fno, []).append(val)
    return out


def msg(m, *path):
    """Follows message fields by number; returns the decoded message or {}."""
    for fno in path:
        vals = m.get(fno)
        if not vals:
            return {}
        m = decode(vals[0])
    return m


def msgs(m, fno):
    return [decode(v) for v in m.get(fno, [])]


def string(m, fno, default=""):
    vals = m.get(fno)
    return vals[0].decode("utf-8", "replace") if vals else default


def uuid(m, fno):
    return string(msg(m, fno), 1)  # rv.data.UUID { string string = 1; }


def floats(m, *fnos):
    def one(raw):
        return struct.unpack("<f" if len(raw) == 4 else "<d", raw)[0]
    return [one(m[f][0]) if f in m else 0.0 for f in fnos]


# --- RTF to text ---------------------------------------------------------------------------

_SKIP_DESTINATIONS = {"fonttbl", "colortbl", "expandedcolortbl", "stylesheet", "info", "pict",
                      "listtable", "listoverridetable", "header", "footer", "*"}


def rtf_to_text(rtf):
    """Minimal RTF reader for the Cocoa RTF ProPresenter writes: keeps text, paragraph and line
    breaks, \\'hh and \\uN escapes; drops tables, pictures, and formatting."""
    if isinstance(rtf, bytes):
        rtf = rtf.decode("latin-1")
    out, stack = [], []
    skip = False
    uc = 1          # chars to skip after \uN
    pending_skip = 0
    i, n = 0, len(rtf)
    while i < n:
        ch = rtf[i]
        if ch == "{":
            stack.append((skip, uc))
            i += 1
            # A group whose first control word is a skipped destination is dropped whole.
            m = re.match(r"\\(\*|[a-z]+)", rtf[i:])
            if m and m.group(1) in _SKIP_DESTINATIONS:
                skip = True
        elif ch == "}":
            skip, uc = stack.pop() if stack else (False, 1)
            i += 1
        elif ch == "\\":
            m = re.match(r"\\([a-z]+)(-?\d+)? ?|\\'([0-9a-f]{2})|\\(.)", rtf[i:], re.I | re.S)
            i += m.end()
            word, arg, hexv, sym = m.groups()
            if skip:
                continue
            if hexv:
                if pending_skip:
                    pending_skip -= 1
                else:
                    out.append(bytes([int(hexv, 16)]).decode("cp1252", "replace"))
            elif sym is not None:
                if sym in "\\{}":
                    out.append(sym)
                elif sym == "\n" or sym == "\r":
                    out.append("\n")      # Cocoa writes "\<newline>" for a line break
                elif sym == "~":
                    out.append("\u00a0")
            elif word == "par":
                out.append("\n")
            elif word == "line":
                out.append("\n")
            elif word == "tab":
                out.append("\t")
            elif word == "uc":
                uc = int(arg)
            elif word == "u":
                code = int(arg)
                out.append(chr(code + 65536 if code < 0 else code))
                pending_skip = uc
        else:
            if ch in "\r\n":
                i += 1
                continue
            if not skip:
                if pending_skip:
                    pending_skip -= 1
                else:
                    out.append(ch)
            i += 1
    text = "".join(out).replace("\u2028", "\n").replace("\u2029", "\n")
    return "\n".join(line.rstrip() for line in text.split("\n")).strip("\n")


# --- presentation ---------------------------------------------------------------------------

@dataclass
class Slide:
    uuid: str
    label: str            # the cue's own name, often empty
    text: str             # text of every text element, joined
    notes: str
    enabled: bool = True


@dataclass
class Group:
    uuid: str
    name: str
    color: tuple          # (r, g, b, a), 0..1
    slides: list = field(default_factory=list)


@dataclass
class Song:
    name: str
    category: str
    groups: list
    arrangements: dict    # name -> [group names, in order]
    selected_arrangement: str | None
    ccli: dict
    music_key: str
    bible_reference: dict
    loose_slides: int     # cues not in any group


def _slide_text(cue):
    texts, notes = [], ""
    for action in msgs(cue, 10):
        pres = msg(action, 23, 2)          # Action.slide -> SlideType.presentation
        if not pres:
            continue
        for el in msgs(msg(pres, 1), 1):   # PresentationSlide.base_slide -> Slide.elements
            graphics = msg(el, 1)          # Slide.Element.element (Graphics.Element)
            rtf = msg(graphics, 13).get(5)  # Graphics.Element.text -> Text.rtf_data
            if rtf:
                t = rtf_to_text(rtf[0])
                if t.strip():
                    texts.append(t)
        n = msg(pres, 2).get(1)            # PresentationSlide.notes -> rtf_data
        if n:
            notes = rtf_to_text(n[0])
    return "\n".join(texts), notes


def read(path):
    p = decode(open(path, "rb").read())
    cues = {}
    for cue in msgs(p, 13):
        text, notes = _slide_text(cue)
        enabled = cue.get(12, [0])[0] == 1   # Cue.isEnabled; proto3 leaves out false
        cues[uuid(cue, 1)] = Slide(uuid(cue, 1), string(cue, 2), text, notes, enabled)

    groups, grouped = [], set()
    for cg in msgs(p, 12):
        g = msg(cg, 1)
        ids = [string(decode(v), 1) for v in cg.get(2, [])]
        grouped.update(ids)
        groups.append(Group(uuid(g, 1), string(g, 2), tuple(floats(msg(g, 3), 1, 2, 3, 4)),
                            [cues[i] for i in ids if i in cues]))

    by_id = {g.uuid: g.name for g in groups}
    arrangements, selected = {}, None
    sel_id = uuid(p, 10)
    for a in msgs(p, 11):
        name = string(a, 2)
        arrangements[name] = [by_id.get(string(decode(v), 1), "?") for v in a.get(3, [])]
        if uuid(a, 1) == sel_id:
            selected = name

    c = msg(p, 14)
    ccli = {"author": string(c, 1), "artist": string(c, 2), "title": string(c, 3), "publisher": string(c, 4),
            "year": c.get(5, [0])[0], "song_number": c.get(6, [0])[0], "display": bool(c.get(7, [0])[0])}
    b = msg(p, 15)
    bible = {"book": string(b, 2), "translation": string(b, 6)} if b else {}
    return Song(string(p, 3), string(p, 6), groups, arrangements, selected, ccli, string(p, 22), bible,
                len(set(cues) - grouped))
