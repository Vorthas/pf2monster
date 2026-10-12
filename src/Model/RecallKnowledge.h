// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QString>
#include <QStringList>

// Recall Knowledge to identify a creature (GM Core pg. 54): the DC for its
// level adjusted for rarity, and the skills its creature traits call for
// (resources/recall-knowledge.json), as Archives of Nethys prints it: "DC
// 32 (includes +5 from Rare) • Undead (Religion)".
namespace monster
{
    struct Monster;

    struct RecallKnowledge
    {
        int dc = 0;
        QString note;               // "includes +5 from Rare", "incredibly hard"
        QStringList identifications; // "Undead (Religion)", in trait order
        QStringList otherSkills;    // from adjustments ("Society or ...")
        int unspecificLore = 0;     // easy, before rarity
        int specificLore = 0;       // very easy, before rarity
    };

    // For the monster as the statblock shows it (adjusted: its shown level,
    // rarity, traits).
    RecallKnowledge recallKnowledge(const Monster &monster);

    // "DC 20 • Dragon (Arcana)", with the Lore DCs when the monster asks
    // for them ("• Unspecific Lore DC 18 • Specific Lore DC 15").
    QString recallKnowledgeText(const Monster &monster);

    // Whether its type or traits include a creature trait from the table
    // (aberration, animal, ... undead); one without is missing its type.
    bool hasCreatureTypeTrait(const Monster &monster);

    // GM Core's skills for a creature trait ("beast": Arcana, Nature), or
    // none.
    QStringList builtInRecallSkills(const QString &trait);
} // namespace monster
