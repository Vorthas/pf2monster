// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

// The Mythic adjustment (War of Immortals, resources/adjustments/
// mythic.json): the basic mythic abilities by level, and optionally one of
// the Ambusher, Brute, Caster and Striker templates. What the user picked in
// the Mythic dialog is kept as the applied entry's options:
//   {"template": "brute", "tradition": "arcane",
//    "slots": {"1": "resilience:fortitude", "4": "skill", ...},
//    "skills": ["athletics"], "abilities": {"titanic-might": false},
//    "fullText": false, "callIt": "the mythic medusa head", "earlyPoints": false,
//    "prefixName": true}
// Slots and skills not chosen (or no longer allowed) take their defaults.
namespace monster
{
    struct Monster;

    struct MythicTemplate
    {
        QString id, name, summary, weakness;
        QStringList noResilience; // saves it can't gain resilience in
        QStringList noImmunity;   // "strikes" and/or "spells"
        bool noResistance = false;
        bool fullResistance = false;    // resistance equal to its level, any level
        bool noRemoveCondition = false;
        QString firstResilience;        // the 1st-level slot is fixed to this save
        QStringList abilities;          // keys into the abilities
    };

    struct MythicAbility
    {
        QString key, name;
        QString type, actions, traits, description; // as a special ability
        // Paizo's short form (Mythic Ogre Boss, Mythic Lich): `compact`, or
        // the name alone when it has none, or left out (`compactOmit`: shown
        // only in the save line or a list).
        QString compact;
        bool compactOmit = false;
        QString summary; // for those that only change numbers
        QString skill;   // a skill set to extreme ("tradition": the tradition's)
        bool spells = false;      // spell DC and attack set to high/extreme
        bool costsPoints = false; // needs the Mythic Points pool
    };

    // A row of the basic abilities' table at or below the creature's level.
    struct MythicSlot
    {
        int level = 0;
        QStringList choices; // what may be picked ("resilience:will", ...)
        QString choice;      // what is (stored, else the default); empty: nothing
    };

    const std::vector<MythicTemplate> &mythicTemplates();
    const MythicTemplate *findMythicTemplate(const QString &id);
    const MythicAbility *findMythicAbility(const QString &key);

    // `monster` as it is when Mythic applies (after the adjustments before
    // it), at `level`.
    std::vector<MythicSlot> mythicSlots(const Monster &monster, int level,
                                        const QJsonObject &options);
    // The skills Mythic Skill applies to (one, two from 12th level).
    QStringList mythicSkills(const Monster &monster, int level, const QJsonObject &options);
    // "arcane", "divine", "occult" or "primal" (chosen, else read from the
    // spellcasting, else arcane), and the skill it goes with.
    QString mythicTradition(const Monster &monster, const QJsonObject &options);
    QString traditionSkill(const QString &tradition);
    // Whether a template ability is ticked. Default: yes, except those that
    // cost Mythic Points below 4th level (no Mythic Points yet).
    bool mythicAbilityOn(const QJsonObject &options, const QString &key, int level);
    // How the abilities' text names the creature (chosen, else "the mythic
    // medusa head" for a name in lower case, the name itself for a proper
    // name such as "Sziskazha"). `name` is the name without prefixes.
    QString mythicCallIt(const QString &name, const QJsonObject &options);
    // Whether the statblock's name gets "Mythic" before it (chosen, else
    // yes for a name in lower case, no for a proper name).
    bool mythicPrefixName(const QString &name, const QJsonObject &options);
    // "Resilience: Fortitude", "Mythic Skill", ... for a slot choice.
    QString mythicChoiceLabel(const QString &choice);

    // Works the choices into the monster (see Adjustments.cpp); `name` is
    // its name without the adjustments' prefixes.
    void applyMythic(Monster &monster, const QJsonObject &options, int level, const QString &name);
} // namespace monster
