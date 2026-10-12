// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

// A creature, in the shape of monster.pf2.tools' JSON so files go both ways
// (a file saved here uploads to the site, a file downloaded there opens
// here). Keys this model doesn't know are kept in `extra` and written back
// unchanged. What the site doesn't have goes under the "pf2monster" key.
namespace monster
{
    // A field the site stores as a number or as text, depending on whether
    // it was picked from a list or typed. Kept as it came, so a file comes
    // back out the same.
    class Value
    {
    public:
        Value() = default;
        Value(int number) : json_(number) {}
        Value(const QString &text) : json_(text) {}

        static Value fromJson(const QJsonValue &json);
        QJsonValue toJson() const { return json_; }

        bool isEmpty() const;
        QString text() const;
        // The number it holds, if it is one (or text that reads as one).
        std::optional<int> number() const;

    private:
        QJsonValue json_ = QString();
    };

    // A statistic: the value the statblock uses (empty: the benchmark's
    // value), the benchmark it was built from, and a note ("+2 vs. ...").
    struct Stat
    {
        Value value;
        QString benchmark = QStringLiteral("moderate");
        QString note;
        QString name; // lore skills only: "Lore" names
        QJsonObject extra;

        static Stat fromJson(const QJsonValue &json, bool named = false);
        QJsonObject toJson(bool named = false) const;
    };

    struct Strike
    {
        QString type = QStringLiteral("Melee"); // "Melee" or "Ranged"
        QString name = QStringLiteral("Strike");
        Value attack;
        QString traits;
        QString damage;
        QJsonObject extra;

        static Strike fromJson(const QJsonObject &json);
        QJsonObject toJson() const;
    };

    // A special ability.
    struct Special
    {
        QString name = QStringLiteral("Ability");
        QString type = QStringLiteral("offense"); // general, offense, defense
        // none, one, two, three, free, reaction, "1 minute", "10 minutes"
        QString actions = QStringLiteral("one");
        QString traits;
        QString range;
        QString description;
        QJsonObject extra;
        // Set for an ability an adjustment makes (Mythic's, by its key): in
        // the monster it only holds the ability's place in the list (the
        // user orders them, the adjustment writes them); in the adjusted
        // monster it is the ability itself. Saved under "pf2monster", not
        // with the website's specials.
        QString generated;
        bool placeholder = false; // not yet filled in by its adjustment
        // In the adjusted monster: the generated key of the entry it is
        // listed under, as a bullet (Mythic Power's actions); not saved.
        QString under;

        static Special fromJson(const QJsonObject &json);
        QJsonObject toJson() const;
    };

    // A part of the creature with Hit Points of its own (a hydra's head):
    // "**HP** 50 (head); **Immunities** area damage; **Weaknesses** slashing
    // 5" under the creature's own HP. Not on the website: kept under
    // "pf2monster".
    struct HpPart
    {
        QString name = QStringLiteral("head");
        QString hp;
        QString note; // "void healing, head regrowth"
        QString immunities, resistances, weaknesses;

        static HpPart fromJson(const QJsonObject &json);
        QJsonObject toJson() const;
        // The website workaround of an HP note such as "50 (head),
        // Immunities area damage; Weaknesses slashing 5" as a part, if the
        // note is one.
        static std::optional<HpPart> fromNote(const QString &note);
    };

    // Spells: index 0 is 10th rank, down to 9 for 1st rank; 10 is cantrips.
    inline constexpr int kSpellRanks = 11;
    inline constexpr int kCantrips = 10;

    // An extra spellcasting entry ("Additional Spells").
    struct SpellList
    {
        QString name;
        QString note;
        Stat spellattack;
        Stat spelldc;
        std::array<QString, kSpellRanks> spells;
        QString constant;
        Value focuspoints = 0;
        Value cantriplevel;
        QJsonObject extra;

        static SpellList fromJson(const QJsonObject &json);
        QJsonObject toJson() const;
    };

    // The statistics that are a Stat, by their JSON key, in the site's order.
    inline constexpr std::array kAbilities = {
        "strength", "dexterity", "constitution",
        "intelligence", "wisdom", "charisma"};
    inline constexpr std::array kSkills = {
        "acrobatics", "arcana", "athletics", "crafting", "deception",
        "diplomacy", "intimidation", "medicine", "nature", "occultism",
        "performance", "religion", "society", "stealth", "survival",
        "thievery"};
    inline constexpr std::array kLores = {"lore", "lorealt"};
    // A Lore skill's key: the website's two, or one of this app's extra
    // ones ("lore3", "lore4", ...; kept under "pf2monster").
    bool isLoreKey(const QString &key);
    inline constexpr std::array kOtherStats = {
        "perception", "ac", "fortitude", "reflex", "will", "hp",
        "resistance", "weakness", "immunity",
        "spellattack", "spelldc", "minprof"};

    struct Monster
    {
        QString name;
        Value level = 0;     // the level the benchmarks are built from
        Value showlevel;     // the level shown, when it differs (empty: level)
        Value adjust = 0;    // the site's -5..+5 level adjustment
        QString alignment = QStringLiteral("-");
        QString size = QStringLiteral("medium");
        QString type = QStringLiteral("humanoid");
        QString creature;    // the label before the level ("CREATURE")
        QString traits;      // comma separated
        QStringList tags;
        QString languages;
        QString info;
        QString description; // markdown
        QString items;
        QString speed;
        QString savenote;
        QString imgurl;
        QString roadmap;

        // Every Stat, by its JSON key (see the k... lists above).
        std::vector<std::pair<QString, Stat>> stats;

        std::vector<Strike> strikes;
        std::vector<Special> specials;

        // A non-combat level (GM Core, Building NPCs: a barrister of level
        // -1 in combat, 4 for legal matters): the statistics listed take
        // their hints from its tables. Not shown in the statblock; kept
        // under "pf2monster".
        std::optional<int> nonCombatLevel;
        QString nonCombatLabel; // "legal matters", for the user
        QStringList nonCombatStats; // "diplomacy", "perception", ...
        // Every Lore skill's key, the website's two first.
        QStringList loreKeys() const;
        // Adds an extra Lore skill (after the others); returns its key.
        QString addLore();
        // Removes an extra Lore skill; the ones after it move up a key, so
        // the keys stay "lore3", "lore4", ... in order.
        void removeLore(const QString &key);

        // The level whose tables give `statKey`'s hint.
        int hintLevel(const QString &statKey) const;

        // In the adjusted monster: the traits its adjustments added ("fire"),
        // which the statblock puts in alphabetical order; not saved.
        QStringList addedTraits;
        // In the adjusted monster: what its templates put after its HP
        // ("fast healing 7", "void healing"); not saved.
        QStringList hpAbilities;
        // In the adjusted monster: what its adjustments do to Recall
        // Knowledge (Rumored Cryptid's Creature Obscura: "incredibly
        // hard", Society); not saved.
        QString recallDifficulty;
        QStringList recallSkills;

        std::vector<HpPart> hpParts;
        // The label of the creature's own HP line once it has parts.
        QString mainPart = QStringLiteral("body");

        QString spelltype;
        Value focuspoints = 0;
        Value cantriplevel;
        QString ritualtype;
        QString rituals;
        std::array<QString, kSpellRanks> spells;
        QString constant;
        std::vector<SpellList> morespells;

        // The statblock's look: a theme preset's key ("starfinder"), empty
        // for Pathfinder. Kept under "pf2monster".
        QString theme;
        // Where its Recall Knowledge line goes: "" (the statblock, below
        // the traits), "above" or "below" the description, or "off"; and
        // whether it shows the Lore DCs too.
        QString recallKnowledge;
        bool recallLore = false;

        // Keys the model doesn't know, written back as they came.
        QJsonObject extra;
        // This app's own data (the "pf2monster" key).
        QJsonObject own;

        Monster();

        Stat &stat(const QString &key);
        const Stat *findStat(const QString &key) const;

        static Monster fromJson(const QJsonObject &json);
        QJsonObject toJson() const;

        // Reads or writes a .json file. On failure, false and `error` set.
        static std::optional<Monster> load(const QString &path, QString *error = nullptr);
        bool save(const QString &path, QString *error = nullptr) const;
    };

    // The version of the "pf2monster" data this build writes.
    inline constexpr int kFormatVersion = 1;
} // namespace monster
