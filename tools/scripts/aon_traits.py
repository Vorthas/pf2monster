#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
"""Builds resources/traits.json from GM Core's "Trait Abilities".

Reads the "Building Creatures" rules page on Archives of Nethys (Paizo's
official rules site; the content is under the ORC license):
    https://2e.aonprd.com/Rules.aspx?ID=2874
either downloaded, or from a saved copy of the page given as an argument:
    tools/scripts/aon_traits.py [rules.html]

Each trait's lines are kept as on the page, as Markdown: the labels in bold
("**Senses** usually darkvision"), book titles in italics. The editor shows
the lines of the monster's type and traits.
"""

import html
import json
import pathlib
import re
import sys
import urllib.request

URL = "https://2e.aonprd.com/Rules.aspx?ID=2874"
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "traits.json"


def markdown(fragment):
    """One line of the page as Markdown: <b> -> **, <i> -> _, tags dropped."""
    text = re.sub(r"<b>\s*(.*?)\s*</b>", r"**\1**", fragment, flags=re.S)
    text = re.sub(r"<i>\s*(.*?)\s*</i>", r"_\1_", text, flags=re.S)
    text = re.sub(r"<[^>]+>", "", text)
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


def main():
    if len(sys.argv) > 1:
        page = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        page = urllib.request.urlopen(request).read().decode("utf-8")

    # The section: from its title (an <h2>; the page's contents list has the
    # same link earlier) to the next rule block ("Building NPCs").
    start = re.search(r'<h2 class="title">\s*<a [^>]*>Trait Abilities</a>', page).end()
    body = page[page.index("<div>", start):page.index('<div class="rule">', start)]
    # One <h2> per trait; its lines are separated by <br />.
    parts = re.split(r'<h2 class="title">(.*?)</h2>', body)
    traits = {}
    for name, content in zip(parts[1::2], parts[2::2]):
        name = markdown(name)
        lines = [markdown(line) for line in re.split(r"<br\s*/?>", content)]
        traits[name.lower()] = {"name": name, "lines": [line for line in lines if line]}

    data = {
        "source": "Pathfinder GM Core, \"Building Creatures\", Trait Abilities, via Archives of "
                  "Nethys: " + URL,
        "license": "ORC License: Paizo's Pathfinder GM Core is ORC licensed material.",
        "traits": traits,
    }
    OUT.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{len(traits)} traits written to {OUT}")


if __name__ == "__main__":
    main()
