"""Minimal read-only Planning Center API client (JSON:API) for the P3 spike.

The personal access token is read from PCO_APP_ID / PCO_SECRET in the environment, or from
~/.config/visualis/pco.env. It's never printed or written anywhere else.
"""
import base64
import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

BASE = "https://api.planningcenteronline.com"
ENV_FILE = Path.home() / ".config" / "visualis" / "pco.env"


def _credentials():
    app_id, secret = os.environ.get("PCO_APP_ID"), os.environ.get("PCO_SECRET")
    if not (app_id and secret) and ENV_FILE.exists():
        for line in ENV_FILE.read_text().splitlines():
            key, _, value = line.partition("=")
            if key.strip() == "PCO_APP_ID":
                app_id = value.strip()
            elif key.strip() == "PCO_SECRET":
                secret = value.strip()
    if not (app_id and secret):
        raise SystemExit(f"no Planning Center token: set PCO_APP_ID and PCO_SECRET or create {ENV_FILE}")
    return app_id, secret


class Client:
    def __init__(self):
        app_id, secret = _credentials()
        self._auth = "Basic " + base64.b64encode(f"{app_id}:{secret}".encode()).decode()
        self.requests = 0
        self.rate = None  # (count, limit, period) from the last response

    def get(self, path, params=None):
        """GETs one page. Retries once on 429 after the server's Retry-After."""
        url = path if path.startswith("http") else BASE + path
        if params:
            url += ("&" if "?" in url else "?") + urllib.parse.urlencode(params)
        for attempt in range(3):
            req = urllib.request.Request(url, headers={"Authorization": self._auth, "User-Agent": "visualis-spike",
                                                       "Accept": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=30) as resp:
                    self.requests += 1
                    h = resp.headers
                    self.rate = (h.get("X-PCO-API-Request-Rate-Count"), h.get("X-PCO-API-Request-Rate-Limit"),
                                 h.get("X-PCO-API-Request-Rate-Period"))
                    return json.load(resp)
            except urllib.error.HTTPError as e:
                self.requests += 1
                if e.code == 429 and attempt < 2:
                    wait = int(e.headers.get("Retry-After", "20"))
                    print(f"  rate limited, waiting {wait} s")
                    time.sleep(wait)
                    continue
                body = e.read().decode(errors="replace")[:300]
                raise RuntimeError(f"GET {url} -> {e.code}: {body}") from None
        raise RuntimeError(f"GET {url}: still rate limited")

    def get_all(self, path, params=None):
        """GETs every page (following links.next). Returns (data, included)."""
        params = {"per_page": 100, **(params or {})}
        data, included = [], []
        page = self.get(path, params)
        while True:
            data += page.get("data", [])
            included += page.get("included", [])
            nxt = page.get("links", {}).get("next")
            if not nxt:
                return data, included
            page = self.get(nxt)
