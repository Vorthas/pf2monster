// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

// Adjustments (Elite, Weak, ...) and later templates: changes applied on top
// of the creature the form edits (resources/adjustments/*.json, one file
// each). The monster keeps the ids of those applied, in order, under its
// "pf2monster" key; the statblock shows the result, worked out again from
// the base creature each time. They stack, repeats included.
namespace monster
{
    struct Monster;

    struct Modifier
    {
        // A range of levels (the level before the change) and what it gets.
        struct Step
        {
            std::optional<int> min, max;
            int change = 0;
        };

        // A statistic of some kind ("fly" Speed, "fire" resistance) whose
        // amount depends on the level before the change (0: none).
        struct Typed
        {
            QString type;
            std::vector<Step> steps;
            // Instead of the steps: the amount from its new level, as in
            // {=...} text, with `resmin` / `resmax` the resistance table's
            // ends ("max(1, level)", "resmin").
            QString expr;
            // After the amount: "all damage 5 (except force, ghost touch,
            // spirit, or vitality; double resistance to non-magical)".
            QString suffix;
            // Only when this choice is made (see Ability::when).
            QString when;
            // Neither steps nor expr: an entry without an amount, added as
            // written ("strigoi weaknesses").
            bool plain() const { return steps.empty() && expr.isEmpty(); }
        };

        // A Speed worked out from its other Speeds (Amphibious: swim Speed
        // half its land Speed).
        struct DerivedSpeed
        {
            QString type;        // "swim", or "land"
            QString from;        // "land", "swim", ... or "fastest"
            bool half = false;   // rounded down to a multiple of 5
            int min = 5;
            bool onlyIfMissing = true; // else raised to it if smaller
            QString when;              // only when this choice is made
        };

        // An ability it adds to the statblock. Its text can hold:
        //   {name}, {Name}  what the creature is called ("the guard",
        //                   "Sziskazha")
        //   {halfHp}        half its Hit Points
        //   {level}         its level after the change
        //   {=expr}         a number from its level: + - * / (rounded
        //                   down), parentheses, min(a, b), max(a, b);
        //                   "{=max(2, level)}d6"
        //   {steps|t|5=t|15=t}  the text of the last step its level reaches
        //   {dc:hard}       GM Core's DC for its level at that difficulty
        //   {spelldc:high}  the creature spell DC table at its level
        //   {abilitydc}     the highest DC in its other abilities, else
        //                   the high spell DC for its level
        //   {opt:key}       a choice's text ({opt:feats}: "A and B");
        //                   {opt:energy|lower} in lower case
        //   {skill:high}    the skill table at its level ("+17")
        //   {spellattack:high}, {area:limited}  those tables at its level
        //   {statdc:will}   10 + its Will (any statistic)
        //   {=...} can also name its statistics: {=hp / 3}, {=spelldc - 2}
        //   {strikeattack:best}  its best melee Strike's attack ("+15");
        //                   {strikeattack:claw} its claw's, else the best
        //   {owndc:moderate}  the highest DC it lists (abilities, spells),
        //                   else the spell DC table's for its level
        //   {tradition}     the "tradition" choice, else its tradition
        //                   trait (arcane, divine, occult, primal), else
        //                   nothing
        //   {opt:key|cap}   a choice's text with a capital first letter
        //   {strikedice:claw}    the dice of its Strike of that name
        //                   ("1d8+9"), else of its best melee Strike
        // The traits and name can hold them too ("arcane,
        // {opt:energy|lower}", "Curse of the Were{opt:animal|lower}"); a
        // trait that comes out empty is left out.
        // Not added if the creature has an ability of that name already
        // (Reactive Strike, or the user's own Energy Wave).
        struct Ability
        {
            QString key; // unique within the adjustment
            QString name;
            QString type;    // general, defense, offense
            QString actions; // none, one, two, three, "one to three", ..., free, reaction
            QString traits;
            QString description;
            // Only when this choice is made: "abilities:stench" (picked),
            // "skills:diplomacy" (chosen), "primaryExtreme" (ticked); "a&b"
            // for both; "!fullText" when it isn't.
            QString when;
        };

        // A Strike it adds (Mutant's marrowlance): the attack and damage of
        // the creature's best Strike of that type, else the benchmarks for
        // its level; the damage gets `damageType`.
        struct MadeStrike
        {
            QString key;
            QString name;
            // Copied from its "best" Strike of that type (the highest
            // attack), or its "lowest" one (the least damage: NPC Core's
            // jaws "deal damage equal to the creature's lowest melee
            // Strike").
            QString from = QStringLiteral("best");
            QString type = QStringLiteral("Ranged");
            QString traits;
            QString damageType;
            QString attackBenchmark = QStringLiteral("moderate");
            QString damageBenchmark = QStringLiteral("low");
            QString when;
            // Templates' Strikes:
            //  from "agile": its agile Strike's damage, else the damage
            //    table's entry nearest three-quarters of its best Strike's
            //    (claws); "nonagile": its best non-agile Strike's (jaws);
            //  attack "best": its best melee Strike's attack (else the
            //    benchmark).
            //  `damage`: the damage written out, with {=...} ("{=max(1,
            //    level / 3)}d8 void"), instead of copying it.
            //  `extra`: after the damage ("plus Grab"), with {opt:...}.
            QString damageText;
            QString extra;
            // from "nonagile" without one: the damage table's entry nearest
            // its best Strike's average times this (Werecreature's jaws:
            // one-third more than its agile Strikes).
            double fallbackScale = 0;
            // A reach trait this much longer than its size's (Failed
            // Prophet's fist: 5).
            int reachPlus = 0;
            // Takes the place of its Strike of that name (Failed Prophet's
            // fist), instead of leaving it be.
            bool replace = false;
            // The die its damage uses ("d8"): a copied "2d4+9" becomes
            // "2d8+9" (serpentfolk fangs from their best Strike).
            QString die;
            // If it has a Strike of that name already, that one gets
            // `extra` and `traits` instead of it being left be.
            bool mergeExtra = false;
        };

        // A choice the options dialog offers (resources/adjustments: "dialog":
        // "options"). Kept in the applied entry's options under `key`: a
        // list of item keys ("pick"), one item key ("choice"), a bool
        // ("check") or text ("text").
        struct Choice
        {
            struct Item
            {
                QString key;
                QString label;
                QString tip;
                bool on = false;              // picked by default ("pick")
                std::optional<int> minLevel;  // offered from this level
                // Changes made only when it is picked or chosen.
                std::shared_ptr<const Modifier> changes;
            };
            QString key;
            QString type; // pick, choice, check, text
            QString label;
            QString note; // "Two to three of these", under the label
            QString placeholder;
            QJsonValue defaultValue; // check, text, choice
            // A "check" offered from this level (after the change): below
            // it, hidden and not ticked.
            std::optional<int> minLevel;
            // Offered only when this choice is made ("extras:hybrid-form").
            QString when;
            std::vector<Item> items;
            // "domains": the items are the domains in resources/domains.json
            // (after a "(none)" item).
            QString itemsFrom;
        };

        QString id;     // "elite"; a homebrew one "homebrew:custom-stonekin"
        // A homebrew one's file (empty: built in).
        QString file;
        int order = 1000; // menu order among the built-in ones
        QString kind;   // "adjustment" or "template"
        QString name;   // "Elite"
        QString prefix; // put before the name in the statblock
        // Whether % and the abilities' text name it with the prefix too
        // (false for Elite and Weak: "the guard").
        bool prefixInText = true;
        QString rules;  // a summary of the rules, for the menu's status tip
        // The submenu it is in ("Elemental"), or empty; submenus can nest
        // ("Undead/Vampire").
        QString group;
        // What the rules leave to the GM ("you can ..."), shown under it in
        // the form; not applied.
        QString optional;
        // "mythic": chosen in a dialog (the choices are kept with it, see
        // Model/Mythic.h); "options": the general options dialog, from
        // `choices`; empty: added as it is.
        QString dialog;
        // The levels it can be applied at (the level so far); outside them it
        // changes nothing (Weak: level 1 and up).
        std::optional<int> minLevel, maxLevel;
        std::vector<Step> level; // the level change; first match wins
        int bonus = 0;  // AC, attacks, DCs, saves, Perception, skills
        int strikeDamage = 0;
        std::vector<Step> hp;    // HP change by the level before it
        // Added if missing: traits; languages (only to a creature that has
        // some); senses ("tremorsense (imprecise) 60 feet", after
        // Perception); immunities.
        QStringList traits, languages, senses, immunities;
        // Added, or raised to this amount if it has a smaller one.
        std::vector<Typed> speeds, resistances, weaknesses;
        // A damage type its Strikes gain (Fire): one die of a Strike with
        // several becomes this type; one with a single die deals 1 more.
        QString strikeDie;
        QStringList removeTraits;
        // Skills set to its highest skill modifier (left out if it has no
        // skills).
        QStringList skillsToHighest;
        std::vector<DerivedSpeed> derivedSpeeds;
        QString size; // the size it becomes ("tiny"), or empty
        // Miniature: its lowest-reach melee Strikes get this reach, the
        // others `reachOthers` ("reach 0 feet" / "reach 5 feet" traits).
        std::optional<int> reachLowest, reachOthers;
        std::vector<Ability> abilities;
        // Attributes raised to at least this ("intelligence": -3).
        std::vector<std::pair<QString, int>> attributeMin;
        // "rare": at least rare (a unique one stays); "raise": common ->
        // uncommon -> rare.
        QString rarity;
        // A trait (or the creature type) that becomes another: animal ->
        // beast.
        std::vector<std::pair<QString, QString>> replaceTraits;
        // Sizes it grows by; if that makes it Huge or bigger, its melee
        // Strikes' reach grows by `growReach` feet.
        int grow = 0;
        int growReach = 0;
        // A sense it has that becomes another (darkvision -> greater
        // darkvision); `senses` it lacks are added as usual.
        std::vector<std::pair<QString, QString>> senseUpgrades;
        // Skills raised to a benchmark for the new level: "high", ..., or
        // "primary" (high; extreme at 11th level and up when the
        // "primaryExtreme" choice is ticked) and "secondary" (moderate).
        std::vector<std::pair<QString, QString>> skillBenchmarks;
        std::vector<MadeStrike> strikes;
        std::vector<Choice> choices;

        // NPC Core's keys.
        // A trait replaced by another, or the other added if it's missing
        // (an ancestry's: any ancestry -> dwarf, on any NPC); the creature
        // type too (humanoid -> plant). kAnyAncestry replaces all its
        // ancestry traits.
        std::vector<std::pair<QString, QString>> replaceOrAdd;
        // Languages added even to a creature with none (ancestries).
        bool languagesAlways = false;
        // The land Speed: set to `landSet`, raised by `landAdd`, lowered to
        // `landMax` ("20 feet if higher"), raised to `landMin` ("30 feet if
        // lower"; Nosferatu: "5 feet or to 30 feet, whichever is higher").
        std::optional<int> landSet, landAdd, landMax, landMin;
        QStringList items; // added to its Items if missing
        // Statistics changed by an amount when it has them (Will -1,
        // Athletics -2).
        std::vector<std::pair<QString, int>> statChanges;
        // A Lore skill by name ("Labor Lore", or "{opt:lore}"): raised to a
        // benchmark of the new level, or to its highest skill modifier
        // ("highest"). Uses a Lore row of that name, else an empty one,
        // else a new one.
        struct Lore
        {
            QString name;
            QString benchmark;
        };
        std::vector<Lore> lores;
        // A skill it gains if it has none: at `benchmark` for the new level,
        // or `better` when the attribute `ifHigh` is high or extreme for it
        // (Retired: Deception moderate, high with high Charisma). `note`
        // goes in the skill's note, with {+N} for its modifier plus N
        // ("+16 to hide its identity").
        struct NewSkill
        {
            QString skill;
            QString benchmark;
            QString ifHigh;
            QString better;
            QString note;
        };
        std::vector<NewSkill> newSkills;
        // Strikes renamed and retyped (Lizardfolk: fist -> claw, slashing
        // instead of bludgeoning).
        struct Rename
        {
            QString from, name, damageFrom, damageTo;
        };
        std::vector<Rename> renameStrikes;

        // Templates' keys.
        // Left out of `bonus`: "perception", "dcs" (several templates list
        // only some of the statistics).
        QStringList bonusSkip;
        int spellBonus = 0; // spell DCs and spell attacks (Lich)
        // Loses the traits of its life: human, humanoid and the ancestry
        // traits; a creature type among them becomes `livingType`.
        bool removeLiving = false;
        QString livingType;
        // After its HP, as Paizo prints them: "void healing", or a typed
        // amount ("fast healing" 7).
        QStringList hpAbilities;
        std::vector<Typed> hpAmounts;
        QString saveNote; // added to the saves' note
        // Traits its Strikes gain: all of them, melee ones.
        QStringList strikeTraitsAll, strikeTraitsMelee;
        // Added to each Strike's damage ("plus 1d6 {opt:energy}").
        QString strikeDamageAll, strikeDamageMelee;
        // Added to the Strikes whose damage is of that type ("piercing" ->
        // "plus serpentfolk venom"); joined to a rider it has already
        // ("plus Grab and serpentfolk venom").
        std::vector<std::pair<QString, QString>> strikeDamageByType;
        // After the languages' semicolon, added if missing or raised to
        // this amount ("telepathy 100 feet"), even to a creature without
        // languages.
        QStringList languageExtras;
        // Innate spells added to a spell list of that name ("Divine
        // Innate"), made if it has none, with the DC from the spell DC
        // table at `dc` for its level: "telekinetic haul (x3)" at the rank
        // "(level + 1) / 2" works out to (as {=...}).
        struct InnateSpell
        {
            QString list;
            QString spell;
            QString rank;
            QString dc = QStringLiteral("high");
            QString when;
            // Gets a spell attack too (DC - 8: Divine Warden's divine lance).
            bool attack = false;
        };
        std::vector<InnateSpell> innateSpells;
        // Rank "cantrip" puts it among the cantrips; dc "own:moderate" is
        // the highest DC it lists, else the table's.

        // Batch 3's keys.
        // Attributes set to these (Ghost: Strength -5, Constitution +0).
        std::vector<std::pair<QString, int>> attributeSet;
        // Its land Speed becomes a fly Speed (Ghost: "fly 25 feet").
        bool landToFly = false;
        // Its best melee Strike made into another (Ghost's ghostly hand):
        // renamed, `attack` more, these traits instead of its own, and
        // its damage type `damageType`.
        struct ConvertStrike
        {
            QString name, traits, damageType;
            int attack = 0;
        };
        std::optional<ConvertStrike> convertStrike;
        // Its abilities whose name holds `match` ("breath"): renamed (if
        // the name lacks `rename`), `addDamage` after their first damage
        // ("plus 4d6 void damage"), `append` after their text, `traits`
        // added (Ravener's Void Breath). Text can hold {name}, {opt:...}.
        struct EditAbility
        {
            QString match, rename, addDamage, append, traits;
        };
        std::vector<EditAbility> editAbilities;
        // Domain spells for the domains chosen in `choices` (keys of
        // "itemsFrom": "domains" choices): each domain's spell, and its
        // advanced spell from `advancedLevel`, in a focus spell list
        // `list` with `points` Focus Points (steps by its new level) and
        // the DC `dc` (as InnateSpell's).
        struct DomainSpells
        {
            QStringList choices;
            QString list;
            int advancedLevel = 0;
            std::vector<Step> points;
            QString dc;
        };
        std::optional<DomainSpells> domainSpells;
        // Recall Knowledge (when its choice is made): the DC at this
        // difficulty instead of its rarity's, and more skills (Rumored
        // Cryptid's Creature Obscura: incredibly hard, Society or a Lore).
        QString recallDifficulty;
        QStringList recallSkills;
        QString recallWhen;
    };

    // A domain from resources/domains.json: its domain spell and advanced
    // domain spell.
    struct Domain
    {
        QString name, spell, advanced;
        // What a choice stores: the name in lower case ("death"), a
        // homebrew one's with the prefix ("homebrew:storm").
        QString key;
        QString file; // a homebrew one's file (empty: built in)
    };
    // The built-in domains and the homebrew ones (domains/ folders), in
    // alphabetical order.
    const std::vector<Domain> &domains();

    // Reads all homebrew again (Homebrew > Reload Homebrew, or after an
    // editor saved): domains, traits, adjustments and templates; problems
    // go to homebrewProblems() (cleared first). The first use of any of
    // them loads it all the first time.
    void reloadHomebrew();

    // What % and the adjustments' abilities call the creature: the name
    // typed with the prefixes before it lower-cased ("Experimental flame
    // drake" -> "experimental flame drake"), leaving out those of
    // adjustments with prefixInText off (Elite, Weak); a proper name
    // (capitalized, "Sziskazha") as typed.
    QString spokenName(const QString &adjustedName, const QString &baseName);

    // The applied entry's choices with each one not made yet at its
    // default (an options dialog's adjustment).
    QJsonObject optionsWithDefaults(const Modifier &modifier, const QJsonObject &options);

    // What an ability an adjustment makes (Special::generated) is called
    // after: the adjustment's name ("Amphibious", "Mythic").
    QString generatedSource(const QString &generatedKey);

    // The name the form's applied adjustments list shows: a homebrew one's
    // with " (homebrew)" ("Goron (homebrew)"), so it's told apart from a
    // built-in, even one of the same name (the user, 2026-10-26). The
    // menus mark homebrew their own way (MainWindow's markHomebrew()).
    QString shownName(const Modifier &modifier);

    // Every adjustment and template, in menu order: the built-in ones,
    // then the homebrew ones (Model/Homebrew.h).
    const std::vector<Modifier> &modifiers();
    // An adjustment or template from a file's object, as `id` (the
    // homebrew editor's preview); no checks.
    Modifier modifierFromJson(const QJsonObject &root, const QString &id);
    // While a homebrew entry is being edited: an adjustment findModifier()
    // also knows, under kPreviewModifierId; nullopt removes it.
    inline const QString kPreviewModifierId = QStringLiteral("homebrew-preview:entry");
    void setPreviewModifier(const std::optional<Modifier> &modifier);
    // The keys an adjustment/template file can have at its top level and in
    // "changes" (for the editor's checks).
    const QSet<QString> &modifierTopKeys();
    const QSet<QString> &modifierChangeKeys();
    // A submenu path tidied: "Undead / Vampire/" is "Undead/Vampire".
    QString groupPath(const QString &group);
    // In replaceOrAdd, the trait it has: every ancestry trait the creature
    // has ({"any ancestry": "stonekin"}).
    inline const QString kAnyAncestry = QStringLiteral("any ancestry");
    // The ancestry traits, in lower case: the built-in ones (resources/
    // ancestry-traits.json, from tools/scripts/aon_ancestries.py), homebrew
    // traits marked "ancestry", and every trait an "any ancestry"
    // replacement gives (so a homebrew ancestry replaces another).
    QSet<QString> ancestryTraits();

    // Reads them all again (Homebrew > Reload Homebrew); problems with
    // homebrew files go to homebrewProblems(). Pointers from modifiers() and
    // findModifier() no longer hold afterwards.
    void reloadModifiers();
    const Modifier *findModifier(const QString &id);

    // The ids applied to the monster, in order.
    QStringList appliedModifiers(const Monster &monster);
    // Replaces them with these, without options.
    void setAppliedModifiers(Monster &monster, const QStringList &ids);
    // The choices kept with the applied one at `index` (a dialog's).
    QJsonObject modifierOptions(const Monster &monster, int index);
    void addAppliedModifier(Monster &monster, const QString &id,
                            const QJsonObject &options = QJsonObject());
    void setModifierOptions(Monster &monster, int index, const QJsonObject &options);
    void moveAppliedModifier(Monster &monster, int from, int to);
    // The applied entry with id `from` gets id `to` instead, its options
    // kept (the homebrew editor's preview); false if none has `from`.
    bool replaceAppliedModifier(Monster &monster, const QString &from, const QString &to);
    void removeAppliedModifier(Monster &monster, int index);

    // For each applied one, whether it changes anything at the level it
    // comes to (false: outside its levels, or unknown to this build).
    QList<bool> modifiersInEffect(const Monster &monster);

    // The level shown after the applied ones (the display level when one is
    // set), or nullopt if none changes it.
    std::optional<int> adjustedLevel(const Monster &monster);

    // The monster with its applied adjustments worked in: numbers, level
    // (as the display level), DCs in ability text, the name's prefix. The
    // base monster when none are applied (ids this build doesn't know are
    // skipped).
    Monster adjusted(const Monster &monster);
    // Makes the monster's places for the abilities its adjustments make
    // (Special::generated) match them: places for ones no longer made go,
    // new ones get a place at the end. True if anything changed.
    bool syncGeneratedSpecials(Monster &monster);

    // The same with only the first `count` applied; `level` gets the level
    // they come to (what the next one would see).
    Monster adjustedUpTo(const Monster &monster, int count, int *level = nullptr);
} // namespace monster
