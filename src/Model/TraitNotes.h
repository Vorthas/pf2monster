// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

// GM Core's Trait Abilities (resources/traits.json, made by tools/scripts/
// aon_traits.py): what creatures with a trait typically have ("Undead:
// Senses darkvision; HP void healing; ..."), shown in the editor for the
// monster's type and traits. Homebrew traits (traits/ folders) add their
// own notes beside them, and the skills that identify them (Recall
// Knowledge).
namespace monster
{
    struct Monster;

    struct TraitNote
    {
        QString name;      // "Undead"
        QStringList lines; // Markdown: "**Senses** darkvision"
        QString file;      // a homebrew note's file (empty: built in)
    };

    // The notes for the monster's creature type, then its traits in the
    // order typed, each trait's built-in note before its homebrew one;
    // traits without notes are left out.
    std::vector<TraitNote> traitNotes(const Monster &monster);

    // A homebrew trait: its note and the skills that identify a creature
    // with it ({"format": 1, "traits": {"stonekin": {"name": "Stonekin", "lines":
    // [...], "recall": ["Society"], "ancestry": true}}}).
    struct HomebrewTrait
    {
        QString key; // lower case, as typed among the traits ("stonekin")
        QString name;
        QStringList lines;
        QStringList recall;
        bool ancestry = false; // replaced by an adjustment's "any ancestry"
        QString file;
    };
    const std::vector<HomebrewTrait> &homebrewTraits();

    // The built-in notes, by trait (lower case), for copying in the editor.
    QStringList builtInTraitKeys();
    TraitNote builtInTraitNote(const QString &key);

    // Reads the homebrew traits again (see reloadHomebrew()).
    void reloadTraitNotes();
} // namespace monster
