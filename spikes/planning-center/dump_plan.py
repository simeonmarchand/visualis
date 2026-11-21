#!/usr/bin/env python3
"""Dumps one Planning Center service plan, with its items, songs, and arrangements, to JSON.

    ./dump_plan.py                        # the next upcoming plan of the first service type
    ./dump_plan.py --list                 # service types and upcoming plans
    ./dump_plan.py --plan 93522171        # a specific plan

Writes data/plan-<id>/raw/*.json (every response, as returned) and data/plan-<id>/plan.json
(the fields Visualis would import), then prints a report of what the API returned.
data/ is git-ignored: plans can contain people's names.
"""
import argparse
import json
import re
import time
from pathlib import Path

from pco import Client

SERVICES = "/services/v2"


def first(included, rel):
    """The included record a relationship points to, or None."""
    ref = (rel or {}).get("data")
    if not ref:
        return None
    return next((i for i in included if i["type"] == ref["type"] and i["id"] == ref["id"]), None)


def list_plans(c):
    types, _ = c.get_all(f"{SERVICES}/service_types")
    for st in types:
        print(f"service type {st['id']}: {st['attributes']['name']}")
        plans = c.get(f"{SERVICES}/service_types/{st['id']}/plans", {"filter": "future", "order": "sort_date", "per_page": 10})
        for p in plans["data"]:
            a = p["attributes"]
            print(f"   plan {p['id']}: {a['dates']}  {a.get('title') or ''}  ({a['items_count']} items)")
    return types


def find_plan(c, plan_id):
    types, _ = c.get_all(f"{SERVICES}/service_types")
    for st in types:
        if plan_id:
            try:
                return st["id"], c.get(f"{SERVICES}/service_types/{st['id']}/plans/{plan_id}")["data"]
            except RuntimeError:
                continue
        plans = c.get(f"{SERVICES}/service_types/{st['id']}/plans", {"filter": "future", "order": "sort_date", "per_page": 1})
        if plans["data"]:
            return st["id"], plans["data"][0]
    raise SystemExit(f"plan {plan_id} not found" if plan_id else "no upcoming plans")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    c = Client()
    if args.list:
        list_plans(c)
        return

    t0 = time.perf_counter()
    st_id, plan = find_plan(c, args.plan)
    base = f"{SERVICES}/service_types/{st_id}/plans/{plan['id']}"
    out = Path(__file__).parent / "data" / f"plan-{plan['id']}"
    raw = out / "raw"
    raw.mkdir(parents=True, exist_ok=True)

    def save(name, obj):
        (raw / f"{name}.json").write_text(json.dumps(obj, indent=1, ensure_ascii=False))

    save("plan", plan)
    items, inc = c.get_all(f"{base}/items", {"include": "song,arrangement,key,item_notes,media"})
    save("items", {"data": items, "included": inc})
    times, _ = c.get_all(f"{base}/plan_times")
    save("plan_times", times)
    notes, _ = c.get_all(f"{base}/notes")
    save("plan_notes", notes)
    attachments, _ = c.get_all(f"{base}/all_attachments")
    save("attachments", attachments)

    model_items = []
    for it in items:
        a, rel = it["attributes"], it.get("relationships", {})
        song, arr, key = first(inc, rel.get("song")), first(inc, rel.get("arrangement")), first(inc, rel.get("key"))
        entry = {
            "pco_item_id": it["id"],
            "kind": a["item_type"],                    # song | header | media | item
            "title": a["title"],
            "position": a["sequence"],
            "service_position": a["service_position"],  # pre | during | post
            "length_s": a["length"],
            "description": a["description"],
            "notes": [first(inc, {"data": n}) for n in (rel.get("item_notes", {}).get("data") or [])],
            "media": [first(inc, {"data": m}) for m in (rel.get("media", {}).get("data") or [])],
        }
        if song:
            s = song["attributes"]
            entry["song"] = {"pco_song_id": song["id"], "title": s["title"], "ccli_number": s.get("ccli_number"),
                             "author": s.get("author"), "copyright": s.get("copyright"), "admin": s.get("admin")}
        if arr:
            # The included arrangement may omit long fields, so fetch it and its sections directly.
            sid = song["id"] if song else rel["song"]["data"]["id"]
            full = c.get(f"{SERVICES}/songs/{sid}/arrangements/{arr['id']}")
            save(f"arrangement-{arr['id']}", full)
            sections = c.get(f"{SERVICES}/songs/{sid}/arrangements/{arr['id']}/sections")
            save(f"arrangement-{arr['id']}-sections", sections)
            fa = full["data"]["attributes"]
            entry["arrangement"] = {
                "pco_arrangement_id": arr["id"], "name": fa.get("name"),
                "sequence": fa.get("sequence"),                 # default order of sections
                "item_sequence": a.get("custom_arrangement_sequence"),  # this service's order, if changed
                "lyrics": fa.get("lyrics"), "chord_chart": bool(fa.get("chord_chart")),
                "bpm": fa.get("bpm"), "meter": fa.get("meter"), "length_s": fa.get("length"),
                "sections": sections.get("data", [{}])[0].get("attributes", {}).get("sections")
                            if sections.get("data") else None,
            }
        if key:
            entry["key"] = key["attributes"].get("name") or a.get("key_name")
        model_items.append(entry)

    pa = plan["attributes"]
    model = {
        "pco_plan_id": plan["id"], "pco_service_type_id": st_id, "dates": pa["dates"], "sort_date": pa["sort_date"],
        "title": pa.get("title"), "series_title": pa.get("series_title"), "updated_at": pa["updated_at"],
        "planning_center_url": pa.get("planning_center_url"),
        "times": [{"kind": t["attributes"].get("time_type"), "starts_at": t["attributes"].get("starts_at")} for t in times],
        "items": model_items,
        "attachments": [{"filename": x["attributes"].get("filename"), "content_type": x["attributes"].get("content_type"),
                         "attachable": x["attributes"].get("attachable_type"), "size": x["attributes"].get("file_size")}
                        for x in attachments],
    }
    (out / "plan.json").write_text(json.dumps(model, indent=1, ensure_ascii=False))
    elapsed = time.perf_counter() - t0

    # Report: shape, not content, so it's safe to paste anywhere.
    kinds = {}
    for e in model_items:
        kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
    print(f"plan {plan['id']} ({pa['dates']}): {len(items)} items {kinds}, {len(times)} times, "
          f"{len(notes)} plan notes, {len(attachments)} attachments")
    for e in model_items:
        line = f"  {e['position']:>2}. [{e['kind']}] {e['title']}"
        if "song" in e:
            s = e["song"]
            line += f"  ccli={s['ccli_number'] or '-'}"
        if "arrangement" in e:
            ar = e["arrangement"]
            lyr = ar["lyrics"] or ""
            labels = re.findall(r"^\s*([A-Z][A-Za-z ]*\d*)\s*$", lyr, re.M)
            line += (f"  arr={ar['name']!r} seq={ar['sequence']} item_seq={ar['item_sequence']} "
                     f"lyrics={len(lyr)} chars, {len(labels)} label lines, sections={len(ar['sections'] or [])}")
        if e.get("key"):
            line += f"  key={e['key']}"
        print(line)
    for x in model["attachments"]:
        print(f"  attachment: {x['content_type']} on {x['attachable']} ({x['size']} bytes)")
    print(f"{c.requests} requests in {elapsed:.1f} s; rate {c.rate[0]}/{c.rate[1]} per {c.rate[2]} s; wrote {out}")


if __name__ == "__main__":
    main()
