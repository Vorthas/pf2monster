#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
"""Builds resources/ancestry-traits.json: the traits of the ancestries.

Reads the ancestries list on Archives of Nethys (Paizo's official rules
site; the content is under the ORC license):
    https://2e.aonprd.com/Ancestries.aspx
and keeps each ancestry's trait, in lower case. Versatile heritages
(changeling, nephilim, ...) are on another page and aren't ancestries, so
they stay out. "grippli" is added by hand: the tripkee's name before the
remaster, which older creatures still have.
    tools/scripts/aon_ancestries.py [ancestries.html]

An adjustment's "any ancestry" replaces these traits (Model/Adjustments.h).
"""

import html
import json
import pathlib
import re
import sys
import urllib.request

URL = "https://2e.aonprd.com/Ancestries.aspx"
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "ancestry-traits.json"
EXTRA = ["grippli"]


def main():
    if len(sys.argv) > 1:
        page = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        page = urllib.request.urlopen(request).read().decode("utf-8")

    traits = []
    for name in re.findall(r'<a href="Ancestries\.aspx\?ID=\d+"[^>]*>([^<]+)</a>', page):
        trait = html.unescape(name).strip().lower()
        if trait and trait not in traits:
            traits.append(trait)
    if len(traits) < 40:
        sys.exit(f"Only {len(traits)} ancestries found; has the page changed?")
    traits = sorted(set(traits + EXTRA))

    data = {
        "source": "Ancestries, via Archives of Nethys: " + URL,
        "license": "ORC License: Paizo's Pathfinder rules are ORC licensed material.",
        "traits": traits,
    }
    OUT.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{len(traits)} ancestry traits written to {OUT}")


if __name__ == "__main__":
    main()
