#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
"""Builds resources/hazard-benchmarks.json from GM Core's "Building Hazards" tables.

Reads the rules page on Archives of Nethys (Paizo's official rules site; the
content is under the ORC license):
    https://2e.aonprd.com/Rules.aspx?ID=2864
either downloaded, or from a saved copy of the page given as an argument:
    tools/scripts/aon_hazard_benchmarks.py [rules.html]

Levels -1 to 24 come from the page as they are. The tables stop at 24; the
level-25 row below is this app's own extrapolation (each column's step
pattern over levels 17-24, matching the creature tables' level 25 where they
overlap: AC 55/52, saves 47/43; decided with the user 2026-10-26) and is
marked as such in the output.
"""

import html
import json
import pathlib
import re
import sys
import urllib.request

URL = "https://2e.aonprd.com/Rules.aspx?ID=2864"
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "hazard-benchmarks.json"

# Level 25, not in the rules. Same shapes as the parsed rows.
LEVEL_25 = {
    "stealthDisable": [60, 57, [50, 45]],
    "ac": [55, 52, 49],
    "save": [47, 43, 37],
    "hardness": [[49, 53]],
    "hp": [[192, 200]],
    "attack": [49, 45],
    "damage": [{"dice": "8d12+56", "average": 108}, {"dice": "4d12+28", "average": 54}],
    "dc": [54, 49],
}

DASH = "–"  # en dash, as the page writes ranges and negative levels


def text(cell):
    return html.unescape(re.sub(r"<[^>]+>", "", cell)).strip()


def number(cell):
    """'+3' -> 3, '–1' -> -1."""
    return int(cell.replace(DASH, "-").replace("−", "-"))


def number_range(cell, separator):
    """'12 to 11' -> [12, 11], '2–4' -> [2, 4]: in the page's order."""
    a, b = cell.split(separator)
    return [number(a), number(b)]


def damage(cell):
    m = re.fullmatch(r"(\S+) \((\d+)\)", cell)
    return {"dice": m.group(1), "average": int(m.group(2))}


def proficiency_levels(cell):
    """'0 or lower' / '1–4' / '19 or higher' -> the levels -1..25 it covers."""
    if "or lower" in cell:
        return range(-1, int(cell.split()[0]) + 1)
    if "or higher" in cell:
        return range(int(cell.split()[0]), 26)
    a, b = cell.split(DASH)
    return range(int(a), int(b) + 1)


def rank(cell):
    """'Trained (expert for Perception)' -> ('trained', 'expert')."""
    m = re.fullmatch(r"(\w+)(?: \((\w+) for Perception\))?", cell)
    return m.group(1).lower(), (m.group(2) or m.group(1)).lower()


def main():
    if len(sys.argv) > 1:
        page = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        page = urllib.request.urlopen(request).read().decode("utf-8")

    found = {}
    for m in re.finditer(r"<table.*?</table>", page, re.S):
        heads = re.findall(r"<h[1-4][^>]*>(.*?)</h[1-4]>", page[max(0, m.start() - 1500):m.start()], re.S)
        title = text(heads[-1]) if heads else ""
        t = re.fullmatch(r"Table 2" + DASH + r"\d+: (.*)", title)
        if not t:
            continue
        rows = [[text(c) for c in re.findall(r"<t[hd][^>]*>(.*?)</t[hd]>", r, re.S)]
                for r in re.findall(r"<tr.*?</tr>", m.group(0), re.S)]
        found[t.group(1)] = rows[1:]

    wanted = ["Stealth and Disable DCs", "Minimum Proficiency", "Defenses", "Offense"]
    missing = [name for name in wanted if name not in found]
    if missing:
        sys.exit("Tables not found on the page: " + ", ".join(missing))

    tables = {
        "stealthDisable": {"columns": ["extreme", "high", "low"], "rows": {}},
        "minProficiency": {"columns": ["high", "moderate", "highPerception"], "rows": {}},
        "ac": {"columns": ["extreme", "high", "low"], "rows": {}},
        "save": {"columns": ["extreme", "high", "low"], "rows": {}},
        "hardness": {"columns": ["range"], "rows": {}},
        "hp": {"columns": ["range"], "rows": {}},
        "attack": {"columns": ["simple", "complex"], "rows": {}},
        "damage": {"columns": ["simple", "complex"], "rows": {}},
        "dc": {"columns": ["extreme", "hard"], "rows": {}},
    }

    # Level | Extreme | High | Low ("12 to 11")
    for row in found["Stealth and Disable DCs"]:
        tables["stealthDisable"]["rows"][str(number(row[0]))] = [
            number(row[1]), number(row[2]), number_range(row[3], " to ")]

    # Levels | High | Moderate
    for row in found["Minimum Proficiency"]:
        high, high_perception = rank(row[1])
        moderate, _ = rank(row[2])
        for level in proficiency_levels(row[0]):
            tables["minProficiency"]["rows"][str(level)] = [high, moderate, high_perception]

    # Level | EAC | HAC | LAC | E Save | H Save | L Save | Hardness | HP*
    for row in found["Defenses"]:
        level = str(number(row[0]))
        tables["ac"]["rows"][level] = [number(c) for c in row[1:4]]
        tables["save"]["rows"][level] = [number(c) for c in row[4:7]]
        tables["hardness"]["rows"][level] = [number_range(row[7], DASH)]
        tables["hp"]["rows"][level] = [number_range(row[8], DASH)]

    # Level | S. Atk | C. Atk | Simple Dmg | Complex Dmg | EDC | HDC
    for row in found["Offense"]:
        level = str(number(row[0]))
        tables["attack"]["rows"][level] = [number(row[1]), number(row[2])]
        tables["damage"]["rows"][level] = [damage(row[3]), damage(row[4])]
        tables["dc"]["rows"][level] = [number(row[5]), number(row[6])]

    for key, table in tables.items():
        levels = sorted(int(level) for level in table["rows"])
        expected = list(range(-1, 26)) if key == "minProficiency" else list(range(-1, 25))
        if levels != expected:
            sys.exit(f"{key}: levels {levels[0]}..{levels[-1]}, {len(levels)} rows")
        if key in LEVEL_25:
            table["rows"]["25"] = LEVEL_25[key]

    data = {
        "source": "Pathfinder GM Core, \"Building Hazards\" (pg. 109), via Archives of Nethys: " + URL,
        "license": "ORC License: Paizo's Pathfinder GM Core is ORC licensed material.",
        "levels": [-1, 25],
        "extrapolated": [25],
        "note": "Level 25 is this app's extrapolation; the rules' tables stop at level 24 "
                "(Minimum Proficiency's \"19 or higher\" covers it). Ranges are in the page's "
                "order: Stealth and Disable's low as \"12 to 11\" (high to low), Hardness and HP "
                "low to high. The Broken Threshold is usually half the HP.",
        "tables": tables,
    }
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote {OUT} ({len(tables)} tables)")


if __name__ == "__main__":
    main()
