#!/usr/bin/env python3
import json, time, sys, requests
BASE = "https://api.polyhaven.com"
session = requests.Session()
get = session.get
def j(url, **kw):
    r = get(url, timeout=30, **kw); r.raise_for_status(); return r.json()
if __name__ == "__main__":
    assets = j(f"{BASE}/assets", params={"t": "hdris"})
    list ={}
    for a in assets:
        data = files = j(f"{BASE}/files/{a}")
        print(data['hdri']['4k']['hdr']['url'])
        list[a] = data['hdri']['4k']['hdr']['url']
    with open("assets/skies.json", "w", encoding="utf-8") as f:
        json.dump(list, f, ensure_ascii=False, indent=2)
    print(f"wrote {len(list)} entries to skies.json")
    