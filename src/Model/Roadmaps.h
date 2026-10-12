// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

// GM Core's creature and class road maps (resources/roadmaps/*.json, one
// file each): the benchmarks a creature of a role starts from, and notes on
// what the rules say that isn't a number. The monster's "roadmap" key holds
// the id of the one applied, as on the website for the base ones ("brute",
// "spellcaster (int)", ...); class ones have ids of their own ("fighter
// (str)").
namespace monster
{
    struct Monster;

    struct Roadmap
    {
        QString id;       // what the monster file holds: the website's id for base ones
        QString name;     // "Spellcaster"
        QString variant;  // "Intelligence", or empty
        QString list;     // "base" or "class": the Roadmap submenu it's in
        QString rules;    // the rules' own text, for the menu's status tip
        // Statistic key -> benchmarks: the first is applied, the rest are
        // the alternatives the rules allow.
        std::vector<std::pair<QString, QStringList>> benchmarks;
        // Editor section key ("defense", "offense", ...) -> note.
        std::vector<std::pair<QString, QString>> notes;
        QString file; // a homebrew one's file (empty: built in); its id is "homebrew:..."
        int order = 1000;

        // "Spellcaster (Intelligence)", or the name alone.
        QString title() const;
        const QStringList *benchmarksFor(const QString &statKey) const;
    };

    // Every road map, in menu order: the built-in ones, then the homebrew
    // ones (roadmaps/ folders).
    const std::vector<Roadmap> &roadmaps();
    // Reads them again (see reloadHomebrew()).
    void reloadRoadmaps();

    // The editor sections a road map can have notes for, with their titles
    // ("defense" -> "Defense"), in the form's order.
    const std::vector<std::pair<QString, QString>> &roadmapNoteSections();
    // The road map with this id, or null (none, or one this build lacks).
    const Roadmap *findRoadmap(const QString &id);

    // Sets the monster's core statistics (Perception, attributes, AC, saves,
    // HP, spell DC and attack) to the road map's benchmarks, moderate where
    // it names none, and empties their values so they follow the hints;
    // spell DC and attack values that were filled are refilled from the new
    // hint. Skills it names get its benchmark and the hint's value. Records
    // its id in the monster.
    void applyRoadmap(Monster &monster, const Roadmap &roadmap);

    // True if applying would replace a value, or a benchmark other than
    // moderate, that the monster has.
    bool roadmapReplacesValues(const Monster &monster, const Roadmap &roadmap);

    // The other benchmarks the monster's road map allows for a statistic,
    // while its benchmark is one of those the road map gives; else empty.
    QStringList roadmapAlternatives(const Monster &monster, const QString &statKey);

    // The note of the monster's road map for an editor section, with
    // {maxrank} and {maxrank-1} worked out for its level; empty if none.
    QString roadmapNote(const Monster &monster, const QString &section);
} // namespace monster
