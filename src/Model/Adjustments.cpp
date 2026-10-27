// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Adjustments.h"

#include "Model/Benchmarks.h"
#include "Model/Homebrew.h"
#include "Model/Mythic.h"
#include "Model/Roadmaps.h"
#include "Model/TraitNotes.h"
#include "Model/Monster.h"
#include "Model/ScribeExport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <tuple>

namespace monster
{
    namespace
    {
        const QString kModifiersKey = QStringLiteral("modifiers");

        // Where the abilities adjustments make name the creature, until
        // nameAbilities() writes the name in.
        const QString kCallIt = QStringLiteral("\u0001callit\u0001");
        const QString kCallItCapital = QStringLiteral("\u0001CallIt\u0001");

        // As % names it ("the experimental flame drake"; a proper name as it
        // is), with every prefix: in the abilities' text.
        void nameAbilities(Monster &m, const QString &baseName)
        {
            const QString spoken = spokenName(m.name, baseName);
            const QString callIt = spoken.isEmpty()          ? QStringLiteral("the creature")
                                   : spoken.at(0).isLower() ? QStringLiteral("the ") + spoken
                                                            : spoken;
            const QString capital = callIt.left(1).toUpper() + callIt.mid(1);
            for (Special &s : m.specials)
            {
                s.description.replace(kCallIt, callIt).replace(kCallItCapital, capital);
            }
        }

        std::vector<Modifier::Step> readSteps(const QJsonValue &json)
        {
            std::vector<Modifier::Step> steps;
            for (const QJsonValue &v : json.toArray())
            {
                const QJsonObject o = v.toObject();
                Modifier::Step step;
                if (o.contains(QStringLiteral("min")))
                {
                    step.min = o.value(QStringLiteral("min")).toInt();
                }
                if (o.contains(QStringLiteral("max")))
                {
                    step.max = o.value(QStringLiteral("max")).toInt();
                }
                step.change = o.value(QStringLiteral("change")).toInt();
                steps.push_back(step);
            }
            return steps;
        }

        QStringList readStrings(const QJsonValue &json)
        {
            QStringList list;
            for (const QJsonValue &v : json.toArray())
            {
                list << v.toString();
            }
            return list;
        }

        // [{"type": "fly", "steps": [...]}, ...]
        std::vector<Modifier::Typed> readTyped(const QJsonValue &json)
        {
            std::vector<Modifier::Typed> list;
            for (const QJsonValue &v : json.toArray())
            {
                const QJsonObject o = v.toObject();
                list.push_back({o.value(QStringLiteral("type")).toString(),
                                readSteps(o.value(QStringLiteral("steps"))),
                                o.value(QStringLiteral("expr")).toString(),
                                o.value(QStringLiteral("suffix")).toString(),
                                o.value(QStringLiteral("when")).toString()});
            }
            return list;
        }

        int stepFor(const std::vector<Modifier::Step> &steps, int level)
        {
            for (const Modifier::Step &step : steps)
            {
                if ((!step.min || level >= *step.min) && (!step.max || level <= *step.max))
                {
                    return step.change;
                }
            }
            return 0;
        }

        // The "changes" of an adjustment file, or of one of its choices.
        void readChanges(Modifier &m, const QJsonObject &changes)
        {
            m.level = readSteps(changes.value(QStringLiteral("level")));
            m.bonus = changes.value(QStringLiteral("bonus")).toInt();
            m.strikeDamage = changes.value(QStringLiteral("strikeDamage")).toInt();
            m.hp = readSteps(changes.value(QStringLiteral("hp")));
            m.traits = readStrings(changes.value(QStringLiteral("traits")));
            m.languages = readStrings(changes.value(QStringLiteral("languages")));
            m.senses = readStrings(changes.value(QStringLiteral("senses")));
            m.immunities = readStrings(changes.value(QStringLiteral("immunities")));
            m.speeds = readTyped(changes.value(QStringLiteral("speeds")));
            m.resistances = readTyped(changes.value(QStringLiteral("resistances")));
            m.weaknesses = readTyped(changes.value(QStringLiteral("weaknesses")));
            m.strikeDie = changes.value(QStringLiteral("strikeDie")).toString();
            m.removeTraits = readStrings(changes.value(QStringLiteral("removeTraits")));
            m.skillsToHighest = readStrings(changes.value(QStringLiteral("skillsToHighest")));
            for (const QJsonValue &v : changes.value(QStringLiteral("derivedSpeeds")).toArray())
            {
                const QJsonObject o = v.toObject();
                Modifier::DerivedSpeed d;
                d.type = o.value(QStringLiteral("type")).toString();
                d.from = o.value(QStringLiteral("from")).toString();
                d.half = o.value(QStringLiteral("half")).toBool();
                d.min = o.value(QStringLiteral("min")).toInt(5);
                d.onlyIfMissing = o.value(QStringLiteral("onlyIfMissing")).toBool(true);
                d.when = o.value(QStringLiteral("when")).toString();
                m.derivedSpeeds.push_back(d);
            }
            m.size = changes.value(QStringLiteral("size")).toString();
            const QJsonObject mins = changes.value(QStringLiteral("attributeMin")).toObject();
            for (auto it = mins.begin(); it != mins.end(); ++it)
            {
                m.attributeMin.emplace_back(it.key(), it.value().toInt());
            }
            if (const QJsonObject reach = changes.value(QStringLiteral("reach")).toObject(); !reach.isEmpty())
            {
                m.reachLowest = reach.value(QStringLiteral("lowest")).toInt();
                m.reachOthers = reach.value(QStringLiteral("others")).toInt();
            }
            m.rarity = changes.value(QStringLiteral("rarity")).toString();
            const QJsonObject replace = changes.value(QStringLiteral("replaceTraits")).toObject();
            for (auto it = replace.begin(); it != replace.end(); ++it)
            {
                m.replaceTraits.emplace_back(it.key(), it.value().toString());
            }
            if (const QJsonObject grow = changes.value(QStringLiteral("grow")).toObject(); !grow.isEmpty())
            {
                m.grow = grow.value(QStringLiteral("sizes")).toInt();
                m.growReach = grow.value(QStringLiteral("reach")).toInt();
            }
            const QJsonObject upgrades = changes.value(QStringLiteral("senseUpgrades")).toObject();
            for (auto it = upgrades.begin(); it != upgrades.end(); ++it)
            {
                m.senseUpgrades.emplace_back(it.key(), it.value().toString());
            }
            const QJsonObject swaps = changes.value(QStringLiteral("replaceOrAdd")).toObject();
            for (auto it = swaps.begin(); it != swaps.end(); ++it)
            {
                m.replaceOrAdd.emplace_back(it.key(), it.value().toString());
            }
            m.languagesAlways = changes.value(QStringLiteral("languagesAlways")).toBool();
            if (const QJsonObject land = changes.value(QStringLiteral("landSpeed")).toObject(); !land.isEmpty())
            {
                for (const auto &[key, target] : {std::pair{"set", &m.landSet}, std::pair{"add", &m.landAdd},
                                                  std::pair{"max", &m.landMax}, std::pair{"min", &m.landMin}})
                {
                    if (land.contains(QLatin1String(key)))
                    {
                        *target = land.value(QLatin1String(key)).toInt();
                    }
                }
            }
            m.items = readStrings(changes.value(QStringLiteral("items")));
            const QJsonObject statChanges = changes.value(QStringLiteral("statChanges")).toObject();
            for (auto it = statChanges.begin(); it != statChanges.end(); ++it)
            {
                m.statChanges.emplace_back(it.key(), it.value().toInt());
            }
            for (const QJsonValue &v : changes.value(QStringLiteral("lores")).toArray())
            {
                const QJsonObject o = v.toObject();
                m.lores.push_back({o.value(QStringLiteral("name")).toString(),
                                   o.value(QStringLiteral("benchmark")).toString(QStringLiteral("high"))});
            }
            for (const QJsonValue &v : changes.value(QStringLiteral("newSkills")).toArray())
            {
                const QJsonObject o = v.toObject();
                m.newSkills.push_back({o.value(QStringLiteral("skill")).toString(),
                                       o.value(QStringLiteral("benchmark")).toString(QStringLiteral("moderate")),
                                       o.value(QStringLiteral("ifHigh")).toString(),
                                       o.value(QStringLiteral("better")).toString(QStringLiteral("high")),
                                       o.value(QStringLiteral("note")).toString()});
            }
            for (const QJsonValue &v : changes.value(QStringLiteral("renameStrikes")).toArray())
            {
                const QJsonObject o = v.toObject();
                m.renameStrikes.push_back({o.value(QStringLiteral("from")).toString(),
                                           o.value(QStringLiteral("name")).toString(),
                                           o.value(QStringLiteral("damageFrom")).toString(),
                                           o.value(QStringLiteral("damageTo")).toString()});
            }
            m.bonusSkip = readStrings(changes.value(QStringLiteral("bonusSkip")));
            m.spellBonus = changes.value(QStringLiteral("spellBonus")).toInt();
            m.removeLiving = changes.value(QStringLiteral("removeLiving")).toBool();
            m.livingType = changes.value(QStringLiteral("livingType")).toString();
            for (const QJsonValue &v : changes.value(QStringLiteral("hpAbilities")).toArray())
            {
                if (v.isString())
                {
                    m.hpAbilities << v.toString();
                }
                else
                {
                    const std::vector<Modifier::Typed> one = readTyped(QJsonArray{v});
                    m.hpAmounts.insert(m.hpAmounts.end(), one.begin(), one.end());
                }
            }
            m.saveNote = changes.value(QStringLiteral("saveNote")).toString();
            const QJsonObject strikeTraits = changes.value(QStringLiteral("strikeTraits")).toObject();
            m.strikeTraitsAll = readStrings(strikeTraits.value(QStringLiteral("all")));
            m.strikeTraitsMelee = readStrings(strikeTraits.value(QStringLiteral("melee")));
            const QJsonObject damageAdd = changes.value(QStringLiteral("strikeDamageAdd")).toObject();
            m.strikeDamageAll = damageAdd.value(QStringLiteral("all")).toString();
            m.strikeDamageMelee = damageAdd.value(QStringLiteral("melee")).toString();
            // Any other key is a damage type ("piercing").
            for (auto it = damageAdd.begin(); it != damageAdd.end(); ++it)
            {
                if (it.key() != QLatin1String("all") && it.key() != QLatin1String("melee"))
                {
                    m.strikeDamageByType.emplace_back(it.key(), it.value().toString());
                }
            }
            m.languageExtras = readStrings(changes.value(QStringLiteral("languageExtras")));
            const QJsonObject skills = changes.value(QStringLiteral("skillBenchmarks")).toObject();
            for (auto it = skills.begin(); it != skills.end(); ++it)
            {
                m.skillBenchmarks.emplace_back(it.key(), it.value().toString());
            }
            for (const QJsonValue &v : changes.value(QStringLiteral("innateSpells")).toArray())
            {
                const QJsonObject o = v.toObject();
                Modifier::InnateSpell spell;
                spell.list = o.value(QStringLiteral("list")).toString(QStringLiteral("Divine Innate"));
                spell.spell = o.value(QStringLiteral("spell")).toString();
                spell.rank = o.value(QStringLiteral("rank")).toString();
                spell.dc = o.value(QStringLiteral("dc")).toString(spell.dc);
                spell.when = o.value(QStringLiteral("when")).toString();
                spell.attack = o.value(QStringLiteral("attack")).toBool();
                m.innateSpells.push_back(spell);
            }
            const QJsonObject set = changes.value(QStringLiteral("attributeSet")).toObject();
            for (auto it = set.begin(); it != set.end(); ++it)
            {
                m.attributeSet.emplace_back(it.key(), it.value().toInt());
            }
            m.landToFly = changes.value(QStringLiteral("landToFly")).toBool();
            if (const QJsonObject o = changes.value(QStringLiteral("recallKnowledge")).toObject(); !o.isEmpty())
            {
                m.recallDifficulty = o.value(QStringLiteral("difficulty")).toString();
                m.recallSkills = readStrings(o.value(QStringLiteral("skills")));
                m.recallWhen = o.value(QStringLiteral("when")).toString();
            }
            if (const QJsonObject o = changes.value(QStringLiteral("convertStrike")).toObject(); !o.isEmpty())
            {
                m.convertStrike = Modifier::ConvertStrike{o.value(QStringLiteral("name")).toString(),
                                                          o.value(QStringLiteral("traits")).toString(),
                                                          o.value(QStringLiteral("damageType")).toString(),
                                                          o.value(QStringLiteral("attack")).toInt()};
            }
            for (const QJsonValue &v : changes.value(QStringLiteral("editAbilities")).toArray())
            {
                const QJsonObject o = v.toObject();
                m.editAbilities.push_back({o.value(QStringLiteral("match")).toString(),
                                           o.value(QStringLiteral("rename")).toString(),
                                           o.value(QStringLiteral("addDamage")).toString(),
                                           o.value(QStringLiteral("append")).toString(),
                                           o.value(QStringLiteral("traits")).toString()});
            }
            if (const QJsonObject o = changes.value(QStringLiteral("domainSpells")).toObject(); !o.isEmpty())
            {
                Modifier::DomainSpells d;
                d.choices = readStrings(o.value(QStringLiteral("choices")));
                d.list = o.value(QStringLiteral("list")).toString();
                d.advancedLevel = o.value(QStringLiteral("advancedLevel")).toInt();
                d.points = readSteps(o.value(QStringLiteral("points")));
                d.dc = o.value(QStringLiteral("dc")).toString(QStringLiteral("moderate"));
                m.domainSpells = d;
            }
        }

        // An adjustment or template from its file's object (its kind already
        // checked).
        Modifier readModifier(const QJsonObject &root)
        {
            Modifier m;
            m.kind = root.value(QStringLiteral("kind")).toString();
            m.id = root.value(QStringLiteral("id")).toString();
            m.order = root.value(QStringLiteral("order")).toInt(1000);
            m.name = root.value(QStringLiteral("name")).toString();
            m.prefix = root.value(QStringLiteral("prefix")).toString();
            m.prefixInText = root.value(QStringLiteral("prefixInText")).toBool(true);
            m.rules = root.value(QStringLiteral("rules")).toString();
            m.dialog = root.value(QStringLiteral("dialog")).toString();
            const QJsonObject applies = root.value(QStringLiteral("appliesTo")).toObject();
            if (applies.contains(QStringLiteral("min")))
            {
                m.minLevel = applies.value(QStringLiteral("min")).toInt();
            }
            if (applies.contains(QStringLiteral("max")))
            {
                m.maxLevel = applies.value(QStringLiteral("max")).toInt();
            }
            m.group = groupPath(root.value(QStringLiteral("group")).toString());
            m.optional = root.value(QStringLiteral("optional")).toString();
            readChanges(m, root.value(QStringLiteral("changes")).toObject());
            for (const QJsonValue &v : root.value(QStringLiteral("abilities")).toArray())
            {
                const QJsonObject o = v.toObject();
                m.abilities.push_back({o.value(QStringLiteral("key")).toString(),
                                       o.value(QStringLiteral("name")).toString(),
                                       o.value(QStringLiteral("type")).toString(),
                                       o.value(QStringLiteral("actions")).toString(QStringLiteral("none")),
                                       o.value(QStringLiteral("traits")).toString(),
                                       o.value(QStringLiteral("description")).toString(),
                                       o.value(QStringLiteral("when")).toString()});
            }
            for (const QJsonValue &v : root.value(QStringLiteral("strikes")).toArray())
            {
                const QJsonObject o = v.toObject();
                Modifier::MadeStrike strike;
                strike.key = o.value(QStringLiteral("key")).toString();
                strike.name = o.value(QStringLiteral("name")).toString();
                strike.type = o.value(QStringLiteral("type")).toString(strike.type);
                strike.traits = o.value(QStringLiteral("traits")).toString();
                strike.damageType = o.value(QStringLiteral("damageType")).toString();
                strike.attackBenchmark = o.value(QStringLiteral("attack")).toString(strike.attackBenchmark);
                strike.damageBenchmark = o.value(QStringLiteral("damage")).toString(strike.damageBenchmark);
                strike.when = o.value(QStringLiteral("when")).toString();
                strike.from = o.value(QStringLiteral("from")).toString(strike.from);
                strike.damageText = o.value(QStringLiteral("damageText")).toString();
                strike.extra = o.value(QStringLiteral("extra")).toString();
                strike.fallbackScale = o.value(QStringLiteral("fallback")).toDouble();
                strike.reachPlus = o.value(QStringLiteral("reachPlus")).toInt();
                strike.replace = o.value(QStringLiteral("replace")).toBool();
                strike.die = o.value(QStringLiteral("die")).toString().trimmed();
                strike.mergeExtra = o.value(QStringLiteral("mergeExtra")).toBool();
                m.strikes.push_back(strike);
            }
            for (const QJsonValue &v : root.value(QStringLiteral("choices")).toArray())
            {
                const QJsonObject o = v.toObject();
                Modifier::Choice choice;
                choice.key = o.value(QStringLiteral("key")).toString();
                choice.type = o.value(QStringLiteral("type")).toString();
                choice.label = o.value(QStringLiteral("label")).toString();
                choice.note = o.value(QStringLiteral("note")).toString();
                choice.placeholder = o.value(QStringLiteral("placeholder")).toString();
                choice.defaultValue = o.value(QStringLiteral("default"));
                choice.when = o.value(QStringLiteral("when")).toString();
                if (o.contains(QStringLiteral("minLevel")))
                {
                    choice.minLevel = o.value(QStringLiteral("minLevel")).toInt();
                }
                choice.itemsFrom = o.value(QStringLiteral("itemsFrom")).toString();
                if (choice.itemsFrom == QLatin1String("domains"))
                {
                    Modifier::Choice::Item none;
                    none.key = QString();
                    none.label = QStringLiteral("(none)");
                    choice.items.push_back(none);
                    for (const Domain &d : domains())
                    {
                        Modifier::Choice::Item item;
                        item.key = d.key;
                        item.label = d.name;
                        item.tip = QStringLiteral("%1; advanced: %2").arg(d.spell, d.advanced);
                        if (!d.file.isEmpty())
                        {
                            item.tip = QStringLiteral("Homebrew, from %1. ").arg(d.file) + item.tip;
                        }
                        choice.items.push_back(item);
                    }
                }
                for (const QJsonValue &iv : o.value(QStringLiteral("items")).toArray())
                {
                    const QJsonObject io = iv.toObject();
                    Modifier::Choice::Item item;
                    item.key = io.value(QStringLiteral("key")).toString();
                    item.label = io.value(QStringLiteral("label")).toString();
                    item.tip = io.value(QStringLiteral("tip")).toString();
                    item.on = io.value(QStringLiteral("on")).toBool();
                    if (io.contains(QStringLiteral("minLevel")))
                    {
                        item.minLevel = io.value(QStringLiteral("minLevel")).toInt();
                    }
                    if (const QJsonObject changes = io.value(QStringLiteral("changes")).toObject();
                        !changes.isEmpty())
                    {
                        auto made = std::make_shared<Modifier>();
                        made->id = m.id;
                        readChanges(*made, changes);
                        item.changes = made;
                    }
                    choice.items.push_back(item);
                }
                m.choices.push_back(choice);
            }
            return m;
        }

        // The keys an adjustment or template file can have, for telling the
        // user about typos in homebrew (see checkKeys()).
        const QSet<QString> kTopKeys{"format", "kind", "id", "name", "prefix", "prefixInText", "rules",
                                     "dialog", "appliesTo", "group", "optional", "order", "source",
                                     "license", "changes", "abilities", "strikes", "choices", "note"};
        const QSet<QString> kChangeKeys{
            "attributeMin", "attributeSet", "bonus", "bonusSkip", "convertStrike", "derivedSpeeds",
            "domainSpells", "editAbilities", "grow", "hp", "hpAbilities", "immunities", "innateSpells",
            "items", "landSpeed", "landToFly", "languageExtras", "languages", "languagesAlways", "level", "livingType",
            "lores", "newSkills", "rarity", "reach", "recallKnowledge", "removeLiving", "removeTraits",
            "renameStrikes", "replaceOrAdd", "replaceTraits", "resistances", "saveNote", "senses",
            "senseUpgrades", "size", "skillBenchmarks", "skillsToHighest", "speeds", "spellBonus",
            "statChanges", "strikeDamage", "strikeDamageAdd", "strikeDie", "strikeTraits", "traits",
            "weaknesses"};
        const QSet<QString> kAbilityKeys{"key", "name", "type", "actions", "traits", "description", "when"};
        const QSet<QString> kStrikeKeys{"key",   "name",       "type",  "traits",   "damageType",
                                        "attack", "damage",    "when",  "from",     "damageText",
                                        "extra", "fallback",   "reachPlus", "replace",
                                        "die",   "mergeExtra"};
        const QSet<QString> kChoiceKeys{"key",  "type",    "label",    "note",      "placeholder",
                                        "default", "when", "minLevel", "itemsFrom", "items"};
        const QSet<QString> kItemKeys{"key", "label", "tip", "on", "minLevel", "changes"};

        // A homebrew adjustment or template file checked for what the user
        // might have got wrong; nullopt (with problems recorded) if it can't
        // be used at all.
        std::optional<Modifier> readHomebrewModifier(const QString &path)
        {
            QString text;
            const std::optional<QJsonObject> read = readHomebrewJson(path, &text);
            if (!read)
            {
                return std::nullopt;
            }
            const QJsonObject &root = *read;
            auto problem = [&](const QString &key, const QString &message) {
                const auto [line, column] = keyPosition(text, key);
                addHomebrewProblem({path, line, column, message});
            };
            bool usable = true;
            if (root.value(QStringLiteral("format")).toInt() != 1)
            {
                problem(QStringLiteral("format"), root.contains(QStringLiteral("format"))
                                                      ? QStringLiteral("\"format\" should be 1")
                                                      : QStringLiteral("\"format\": 1 is missing (add it as the first key)"));
            }
            const QString kind = root.value(QStringLiteral("kind")).toString();
            if (kind != QLatin1String("adjustment") && kind != QLatin1String("template"))
            {
                problem(QStringLiteral("kind"),
                        QStringLiteral("\"kind\" should be \"adjustment\" or \"template\", so this file wasn't loaded"));
                usable = false;
            }
            const QString id = root.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty())
            {
                problem(QStringLiteral("id"), QStringLiteral("it needs an \"id\" (a short name without spaces, "
                                                            "like \"custom-stonekin\"), so this file wasn't loaded"));
                usable = false;
            }
            else if (id.contains(QLatin1Char(':')))
            {
                problem(QStringLiteral("id"), QStringLiteral("its \"id\" can't contain a colon, so this file wasn't loaded"));
                usable = false;
            }
            if (root.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            {
                problem(QStringLiteral("name"), QStringLiteral("it needs a \"name\" (what the menu shows), so this "
                                                              "file wasn't loaded"));
                usable = false;
            }
            checkKeys(path, text, root, kTopKeys);
            checkKeys(path, text, root.value(QStringLiteral("changes")).toObject(), kChangeKeys,
                      QStringLiteral("in \"changes\""));
            for (const QJsonValue &v : root.value(QStringLiteral("abilities")).toArray())
            {
                checkKeys(path, text, v.toObject(), kAbilityKeys, QStringLiteral("in an ability"));
            }
            for (const QJsonValue &v : root.value(QStringLiteral("strikes")).toArray())
            {
                checkKeys(path, text, v.toObject(), kStrikeKeys, QStringLiteral("in a Strike"));
            }
            for (const QJsonValue &v : root.value(QStringLiteral("choices")).toArray())
            {
                const QJsonObject choice = v.toObject();
                checkKeys(path, text, choice, kChoiceKeys, QStringLiteral("in a choice"));
                for (const QJsonValue &item : choice.value(QStringLiteral("items")).toArray())
                {
                    checkKeys(path, text, item.toObject(), kItemKeys, QStringLiteral("in a choice's item"));
                    checkKeys(path, text, item.toObject().value(QStringLiteral("changes")).toObject(), kChangeKeys,
                              QStringLiteral("in a choice item's \"changes\""));
                }
            }
            if (!usable)
            {
                return std::nullopt;
            }
            Modifier m = readModifier(root);
            m.id = kHomebrewPrefix + id;
            m.file = path;
            return m;
        }

        std::vector<Modifier> load()
        {
            std::vector<std::pair<int, Modifier>> loaded;
            const QDir dir(QStringLiteral(":/data/adjustments"));
            for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
            {
                QFile file(dir.filePath(name));
                if (!file.open(QIODevice::ReadOnly))
                {
                    continue;
                }
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                const QString kind = root.value(QStringLiteral("kind")).toString();
                if (kind != QLatin1String("adjustment") && kind != QLatin1String("template"))
                {
                    qWarning("%s is not an adjustment or template", qPrintable(name));
                    continue;
                }
                loaded.emplace_back(root.value(QStringLiteral("order")).toInt(1000), readModifier(root));
            }
            // Homebrew after the built-in ones, in folder order; an id
            // already taken by an earlier homebrew file is reported.
            QHash<QString, QString> taken;
            for (const QString &path : homebrewFiles(QStringLiteral("adjustments")))
            {
                std::optional<Modifier> m = readHomebrewModifier(path);
                if (!m)
                {
                    continue;
                }
                if (const auto it = taken.constFind(m->id); it != taken.constEnd())
                {
                    QFile file(path);
                    QString text;
                    if (file.open(QIODevice::ReadOnly))
                    {
                        text = QString::fromUtf8(file.readAll());
                    }
                    const auto [line, column] = keyPosition(text, QStringLiteral("id"));
                    addHomebrewProblem({path, line, column,
                                        QStringLiteral("its id \"%1\" is already used by %2, which was loaded "
                                                       "instead; give this one another id")
                                            .arg(m->id.mid(kHomebrewPrefix.size()), *it)});
                    continue;
                }
                taken.insert(m->id, path);
                // After the built-in ones of the same order.
                loaded.emplace_back(m->order, std::move(*m));
            }
            std::stable_sort(loaded.begin(), loaded.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            std::vector<Modifier> list;
            for (auto &entry : loaded)
            {
                list.push_back(std::move(entry.second));
            }
            return list;
        }

        // The level the rules look at: the one the statblock shows.
        int shownLevel(const Monster &m)
        {
            if (const std::optional<int> shown = m.showlevel.number())
            {
                return *shown;
            }
            return m.level.number().value_or(0);
        }

        // `value` with `delta` added to its number: a number stays a
        // number; in text ("+12 (+14 with ...)", "250 (regeneration)") the
        // leading number changes and the rest stays. Text without one is
        // kept.
        Value add(const Value &value, int delta)
        {
            if (const QJsonValue json = value.toJson(); json.isDouble())
            {
                return Value(json.toInt() + delta);
            }
            static const QRegularExpression leading(QStringLiteral(R"(^(\s*)([+-]?)(\d+))"));
            const QString text = value.text();
            const QRegularExpressionMatch match = leading.match(text);
            if (!match.hasMatch())
            {
                return value;
            }
            const int number =
                (match.captured(2) == QLatin1String("-") ? -1 : 1) * match.captured(3).toInt() + delta;
            QString shown = QString::number(number);
            if (match.captured(2) == QLatin1String("+") && number >= 0)
            {
                shown.prepend(QLatin1Char('+'));
            }
            return Value(match.captured(1) + shown + text.mid(match.capturedEnd()));
        }

        bool hasSpells(const Monster &m)
        {
            return std::any_of(m.spells.begin(), m.spells.end(),
                               [](const QString &s) { return !s.trimmed().isEmpty(); }) ||
                   !m.constant.trimmed().isEmpty();
        }

        // Empty values the statblock fills from their hints get the hint's
        // number, so the changes have something to add to.
        void fillHints(Monster &m)
        {
            const bool spells = hasSpells(m);
            for (auto &[key, stat] : m.stats)
            {
                const bool hinted = usesHint(key) ||
                                    (spells && (key == QLatin1String("spelldc") ||
                                                key == QLatin1String("spellattack")));
                if (hinted && stat.value.isEmpty())
                {
                    if (const Hint h = statHint(m, key); h.valid && h.value)
                    {
                        stat.value = Value(*h.value);
                    }
                }
            }
            const int level = m.level.number().value_or(0);
            for (SpellList &list : m.morespells)
            {
                if (list.spelldc.value.isEmpty())
                {
                    if (const Hint h = hint(Table::SpellDc, level, list.spelldc.benchmark);
                        h.valid && h.value)
                    {
                        list.spelldc.value = Value(*h.value);
                    }
                }
            }
            for (Strike &strike : m.strikes)
            {
                if (strike.attack.isEmpty())
                {
                    strike.attack = Value(0);
                }
            }
        }

        // The statistics that take the bonus (attributes, HP and the
        // resistances don't).
        bool takesBonus(const QString &key)
        {
            static const QStringList keys = [] {
                QStringList list{"perception", "ac", "fortitude", "reflex", "will",
                                 "spelldc", "spellattack"};
                for (const char *skill : kSkills)
                {
                    list << QString::fromLatin1(skill);
                }
                return list;
            }();
            return keys.contains(key) || isLoreKey(key);
        }

        // "3d6+14 piercing plus ..." -> "3d6+16 piercing plus ...": the first
        // roll's modifier; one is added if it has none.
        QString addDamage(const QString &damage, int delta)
        {
            static const QRegularExpression roll(
                QStringLiteral(R"((\d+d\d+)(?:\s*([+-])\s*(\d+)(?!\s*d\d))?)"));
            const QRegularExpressionMatch match = roll.match(damage);
            if (!match.hasMatch() || delta == 0)
            {
                return damage;
            }
            int modifier = match.captured(3).toInt();
            if (match.captured(2) == QLatin1String("-"))
            {
                modifier = -modifier;
            }
            modifier += delta;
            QString text = match.captured(1);
            if (modifier != 0)
            {
                text += (modifier > 0 ? QStringLiteral("+") : QString()) + QString::number(modifier);
            }
            QString out = damage;
            out.replace(match.capturedStart(), match.capturedLength(), text);
            return out;
        }

        // "DC 34" in text, flat checks ("DC 5 flat check") excepted.
        QString addTextDcs(const QString &text, int delta)
        {
            static const QRegularExpression dc(
                QStringLiteral(R"(\bDC (\d+)\b(?!\s+flat))"));
            QString out;
            qsizetype at = 0;
            for (auto it = dc.globalMatch(text); it.hasNext();)
            {
                const QRegularExpressionMatch match = it.next();
                out += QStringView(text).mid(at, match.capturedStart(1) - at);
                out += QString::number(match.captured(1).toInt() + delta);
                at = match.capturedEnd(1);
            }
            out += QStringView(text).mid(at);
            return out;
        }

        // A comma-separated list split at its first ';' ("Aklo, Common;
        // telepathy 100 feet"): the items before it, and the rest.
        std::pair<QStringList, QString> splitList(const QString &text)
        {
            const qsizetype semi = text.indexOf(QLatin1Char(';'));
            const QString head = semi < 0 ? text : text.left(semi);
            QStringList items;
            for (const QString &item : head.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                if (!item.trimmed().isEmpty())
                {
                    items << item.trimmed();
                }
            }
            return {items, semi < 0 ? QString() : text.mid(semi)};
        }

        QString joinList(const QStringList &items, const QString &rest)
        {
            return items.join(QStringLiteral(", ")) + rest;
        }

        // The index of the item that starts with the word `word` ("fire" in
        // "fire 10", "fly" in "fly 30 feet"), or -1.
        qsizetype findItem(const QStringList &items, const QString &word)
        {
            const QRegularExpression re(QStringLiteral(R"(^%1\b)").arg(QRegularExpression::escape(word)),
                                        QRegularExpression::CaseInsensitiveOption);
            for (qsizetype i = 0; i < items.size(); ++i)
            {
                if (re.match(items[i]).hasMatch())
                {
                    return i;
                }
            }
            return -1;
        }

        // Adds `item` to the list unless an item starting with its first
        // word is there.
        void addMissing(QString &list, const QString &item)
        {
            auto [items, rest] = splitList(list);
            if (findItem(items, item.section(QLatin1Char(' '), 0, 0)) < 0)
            {
                items << item;
                list = joinList(items, rest);
            }
        }

        // The first number in `text` ("fly 30 feet" -> 30), if any.
        std::optional<int> firstNumber(const QString &text)
        {
            static const QRegularExpression number(QStringLiteral(R"(\d+)"));
            const QRegularExpressionMatch match = number.match(text);
            return match.hasMatch() ? std::optional<int>(match.captured().toInt()) : std::nullopt;
        }

        // The amount of the `type` item in a list ("fire 10" -> 10), if it
        // has one with a number.
        std::optional<int> itemAmount(const QString &list, const QString &type)
        {
            const QStringList items = splitList(list).first;
            const qsizetype at = findItem(items, type);
            return at < 0 ? std::nullopt : firstNumber(items[at]);
        }

        // Sets the amount of the `type` item in a list, in its place ("fire
        // 12" -> "fire 2"); the rest of the item stays.
        void setItemAmount(QString &list, const QString &type, int amount)
        {
            auto [items, rest] = splitList(list);
            static const QRegularExpression number(QStringLiteral(R"(\d+)"));
            if (const qsizetype at = findItem(items, type); at >= 0)
            {
                if (const QRegularExpressionMatch match = number.match(items[at]); match.hasMatch())
                {
                    items[at].replace(match.capturedStart(), match.capturedLength(), QString::number(amount));
                    list = joinList(items, rest);
                }
            }
        }

        // Removes the `type` item from a list.
        void removeItem(QString &list, const QString &type)
        {
            auto [items, rest] = splitList(list);
            if (const qsizetype at = findItem(items, type); at >= 0)
            {
                items.removeAt(at);
                list = joinList(items, rest);
            }
        }

        // Adds "`type` `amount``unit`" to the list, or raises the amount of
        // the `type` item there if it is smaller ("fire 5" -> "fire 10",
        // "fly 20 feet" -> "fly 25 feet"); the rest of that item stays.
        void raiseItem(QString &list, const QString &type, int amount, const QString &unit)
        {
            auto [items, rest] = splitList(list);
            const qsizetype at = findItem(items, type);
            if (at < 0)
            {
                items << QStringLiteral("%1 %2%3").arg(type).arg(amount).arg(unit);
            }
            else
            {
                static const QRegularExpression number(QStringLiteral(R"(\d+)"));
                const QRegularExpressionMatch match = number.match(items[at]);
                if (!match.hasMatch())
                {
                    return; // "fire (see below)": left as written
                }
                if (match.captured().toInt() < amount)
                {
                    items[at].replace(match.capturedStart(), match.capturedLength(), QString::number(amount));
                }
            }
            list = joinList(items, rest);
        }

        // "3d8+15 bludgeoning plus venom" -> "2d8+15 bludgeoning plus 1d8
        // fire plus venom"; "1d6+2 bludgeoning" -> "1d6+2 bludgeoning plus
        // 1 fire". A Strike already of that type: one die gains nothing, a
        // single one deals 1 more. Text without a roll stays.
        QString addStrikeDie(const QString &damage, const QString &type)
        {
            static const QRegularExpression roll(QStringLiteral(
                R"((\d+)d(\d+)(\s*[+-]\s*\d+(?!\s*d\d))?(\s+([A-Za-z]+))?)"));
            const QRegularExpressionMatch match = roll.match(damage);
            if (!match.hasMatch())
            {
                return damage;
            }
            const int dice = match.captured(1).toInt();
            const bool same = match.captured(5).compare(type, Qt::CaseInsensitive) == 0;
            if (same)
            {
                return dice > 1 ? damage : addDamage(damage, 1);
            }
            QString first = match.captured(0);
            // What it gains: "1d8" (the die taken from the first roll) or 1.
            const QString die = QStringLiteral("d") + match.captured(2);
            if (dice > 1)
            {
                first.replace(0, match.captured(1).size(), QString::number(dice - 1));
            }
            QString out = damage;
            out.replace(match.capturedStart(), match.capturedLength(), first);
            const qsizetype after = match.capturedStart() + first.size();
            // Joins damage of that type already there ("plus 1d6 fire" ->
            // "plus 2d6 fire", "plus 1 fire" -> "plus 2 fire").
            const QRegularExpression existing(
                dice > 1 ? QStringLiteral(R"(\bplus (\d+)%1 %2\b)").arg(die, QRegularExpression::escape(type))
                         : QStringLiteral(R"(\bplus (\d+) %1\b)").arg(QRegularExpression::escape(type)),
                QRegularExpression::CaseInsensitiveOption);
            if (const QRegularExpressionMatch found = existing.match(out, after); found.hasMatch())
            {
                out.replace(found.capturedStart(1), found.capturedLength(1),
                            QString::number(found.captured(1).toInt() + 1));
                return out;
            }
            out.insert(after, dice > 1 ? QStringLiteral(" plus 1%1 %2").arg(die, type)
                                       : QStringLiteral(" plus 1 %1").arg(type));
            return out;
        }

        // The index of the land Speed in a Speed list (the item that
        // starts with a number: "25 feet"), or -1.
        qsizetype landItem(const QStringList &items)
        {
            static const QRegularExpression leading(QStringLiteral(R"(^\d)"));
            for (qsizetype i = 0; i < items.size(); ++i)
            {
                if (leading.match(items[i]).hasMatch())
                {
                    return i;
                }
            }
            return -1;
        }

        void deriveSpeed(Monster &m, const Modifier::DerivedSpeed &d)
        {
            auto [items, rest] = splitList(m.speed);
            auto feetOf = [&](const QString &type) -> std::optional<int> {
                const qsizetype at = type == QLatin1String("land") ? landItem(items) : findItem(items, type);
                return at < 0 ? std::nullopt : firstNumber(items[at]);
            };
            std::optional<int> source;
            if (d.from == QLatin1String("fastest"))
            {
                for (const QString &item : items)
                {
                    if (const std::optional<int> feet = firstNumber(item); feet && (!source || *feet > *source))
                    {
                        source = feet;
                    }
                }
            }
            else
            {
                source = feetOf(d.from);
            }
            if (!source)
            {
                return;
            }
            // Rounded down to a multiple of 5, never below the minimum.
            int feet = d.half ? *source / 2 / 5 * 5 : *source;
            feet = std::max(feet, d.min);
            if (d.type == QLatin1String("land"))
            {
                if (landItem(items) < 0)
                {
                    items.prepend(QStringLiteral("%1 feet").arg(feet));
                    m.speed = joinList(items, rest);
                }
                return;
            }
            if (d.onlyIfMissing && findItem(items, d.type) >= 0)
            {
                return;
            }
            raiseItem(m.speed, d.type, feet, QStringLiteral(" feet"));
        }

        // Sets the skills to its highest skill modifier (Lore included);
        // nothing if it has no skills.
        void skillsToHighest(Monster &m, const QStringList &skills)
        {
            std::optional<int> highest;
            QStringList keys;
            for (const char *skill : kSkills)
            {
                keys << QString::fromLatin1(skill);
            }
            keys += m.loreKeys();
            for (const QString &key : keys)
            {
                if (const Stat *stat = m.findStat(key))
                {
                    if (const std::optional<int> n = stat->value.number(); n && (!highest || *n > *highest))
                    {
                        highest = n;
                    }
                }
            }
            if (!highest)
            {
                return;
            }
            for (const QString &key : skills)
            {
                Stat &stat = m.stat(key);
                if (stat.value.number().value_or(*highest - 1) < *highest)
                {
                    stat.value = Value(*highest);
                }
            }
        }

        // Miniature: the lowest-reach melee Strikes get `lowest` reach, the
        // others `others`, as "reach N feet" traits. A Strike without one
        // has its size's reach.
        // "reach 10 feet" among a Strike's traits.
        const QRegularExpression &reachTrait()
        {
            static const QRegularExpression re(QStringLiteral(R"(\breach\s+(\d+)\s*(?:feet|foot|ft\.?))"),
                                               QRegularExpression::CaseInsensitiveOption);
            return re;
        }

        bool isMelee(const Strike &s)
        {
            return s.type.compare(QLatin1String("Ranged"), Qt::CaseInsensitive) != 0;
        }

        // A Strike's reach: its "reach N feet" trait, else its size's (tall
        // creatures').
        int reachOf(const Strike &s, const QString &size)
        {
            static const QHash<QString, int> bySize{{"tiny", 0},  {"small", 5},  {"medium", 5},
                                                    {"large", 10}, {"huge", 15}, {"gargantuan", 20}};
            const QRegularExpressionMatch match = reachTrait().match(s.traits);
            return match.hasMatch() ? match.captured(1).toInt() : bySize.value(size.trimmed().toLower(), 5);
        }

        // Gives the Strike a "reach N feet" trait, in place of the one it has.
        void setStrikeReach(Strike &s, int feet)
        {
            const QString trait = QStringLiteral("reach %1 feet").arg(feet);
            if (const QRegularExpressionMatch match = reachTrait().match(s.traits); match.hasMatch())
            {
                s.traits.replace(match.capturedStart(), match.capturedLength(), trait);
            }
            else
            {
                s.traits = s.traits.trimmed().isEmpty() ? trait : s.traits.trimmed() + QStringLiteral(", ") + trait;
            }
        }

        void setReach(Monster &m, int lowest, int others)
        {
            auto reach = [&](const Strike &s) { return reachOf(s, m.size); };
            auto melee = isMelee;
            std::optional<int> least;
            for (const Strike &s : m.strikes)
            {
                if (melee(s) && (!least || reach(s) < *least))
                {
                    least = reach(s);
                }
            }
            for (Strike &s : m.strikes)
            {
                if (melee(s))
                {
                    setStrikeReach(s, reach(s) == *least ? lowest : others);
                }
            }
        }

        // Primeval: grows by `sizes` (Gargantuan at most); if that makes it
        // Huge or bigger, its melee Strikes' reach grows by `reach` feet.
        void grow(Monster &m, int sizes, int reach)
        {
            static const QStringList order{"tiny", "small", "medium", "large", "huge", "gargantuan"};
            const qsizetype at = order.indexOf(m.size.trimmed().toLower());
            if (at < 0 || sizes <= 0)
            {
                return;
            }
            const QString old = m.size;
            m.size = order.at(std::min<qsizetype>(at + sizes, order.size() - 1));
            // Only when growing makes it Huge or Gargantuan: one already
            // Gargantuan stays as it is.
            if (reach > 0 && m.size != old.trimmed().toLower() &&
                order.indexOf(m.size) >= order.indexOf(QStringLiteral("huge")))
            {
                for (Strike &s : m.strikes)
                {
                    if (isMelee(s))
                    {
                        setStrikeReach(s, reachOf(s, old) + reach);
                    }
                }
            }
        }

        // The rarity among the traits ("uncommon"), empty for common.
        void setRarity(Monster &m, const QString &how)
        {
            static const QStringList rarities{"uncommon", "rare", "unique"};
            auto [items, rest] = splitList(m.traits);
            qsizetype at = -1;
            QString current;
            for (qsizetype i = 0; i < items.size(); ++i)
            {
                if (rarities.contains(items[i].toLower()))
                {
                    at = i;
                    current = items[i].toLower();
                }
            }
            QString wanted = current;
            if (how == QLatin1String("raise"))
            {
                wanted = current.isEmpty() ? QStringLiteral("uncommon") : current == QLatin1String("uncommon") ? QStringLiteral("rare") : current;
            }
            else if (rarities.indexOf(how) > rarities.indexOf(current))
            {
                wanted = how;
            }
            if (wanted == current)
            {
                return;
            }
            wanted[0] = wanted[0].toUpper();
            if (at >= 0)
            {
                items[at] = wanted;
            }
            else
            {
                items.prepend(wanted); // first, as the website writes it
            }
            m.traits = joinList(items, rest);
        }

        // Animal -> beast: the creature type, or a trait.
        void replaceTrait(Monster &m, const QString &from, const QString &to)
        {
            if (m.type.trimmed().compare(from, Qt::CaseInsensitive) == 0)
            {
                m.type = to;
                return;
            }
            auto [items, rest] = splitList(m.traits);
            if (const qsizetype at = findItem(items, from); at >= 0 && items[at].compare(from, Qt::CaseInsensitive) == 0)
            {
                items.removeAt(at);
                m.traits = joinList(items, rest);
                if (findItem(items, to) < 0)
                {
                    addMissing(m.traits, to);
                    m.addedTraits << to;
                }
            }
        }

        // Whether a sense is in the Perception note as a word ("darkvision"
        // in "greater darkvision"): "scent (imprecise) 30 feet" -> "scent".
        bool hasSense(const QString &note, const QString &sense)
        {
            static const QRegularExpression cut(QStringLiteral(R"(\s*[\(\d].*$)"));
            const QString base = QString(sense).remove(cut).trimmed();
            if (base.isEmpty())
            {
                return false;
            }
            const QRegularExpression word(QStringLiteral(R"(\b%1\b)").arg(QRegularExpression::escape(base)),
                                          QRegularExpression::CaseInsensitiveOption);
            return word.match(note).hasMatch();
        }

        // A sense it has that becomes another, in place: "darkvision" ->
        // "greater darkvision" (not when it has that already).
        void upgradeSense(Monster &m, const QString &from, const QString &to)
        {
            QString &note = m.stat(QStringLiteral("perception")).note;
            if (hasSense(note, to))
            {
                return;
            }
            const QRegularExpression word(QStringLiteral(R"(\b%1\b)").arg(QRegularExpression::escape(from)),
                                          QRegularExpression::CaseInsensitiveOption);
            note.replace(word, to);
        }

        // Whether an ability, Strike or change that is only made "when" a
        // choice is made has it ("abilities:stench", "primaryExtreme").
        bool chosen(const QJsonObject &options, const QString &when)
        {
            if (when.isEmpty())
            {
                return true;
            }
            // "abilities:ruinous-weapons&energy:fire": both.
            if (when.contains(QLatin1Char('&')))
            {
                const QStringList parts = when.split(QLatin1Char('&'));
                return std::all_of(parts.begin(), parts.end(),
                                   [&](const QString &part) { return chosen(options, part.trimmed()); });
            }
            // "!fullText": not made.
            if (when.startsWith(QLatin1Char('!')))
            {
                return !chosen(options, when.mid(1));
            }
            const qsizetype colon = when.indexOf(QLatin1Char(':'));
            const QJsonValue value = options.value(colon < 0 ? when : when.left(colon));
            if (colon < 0)
            {
                return value.toBool();
            }
            const QString item = when.mid(colon + 1);
            if (value.isArray())
            {
                return value.toArray().contains(item);
            }
            return value.toString() == item;
        }

        // A number from the level: "max(2, level)", "(level + 1) / 2". Integer
        // arithmetic, rounded down; nullopt if it can't be read.
        class Expression
        {
        public:
            // `monster`: its statistics can be named ("hp / 3", "spelldc - 2",
            // "10 + will").
            Expression(const QString &text, int level, const Monster *monster = nullptr)
                : text_(text), level_(level), monster_(monster)
            {
            }

            std::optional<int> value()
            {
                const std::optional<int> v = sum();
                skip();
                return pos_ == text_.size() ? v : std::nullopt;
            }

        private:
            void skip()
            {
                while (pos_ < text_.size() && text_[pos_].isSpace())
                {
                    ++pos_;
                }
            }

            bool take(QChar c)
            {
                skip();
                if (pos_ < text_.size() && text_[pos_] == c)
                {
                    ++pos_;
                    return true;
                }
                return false;
            }

            // Deeper than any real formula; a file nesting brackets (or
            // minus signs) further would otherwise exhaust the stack.
            static constexpr int kMaxDepth = 32;

            // Worked in 64 bits, so an int result too large to fit (an
            // overflow, or INT_MIN / -1, which traps) reads as nullopt.
            static std::optional<int> fits(qint64 v)
            {
                return v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()
                           ? std::optional<int>(static_cast<int>(v))
                           : std::nullopt;
            }

            static qint64 floorDiv(qint64 a, qint64 b)
            {
                const qint64 q = a / b;
                return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
            }

            std::optional<int> sum()
            {
                std::optional<int> v = product();
                while (v)
                {
                    if (take(QLatin1Char('+')))
                    {
                        const std::optional<int> r = product();
                        v = r ? fits(qint64(*v) + *r) : std::nullopt;
                    }
                    else if (take(QLatin1Char('-')))
                    {
                        const std::optional<int> r = product();
                        v = r ? fits(qint64(*v) - *r) : std::nullopt;
                    }
                    else
                    {
                        break;
                    }
                }
                return v;
            }

            std::optional<int> product()
            {
                std::optional<int> v = atom();
                while (v)
                {
                    if (take(QLatin1Char('*')))
                    {
                        const std::optional<int> r = atom();
                        v = r ? fits(qint64(*v) * *r) : std::nullopt;
                    }
                    else if (take(QLatin1Char('/')))
                    {
                        const std::optional<int> r = atom();
                        v = r && *r != 0 ? fits(floorDiv(*v, *r)) : std::nullopt;
                    }
                    else
                    {
                        break;
                    }
                }
                return v;
            }

            std::optional<int> atom()
            {
                if (depth_ >= kMaxDepth)
                {
                    return std::nullopt;
                }
                ++depth_;
                const std::optional<int> v = operand();
                --depth_;
                return v;
            }

            std::optional<int> operand()
            {
                skip();
                if (take(QLatin1Char('-')))
                {
                    const std::optional<int> v = atom();
                    return v ? fits(-qint64(*v)) : std::nullopt;
                }
                if (take(QLatin1Char('(')))
                {
                    const std::optional<int> v = sum();
                    return take(QLatin1Char(')')) ? v : std::nullopt;
                }
                const qsizetype start = pos_;
                while (pos_ < text_.size() && text_[pos_].isLetterOrNumber())
                {
                    ++pos_;
                }
                const QString word = text_.mid(start, pos_ - start);
                if (word.isEmpty())
                {
                    return std::nullopt;
                }
                if (word == QLatin1String("level"))
                {
                    return level_;
                }
                // The resistance table's ends for the level.
                if (word == QLatin1String("resmin") || word == QLatin1String("resmax"))
                {
                    const Hint h = hint(Table::Resistance, level_);
                    return word == QLatin1String("resmin") ? h.min : h.max;
                }
                if (word == QLatin1String("min") || word == QLatin1String("max"))
                {
                    if (!take(QLatin1Char('(')))
                    {
                        return std::nullopt;
                    }
                    const std::optional<int> a = sum();
                    if (!a || !take(QLatin1Char(',')))
                    {
                        return std::nullopt;
                    }
                    const std::optional<int> b = sum();
                    if (!b || !take(QLatin1Char(')')))
                    {
                        return std::nullopt;
                    }
                    return word == QLatin1String("min") ? std::min(*a, *b) : std::max(*a, *b);
                }
                bool ok = false;
                const int n = word.toInt(&ok);
                if (ok)
                {
                    return n;
                }
                if (monster_)
                {
                    if (const Stat *stat = monster_->findStat(word))
                    {
                        return stat->value.number();
                    }
                }
                return std::nullopt;
            }

            QString text_;
            int level_;
            const Monster *monster_ = nullptr;
            qsizetype pos_ = 0;
            int depth_ = 0;
        };

        // A choice's text for {opt:key}: the picked items' labels ("A and
        // B", "A, B and C"), the chosen one's, or the typed text (else the
        // choice's default).
        QString choiceText(const Modifier &mod, const QJsonObject &options, const QString &key, int level)
        {
            for (const Modifier::Choice &c : mod.choices)
            {
                if (c.key != key)
                {
                    continue;
                }
                const QJsonValue value = options.value(key);
                if (c.type == QLatin1String("text"))
                {
                    const QString text = value.toString().trimmed();
                    return text.isEmpty() ? c.defaultValue.toString() : text;
                }
                QStringList labels;
                for (const Modifier::Choice::Item &item : c.items)
                {
                    const bool on = value.isArray() ? value.toArray().contains(item.key) : value.toString() == item.key;
                    if (on && (!item.minLevel || level >= *item.minLevel))
                    {
                        labels << item.label;
                    }
                }
                if (labels.size() < 2)
                {
                    return labels.join(QString());
                }
                const QString last = labels.takeLast();
                return labels.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
            }
            return QString();
        }

        // {opt:key} in a short text (a Strike's added damage, an immunity).
        QString expandOptions(const Modifier &mod, const QJsonObject &options, const QString &text, int level)
        {
            static const QRegularExpression opt(QStringLiteral(R"(\{opt:([^{}]*)\})"));
            QString out = text;
            for (auto it = opt.globalMatch(text); it.hasNext();)
            {
                const QRegularExpressionMatch match = it.next();
                const QString key = match.captured(1).section(QLatin1Char('|'), 0, 0);
                out.replace(match.captured(0), choiceText(mod, options, key, level).toLower());
            }
            return out.trimmed();
        }

        // What an ability's text holds in braces, worked out (see
        // Modifier::Ability).
        struct TextContext
        {
            const Modifier *mod = nullptr;
            const Monster *monster = nullptr; // for {statdc:...}
            QJsonObject options;
            int level = 0;
            QString callIt, capital;
            int halfHp = 0;
            int abilityDc = 0;
        };

        // Its melee Strike with the highest attack, if any.
        const Strike *bestMelee(const Monster &m)
        {
            const Strike *best = nullptr;
            for (const Strike &s : m.strikes)
            {
                if (isMelee(s) &&
                    (!best || s.attack.number().value_or(-99) > best->attack.number().value_or(-99)))
                {
                    best = &s;
                }
            }
            return best;
        }

        // Its Strike of that name, else its best melee Strike.
        const Strike *namedStrike(const Monster &m, const QString &name)
        {
            for (const Strike &s : m.strikes)
            {
                if (s.name.trimmed().compare(name, Qt::CaseInsensitive) == 0)
                {
                    return &s;
                }
            }
            return bestMelee(m);
        }

        // The highest DC it lists (its abilities' text, its spell DCs), else
        // the spell DC table's `benchmark` for its level (Divine Warden:
        // "if the creature didn't have any listed DCs, use the moderate
        // DC").
        int ownDc(const Monster &m, int level, const QString &benchmark)
        {
            std::optional<int> best;
            auto consider = [&](std::optional<int> dc) {
                if (dc && (!best || *dc > *best))
                {
                    best = dc;
                }
            };
            for (const Special &s : m.specials)
            {
                if (s.placeholder)
                {
                    continue;
                }
                static const QRegularExpression dc(QStringLiteral(R"(\bDC (\d+)\b(?!\s+flat))"));
                for (auto it = dc.globalMatch(s.description); it.hasNext();)
                {
                    consider(it.next().captured(1).toInt());
                }
            }
            if (const Stat *spellDc = m.findStat(QStringLiteral("spelldc")))
            {
                consider(spellDc->value.number());
            }
            for (const SpellList &list : m.morespells)
            {
                consider(list.spelldc.value.number());
            }
            return best.value_or(hint(Table::SpellDc, level, benchmark).value.value_or(0));
        }

        // The "tradition" choice, else its tradition trait, else one its
        // abilities have (a dragon's Breath Weapon), else nothing.
        QString traditionOf(const Monster &m, const QJsonObject &options)
        {
            const QString chosenOne = options.value(QStringLiteral("tradition")).toString();
            if (!chosenOne.isEmpty() && chosenOne != QLatin1String("auto"))
            {
                return chosenOne;
            }
            auto find = [](const QString &traits) {
                for (const QString &trait : splitList(traits).first)
                {
                    const QString t = trait.trimmed().toLower();
                    if (t == QLatin1String("arcane") || t == QLatin1String("divine") ||
                        t == QLatin1String("occult") || t == QLatin1String("primal"))
                    {
                        return t;
                    }
                }
                return QString();
            };
            if (const QString own = find(m.traits); !own.isEmpty())
            {
                return own;
            }
            for (const Special &s : m.specials)
            {
                if (const QString t = find(s.traits); !s.placeholder && !t.isEmpty())
                {
                    return t;
                }
            }
            return QString();
        }

        // "death, , primal" -> "death, primal".
        QString withoutEmptyItems(const QString &list)
        {
            QStringList items;
            for (const QString &item : list.split(QLatin1Char(',')))
            {
                if (!item.trimmed().isEmpty())
                {
                    items << item.trimmed();
                }
            }
            return items.join(QStringLiteral(", "));
        }

        QString expandText(const QString &text, const TextContext &c)
        {
            static const QRegularExpression brace(QStringLiteral(R"(\{([^{}]*)\})"));
            QString out;
            qsizetype at = 0;
            for (auto it = brace.globalMatch(text); it.hasNext();)
            {
                const QRegularExpressionMatch match = it.next();
                out += QStringView(text).mid(at, match.capturedStart() - at);
                at = match.capturedEnd();
                const QString inner = match.captured(1);
                QString made = match.captured(0); // unknown: kept as written
                if (inner == QLatin1String("name"))
                {
                    made = c.callIt;
                }
                else if (inner == QLatin1String("Name"))
                {
                    made = c.capital;
                }
                else if (inner == QLatin1String("halfHp"))
                {
                    made = QString::number(c.halfHp);
                }
                else if (inner == QLatin1String("level"))
                {
                    made = QString::number(c.level);
                }
                else if (inner == QLatin1String("abilitydc"))
                {
                    made = QString::number(c.abilityDc);
                }
                else if (inner.startsWith(QLatin1Char('=')))
                {
                    if (const std::optional<int> v = Expression(inner.mid(1), c.level, c.monster).value())
                    {
                        made = QString::number(*v);
                    }
                }
                else if (inner.startsWith(QLatin1String("dc:")))
                {
                    made = QString::number(levelDc(c.level, inner.mid(3)));
                }
                else if (inner.startsWith(QLatin1String("spelldc:")))
                {
                    if (const Hint h = hint(Table::SpellDc, c.level, inner.mid(8)); h.valid && h.value)
                    {
                        made = QString::number(*h.value);
                    }
                }
                else if (inner.startsWith(QLatin1String("skill:")))
                {
                    if (const Hint h = hint(Table::Skill, c.level, inner.mid(6)); h.valid && h.value)
                    {
                        made = QStringLiteral("+") + QString::number(*h.value);
                    }
                }
                else if (inner.startsWith(QLatin1String("spellattack:")))
                {
                    if (const Hint h = hint(Table::SpellAttack, c.level, inner.mid(12)); h.valid && h.value)
                    {
                        made = QStringLiteral("+") + QString::number(*h.value);
                    }
                }
                else if (inner.startsWith(QLatin1String("area:")))
                {
                    // "11d6 (39)": the dice.
                    made = hint(Table::AreaDamage, c.level, inner.mid(5)).text.section(QLatin1Char(' '), 0, 0);
                }
                else if (inner.startsWith(QLatin1String("strikeattack:")) && c.monster)
                {
                    // Its Strike of that name's attack, else its best melee
                    // Strike's ("best").
                    if (const Strike *strike = namedStrike(*c.monster, inner.mid(13)))
                    {
                        made = withSign(strike->attack.text().trimmed());
                    }
                }
                else if (inner.startsWith(QLatin1String("owndc:")) && c.monster)
                {
                    made = QString::number(ownDc(*c.monster, c.level, inner.mid(6)));
                }
                else if (inner == QLatin1String("tradition") && c.monster)
                {
                    made = traditionOf(*c.monster, c.options);
                }
                else if (inner.startsWith(QLatin1String("strikedice:")) && c.monster)
                {
                    // The dice of its Strike of that name, else of its best
                    // melee Strike ("1d8+9").
                    static const QRegularExpression roll(
                        QStringLiteral(R"(\d+d\d+(?:\s*[+-]\s*\d+(?!\s*d\d))?)"));
                    if (const Strike *source = namedStrike(*c.monster, inner.mid(11)))
                    {
                        made = roll.match(source->damage).captured().remove(QLatin1Char(' '));
                    }
                }
                else if (inner.startsWith(QLatin1String("statdc:")) && c.monster)
                {
                    // A save's or skill's DC: 10 + its modifier.
                    if (const Stat *stat = c.monster->findStat(inner.mid(7)))
                    {
                        if (const std::optional<int> n = stat->value.number())
                        {
                            made = QString::number(10 + *n);
                        }
                    }
                }
                else if (inner.startsWith(QLatin1String("opt:")) && c.mod)
                {
                    // "{opt:energy|lower}": in lower case.
                    const QString key = inner.mid(4).section(QLatin1Char('|'), 0, 0);
                    made = choiceText(*c.mod, c.options, key, c.level);
                    if (inner.endsWith(QLatin1String("|lower")))
                    {
                        made = made.toLower();
                    }
                    else if (inner.endsWith(QLatin1String("|cap")) && !made.isEmpty())
                    {
                        made[0] = made[0].toUpper();
                    }
                }
                else if (inner.startsWith(QLatin1String("steps|")))
                {
                    // "steps|first|5=at 5th|15=at 15th": the last one reached.
                    const QStringList parts = inner.mid(6).split(QLatin1Char('|'));
                    made = parts.value(0);
                    for (qsizetype i = 1; i < parts.size(); ++i)
                    {
                        const qsizetype eq = parts[i].indexOf(QLatin1Char('='));
                        bool ok = false;
                        const int from = parts[i].left(eq).toInt(&ok);
                        if (eq > 0 && ok && c.level >= from)
                        {
                            made = parts[i].mid(eq + 1);
                        }
                    }
                }
                out += made;
            }
            out += QStringView(text).mid(at);
            // A step that gave nothing leaves no doubled spaces behind, and
            // an empty choice no "()".
            static const QRegularExpression spaces(QStringLiteral(R"(  +)"));
            static const QRegularExpression emptyParens(QStringLiteral(R"(\s*\(\s*\))"));
            out.remove(emptyParens);
            return out.replace(spaces, QStringLiteral(" ")).trimmed();
        }

        // "plus 2d6 void" onto damage that has "plus 3d6 void" already:
        // "plus 5d6 void" (same die and type). False if it has no such
        // rider.
        bool addToRider(QString &damage, const QString &rider)
        {
            static const QRegularExpression parts(QStringLiteral(R"(^plus (\d+)d(\d+) ([a-z]+)$)"),
                                                  QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch want = parts.match(rider.trimmed());
            if (!want.hasMatch())
            {
                return false;
            }
            const QRegularExpression existing(
                QStringLiteral(R"(\bplus (\d+)d%1 %2\b)").arg(want.captured(2), QRegularExpression::escape(want.captured(3))),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch found = existing.match(damage);
            if (!found.hasMatch())
            {
                return false;
            }
            damage.replace(found.capturedStart(1), found.capturedLength(1),
                           QString::number(found.captured(1).toInt() + want.captured(1).toInt()));
            return true;
        }

        // A rider ("plus serpentfolk venom") onto a Strike's damage, joined
        // to one it has as Paizo writes them: "plus Grab and serpentfolk
        // venom", "plus 1d6 fire, Grab, and serpentfolk venom". Nothing if
        // it has it already.
        void joinRider(QString &damage, const QString &rider)
        {
            const QString text = rider.trimmed();
            if (text.isEmpty() || addToRider(damage, text))
            {
                return;
            }
            const bool plus = text.startsWith(QLatin1String("plus "), Qt::CaseInsensitive);
            const QString what = plus ? text.mid(5).trimmed() : text;
            if (damage.contains(what, Qt::CaseInsensitive))
            {
                return;
            }
            static const QRegularExpression hasPlus(QStringLiteral(R"(\bplus\s)"), QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch found = hasPlus.match(damage);
            QString out = damage.trimmed();
            if (!plus || !found.hasMatch())
            {
                damage = out + QLatin1Char(' ') + text;
                return;
            }
            // "plus A and B" -> "plus A, B, and C".
            const qsizetype lastAnd = out.lastIndexOf(QLatin1String(" and "), -1, Qt::CaseInsensitive);
            if (lastAnd > found.capturedStart())
            {
                out.replace(lastAnd, 5, QStringLiteral(", "));
                damage = out + QStringLiteral(", and ") + what;
            }
            else
            {
                damage = out + QStringLiteral(" and ") + what;
            }
        }

        // "telepathy 100 feet" after the languages' semicolon ("Aklo,
        // Common; telepathy 100 feet"): added, or its amount raised if it
        // has a smaller one. A line of only such things ("telepathy 30
        // feet") counts them as after the semicolon.
        void addLanguageExtra(QString &languages, const QString &extra)
        {
            const QString wanted = extra.trimmed();
            if (wanted.isEmpty())
            {
                return;
            }
            const QString word = wanted.section(QLatin1Char(' '), 0, 0);
            const qsizetype semi = languages.indexOf(QLatin1Char(';'));
            QStringList head, extras;
            for (const QString &item : (semi < 0 ? languages : languages.left(semi)).split(QLatin1Char(',')))
            {
                if (!item.trimmed().isEmpty())
                {
                    head << item.trimmed();
                }
            }
            if (semi >= 0)
            {
                for (const QString &item : languages.mid(semi + 1).split(QLatin1Char(',')))
                {
                    if (!item.trimmed().isEmpty())
                    {
                        extras << item.trimmed();
                    }
                }
            }
            if (const qsizetype at = findItem(head, word); at >= 0)
            {
                extras << head.takeAt(at);
            }
            if (const qsizetype at = findItem(extras, word); at >= 0)
            {
                static const QRegularExpression number(QStringLiteral(R"(\d+)"));
                const QRegularExpressionMatch has = number.match(extras[at]);
                const std::optional<int> amount = firstNumber(wanted);
                if (has.hasMatch() && amount && has.captured().toInt() < *amount)
                {
                    extras[at].replace(has.capturedStart(), has.capturedLength(), QString::number(*amount));
                }
            }
            else
            {
                extras << wanted;
            }
            const QString list = head.join(QStringLiteral(", "));
            languages = extras.isEmpty() ? list
                        : list.isEmpty() ? extras.join(QStringLiteral(", "))
                                         : list + QStringLiteral("; ") + extras.join(QStringLiteral(", "));
        }

        // The type of a Strike's damage: the word after its first roll
        // ("1d6+5 piercing plus venom" -> "piercing"), or empty.
        QString damageTypeOf(const QString &damage)
        {
            static const QRegularExpression type(QStringLiteral(R"(\d+d\d+(?:\s*[+-]\s*\d+(?!\s*d\d))?\s+([A-Za-z]+))"));
            return type.match(damage).captured(1).toLower();
        }

        // Its abilities that `edit` matches, changed (see
        // Modifier::EditAbility).
        void editAbilities(Monster &m, const Modifier::EditAbility &edit, const TextContext &context)
        {
            // Its damage: "8d8 cold damage", and any joined to it ("and 8d8
            // void damage", "plus 2d6 fire damage").
            static const QRegularExpression damage(QStringLiteral(
                R"(\b\d+d\d+(?:\s*[+-]\s*\d+)?\s+[a-z]+\s+damage\b)"
                R"((?:,?\s+(?:and|plus)\s+\d+d\d+(?:\s*[+-]\s*\d+)?\s+(?:persistent\s+)?[a-z]+\s+damage\b)*)"));
            for (Special &s : m.specials)
            {
                if (s.placeholder || !s.name.contains(edit.match, Qt::CaseInsensitive))
                {
                    continue;
                }
                if (!edit.rename.isEmpty() && !s.name.contains(edit.rename, Qt::CaseInsensitive))
                {
                    // Its old name in any text that names it ("can't use
                    // Frost Breath again", "recharges its Frost Breath").
                    const QString old = s.name.trimmed();
                    const QRegularExpression named(QStringLiteral(R"(\b%1\b)").arg(QRegularExpression::escape(old)));
                    for (Special &other : m.specials)
                    {
                        other.description.replace(named, edit.rename);
                    }
                    s.name = edit.rename;
                }
                if (!edit.addDamage.isEmpty() && !s.description.contains(edit.addDamage, Qt::CaseInsensitive))
                {
                    if (const QRegularExpressionMatch match = damage.match(s.description); match.hasMatch())
                    {
                        s.description.insert(match.capturedEnd(), QStringLiteral(" plus ") + edit.addDamage);
                    }
                    else
                    {
                        s.description = s.description.trimmed() +
                                        QStringLiteral(" It deals an additional %1.").arg(edit.addDamage);
                    }
                }
                const QString append = expandText(edit.append, context);
                if (!append.isEmpty() && !s.description.contains(append))
                {
                    s.description = s.description.trimmed() + QLatin1Char(' ') + append;
                }
                for (const QString &trait : withoutEmptyItems(expandText(edit.traits, context)).split(
                         QStringLiteral(", "), Qt::SkipEmptyParts))
                {
                    if (findItem(splitList(s.traits).first, trait) < 0)
                    {
                        addMissing(s.traits, trait);
                    }
                }
            }
        }

        // The highest DC in its abilities' text (flat checks aside), if any.
        std::optional<int> highestAbilityDc(const Monster &m)
        {
            static const QRegularExpression dc(QStringLiteral(R"(\bDC (\d+)\b(?!\s+flat))"));
            std::optional<int> highest;
            for (const Special &s : m.specials)
            {
                if (s.placeholder)
                {
                    continue;
                }
                for (auto it = dc.globalMatch(s.description); it.hasNext();)
                {
                    const int n = it.next().captured(1).toInt();
                    if (!highest || n > *highest)
                    {
                        highest = n;
                    }
                }
            }
            return highest;
        }

        // Skills raised to a benchmark of the level (see
        // Modifier::skillBenchmarks).
        void raiseSkills(Monster &m, const Modifier &mod, const QJsonObject &options, int level)
        {
            for (const auto &[skill, wanted] : mod.skillBenchmarks)
            {
                QString benchmark = wanted;
                if (wanted == QLatin1String("primary"))
                {
                    benchmark = options.value(QStringLiteral("primaryExtreme")).toBool() && level >= 11
                                    ? QStringLiteral("extreme")
                                    : QStringLiteral("high");
                }
                else if (wanted == QLatin1String("secondary"))
                {
                    benchmark = QStringLiteral("moderate");
                }
                const Hint h = hint(Table::Skill, level, benchmark);
                if (!h.valid || !h.value)
                {
                    continue;
                }
                Stat &stat = m.stat(skill);
                if (stat.value.number().value_or(*h.value - 1) < *h.value)
                {
                    stat.value = Value(*h.value);
                }
            }
        }

        // Its highest skill modifier (Lore included), if it has skills.
        std::optional<int> highestSkill(const Monster &m)
        {
            std::optional<int> highest;
            QStringList keys;
            for (const char *skill : kSkills)
            {
                keys << QString::fromLatin1(skill);
            }
            keys += m.loreKeys();
            for (const QString &key : keys)
            {
                if (const Stat *stat = m.findStat(key))
                {
                    if (const std::optional<int> n = stat->value.number(); n && (!highest || *n > *highest))
                    {
                        highest = n;
                    }
                }
            }
            return highest;
        }

        // A trait swapped for another, or the other added (see
        // Modifier::replaceOrAdd).
        void replaceOrAddTrait(Monster &m, const QString &from, const QString &to)
        {
            if (m.type.trimmed().compare(from, Qt::CaseInsensitive) == 0)
            {
                m.type = to;
                return;
            }
            auto [items, rest] = splitList(m.traits);
            for (qsizetype i = 0; i < items.size(); ++i)
            {
                if (items[i].compare(from, Qt::CaseInsensitive) == 0)
                {
                    items.removeAt(i);
                    break;
                }
            }
            m.traits = joinList(items, rest);
            if (findItem(items, to) < 0 && m.type.trimmed().compare(to, Qt::CaseInsensitive) != 0)
            {
                addMissing(m.traits, to);
                m.addedTraits << to;
            }
        }

        // Every ancestry trait it has (its creature type too) becomes `to`,
        // or `to` is added if it has none (Modifier::replaceOrAdd's "any
        // ancestry").
        void replaceAncestry(Monster &m, const QString &to)
        {
            const QSet<QString> ancestries = ancestryTraits();
            const QString key = to.trimmed().toLower();
            bool has = m.type.trimmed().compare(to, Qt::CaseInsensitive) == 0;
            if (!has && ancestries.contains(m.type.trimmed().toLower()))
            {
                m.type = to;
                has = true;
            }
            auto [items, rest] = splitList(m.traits);
            items.removeIf([&](const QString &t) {
                const QString trait = t.trimmed().toLower();
                return trait != key && ancestries.contains(trait);
            });
            m.traits = joinList(items, rest);
            if (!has && findItem(items, to) < 0)
            {
                addMissing(m.traits, to);
                m.addedTraits << to;
            }
        }

        // Sets, lowers or raises its land Speed (the Speed item that starts
        // with a number); added if it has none and one is set or raised.
        void changeLandSpeed(Monster &m, const Modifier &mod)
        {
            if (!mod.landSet && !mod.landAdd && !mod.landMax && !mod.landMin)
            {
                return;
            }
            auto [items, rest] = splitList(m.speed);
            const qsizetype at = landItem(items);
            std::optional<int> feet = at < 0 ? std::nullopt : firstNumber(items[at]);
            std::optional<int> wanted = feet;
            if (mod.landSet)
            {
                wanted = mod.landSet;
            }
            if (mod.landAdd && wanted)
            {
                *wanted += *mod.landAdd;
            }
            if (mod.landMax && wanted && *wanted > *mod.landMax)
            {
                wanted = mod.landMax;
            }
            if (mod.landMin && (!wanted || *wanted < *mod.landMin))
            {
                wanted = mod.landMin;
            }
            if (!wanted || wanted == feet)
            {
                return;
            }
            if (at < 0)
            {
                items.prepend(QStringLiteral("%1 feet").arg(*wanted));
            }
            else
            {
                static const QRegularExpression number(QStringLiteral(R"(\d+)"));
                const QRegularExpressionMatch match = number.match(items[at]);
                items[at].replace(match.capturedStart(), match.capturedLength(), QString::number(*wanted));
            }
            m.speed = joinList(items, rest);
        }

        // A skill's value for a benchmark at the level, if the table has it.
        std::optional<int> skillValue(int level, const QString &benchmark)
        {
            const Hint h = hint(Table::Skill, level, benchmark);
            return h.valid ? h.value : std::nullopt;
        }

        void raiseTo(Stat &stat, std::optional<int> value)
        {
            if (value && stat.value.number().value_or(*value - 1) < *value)
            {
                stat.value = Value(*value);
            }
        }

        // NPC Core's Lore skills (see Modifier::lores); `name` with its
        // choices filled in.
        void addLore(Monster &m, const QString &name, const QString &benchmark, int level)
        {
            if (name.trimmed().isEmpty())
            {
                return;
            }
            const std::optional<int> value =
                benchmark == QLatin1String("highest") ? highestSkill(m) : skillValue(level, benchmark);
            QString key;
            for (const QString &k : m.loreKeys())
            {
                if (m.stat(k).name.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                {
                    key = k;
                    break;
                }
            }
            if (key.isEmpty())
            {
                for (const QString &k : m.loreKeys())
                {
                    if (m.stat(k).value.isEmpty())
                    {
                        key = k;
                        break;
                    }
                }
            }
            if (key.isEmpty())
            {
                key = m.addLore();
            }
            Stat &stat = m.stat(key);
            stat.name = name.trimmed();
            raiseTo(stat, value);
        }

        // {+4} in a statistic's note: its modifier plus 4 ("+16 to hide
        // its identity"), once every adjustment is applied.
        void fillNoteBonuses(Monster &m)
        {
            static const QRegularExpression plus(QStringLiteral(R"(\{\+(\d+)\})"));
            for (auto &[key, stat] : m.stats)
            {
                if (!stat.note.contains(QLatin1String("{+")))
                {
                    continue;
                }
                const int base = stat.value.number().value_or(0);
                QString out;
                qsizetype at = 0;
                for (auto it = plus.globalMatch(stat.note); it.hasNext();)
                {
                    const QRegularExpressionMatch match = it.next();
                    out += QStringView(stat.note).mid(at, match.capturedStart() - at);
                    const int n = base + match.captured(1).toInt();
                    out += (n >= 0 ? QStringLiteral("+") : QString()) + QString::number(n);
                    at = match.capturedEnd();
                }
                out += QStringView(stat.note).mid(at);
                stat.note = out;
            }
        }

        // Mutant's marrowlance: the attack and the first roll of the damage of
        // its best Strike of that type, else the benchmarks for its level.
        Strike makeStrike(const Monster &m, const Modifier::MadeStrike &made, int level)
        {
            Strike strike;
            strike.type = made.type;
            strike.name = made.name;
            strike.traits = made.traits;
            static const QRegularExpression roll(QStringLiteral(R"(\d+d\d+(?:\s*[+-]\s*\d+(?!\s*d\d))?)"));
            // The average of a Strike's first roll ("2d8+14" -> 23).
            auto average = [](const QString &damage) {
                static const QRegularExpression parts(QStringLiteral(R"((\d+)d(\d+)(?:\s*([+-])\s*(\d+)(?!\s*d\d))?)"));
                const QRegularExpressionMatch match = parts.match(damage);
                if (!match.hasMatch())
                {
                    return 0.0;
                }
                const int modifier = match.captured(4).toInt() * (match.captured(3) == QLatin1String("-") ? -1 : 1);
                return match.captured(1).toInt() * (match.captured(2).toInt() + 1) / 2.0 + modifier;
            };
            auto agile = [](const Strike &s) {
                return findItem(splitList(s.traits).first, QStringLiteral("agile")) >= 0;
            };
            // Its Strike of that type that is best (highest attack) or
            // lowest (least damage), among those `keep` allows.
            auto pick = [&](bool lowest, const std::function<bool(const Strike &)> &keep) -> const Strike * {
                const Strike *found = nullptr;
                for (const Strike &s : m.strikes)
                {
                    if (s.type.compare(made.type, Qt::CaseInsensitive) != 0 || !roll.match(s.damage).hasMatch() ||
                        !keep(s))
                    {
                        continue;
                    }
                    if (!found ||
                        (lowest ? average(s.damage) < average(found->damage)
                                : s.attack.number().value_or(-99) > found->attack.number().value_or(-99)))
                    {
                        found = &s;
                    }
                }
                return found;
            };
            auto any = [](const Strike &) { return true; };
            const Strike *source = nullptr;
            QString dice;
            // Its best Strike's attack, and the damage table's entry for its
            // level nearest that Strike's average damage times `scale`.
            auto scaled = [&](double scale) {
                if (const Strike *best = pick(false, any))
                {
                    strike.attack = best->attack;
                    const double wanted = average(best->damage) * scale;
                    double nearest = -1;
                    for (const QString &benchmark : benchmarks(Table::StrikeDamage))
                    {
                        const Hint h = hint(Table::StrikeDamage, level, benchmark);
                        const double avg = average(h.text);
                        if (h.valid && (nearest < 0 || std::abs(avg - wanted) < std::abs(nearest - wanted)))
                        {
                            nearest = avg;
                            dice = roll.match(h.text).captured();
                        }
                    }
                }
            };
            if (made.from == QLatin1String("agile"))
            {
                // Its agile Strike's damage, else three-quarters of its best
                // Strike's.
                source = pick(false, agile);
                if (!source)
                {
                    scaled(0.75);
                }
            }
            else if (made.from == QLatin1String("nonagile"))
            {
                source = pick(false, [&](const Strike &s) { return !agile(s); });
                if (!source && made.fallbackScale > 0)
                {
                    scaled(made.fallbackScale);
                }
            }
            else if (made.from == QLatin1String("best") || made.from == QLatin1String("lowest"))
            {
                source = pick(made.from == QLatin1String("lowest"), any);
            }
            if (source)
            {
                strike.attack = source->attack;
                dice = roll.match(source->damage).captured();
            }
            if (made.attackBenchmark == QLatin1String("best"))
            {
                const Strike *best = pick(false, any);
                strike.attack = best ? best->attack
                                     : Value(hint(Table::StrikeAttack, level, QStringLiteral("moderate")).value.value_or(0));
            }
            else if (strike.attack.isEmpty())
            {
                strike.attack = Value(hint(Table::StrikeAttack, level, made.attackBenchmark).value.value_or(0));
            }
            if (!made.damageText.isEmpty())
            {
                // Written out: "{=max(1, level / 3)}d8 void".
                static const QRegularExpression formula(QStringLiteral(R"(\{=([^{}]*)\})"));
                QString text = made.damageText;
                for (auto it = formula.globalMatch(made.damageText); it.hasNext();)
                {
                    const QRegularExpressionMatch match = it.next();
                    if (const std::optional<int> v = Expression(match.captured(1), level).value())
                    {
                        text.replace(match.captured(0), QString::number(*v));
                    }
                }
                strike.damage = text;
            }
            else
            {
                if (dice.isEmpty())
                {
                    // "2d6+6 (13)": the dice without the average.
                    dice = roll.match(hint(Table::StrikeDamage, level, made.damageBenchmark).text).captured();
                }
                if (!made.die.isEmpty())
                {
                    // "2d4+9" with die "d6": "2d6+9".
                    static const QRegularExpression size(QStringLiteral(R"((?<=\d)d\d+)"));
                    const QString die = made.die.startsWith(QLatin1Char('d'), Qt::CaseInsensitive)
                                            ? made.die.toLower()
                                            : QStringLiteral("d") + made.die;
                    if (const QRegularExpressionMatch match = size.match(dice); match.hasMatch())
                    {
                        dice.replace(match.capturedStart(), match.capturedLength(), die);
                    }
                }
                strike.damage = made.damageType.isEmpty() ? dice : dice + QLatin1Char(' ') + made.damageType;
            }
            if (!made.extra.isEmpty())
            {
                strike.damage += QLatin1Char(' ') + made.extra;
            }
            if (made.reachPlus > 0)
            {
                setStrikeReach(strike, reachOf(Strike(), m.size) + made.reachPlus);
            }
            return strike;
        }

        // Puts an ability an adjustment makes in the place the user gave
        // it, else at the end.
        void placeGenerated(Monster &m, const Special &made)
        {
            const auto place = std::find_if(m.specials.begin(), m.specials.end(), [&](const Special &s) {
                return s.placeholder && s.generated == made.generated;
            });
            if (place != m.specials.end())
            {
                *place = made;
            }
            else
            {
                m.specials.push_back(made);
            }
        }

        // A typed statistic's amount: its formula at the new level, else its
        // step for the level before.
        int typedAmount(const Modifier::Typed &t, int before, int level)
        {
            if (!t.expr.isEmpty())
            {
                return Expression(t.expr, level).value().value_or(0);
            }
            return stepFor(t.steps, before);
        }

        // The traits of a life as a living creature (Monster Core's "such as
        // human and humanoid"): those and the ancestries, but not skeleton
        // (an ancestry, and the trait of undead skeletons).
        QSet<QString> livingTraits()
        {
            QSet<QString> set = ancestryTraits();
            set.remove(QStringLiteral("skeleton"));
            set.insert(QStringLiteral("humanoid"));
            return set;
        }

        // Adds an innate spell to the spell list of its name (the main one
        // if that's what it's called), made with the DC from the spell DC
        // table if it has none (see Modifier::InnateSpell).
        // A made spell list's DC: "high" (the spell DC table at its level)
        // or "own:moderate" (the highest DC it lists, else the table's).
        int listDc(const Monster &m, const QString &dc, int level)
        {
            if (dc.startsWith(QLatin1String("own:")))
            {
                return ownDc(m, level, dc.mid(4));
            }
            return hint(Table::SpellDc, level, dc).value.value_or(0);
        }

        void addInnateSpell(Monster &m, const Modifier::InnateSpell &spell, int level)
        {
            // Index 0 is 10th rank, 9 is 1st, 10 the cantrips.
            const int index = spell.rank == QLatin1String("cantrip")
                                  ? kCantrips
                                  : 10 - std::clamp(Expression(spell.rank, level).value().value_or(1), 1, 10);
            QString *ranks = nullptr;
            if (m.spelltype.trimmed().compare(spell.list, Qt::CaseInsensitive) == 0)
            {
                ranks = m.spells.data();
            }
            for (SpellList &list : m.morespells)
            {
                if (!ranks && list.name.trimmed().compare(spell.list, Qt::CaseInsensitive) == 0)
                {
                    ranks = list.spells.data();
                }
            }
            if (!ranks)
            {
                SpellList list;
                list.name = spell.list;
                list.spelldc.benchmark = spell.dc.section(QLatin1Char(':'), -1);
                const int dc = listDc(m, spell.dc, level);
                list.spelldc.value = Value(dc);
                if (spell.attack)
                {
                    list.spellattack.value = Value(dc - 8);
                }
                // Its cantrips at its new level's rank (the statblock would
                // use the level the benchmarks are built from).
                list.cantriplevel = Value(std::clamp((level + 1) / 2, 1, 10));
                m.morespells.push_back(list);
                ranks = m.morespells.back().spells.data();
            }
            QString &at = ranks[index];
            if (!at.contains(spell.spell, Qt::CaseInsensitive))
            {
                at = at.trimmed().isEmpty() ? spell.spell : at.trimmed() + QStringLiteral(", ") + spell.spell;
            }
        }

        // Its land Speed becomes a fly Speed of that amount (or raises the
        // one it has): "25 feet, climb 10 feet" -> "fly 25 feet, climb 10
        // feet".
        void landToFly(Monster &m)
        {
            auto [items, rest] = splitList(m.speed);
            const qsizetype at = landItem(items);
            if (at < 0)
            {
                return;
            }
            const std::optional<int> feet = firstNumber(items[at]);
            items.removeAt(at);
            m.speed = joinList(items, rest);
            if (feet)
            {
                raiseItem(m.speed, QStringLiteral("fly"), *feet, QStringLiteral(" feet"));
                // First, where the land Speed was.
                auto [now, tail] = splitList(m.speed);
                const qsizetype fly = findItem(now, QStringLiteral("fly"));
                if (fly > 0)
                {
                    now.move(fly, 0);
                }
                m.speed = joinList(now, tail);
            }
        }

        // Its best melee Strike made into another (Ghost's ghostly hand).
        void convertStrike(Monster &m, const Modifier::ConvertStrike &c)
        {
            Strike *best = nullptr;
            for (Strike &s : m.strikes)
            {
                if (isMelee(s) && (!best || s.attack.number().value_or(-99) > best->attack.number().value_or(-99)))
                {
                    best = &s;
                }
            }
            if (!best)
            {
                return;
            }
            best->name = c.name;
            best->attack = add(best->attack, c.attack);
            best->traits = c.traits;
            if (!c.damageType.isEmpty())
            {
                // "2d8+14 slashing plus Grab" -> "2d8+14 void plus Grab".
                static const QRegularExpression type(
                    QStringLiteral(R"(^(\s*\d+d\d+(?:\s*[+-]\s*\d+(?!\s*d\d))?)(\s+[A-Za-z]+)?)"));
                const QRegularExpressionMatch match = type.match(best->damage);
                if (match.hasMatch())
                {
                    best->damage.replace(match.capturedStart(), match.capturedLength(),
                                         match.captured(1) + QLatin1Char(' ') + c.damageType);
                }
            }
        }

        // Domain spells for the chosen domains (see Modifier::DomainSpells).
        void addDomainSpells(Monster &m, const Modifier::DomainSpells &d, const QJsonObject &options, int level)
        {
            QStringList spells;
            for (const QString &key : d.choices)
            {
                const QString chosenOne = options.value(key).toString();
                for (const Domain &domain : domains())
                {
                    if (!chosenOne.isEmpty() && domain.key == chosenOne)
                    {
                        spells << domain.spell;
                        if (level >= d.advancedLevel)
                        {
                            spells << domain.advanced;
                        }
                    }
                }
            }
            spells.removeDuplicates();
            if (spells.isEmpty())
            {
                return;
            }
            SpellList list;
            list.name = d.list;
            list.spelldc.benchmark = d.dc.section(QLatin1Char(':'), -1);
            list.spelldc.value = Value(listDc(m, d.dc, level));
            list.focuspoints = Value(stepFor(d.points, level));
            const int rank = std::clamp((level + 1) / 2, 1, 10);
            list.spells[static_cast<size_t>(10 - rank)] = spells.join(QStringLiteral(", "));
            m.morespells.push_back(list);
        }

        // The choices made, less those offered only from a level it hasn't
        // reached (hidden in the dialog; kept there in case it does).
        QJsonObject reachableOptions(const Modifier &mod, QJsonObject options, int level)
        {
            for (const Modifier::Choice &c : mod.choices)
            {
                if (c.type == QLatin1String("check") && c.minLevel && level < *c.minLevel)
                {
                    options.insert(c.key, false);
                }
                else if (c.type == QLatin1String("pick"))
                {
                    QJsonArray picked;
                    for (const QJsonValue &v : options.value(c.key).toArray())
                    {
                        const auto item = std::find_if(c.items.begin(), c.items.end(), [&](const auto &i) {
                            return i.key == v.toString();
                        });
                        if (item == c.items.end() || !item->minLevel || level >= *item->minLevel)
                        {
                            picked.append(v);
                        }
                    }
                    options.insert(c.key, picked);
                }
            }
            return options;
        }

        bool appliesAt(const Modifier &mod, int level)
        {
            return (!mod.minLevel || level >= *mod.minLevel) &&
                   (!mod.maxLevel || level <= *mod.maxLevel);
        }

        // `name`: the base monster's, without the prefixes.
        // `owner`: for the changes of a choice, the adjustment whose choices
        // they are (for {opt:...}).
        void apply(Monster &m, const Modifier &mod, const QJsonObject &stored, int &level,
                   const QString &name, const Modifier *owner = nullptr)
        {
            const Modifier &choicesOf = owner ? *owner : mod;
            QJsonObject options =
                mod.dialog == QLatin1String("options") ? optionsWithDefaults(mod, stored) : stored;
            if (mod.dialog == QLatin1String("mythic"))
            {
                applyMythic(m, options, level, name);
            }
            const int before = level;
            level += stepFor(mod.level, before);
            if (mod.dialog == QLatin1String("options"))
            {
                options = reachableOptions(mod, options, level);
            }
            if (mod.bonus != 0)
            {
                const bool dcs = !mod.bonusSkip.contains(QStringLiteral("dcs"));
                for (auto &[key, stat] : m.stats)
                {
                    if (takesBonus(key) && !stat.value.isEmpty() && !mod.bonusSkip.contains(key) &&
                        (dcs || key != QLatin1String("spelldc")))
                    {
                        stat.value = add(stat.value, mod.bonus);
                    }
                }
                for (SpellList &list : m.morespells)
                {
                    for (Stat *stat : {&list.spelldc, &list.spellattack})
                    {
                        if (!stat->value.isEmpty() && (dcs || stat != &list.spelldc))
                        {
                            stat->value = add(stat->value, mod.bonus);
                        }
                    }
                }
                for (Strike &strike : m.strikes)
                {
                    strike.attack = add(strike.attack, mod.bonus);
                }
                if (dcs)
                {
                    for (Special &special : m.specials)
                    {
                        special.description = addTextDcs(special.description, mod.bonus);
                    }
                }
            }
            if (mod.spellBonus != 0)
            {
                for (auto &[key, stat] : m.stats)
                {
                    if ((key == QLatin1String("spelldc") || key == QLatin1String("spellattack")) &&
                        !stat.value.isEmpty())
                    {
                        stat.value = add(stat.value, mod.spellBonus);
                    }
                }
                for (SpellList &list : m.morespells)
                {
                    for (Stat *stat : {&list.spelldc, &list.spellattack})
                    {
                        if (!stat->value.isEmpty())
                        {
                            stat->value = add(stat->value, mod.spellBonus);
                        }
                    }
                }
            }
            for (Strike &strike : m.strikes)
            {
                strike.damage = addDamage(strike.damage, mod.strikeDamage);
            }
            if (const int change = stepFor(mod.hp, before); change != 0)
            {
                Stat &hp = m.stat(QStringLiteral("hp"));
                hp.value = add(hp.value, change);
                if (const std::optional<int> n = hp.value.number(); n && *n < 1)
                {
                    hp.value = add(hp.value, 1 - *n);
                }
            }
            for (const QString &trait : mod.traits)
            {
                if (findItem(splitList(m.traits).first, trait) < 0 &&
                    m.type.trimmed().compare(trait, Qt::CaseInsensitive) != 0)
                {
                    addMissing(m.traits, trait);
                    m.addedTraits << trait;
                }
            }
            if (mod.languagesAlways)
            {
                for (const QString &language : mod.languages)
                {
                    addMissing(m.languages, language);
                }
            }
            else if (!m.languages.trimmed().isEmpty())
            {
                for (const QString &language : mod.languages)
                {
                    // "Common; telepathy 100 feet" has languages; "telepathy
                    // 100 feet" alone has none.
                    if (!splitList(m.languages).first.isEmpty())
                    {
                        addMissing(m.languages, language);
                    }
                }
            }
            for (const QString &extra : mod.languageExtras)
            {
                addLanguageExtra(m.languages, expandOptions(choicesOf, options, extra, level));
            }
            for (const QString &trait : mod.removeTraits)
            {
                auto [items, rest] = splitList(m.traits);
                items.removeIf([&](const QString &t) { return t.compare(trait, Qt::CaseInsensitive) == 0; });
                m.traits = joinList(items, rest);
            }
            if (!mod.rarity.isEmpty())
            {
                setRarity(m, mod.rarity);
            }
            for (const auto &[from, to] : mod.replaceTraits)
            {
                replaceTrait(m, from, to);
            }
            for (const auto &[from, to] : mod.replaceOrAdd)
            {
                if (from.compare(kAnyAncestry, Qt::CaseInsensitive) == 0)
                {
                    replaceAncestry(m, to);
                }
                else
                {
                    replaceOrAddTrait(m, from, to);
                }
            }
            if (mod.removeLiving)
            {
                const QSet<QString> living = livingTraits();
                auto [items, rest] = splitList(m.traits);
                items.removeIf([&](const QString &t) { return living.contains(t.trimmed().toLower()); });
                m.traits = joinList(items, rest);
                if (living.contains(m.type.trimmed().toLower()))
                {
                    m.type = mod.livingType.isEmpty() ? QStringLiteral("-") : mod.livingType;
                    m.addedTraits.removeAll(mod.livingType);
                    // Not also among the traits.
                    auto [left, tail] = splitList(m.traits);
                    left.removeIf([&](const QString &t) { return t.compare(mod.livingType, Qt::CaseInsensitive) == 0; });
                    m.traits = joinList(left, tail);
                }
            }
            for (const QString &ability : mod.hpAbilities)
            {
                if (!m.hpAbilities.contains(ability, Qt::CaseInsensitive))
                {
                    m.hpAbilities << ability;
                }
            }
            for (const Modifier::Typed &t : mod.hpAmounts)
            {
                if (const int amount = typedAmount(t, before, level); amount > 0 && chosen(options, t.when))
                {
                    QString list = m.hpAbilities.join(QStringLiteral(", "));
                    raiseItem(list, t.type, amount, QString());
                    m.hpAbilities = list.split(QStringLiteral(", "), Qt::SkipEmptyParts);
                }
            }
            // In alphabetical order, as Paizo prints them ("coffin
            // restoration, fast healing 7, void healing").
            std::sort(m.hpAbilities.begin(), m.hpAbilities.end(), [](const QString &a, const QString &b) {
                return a.compare(b, Qt::CaseInsensitive) < 0;
            });
            if (!mod.saveNote.isEmpty() && !m.savenote.contains(mod.saveNote))
            {
                m.savenote = m.savenote.trimmed().isEmpty() ? mod.saveNote
                                                            : m.savenote.trimmed() + QStringLiteral("; ") + mod.saveNote;
            }
            for (const QString &listed : mod.items)
            {
                const QString item = expandOptions(choicesOf, options, listed, level);
                if (!item.isEmpty() && !m.items.contains(item, Qt::CaseInsensitive))
                {
                    m.items = m.items.trimmed().isEmpty() ? item : m.items.trimmed() + QStringLiteral(", ") + item;
                }
            }
            // Before the senses it adds: Rumored gives darkvision, or greater
            // darkvision to one that has it.
            for (const auto &[from, to] : mod.senseUpgrades)
            {
                upgradeSense(m, from, to);
            }
            QStringList senses;
            for (const QString &listed : mod.senses)
            {
                // A typed list ("low-light vision, scent (imprecise) 30
                // feet"): each one.
                senses << splitList(expandOptions(choicesOf, options, listed, level)).first;
            }
            for (const QString &sense : senses)
            {
                if (sense.trimmed().isEmpty())
                {
                    continue;
                }
                QString &note = m.stat(QStringLiteral("perception")).note;
                // Darkvision covers low-light vision; greater darkvision,
                // darkvision.
                if (hasSense(note, sense) ||
                    (sense.startsWith(QLatin1String("low-light vision")) &&
                     note.contains(QLatin1String("darkvision"), Qt::CaseInsensitive)))
                {
                    continue;
                }
                addMissing(note, sense);
            }
            for (const auto &[key, least] : mod.attributeMin)
            {
                Stat &stat = m.stat(key);
                if (const std::optional<int> n = stat.value.number(); n && *n < least)
                {
                    stat.value = Value(least);
                }
            }
            for (const auto &[key, value] : mod.attributeSet)
            {
                m.stat(key).value = Value(value);
            }
            skillsToHighest(m, mod.skillsToHighest);
            raiseSkills(m, mod, options, level);
            for (const auto &[key, delta] : mod.statChanges)
            {
                if (m.findStat(key) && !m.stat(key).value.isEmpty())
                {
                    Stat &stat = m.stat(key);
                    stat.value = add(stat.value, delta);
                }
            }
            for (const Modifier::NewSkill &skill : mod.newSkills)
            {
                Stat &stat = m.stat(skill.skill);
                if (stat.value.isEmpty())
                {
                    QString benchmark = skill.benchmark;
                    if (!skill.ifHigh.isEmpty())
                    {
                        const Hint high = hint(Table::Attribute, level, QStringLiteral("high"));
                        if (const std::optional<int> n = m.stat(skill.ifHigh).value.number();
                            n && high.valid && high.value && *n >= *high.value)
                        {
                            benchmark = skill.better;
                        }
                    }
                    raiseTo(stat, skillValue(level, benchmark));
                }
                if (!skill.note.isEmpty() && !stat.note.contains(skill.note))
                {
                    stat.note = stat.note.trimmed().isEmpty() ? skill.note
                                                              : stat.note.trimmed() + QStringLiteral("; ") + skill.note;
                }
            }
            for (const Modifier::Lore &lore : mod.lores)
            {
                QString loreName = lore.name;
                if (loreName.startsWith(QLatin1String("{opt:")) && loreName.endsWith(QLatin1Char('}')))
                {
                    loreName = choiceText(mod, options, loreName.mid(5, loreName.size() - 6), level);
                }
                addLore(m, loreName, lore.benchmark, level);
            }
            for (const Modifier::DerivedSpeed &d : mod.derivedSpeeds)
            {
                if (chosen(options, d.when))
                {
                    deriveSpeed(m, d);
                }
            }
            if (mod.reachLowest && mod.reachOthers)
            {
                setReach(m, *mod.reachLowest, *mod.reachOthers); // at its size before the change
            }
            if (!mod.size.isEmpty())
            {
                m.size = mod.size;
            }
            grow(m, mod.grow, mod.growReach);
            changeLandSpeed(m, mod);
            if (mod.landToFly)
            {
                landToFly(m);
            }
            if ((!mod.recallDifficulty.isEmpty() || !mod.recallSkills.isEmpty()) && chosen(options, mod.recallWhen))
            {
                if (!mod.recallDifficulty.isEmpty())
                {
                    m.recallDifficulty = mod.recallDifficulty;
                }
                for (const QString &skill : mod.recallSkills)
                {
                    if (!m.recallSkills.contains(skill))
                    {
                        m.recallSkills << skill;
                    }
                }
            }
            for (Strike &strike : m.strikes)
            {
                QStringList gained = mod.strikeTraitsAll;
                if (isMelee(strike))
                {
                    gained += mod.strikeTraitsMelee;
                }
                for (const QString &trait : gained)
                {
                    if (findItem(splitList(strike.traits).first, trait) < 0)
                    {
                        addMissing(strike.traits, trait);
                    }
                }
                for (const QString &plus : {mod.strikeDamageAll, isMelee(strike) ? mod.strikeDamageMelee : QString()})
                {
                    if (!plus.isEmpty())
                    {
                        const QString text = expandOptions(choicesOf, options, plus, level);
                        if (!addToRider(strike.damage, text) && !strike.damage.contains(text, Qt::CaseInsensitive))
                        {
                            strike.damage = strike.damage.trimmed() + QLatin1Char(' ') + text;
                        }
                    }
                }
                for (const auto &[type, plus] : mod.strikeDamageByType)
                {
                    if (damageTypeOf(strike.damage) == type.trimmed().toLower())
                    {
                        joinRider(strike.damage, expandOptions(choicesOf, options, plus, level));
                    }
                }
            }
            for (const Modifier::Rename &r : mod.renameStrikes)
            {
                for (Strike &strike : m.strikes)
                {
                    if (strike.name.trimmed().compare(r.from, Qt::CaseInsensitive) != 0)
                    {
                        continue;
                    }
                    strike.name = r.name;
                    if (!r.damageFrom.isEmpty())
                    {
                        const QRegularExpression word(
                            QStringLiteral(R"(\b%1\b)").arg(QRegularExpression::escape(r.damageFrom)),
                            QRegularExpression::CaseInsensitiveOption);
                        strike.damage.replace(word, r.damageTo);
                    }
                }
            }
            // Before its abilities, which name the creature with it.
            const bool prefixed = mod.dialog != QLatin1String("mythic") || mythicPrefixName(name, options);
            // "Were{opt:animal|lower}" -> "Werewolf".
            QString prefix = mod.prefix;
            if (prefix.contains(QLatin1String("{opt:")))
            {
                static const QRegularExpression opt(QStringLiteral(R"(\{opt:([^{}|]*)(?:\|[a-z]*)?\})"));
                for (auto it = opt.globalMatch(mod.prefix); it.hasNext();)
                {
                    const QRegularExpressionMatch match = it.next();
                    prefix.replace(match.captured(0), choiceText(choicesOf, options, match.captured(1), level).toLower());
                }
            }
            if (prefixed && !prefix.isEmpty() &&
                !m.name.startsWith(prefix + QLatin1Char(' '), Qt::CaseInsensitive))
            {
                m.name = prefix + QLatin1Char(' ') + m.name;
            }
            for (const QString &listed : mod.immunities)
            {
                const QString immunity = expandOptions(choicesOf, options, listed, level);
                if (immunity.isEmpty())
                {
                    continue;
                }
                Stat &stat = m.stat(QStringLiteral("immunity"));
                QString list = stat.value.text();
                addMissing(list, immunity);
                stat.value = Value(list);
                // Immunity wins: a resistance or weakness to it goes.
                for (const char *key : {"resistance", "weakness"})
                {
                    Stat &other = m.stat(QString::fromLatin1(key));
                    QString text = other.value.text();
                    removeItem(text, immunity);
                    other.value = Value(text);
                }
            }
            for (const auto &[typed, key, opposite] :
                 {std::tuple{&mod.resistances, "resistance", "weakness"},
                  std::tuple{&mod.weaknesses, "weakness", "resistance"}})
            {
                for (const Modifier::Typed &t : *typed)
                {
                    if (!chosen(options, t.when))
                    {
                        continue;
                    }
                    if (t.plain())
                    {
                        Stat &stat = m.stat(QString::fromLatin1(key));
                        QString list = stat.value.text();
                        addMissing(list, t.suffix.isEmpty() ? t.type : t.type + QLatin1Char(' ') + t.suffix);
                        stat.value = Value(list);
                        continue;
                    }
                    int amount = typedAmount(t, before, level);
                    // Nothing against a type it is immune to.
                    if (amount <= 0 ||
                        findItem(splitList(m.stat(QStringLiteral("immunity")).value.text()).first, t.type) >= 0)
                    {
                        continue;
                    }
                    // A resistance and a weakness to the same type cancel
                    // out up to the smaller one (weakness 10 + resistance 5
                    // = weakness 5).
                    Stat &against = m.stat(QString::fromLatin1(opposite));
                    QString otherList = against.value.text();
                    if (const std::optional<int> existing = itemAmount(otherList, t.type))
                    {
                        if (*existing > amount)
                        {
                            setItemAmount(otherList, t.type, *existing - amount);
                        }
                        else
                        {
                            removeItem(otherList, t.type);
                        }
                        against.value = Value(otherList);
                        amount -= *existing;
                        if (amount <= 0)
                        {
                            continue;
                        }
                    }
                    Stat &stat = m.stat(QString::fromLatin1(key));
                    QString list = stat.value.text();
                    raiseItem(list, t.type, amount,
                              t.suffix.isEmpty() ? QString() : QLatin1Char(' ') + t.suffix);
                    stat.value = Value(list);
                }
            }
            for (const Modifier::Typed &t : mod.speeds)
            {
                if (const int feet = typedAmount(t, before, level); feet > 0 && chosen(options, t.when))
                {
                    raiseItem(m.speed, t.type, feet, QStringLiteral(" feet"));
                }
            }
            if (!mod.strikeDie.isEmpty())
            {
                for (Strike &strike : m.strikes)
                {
                    strike.damage = addStrikeDie(strike.damage, mod.strikeDie);
                }
            }
            if (mod.convertStrike)
            {
                convertStrike(m, *mod.convertStrike);
            }
            for (const Modifier::MadeStrike &listed : mod.strikes)
            {
                Modifier::MadeStrike made = listed;
                made.extra = expandOptions(choicesOf, options, listed.extra, level);
                if (made.replace && chosen(options, made.when))
                {
                    std::erase_if(m.strikes, [&](const Strike &s) {
                        return s.name.trimmed().compare(made.name, Qt::CaseInsensitive) == 0;
                    });
                }
                const auto existing = std::find_if(m.strikes.begin(), m.strikes.end(), [&](const Strike &s) {
                    return s.name.trimmed().compare(made.name, Qt::CaseInsensitive) == 0;
                });
                if (!chosen(options, made.when))
                {
                    continue;
                }
                if (existing == m.strikes.end())
                {
                    // Melee before ranged, as Paizo prints them.
                    Strike strike = makeStrike(m, made, level);
                    const auto at = isMelee(strike) ? std::find_if(m.strikes.begin(), m.strikes.end(),
                                                                   [](const Strike &s) { return !isMelee(s); })
                                                    : m.strikes.end();
                    m.strikes.insert(at, strike);
                }
                else if (made.mergeExtra)
                {
                    // Its own fangs get the venom (and finesse) instead.
                    joinRider(existing->damage, made.extra);
                    for (const QString &trait : splitList(made.traits).first)
                    {
                        if (findItem(splitList(existing->traits).first, trait) < 0)
                        {
                            addMissing(existing->traits, trait);
                        }
                    }
                }
            }
            for (const Modifier::InnateSpell &spell : mod.innateSpells)
            {
                if (chosen(options, spell.when))
                {
                    addInnateSpell(m, spell, level);
                }
            }
            if (mod.domainSpells)
            {
                addDomainSpells(m, *mod.domainSpells, options, level);
            }
            // After its Strikes, which their text can name ({strikedice:claw}).
            // Its name with every adjustment's prefix, filled in once they
            // are all applied (see nameAbilities()).
            TextContext context;
            context.mod = &choicesOf;
            context.monster = &m;
            context.options = options;
            context.level = level;
            context.callIt = kCallIt;
            context.capital = kCallItCapital;
            context.halfHp = firstNumber(m.stat(QStringLiteral("hp")).value.text()).value_or(0) / 2;
            // Its other abilities' DCs (already raised by this one).
            context.abilityDc = highestAbilityDc(m).value_or(
                hint(Table::SpellDc, level, QStringLiteral("high")).value.value_or(0));
            for (const Modifier::EditAbility &edit : mod.editAbilities)
            {
                editAbilities(m, edit, context);
            }
            for (const Modifier::Ability &a : mod.abilities)
            {
                if (!chosen(options, a.when))
                {
                    continue;
                }
                const QString abilityName = expandText(a.name, context);
                // One it has already (its own Reactive Strike) isn't added again.
                const bool has = std::any_of(m.specials.begin(), m.specials.end(), [&](const Special &s) {
                    return !s.placeholder && s.name.trimmed().compare(abilityName, Qt::CaseInsensitive) == 0;
                });
                if (has)
                {
                    continue;
                }
                Special made;
                made.name = abilityName;
                made.type = a.type;
                made.actions = a.actions;
                made.traits = withoutEmptyItems(expandText(a.traits, context));
                made.description = expandText(a.description, context);
                made.generated = mod.id + QLatin1Char(':') + a.key;
                placeGenerated(m, made);
            }
            // The changes that come with a choice (Hybrid Form's Speed, the
            // skills Secret Society Member raises).
            for (const Modifier::Choice &choice : mod.choices)
            {
                if (!chosen(options, choice.when))
                {
                    continue;
                }
                for (const Modifier::Choice::Item &item : choice.items)
                {
                    if (item.changes && chosen(options, choice.key + QLatin1Char(':') + item.key))
                    {
                        int same = level;
                        apply(m, *item.changes, options, same, name, &mod);
                    }
                }
            }
        }
    } // namespace

    QString spokenName(const QString &adjustedName, const QString &baseName)
    {
        const QString base = baseName.trimmed();
        if (base.isEmpty() || !base.at(0).isLower())
        {
            return base; // a proper name ("Sziskazha") stays as it is
        }
        const QString full = adjustedName.trimmed();
        if (!full.endsWith(base, Qt::CaseInsensitive))
        {
            return base;
        }
        // The prefixes, less those left out of the text.
        QStringList words = full.left(full.size() - base.size()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        words.removeIf([](const QString &word) {
            return std::any_of(modifiers().begin(), modifiers().end(), [&](const Modifier &m) {
                return !m.prefixInText && m.prefix.compare(word, Qt::CaseInsensitive) == 0;
            });
        });
        for (QString &word : words)
        {
            word = word.toLower();
        }
        words << base;
        return words.join(QLatin1Char(' '));
    }

    QJsonObject optionsWithDefaults(const Modifier &modifier, const QJsonObject &options)
    {
        QJsonObject result = options;
        for (const Modifier::Choice &c : modifier.choices)
        {
            if (result.contains(c.key))
            {
                continue;
            }
            if (c.type == QLatin1String("pick"))
            {
                QJsonArray picked;
                for (const Modifier::Choice::Item &item : c.items)
                {
                    if (item.on)
                    {
                        picked.append(item.key);
                    }
                }
                result.insert(c.key, picked);
            }
            else if (c.type == QLatin1String("choice"))
            {
                result.insert(c.key, c.defaultValue.isString() || c.items.empty()
                                         ? c.defaultValue.toString()
                                         : c.items.front().key);
            }
            else if (c.type == QLatin1String("check"))
            {
                result.insert(c.key, c.defaultValue.toBool());
            }
            else
            {
                result.insert(c.key, QString());
            }
        }
        return result;
    }

    namespace
    {
        // The built-in domains and the homebrew ones (see domains()).
        std::vector<Domain> loadDomains()
        {
            std::vector<Domain> made;
            QFile file(QStringLiteral(":/data/domains.json"));
            if (file.open(QIODevice::ReadOnly))
            {
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                for (const QJsonValue &v : root.value(QStringLiteral("domains")).toArray())
                {
                    const QJsonObject o = v.toObject();
                    const QString name = o.value(QStringLiteral("name")).toString();
                    made.push_back({name, o.value(QStringLiteral("spell")).toString(),
                                    o.value(QStringLiteral("advanced")).toString(), name.toLower(), QString()});
                }
            }
            else
            {
                qWarning("domains.json is missing from the resources");
            }
            static const QSet<QString> topKeys{"format", "domains", "source", "license", "note"};
            static const QSet<QString> domainKeys{"name", "spell", "advanced", "source"};
            QHash<QString, QString> taken; // homebrew key -> its file
            for (const QString &path : homebrewFiles(QStringLiteral("domains")))
            {
                QString text;
                const std::optional<QJsonObject> root = readHomebrewJson(path, &text);
                if (!root)
                {
                    continue;
                }
                if (root->value(QStringLiteral("format")).toInt() != 1)
                {
                    const auto [line, column] = keyPosition(text, QStringLiteral("format"));
                    addHomebrewProblem({path, line, column, QStringLiteral("\"format\": 1 is missing (add it as the first key)")});
                }
                checkKeys(path, text, *root, topKeys);
                qsizetype from = 0;
                for (const QJsonValue &v : root->value(QStringLiteral("domains")).toArray())
                {
                    const QJsonObject o = v.toObject();
                    const QString name = o.value(QStringLiteral("name")).toString().trimmed();
                    // Where this domain is in the file, for its problems.
                    const auto [line, column] = keyPosition(text, QStringLiteral("name"), from);
                    if (const qsizetype at = text.indexOf(QStringLiteral("\"name\""), from); at >= 0)
                    {
                        from = at + 1;
                    }
                    checkKeys(path, text, o, domainKeys, QStringLiteral("in a domain"));
                    QStringList missing;
                    for (const char *key : {"name", "spell", "advanced"})
                    {
                        if (o.value(QLatin1String(key)).toString().trimmed().isEmpty())
                        {
                            missing << QStringLiteral("\"%1\"").arg(QLatin1String(key));
                        }
                    }
                    if (!missing.isEmpty())
                    {
                        addHomebrewProblem({path, line, column,
                                            QStringLiteral("a domain is missing %1, so it wasn't loaded")
                                                .arg(missing.join(QStringLiteral(" and ")))});
                        continue;
                    }
                    const QString key = kHomebrewPrefix + name.toLower();
                    if (const auto it = taken.constFind(key); it != taken.constEnd())
                    {
                        addHomebrewProblem({path, line, column,
                                            QStringLiteral("the domain \"%1\" is already in %2, which was loaded "
                                                           "instead; give this one another name")
                                                .arg(name, *it)});
                        continue;
                    }
                    taken.insert(key, path);
                    // As typed: a proper name keeps its capitals ("Eye of the Storm").
                    made.push_back({name, o.value(QStringLiteral("spell")).toString().trimmed(),
                                    o.value(QStringLiteral("advanced")).toString().trimmed(), key, path});
                }
            }
            // Homebrew ones among the built-in ones, alphabetically (one
            // named like a built-in one right after it).
            std::stable_sort(made.begin(), made.end(), [](const Domain &a, const Domain &b) {
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            return made;
        }

        // Loaded on first use, or by reloadHomebrew() (once, not twice).
        std::optional<std::vector<Domain>> &domainStore()
        {
            static std::optional<std::vector<Domain>> list;
            return list;
        }

        std::vector<Domain> &loadedDomains()
        {
            if (!domainStore())
            {
                domainStore() = loadDomains();
            }
            return *domainStore();
        }
    } // namespace

    const std::vector<Domain> &domains()
    {
        return loadedDomains();
    }

    namespace
    {
        std::optional<std::vector<Modifier>> &modifierStore()
        {
            static std::optional<std::vector<Modifier>> list;
            return list;
        }

        std::vector<Modifier> &loadedModifiers()
        {
            if (!modifierStore())
            {
                modifierStore() = load();
            }
            return *modifierStore();
        }
    } // namespace

    const std::vector<Modifier> &modifiers()
    {
        return loadedModifiers();
    }

    void reloadModifiers()
    {
        modifierStore() = load();
    }

    void reloadHomebrew()
    {
        clearHomebrewProblems();
        // Domains before the modifiers, whose domain choices list them.
        domainStore() = loadDomains();
        reloadTraitNotes();
        reloadRoadmaps();
        modifierStore() = load();
    }

    QString shownName(const Modifier &modifier)
    {
        return modifier.file.isEmpty() ? modifier.name
                                       : QCoreApplication::translate("monster", "%1 (homebrew)").arg(modifier.name);
    }

    QString generatedSource(const QString &generatedKey)
    {
        // "amphibious:drag-along", "homebrew:custom-stonekin:drag-along"; Mythic's
        // keys have no prefix.
        const qsizetype colon = generatedKey.lastIndexOf(QLatin1Char(':'));
        const Modifier *m = findModifier(colon < 0 ? QStringLiteral("mythic") : generatedKey.left(colon));
        return m ? m->name : generatedKey.left(colon);
    }

    namespace
    {
        std::optional<Modifier> &previewModifier()
        {
            static std::optional<Modifier> preview;
            return preview;
        }
    } // namespace

    Modifier modifierFromJson(const QJsonObject &root, const QString &id)
    {
        Modifier m = readModifier(root);
        m.id = id;
        return m;
    }

    void setPreviewModifier(const std::optional<Modifier> &modifier)
    {
        previewModifier() = modifier;
    }

    const QSet<QString> &modifierTopKeys()
    {
        return kTopKeys;
    }

    const QSet<QString> &modifierChangeKeys()
    {
        return kChangeKeys;
    }

    QString groupPath(const QString &group)
    {
        QStringList parts;
        for (const QString &part : group.split(QLatin1Char('/')))
        {
            if (!part.trimmed().isEmpty())
            {
                parts << part.simplified();
            }
        }
        return parts.join(QLatin1Char('/'));
    }

    QSet<QString> ancestryTraits()
    {
        static const QSet<QString> builtIn = [] {
            QSet<QString> set;
            QFile file(QStringLiteral(":/data/ancestry-traits.json"));
            if (file.open(QIODevice::ReadOnly))
            {
                for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("traits")).toArray())
                {
                    set.insert(v.toString().toLower());
                }
            }
            return set;
        }();
        QSet<QString> set = builtIn;
        for (const HomebrewTrait &trait : homebrewTraits())
        {
            if (trait.ancestry)
            {
                set.insert(trait.key);
            }
        }
        auto given = [&](const Modifier &m) {
            for (const auto &[from, to] : m.replaceOrAdd)
            {
                if (from.compare(kAnyAncestry, Qt::CaseInsensitive) == 0 && !to.trimmed().isEmpty())
                {
                    set.insert(to.trimmed().toLower());
                }
            }
        };
        for (const Modifier &m : modifiers())
        {
            given(m);
        }
        if (previewModifier())
        {
            given(*previewModifier());
        }
        return set;
    }

    const Modifier *findModifier(const QString &id)
    {
        if (previewModifier() && id == kPreviewModifierId)
        {
            return &*previewModifier();
        }
        for (const Modifier &m : modifiers())
        {
            if (m.id == id)
            {
                return &m;
            }
        }
        return nullptr;
    }

    QStringList appliedModifiers(const Monster &monster)
    {
        QStringList ids;
        for (const QJsonValue &v : monster.own.value(kModifiersKey).toArray())
        {
            ids << v.toObject().value(QStringLiteral("id")).toString();
        }
        return ids;
    }

    void setAppliedModifiers(Monster &monster, const QStringList &ids)
    {
        monster.own.remove(kModifiersKey);
        for (const QString &id : ids)
        {
            addAppliedModifier(monster, id);
        }
    }

    QJsonObject modifierOptions(const Monster &monster, int index)
    {
        return monster.own.value(kModifiersKey)
            .toArray()
            .at(index)
            .toObject()
            .value(QStringLiteral("options"))
            .toObject();
    }

    void addAppliedModifier(Monster &monster, const QString &id, const QJsonObject &options)
    {
        QJsonArray list = monster.own.value(kModifiersKey).toArray();
        const Modifier *m = findModifier(id);
        QJsonObject entry{{QStringLiteral("id"), id},
                          {QStringLiteral("kind"), m ? m->kind : QStringLiteral("adjustment")}};
        if (!options.isEmpty())
        {
            entry.insert(QStringLiteral("options"), options);
        }
        list.append(entry);
        monster.own.insert(kModifiersKey, list);
    }

    void setModifierOptions(Monster &monster, int index, const QJsonObject &options)
    {
        QJsonArray list = monster.own.value(kModifiersKey).toArray();
        if (index < 0 || index >= list.size())
        {
            return;
        }
        QJsonObject entry = list.at(index).toObject();
        entry.insert(QStringLiteral("options"), options);
        list.replace(index, entry);
        monster.own.insert(kModifiersKey, list);
    }

    bool replaceAppliedModifier(Monster &monster, const QString &from, const QString &to)
    {
        QJsonArray list = monster.own.value(kModifiersKey).toArray();
        for (qsizetype i = 0; i < list.size(); ++i)
        {
            QJsonObject entry = list[i].toObject();
            if (entry.value(QStringLiteral("id")).toString() == from)
            {
                entry.insert(QStringLiteral("id"), to);
                list[i] = entry;
                monster.own.insert(kModifiersKey, list);
                return true;
            }
        }
        return false;
    }

    void moveAppliedModifier(Monster &monster, int from, int to)
    {
        QJsonArray list = monster.own.value(kModifiersKey).toArray();
        if (from < 0 || to < 0 || from >= list.size() || to >= list.size())
        {
            return;
        }
        const QJsonValue entry = list.takeAt(from);
        list.insert(to, entry);
        monster.own.insert(kModifiersKey, list);
    }

    void removeAppliedModifier(Monster &monster, int index)
    {
        QJsonArray list = monster.own.value(kModifiersKey).toArray();
        if (index < 0 || index >= list.size())
        {
            return;
        }
        list.removeAt(index);
        if (list.isEmpty())
        {
            monster.own.remove(kModifiersKey);
        }
        else
        {
            monster.own.insert(kModifiersKey, list);
        }
    }

    QList<bool> modifiersInEffect(const Monster &monster)
    {
        QList<bool> inEffect;
        int level = shownLevel(monster);
        for (const QString &id : appliedModifiers(monster))
        {
            const Modifier *mod = findModifier(id);
            inEffect << (mod && appliesAt(*mod, level));
            if (inEffect.back())
            {
                level += stepFor(mod->level, level);
            }
        }
        return inEffect;
    }

    std::optional<int> adjustedLevel(const Monster &monster)
    {
        int level = shownLevel(monster);
        bool changed = false;
        for (const QString &id : appliedModifiers(monster))
        {
            if (const Modifier *mod = findModifier(id); mod && appliesAt(*mod, level))
            {
                level += stepFor(mod->level, level);
                changed = true;
            }
        }
        return changed ? std::optional<int>(level) : std::nullopt;
    }

    Monster adjustedUpTo(const Monster &monster, int count, int *levelOut)
    {
        Monster m = monster;
        bool filled = false;
        int level = shownLevel(monster);
        const QStringList ids = appliedModifiers(monster);
        for (int i = 0; i < std::min<int>(count, ids.size()); ++i)
        {
            const Modifier *mod = findModifier(ids[i]);
            if (!mod || !appliesAt(*mod, level))
            {
                continue;
            }
            if (!filled)
            {
                fillHints(m);
                filled = true;
            }
            apply(m, *mod, modifierOptions(monster, i), level, monster.name);
        }
        if (filled)
        {
            nameAbilities(m, monster.name);
            fillNoteBonuses(m);
            // Shown as the display level; the level the benchmarks are built
            // for stays, so nothing else follows the new level's tables.
            m.showlevel = Value(level);
        }
        if (levelOut)
        {
            *levelOut = level;
        }
        // Places no adjustment filled (no longer made) aren't abilities.
        std::erase_if(m.specials, [](const Special &s) { return s.placeholder; });
        return m;
    }

    bool syncGeneratedSpecials(Monster &monster)
    {
        Monster bare = monster;
        std::erase_if(bare.specials, [](const Special &s) { return !s.generated.isEmpty(); });
        std::vector<std::pair<QString, QString>> made; // key, name
        for (const Special &s : adjusted(bare).specials)
        {
            if (!s.generated.isEmpty())
            {
                made.emplace_back(s.generated, s.name);
            }
        }
        auto isMade = [&](const QString &key) {
            return std::any_of(made.begin(), made.end(), [&](const auto &e) { return e.first == key; });
        };
        bool changed = false;
        changed |= std::erase_if(monster.specials, [&](const Special &s) {
                       return !s.generated.isEmpty() && !isMade(s.generated);
                   }) > 0;
        for (const auto &[key, name] : made)
        {
            const bool placed = std::any_of(monster.specials.begin(), monster.specials.end(),
                                            [&](const Special &s) { return s.generated == key; });
            if (!placed)
            {
                Special place;
                place.generated = key;
                place.name = name;
                place.placeholder = true;
                monster.specials.push_back(place);
                changed = true;
            }
        }
        return changed;
    }

    Monster adjusted(const Monster &monster)
    {
        return adjustedUpTo(monster, static_cast<int>(appliedModifiers(monster).size()));
    }
} // namespace monster
