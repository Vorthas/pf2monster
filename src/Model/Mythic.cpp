// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Mythic.h"

#include "Model/Benchmarks.h"
#include "Model/Monster.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

#include <algorithm>

namespace monster
{
    namespace
    {
        struct Data
        {
            std::vector<MythicTemplate> templates;
            QHash<QString, MythicAbility> abilities;
        };

        QStringList strings(const QJsonValue &json)
        {
            QStringList list;
            for (const QJsonValue &v : json.toArray())
            {
                list << v.toString();
            }
            return list;
        }

        const Data &data()
        {
            static const Data loaded = [] {
                Data d;
                QFile file(QStringLiteral(":/data/adjustments/mythic.json"));
                if (!file.open(QIODevice::ReadOnly))
                {
                    qWarning("mythic.json is missing from the resources");
                    return d;
                }
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                const QJsonObject abilities = root.value(QStringLiteral("abilities")).toObject();
                for (auto it = abilities.begin(); it != abilities.end(); ++it)
                {
                    const QJsonObject o = it.value().toObject();
                    MythicAbility a;
                    a.key = it.key();
                    a.name = o.value(QStringLiteral("name")).toString();
                    a.type = o.value(QStringLiteral("type")).toString();
                    a.actions = o.value(QStringLiteral("actions")).toString(QStringLiteral("none"));
                    a.traits = o.value(QStringLiteral("traits")).toString();
                    a.description = o.value(QStringLiteral("description")).toString();
                    a.compact = o.value(QStringLiteral("compact")).toString();
                    a.compactOmit = o.value(QStringLiteral("compactOmit")).toBool();
                    a.summary = o.value(QStringLiteral("summary")).toString();
                    a.skill = o.value(QStringLiteral("skill")).toString();
                    a.spells = o.value(QStringLiteral("spells")).toBool();
                    a.costsPoints = o.value(QStringLiteral("costsPoints")).toBool();
                    d.abilities.insert(a.key, a);
                }
                for (const QJsonValue &v : root.value(QStringLiteral("templates")).toArray())
                {
                    const QJsonObject o = v.toObject();
                    MythicTemplate t;
                    t.id = o.value(QStringLiteral("id")).toString();
                    t.name = o.value(QStringLiteral("name")).toString();
                    t.summary = o.value(QStringLiteral("summary")).toString();
                    t.weakness = o.value(QStringLiteral("weakness")).toString();
                    t.noResilience = strings(o.value(QStringLiteral("noResilience")));
                    t.noImmunity = strings(o.value(QStringLiteral("noImmunity")));
                    t.noResistance = o.value(QStringLiteral("noResistance")).toBool();
                    t.fullResistance = o.value(QStringLiteral("fullResistance")).toBool();
                    t.noRemoveCondition = o.value(QStringLiteral("noRemoveCondition")).toBool();
                    t.firstResilience = o.value(QStringLiteral("firstResilience")).toString();
                    t.abilities = strings(o.value(QStringLiteral("abilities")));
                    d.templates.push_back(t);
                }
                return d;
            }();
            return loaded;
        }

        const QStringList &saves()
        {
            static const QStringList list{"fortitude", "reflex", "will"};
            return list;
        }

        // A statistic's number as the statblock shows it (its hint when
        // empty), or nullopt (an empty skill: untrained).
        std::optional<int> number(const Monster &m, const QString &key)
        {
            const Stat *stat = m.findStat(key);
            if (!stat)
            {
                return std::nullopt;
            }
            if (!stat->value.isEmpty())
            {
                return stat->value.number();
            }
            if (usesHint(key))
            {
                return statHint(m, key).value;
            }
            return std::nullopt;
        }

        QString capitalized(QString text)
        {
            if (!text.isEmpty())
            {
                text[0] = text[0].toUpper();
            }
            return text;
        }

        // Adds `item` to a comma-separated list.
        void addToList(QString &list, const QString &item)
        {
            list = list.trimmed().isEmpty() ? item : list.trimmed() + QStringLiteral(", ") + item;
        }

        Special special(const MythicAbility &a, bool full, const QHash<QString, QString> &tokens)
        {
            Special s;
            s.name = a.name;
            s.type = a.type;
            s.actions = a.actions;
            s.traits = a.traits;
            s.description = full ? a.description : a.compact;
            for (auto it = tokens.begin(); it != tokens.end(); ++it)
            {
                s.description.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
            }
            return s;
        }
    } // namespace

    const std::vector<MythicTemplate> &mythicTemplates()
    {
        return data().templates;
    }

    const MythicTemplate *findMythicTemplate(const QString &id)
    {
        for (const MythicTemplate &t : data().templates)
        {
            if (t.id == id)
            {
                return &t;
            }
        }
        return nullptr;
    }

    const MythicAbility *findMythicAbility(const QString &key)
    {
        const auto it = data().abilities.constFind(key);
        return it == data().abilities.cend() ? nullptr : &*it;
    }

    std::vector<MythicSlot> mythicSlots(const Monster &monster, int level, const QJsonObject &options)
    {
        const MythicTemplate *t = findMythicTemplate(options.value(QStringLiteral("template")).toString());
        const QJsonObject stored = options.value(QStringLiteral("slots")).toObject();
        std::vector<MythicSlot> rows;
        QStringList resilient;
        for (const int at : {1, 4, 7, 10, 13, 17, 20, 23})
        {
            if (at > level)
            {
                break;
            }
            MythicSlot slot;
            slot.level = at;
            QString fallback;
            if (at == 1 || at == 7 || at == 13)
            {
                // Resilience to one save (the highest left first), or
                // resistance.
                if (t && at == 1 && !t->firstResilience.isEmpty())
                {
                    slot.choices << QStringLiteral("resilience:") + t->firstResilience;
                }
                else
                {
                    QStringList open;
                    for (const QString &save : saves())
                    {
                        if (!resilient.contains(save) && !(t && t->noResilience.contains(save)))
                        {
                            open << save;
                        }
                    }
                    std::stable_sort(open.begin(), open.end(), [&](const QString &a, const QString &b) {
                        return number(monster, a).value_or(-99) > number(monster, b).value_or(-99);
                    });
                    for (const QString &save : open)
                    {
                        slot.choices << QStringLiteral("resilience:") + save;
                    }
                    // Brute's resistance is already its level; the others
                    // can't have any.
                    if (!(t && (t->noResistance || t->fullResistance)))
                    {
                        slot.choices << QStringLiteral("resistance");
                    }
                }
            }
            else if (at == 4)
            {
                slot.choices << QStringLiteral("skill");
                if (!(t && t->noRemoveCondition))
                {
                    slot.choices << QStringLiteral("remove-condition");
                }
            }
            else if (at == 10)
            {
                slot.choices << QStringLiteral("recharge");
            }
            else if (at == 17)
            {
                slot.choices << QStringLiteral("undying-myth");
            }
            else if (at == 20)
            {
                slot.choices << QStringLiteral("reroll") << QStringLiteral("defenses");
            }
            else if (at == 23)
            {
                const bool strikes = !(t && t->noImmunity.contains(QLatin1String("strikes")));
                const bool spells = !(t && t->noImmunity.contains(QLatin1String("spells")));
                if (strikes)
                {
                    slot.choices << QStringLiteral("immunity:strikes");
                }
                if (spells)
                {
                    slot.choices << QStringLiteral("immunity:spells");
                }
                // "Only the most powerful creatures (typically level 25)
                // should be immune to both."
                if (strikes && spells && level >= 25)
                {
                    slot.choices << QStringLiteral("immunity:both");
                }
            }
            const QString wanted = stored.value(QString::number(at)).toString();
            slot.choice = slot.choices.contains(wanted) ? wanted : slot.choices.value(0);
            if (slot.choice.startsWith(QLatin1String("resilience:")))
            {
                resilient << slot.choice.mid(11);
            }
            rows.push_back(slot);
        }
        return rows;
    }

    QString traditionSkill(const QString &tradition)
    {
        static const QHash<QString, QString> skills{{"arcane", "arcana"},
                                                    {"divine", "religion"},
                                                    {"occult", "occultism"},
                                                    {"primal", "nature"}};
        return skills.value(tradition, QStringLiteral("arcana"));
    }

    QString mythicTradition(const Monster &monster, const QJsonObject &options)
    {
        const QString chosen = options.value(QStringLiteral("tradition")).toString();
        if (!chosen.isEmpty())
        {
            return chosen;
        }
        QStringList texts{monster.spelltype};
        for (const SpellList &list : monster.morespells)
        {
            texts << list.name;
        }
        for (const QString &text : texts)
        {
            for (const char *tradition : {"arcane", "divine", "occult", "primal"})
            {
                if (text.contains(QLatin1String(tradition), Qt::CaseInsensitive))
                {
                    return QString::fromLatin1(tradition);
                }
            }
        }
        return QStringLiteral("arcane");
    }

    bool mythicAbilityOn(const QJsonObject &options, const QString &key, int level)
    {
        const MythicAbility *a = findMythicAbility(key);
        const bool byDefault = !(a && a->costsPoints && level < 4);
        return options.value(QStringLiteral("abilities")).toObject().value(key).toBool(byDefault);
    }

    bool mythicPrefixName(const QString &name, const QJsonObject &options)
    {
        const QString trimmed = name.trimmed();
        return options.value(QStringLiteral("prefixName"))
            .toBool(trimmed.isEmpty() || trimmed.at(0).isLower());
    }

    QString mythicCallIt(const QString &name, const QJsonObject &options)
    {
        const QString chosen = options.value(QStringLiteral("callIt")).toString().trimmed();
        if (!chosen.isEmpty())
        {
            return chosen;
        }
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty())
        {
            return QStringLiteral("the creature");
        }
        return trimmed.at(0).isLower() ? QStringLiteral("the mythic ") + trimmed : trimmed;
    }

    QStringList mythicSkills(const Monster &monster, int level, const QJsonObject &options)
    {
        const int count = level >= 12 ? 2 : 1;
        // Chosen first, then the template's extreme skill, then the best.
        QStringList candidates = strings(options.value(QStringLiteral("skills")));
        if (const MythicTemplate *t =
                findMythicTemplate(options.value(QStringLiteral("template")).toString()))
        {
            for (const QString &key : t->abilities)
            {
                const MythicAbility *a = findMythicAbility(key);
                if (a && !a->skill.isEmpty() && mythicAbilityOn(options, key, level))
                {
                    candidates << (a->skill == QLatin1String("tradition")
                                       ? traditionSkill(mythicTradition(monster, options))
                                       : a->skill);
                }
            }
        }
        QStringList bySkill;
        for (const char *skill : kSkills)
        {
            bySkill << QString::fromLatin1(skill);
        }
        std::stable_sort(bySkill.begin(), bySkill.end(), [&](const QString &a, const QString &b) {
            return number(monster, a).value_or(-99) > number(monster, b).value_or(-99);
        });
        candidates << bySkill;
        QStringList skills;
        for (const QString &skill : candidates)
        {
            if (!skill.isEmpty() && !skills.contains(skill))
            {
                skills << skill;
            }
            if (skills.size() == count)
            {
                break;
            }
        }
        return skills;
    }

    QString mythicChoiceLabel(const QString &choice)
    {
        if (choice.startsWith(QLatin1String("resilience:")))
        {
            return QObject::tr("Mythic resilience: %1").arg(capitalized(choice.mid(11)));
        }
        static const QHash<QString, QString> labels{
            {"resistance", QObject::tr("Mythic resistance")},
            {"skill", QObject::tr("Mythic Power: Mythic Skill")},
            {"remove-condition", QObject::tr("Mythic Power: Remove a Condition")},
            {"recharge", QObject::tr("Mythic Power: Recharge")},
            {"undying-myth", QObject::tr("Mythic Power: Undying Myth")},
            {"reroll", QObject::tr("Mythic Power: Reroll")},
            {"defenses", QObject::tr("Mythic defenses")},
            {"immunity:strikes", QObject::tr("Mythic immunity: Strikes")},
            {"immunity:spells", QObject::tr("Mythic immunity: spells")},
            {"immunity:both", QObject::tr("Mythic immunity: Strikes and spells")}};
        return labels.value(choice, choice);
    }

    void applyMythic(Monster &m, const QJsonObject &options, int level, const QString &name)
    {
        const bool full = options.value(QStringLiteral("fullText")).toBool();
        const QString callIt = mythicCallIt(name, options);
        const MythicTemplate *t = findMythicTemplate(options.value(QStringLiteral("template")).toString());
        QStringList templateAbilities;
        if (t)
        {
            for (const QString &key : t->abilities)
            {
                if (mythicAbilityOn(options, key, level))
                {
                    templateAbilities << key;
                }
            }
        }

        // The template's numbers: an extreme skill, or high/extreme spells.
        for (const QString &key : templateAbilities)
        {
            const MythicAbility *a = findMythicAbility(key);
            if (!a)
            {
                continue;
            }
            if (!a->skill.isEmpty())
            {
                const QString skill = a->skill == QLatin1String("tradition")
                                          ? traditionSkill(mythicTradition(m, options))
                                          : a->skill;
                if (const Hint h = hint(Table::Skill, level, QStringLiteral("extreme")); h.valid && h.value)
                {
                    m.stat(skill).value = Value(*h.value);
                }
            }
            if (a->spells)
            {
                const QString benchmark = level >= 11 ? QStringLiteral("extreme") : QStringLiteral("high");
                const Hint dc = hint(Table::SpellDc, level, benchmark);
                const Hint attack = hint(Table::SpellAttack, level, benchmark);
                for (const auto &[key, h] : {std::pair{QStringLiteral("spelldc"), dc},
                                             std::pair{QStringLiteral("spellattack"), attack}})
                {
                    if (Stat &stat = m.stat(key); !stat.value.isEmpty() && h.valid && h.value)
                    {
                        stat.value = Value(*h.value);
                    }
                }
                for (SpellList &list : m.morespells)
                {
                    if (dc.valid && dc.value)
                    {
                        list.spelldc.value = Value(*dc.value);
                    }
                    if (!list.spellattack.value.isEmpty() && attack.valid && attack.value)
                    {
                        list.spellattack.value = Value(*attack.value);
                    }
                }
            }
        }

        // The basic abilities' table.
        const std::vector<MythicSlot> rows = mythicSlots(m, level, options);
        QStringList resilient;
        int resistances = 0;
        QStringList power; // Mythic Power actions, in level order
        bool defenses = false;
        QString immunity;
        for (const MythicSlot &slot : rows)
        {
            if (slot.choice.startsWith(QLatin1String("resilience:")))
            {
                resilient << slot.choice.mid(11);
            }
            else if (slot.choice == QLatin1String("resistance"))
            {
                ++resistances;
            }
            else if (slot.choice == QLatin1String("skill"))
            {
                power << QStringLiteral("mythic-skill");
            }
            else if (slot.choice == QLatin1String("defenses"))
            {
                defenses = true;
            }
            else if (slot.choice.startsWith(QLatin1String("immunity:")))
            {
                immunity = slot.choice.mid(9);
            }
            else if (!slot.choice.isEmpty())
            {
                power << slot.choice; // remove-condition, recharge, undying-myth, reroll
            }
        }
        const int resistance = t && t->fullResistance ? level
                               : resistances >= 2     ? level
                               : resistances == 1     ? level / 2
                                                      : 0;

        // The mythic trait, and the lines Paizo writes them in.
        if (!m.traits.contains(QLatin1String("mythic"), Qt::CaseInsensitive))
        {
            addToList(m.traits, QStringLiteral("mythic"));
        }
        if (!resilient.isEmpty())
        {
            // As Paizo writes it: "(Ref and Will)", "(all saves)".
            static const QHash<QString, QString> shortNames{
                {"fortitude", "Fort"}, {"reflex", "Ref"}, {"will", "Will"}};
            QStringList names;
            for (const QString &save : saves())
            {
                if (resilient.contains(save))
                {
                    names << shortNames.value(save);
                }
            }
            const QString note = QStringLiteral("mythic resilience (%1)")
                                     .arg(names.size() == 3 ? QStringLiteral("all saves")
                                                            : names.join(QStringLiteral(" and ")));
            m.savenote = m.savenote.trimmed().isEmpty() ? note : note + QStringLiteral("; ") + m.savenote.trimmed();
        }
        if (resistance > 0)
        {
            QString list = m.stat(QStringLiteral("resistance")).value.text();
            addToList(list, QStringLiteral("mythic resistance %1").arg(resistance));
            m.stat(QStringLiteral("resistance")).value = Value(list);
        }
        if (!immunity.isEmpty())
        {
            QString list = m.stat(QStringLiteral("immunity")).value.text();
            addToList(list, QStringLiteral("mythic immunity"));
            m.stat(QStringLiteral("immunity")).value = Value(list);
        }

        // The abilities: defensive ones, then the offensive ones with Mythic
        // Power and its actions.
        const QString hpText = m.stat(QStringLiteral("hp")).value.text();
        const int maxHp = m.stat(QStringLiteral("hp")).value.number().value_or(hpText.section(QLatin1Char(' '), 0, 0).toInt());
        auto add = [&](const QString &key, QHash<QString, QString> tokens = {}, const QString &under = {}) {
            const MythicAbility *a = findMythicAbility(key);
            if (!a || a->type.isEmpty() || (!full && a->compactOmit))
            {
                return;
            }
            tokens.insert(QStringLiteral("name"), callIt);
            tokens.insert(QStringLiteral("Name"), capitalized(callIt));
            tokens.insert(QStringLiteral("halfHp"), QString::number(maxHp / 2));
            Special made = special(*a, full, tokens);
            made.generated = key;
            made.under = under;
            // In the place the user gave it, else at the end.
            const auto place = std::find_if(m.specials.begin(), m.specials.end(), [&](const Special &s) {
                return s.placeholder && s.generated == key;
            });
            if (place != m.specials.end())
            {
                *place = made;
            }
            else
            {
                m.specials.push_back(made);
            }
        };
        if (!resilient.isEmpty())
        {
            add(QStringLiteral("mythic-resilience"));
        }
        if (resistance > 0)
        {
            add(QStringLiteral("mythic-resistance"), {{"resistance", QString::number(resistance)}});
        }
        if (defenses)
        {
            add(QStringLiteral("mythic-defenses"));
        }
        if (!immunity.isEmpty())
        {
            static const QHash<QString, QString> what{
                {"strikes", "Strikes made with non-mythic weapons and unarmed Strikes from non-mythic "
                            "characters"},
                {"spells", "harmful spells cast by non-mythic creatures"},
                {"both", "harmful spells cast by non-mythic creatures, Strikes made with non-mythic "
                         "weapons, and unarmed Strikes from non-mythic characters"}};
            static const QHash<QString, QString> whatShort{
                {"strikes", "Strikes"}, {"spells", "spells"}, {"both", "Strikes and spells"}};
            add(QStringLiteral("mythic-immunity"),
                {{"immunity", what.value(immunity)}, {"immunityShort", whatShort.value(immunity)}});
        }
        // Mythic Points from 4th level; earlier only if asked for, or if an
        // ability that costs them was ticked.
        bool pool = level >= 4 || options.value(QStringLiteral("earlyPoints")).toBool();
        QStringList pointActions;
        for (const QString &key : templateAbilities)
        {
            const MythicAbility *a = findMythicAbility(key);
            if (!a || a->type.isEmpty())
            {
                continue;
            }
            if (a->costsPoints)
            {
                // Caster's Recharge Spell takes the place of Recharge (the
                // Mythic Lich, 12th level, has only Recharge Spell).
                if (key == QLatin1String("recharge-spell"))
                {
                    power.removeAll(QStringLiteral("recharge"));
                }
                pool = true;
                if (a->type == QLatin1String("offense"))
                {
                    pointActions << key; // listed with Mythic Power
                    continue;
                }
            }
            add(key);
        }
        if (pool)
        {
            // The template's actions first, then the table's (as the Mythic
            // Lich lists Recharge Spell before Remove a Condition).
            add(QStringLiteral("mythic-power"));
            for (const QString &key : pointActions + power)
            {
                if (key == QLatin1String("mythic-skill"))
                {
                    QStringList names;
                    for (const QString &skill : mythicSkills(m, level, options))
                    {
                        names << capitalized(skill);
                    }
                    add(key,
                        {{"skills", names.join(QStringLiteral(" or "))},
                         {"skillList", names.join(QStringLiteral(" or "))}},
                        QStringLiteral("mythic-power"));
                }
                else
                {
                    add(key, {}, QStringLiteral("mythic-power")); // a bullet under Mythic Power
                }
            }
        }
    }
} // namespace monster
