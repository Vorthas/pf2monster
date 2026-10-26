// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// GM Core's "Building Creatures" tables (resources/benchmarks.json, made by
// tools/scripts/aon_benchmarks.py): what a statistic should be at a level
// for a benchmark such as "high". Levels -1 to 24 are the rules'; level 25
// is extrapolated. Hazards have their own tables (GM Core's "Building
// Hazards", resources/hazard-benchmarks.json, from
// tools/scripts/aon_hazard_benchmarks.py), the same levels.
namespace monster
{
    struct Monster;

    enum class Table
    {
        Attribute,
        Perception,
        Skill,
        ArmorClass,
        Save,
        HitPoints,
        Resistance, // resistances and weaknesses: a range, no benchmark
        StrikeAttack,
        StrikeDamage,
        SpellDc,
        SpellAttack,
        AreaDamage, // benchmarks "unlimited" and "limited"
        SafeItem,   // no benchmark
        // Hazards.
        HazardStealthDisable, // Stealth and Disable DCs: extreme, high, low
        HazardMinProficiency, // "high", "moderate" (text: "trained")
        HazardArmorClass,     // extreme, high, low
        HazardSave,           // extreme, high, low
        HazardHardness,       // a range, no benchmark
        HazardHitPoints,      // a range, no benchmark
        HazardAttack,         // "simple", "complex"
        HazardDamage,         // "simple", "complex"
        HazardDc,             // "extreme", "hard"
    };

    // A table's value for one level and benchmark.
    struct Hint
    {
        bool valid = false;
        QString text;              // as shown: "+22", "219–211", "3d8+12 (25)"
        std::optional<int> value;  // what Apply Hints puts in the value box
        int min = 0, max = 0;      // a range's ends (HP, resistances); else value
        bool extrapolated = false; // past the rules' last level
    };

    // The table a statistic of the monster file uses ("strength" ->
    // Attribute, "fortitude" -> Save, ...); nullopt for those without one.
    std::optional<Table> tableFor(const QString &statKey);

    // The benchmarks offered for a table, in order: "extreme", "high", ...
    QStringList benchmarks(Table table);

    // `benchmark` is one of benchmarks(), or a name the website uses that the
    // rules don't ("terrible" for a skill: Low's bottom end). Level is
    // clamped to the tables' levels.
    Hint hint(Table table, int level, const QString &benchmark = QString());

    // The hint for one of the monster's statistics ("perception", "hp",
    // ...), at its level (its non-combat level for the statistics that
    // follow it) and with its benchmark. A hazard's come from the hazard
    // tables ("stealth", "minprof", "ac", the saves, "hp"); a complex
    // hazard's Stealth is the modifier, the DC - 10 (GM Core).
    Hint statHint(const Monster &monster, const QString &statKey);

    // The table a hazard's statistic uses; nullopt for those without one.
    std::optional<Table> hazardTableFor(const QString &statKey);

    // The statistics whose empty value stands for their hint, in the
    // statblock and for Apply Hints (as on the website): Perception, the
    // attributes, AC, saves and HP. Empty skills stay untrained.
    bool usesHint(const QString &statKey);
    // The same for this monster: a hazard's are Stealth and the defenses
    // it shows.
    bool usesHint(const Monster &monster, const QString &statKey);

    // Fills each empty value of those statistics with its hint. Returns
    // how many it filled.
    int applyHints(Monster &monster);

    // GM Core's DC for a level (DCs by Level, resources/dcs.json) at a
    // difficulty ("standard", "hard", "very hard", ...); levels outside the
    // table are clamped to it.
    int levelDc(int level, const QString &difficulty = QStringLiteral("standard"));

    int minLevel();
    int maxLevel();
    // True for a level past the rules' tables.
    bool isExtrapolated(int level);
} // namespace monster
