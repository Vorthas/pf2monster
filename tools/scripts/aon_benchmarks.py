#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
"""Builds resources/benchmarks.json from GM Core's "Building Creatures" tables.

Reads the rules page on Archives of Nethys (Paizo's official rules site; the
content is under the ORC license):
    https://2e.aonprd.com/Rules.aspx?ID=2874
either downloaded, or from a saved copy of the page given as an argument:
    tools/scripts/aon_benchmarks.py [rules.html]

Levels -1 to 24 come from the page as they are. The tables stop at 24; the
level-25 row below is this app's own extrapolation (each table's step pattern
over levels 21-24, checked against the Tarrasque, Treerazer, Dimari-Diji and
the Oliphaunt of Jandelay; decided 2026-10-05) and is marked as such in the
output.
"""

import html
import json
import pathlib
import re
import sys
import urllib.request

URL = "https://2e.aonprd.com/Rules.aspx?ID=2874"
OUT = pathlib.Path(__file__).resolve().parents[2] / "resources" / "benchmarks.json"

# Level 25, not in the rules. Same shapes as the parsed rows.
LEVEL_25 = {
    "attribute": [13, 12, 9, 7],
    "perception": [47, 43, 39, 37, 33],
    "skill": [49, 46, 41, [39, 34]],
    "ac": [55, 52, 51, 49],
    "save": [47, 43, 39, 37, 33],
    "hp": [[685, 665], [548, 532], [413, 397]],
    "resistance": [27, 14],
    "strikeAttack": [47, 45, 43, 37],
    "strikeDamage": [
        {"dice": "5d12+38", "average": 70},
        {"dice": "5d12+22", "average": 54},
        {"dice": "5d10+19", "average": 46},
        {"dice": "5d6+19", "average": 36},
    ],
    "spellDc": [53, 49, 46],
    "spellAttack": [45, 41, 38],
    "areaDamage": [{"dice": "12d6", "average": 42}, {"dice": "26d6", "average": 91}],
    # No item past 20th level exists; the level-24 row carries on.
    "safeItem": [{"level": 20, "note": ""}],
}

# Table title -> (key, column names in the app's order).
TABLES = {
    "Ability Modifier Scales": ("attribute", ["extreme", "high", "moderate", "low"]),
    "Perception": ("perception", ["extreme", "high", "moderate", "low", "terrible"]),
    "Skills": ("skill", ["extreme", "high", "moderate", "low"]),
    "Safe Items": ("safeItem", ["level"]),
    "Armor Class": ("ac", ["extreme", "high", "moderate", "low"]),
    "Saving Throws": ("save", ["extreme", "high", "moderate", "low", "terrible"]),
    "Hit Points": ("hp", ["high", "moderate", "low"]),
    "Resistances and Weaknesses": ("resistance", ["maximum", "minimum"]),
    "Strike Attack Bonus": ("strikeAttack", ["extreme", "high", "moderate", "low"]),
    "Strike Damage": ("strikeDamage", ["extreme", "high", "moderate", "low"]),
    "Spell DC and Spell Attack Bonus": (None, None),  # split into spellDc / spellAttack
    "Area Damage": ("areaDamage", ["unlimited", "limited"]),
}

DASH = "–"  # en dash, as the page writes ranges and negative levels


def text(cell):
    return html.unescape(re.sub(r"<[^>]+>", "", cell)).strip()


def number(cell):
    """'+3' -> 3, '–1' -> -1, '—' -> None."""
    cell = cell.replace(DASH, "-").replace("−", "-")
    if cell in ("—", ""):
        return None
    return int(cell)


def cell_value(key, cell):
    if key == "strikeDamage" or key == "areaDamage":
        m = re.fullmatch(r"(\S+) \((\d+)\)", cell)
        return {"dice": m.group(1), "average": int(m.group(2))}
    if " to " in cell:  # skills' low: "+2 to +1"
        a, b = cell.split(" to ")
        return [number(a), number(b)]
    if key == "hp" and DASH in cell:  # "20–17"
        a, b = cell.split(DASH)
        return [int(a), int(b)]
    if key == "hp":  # level -1 high: "9"
        return [int(cell), int(cell)]
    return number(cell)


def safe_items(rows):
    """'3 or lower' / '4–5' / '6' -> one entry per level -1..24."""
    out = {}
    for creature, item in rows:
        m = re.fullmatch(r"(\d+) \((.*)\)", item)
        entry = {"level": int(m.group(1)) if m else int(item), "note": m.group(2) if m else ""}
        if "or lower" in creature:
            levels = range(-1, int(creature.split()[0]) + 1)
        elif DASH in creature:
            a, b = creature.split(DASH)
            levels = range(int(a), int(b) + 1)
        else:
            levels = [int(creature)]
        for level in levels:
            out[str(level)] = [entry]
    return out


def main():
    if len(sys.argv) > 1:
        page = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        page = urllib.request.urlopen(request).read().decode("utf-8")

    tables = {}
    for m in re.finditer(r"<table.*?</table>", page, re.S):
        heads = re.findall(r"<h[1-4][^>]*>(.*?)</h[1-4]>", page[max(0, m.start() - 1500):m.start()], re.S)
        title = text(heads[-1]) if heads else ""
        t = re.fullmatch(r"Table 2" + DASH + r"\d+: (.*)", title)
        if not t or t.group(1) not in TABLES:
            continue
        rows = [[text(c) for c in re.findall(r"<t[hd][^>]*>(.*?)</t[hd]>", r, re.S)]
                for r in re.findall(r"<tr.*?</tr>", m.group(0), re.S)]
        name = t.group(1)
        key, columns = TABLES[name]
        body = rows[1:]
        if name == "Safe Items":
            tables[key] = {"columns": columns, "rows": safe_items(body)}
        elif key is None:
            # Level | Extreme DC | Extreme attack | High DC | High attack | Moderate DC | Moderate attack
            spell = ["extreme", "high", "moderate"]
            tables["spellDc"] = {"columns": spell, "rows": {}}
            tables["spellAttack"] = {"columns": spell, "rows": {}}
            for row in body:
                level = str(number(row[0]))
                tables["spellDc"]["rows"][level] = [number(c) for c in row[1::2]]
                tables["spellAttack"]["rows"][level] = [number(c) for c in row[2::2]]
        else:
            tables[key] = {"columns": columns,
                           "rows": {str(number(r[0])): [cell_value(key, c) for c in r[1:]] for r in body}}

    missing = {k for k, _ in TABLES.values() if k} | {"spellDc", "spellAttack"}
    missing -= set(tables)
    if missing:
        sys.exit("Tables not found on the page: " + ", ".join(sorted(missing)))
    for key, table in tables.items():
        levels = sorted(int(level) for level in table["rows"])
        if levels != list(range(-1, 25)):
            sys.exit(f"{key}: levels {levels[0]}..{levels[-1]}, {len(levels)} rows")
        table["rows"]["25"] = LEVEL_25[key]

    data = {
        "source": "Pathfinder GM Core, \"Building Creatures\" (pg. 112), via Archives of Nethys: " + URL,
        "license": "ORC License: Paizo's Pathfinder GM Core is ORC licensed material.",
        "levels": [-1, 25],
        "extrapolated": [25],
        "note": "Level 25 is this app's extrapolation; the rules' tables stop at level 24.",
        "tables": tables,
    }
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote {OUT} ({len(tables)} tables)")


if __name__ == "__main__":
    main()
