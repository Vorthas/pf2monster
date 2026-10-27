// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Benchmarks.h"

#include "Model/Monster.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <map>

namespace monster
{
    namespace
    {
        struct Data
        {
            QJsonObject tables;
            int minLevel = -1;
            int maxLevel = 24;
            QList<int> extrapolated;
        };

        // Read once, on first use (thread-safe as a function-local static).
        // The hazard tables join the creature ones as "hazard.<name>".
        const Data &data()
        {
            static const Data loaded = [] {
                Data d;
                QFile file(QStringLiteral(":/data/benchmarks.json"));
                if (!file.open(QIODevice::ReadOnly))
                {
                    qWarning("benchmarks.json is missing from the resources");
                    return d;
                }
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                d.tables = root.value(QStringLiteral("tables")).toObject();
                QFile hazardFile(QStringLiteral(":/data/hazard-benchmarks.json"));
                if (hazardFile.open(QIODevice::ReadOnly))
                {
                    const QJsonObject hazards =
                        QJsonDocument::fromJson(hazardFile.readAll()).object().value(QStringLiteral("tables")).toObject();
                    for (auto it = hazards.begin(); it != hazards.end(); ++it)
                    {
                        d.tables.insert(QStringLiteral("hazard.") + it.key(), it.value());
                    }
                }
                else
                {
                    qWarning("hazard-benchmarks.json is missing from the resources");
                }
                const QJsonArray levels = root.value(QStringLiteral("levels")).toArray();
                d.minLevel = levels.at(0).toInt(-1);
                d.maxLevel = levels.at(1).toInt(24);
                for (const QJsonValue &level : root.value(QStringLiteral("extrapolated")).toArray())
                {
                    d.extrapolated << level.toInt();
                }
                return d;
            }();
            return loaded;
        }

        QString key(Table table)
        {
            switch (table)
            {
            case Table::Attribute: return QStringLiteral("attribute");
            case Table::Perception: return QStringLiteral("perception");
            case Table::Skill: return QStringLiteral("skill");
            case Table::ArmorClass: return QStringLiteral("ac");
            case Table::Save: return QStringLiteral("save");
            case Table::HitPoints: return QStringLiteral("hp");
            case Table::Resistance: return QStringLiteral("resistance");
            case Table::StrikeAttack: return QStringLiteral("strikeAttack");
            case Table::StrikeDamage: return QStringLiteral("strikeDamage");
            case Table::SpellDc: return QStringLiteral("spellDc");
            case Table::SpellAttack: return QStringLiteral("spellAttack");
            case Table::AreaDamage: return QStringLiteral("areaDamage");
            case Table::SafeItem: return QStringLiteral("safeItem");
            case Table::HazardStealthDisable: return QStringLiteral("hazard.stealthDisable");
            case Table::HazardMinProficiency: return QStringLiteral("hazard.minProficiency");
            case Table::HazardArmorClass: return QStringLiteral("hazard.ac");
            case Table::HazardSave: return QStringLiteral("hazard.save");
            case Table::HazardHardness: return QStringLiteral("hazard.hardness");
            case Table::HazardHitPoints: return QStringLiteral("hazard.hp");
            case Table::HazardAttack: return QStringLiteral("hazard.attack");
            case Table::HazardDamage: return QStringLiteral("hazard.damage");
            case Table::HazardDc: return QStringLiteral("hazard.dc");
            }
            return {};
        }

        // Bonuses are shown with their sign; AC, DCs and HP without.
        bool signedTable(Table table)
        {
            return table == Table::Attribute || table == Table::Perception ||
                   table == Table::Skill || table == Table::Save ||
                   table == Table::StrikeAttack || table == Table::SpellAttack ||
                   table == Table::HazardSave || table == Table::HazardAttack;
        }

        QString show(int value, bool withSign)
        {
            return withSign && value >= 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
        }

        Hint number(int value, bool withSign)
        {
            Hint h;
            h.valid = true;
            h.value = value;
            h.min = h.max = value;
            h.text = show(value, withSign);
            return h;
        }

        // A range from the tables, highest first as the rules print it.
        Hint range(const QJsonArray &pair, bool withSign, bool skill)
        {
            Hint h;
            h.valid = true;
            h.max = std::max(pair.at(0).toInt(), pair.at(1).toInt());
            h.min = std::min(pair.at(0).toInt(), pair.at(1).toInt());
            if (skill)
            {
                // Skills' low: "+2 to +1"; the better end is the hint.
                h.value = h.max;
                h.text = QStringLiteral("%1 to %2").arg(show(h.max, withSign), show(h.min, withSign));
            }
            else
            {
                // HP: the middle of the range.
                h.value = static_cast<int>(std::lround((h.min + h.max) / 2.0));
                h.text = h.min == h.max ? QString::number(h.min)
                                        : QStringLiteral("%1–%2").arg(h.min).arg(h.max);
            }
            return h;
        }
    } // namespace

    std::optional<Table> tableFor(const QString &statKey)
    {
        static const QHash<QString, Table> tables{
            {QStringLiteral("strength"), Table::Attribute},
            {QStringLiteral("dexterity"), Table::Attribute},
            {QStringLiteral("constitution"), Table::Attribute},
            {QStringLiteral("intelligence"), Table::Attribute},
            {QStringLiteral("wisdom"), Table::Attribute},
            {QStringLiteral("charisma"), Table::Attribute},
            {QStringLiteral("perception"), Table::Perception},
            {QStringLiteral("ac"), Table::ArmorClass},
            {QStringLiteral("fortitude"), Table::Save},
            {QStringLiteral("reflex"), Table::Save},
            {QStringLiteral("will"), Table::Save},
            {QStringLiteral("hp"), Table::HitPoints},
            {QStringLiteral("resistance"), Table::Resistance},
            {QStringLiteral("weakness"), Table::Resistance},
            {QStringLiteral("spellattack"), Table::SpellAttack},
            {QStringLiteral("spelldc"), Table::SpellDc},
            {QStringLiteral("lore"), Table::Skill},
            {QStringLiteral("lorealt"), Table::Skill},
        };
        if (const auto it = tables.find(statKey); it != tables.end())
        {
            return *it;
        }
        if (isLoreKey(statKey))
        {
            return Table::Skill; // the extra Lore skills
        }
        static const QStringList skills{
            "acrobatics", "arcana", "athletics", "crafting", "deception", "diplomacy",
            "intimidation", "medicine", "nature", "occultism", "performance", "religion",
            "society", "stealth", "survival", "thievery"};
        if (skills.contains(statKey))
        {
            return Table::Skill;
        }
        return std::nullopt;
    }

    QStringList benchmarks(Table table)
    {
        QStringList names;
        for (const QJsonValue &column :
             data().tables.value(key(table)).toObject().value(QStringLiteral("columns")).toArray())
        {
            names << column.toString();
        }
        // Below low, from the rules' text rather than the table: "If a
        // creature has a truly bad ability, you can go as low as -5. That's
        // the terrible range for attribute modifiers" (-4 or -5). The
        // website calls those two Terrible and Abysmal.
        if (table == Table::Attribute)
        {
            names << QStringLiteral("terrible") << QStringLiteral("abysmal");
        }
        // Perception's exception to High, asked for by its own name.
        if (table == Table::HazardMinProficiency)
        {
            names.removeAll(QStringLiteral("highPerception"));
        }
        // Ranges with no benchmark.
        if (table == Table::HazardHardness || table == Table::HazardHitPoints)
        {
            names.clear();
        }
        return names;
    }

    Hint hint(Table table, int level, const QString &benchmark)
    {
        const Data &d = data();
        level = std::clamp(level, d.minLevel, d.maxLevel);
        const QJsonObject t = d.tables.value(key(table)).toObject();
        const QJsonArray row =
            t.value(QStringLiteral("rows")).toObject().value(QString::number(level)).toArray();
        const QJsonArray columns = t.value(QStringLiteral("columns")).toArray();
        const bool withSign = signedTable(table);
        const QString name = benchmark.trimmed().toLower();

        Hint h;
        if (table == Table::Attribute && (name == QLatin1String("terrible") ||
                                          name == QLatin1String("abysmal")))
        {
            h = number(name == QLatin1String("terrible") ? -4 : -5, true);
        }
        else if (table == Table::Resistance)
        {
            // [maximum, minimum]
            h.valid = !row.isEmpty();
            h.max = row.at(0).toInt();
            h.min = row.at(1).toInt();
            h.value = h.max;
            h.text = QStringLiteral("%1–%2").arg(h.min).arg(h.max);
        }
        else if (table == Table::HazardHardness || table == Table::HazardHitPoints)
        {
            // [low, high]
            h = range(row.at(0).toArray(), false, false);
            h.valid = !row.isEmpty();
        }
        else if (table == Table::SafeItem)
        {
            const QJsonObject item = row.at(0).toObject();
            const QString note = item.value(QStringLiteral("note")).toString();
            h = number(item.value(QStringLiteral("level")).toInt(), false);
            if (!note.isEmpty())
            {
                h.text += QStringLiteral(" (%1)").arg(note);
            }
        }
        else
        {
            qsizetype column = -1;
            for (qsizetype i = 0; i < columns.size(); ++i)
            {
                if (columns.at(i).toString() == name)
                {
                    column = i;
                }
            }
            bool skillTerrible = false;
            if (column < 0 && table == Table::Skill && name == QLatin1String("terrible"))
            {
                // Not in the rules ("there's no need for terrible skill
                // modifiers"); the website's files use it: Low's bottom end.
                column = columns.size() - 1;
                skillTerrible = true;
            }
            if (column < 0 || column >= row.size())
            {
                return {}; // not a benchmark of this table
            }
            const QJsonValue cell = row.at(column);
            if (cell.isObject()) // damage: dice and average
            {
                const QJsonObject damage = cell.toObject();
                h.valid = true;
                h.value = damage.value(QStringLiteral("average")).toInt();
                h.min = h.max = *h.value;
                h.text = QStringLiteral("%1 (%2)").arg(damage.value(QStringLiteral("dice")).toString())
                             .arg(*h.value);
            }
            else if (cell.isArray())
            {
                h = range(cell.toArray(), withSign,
                          table == Table::Skill || table == Table::HazardStealthDisable);
                if (skillTerrible)
                {
                    h = number(h.min, withSign);
                }
            }
            else if (cell.isDouble())
            {
                h = number(cell.toInt(), withSign);
            }
            else if (cell.isString()) // a proficiency rank: "trained"
            {
                h.valid = true;
                h.text = cell.toString();
            }
            else
            {
                h.text = QStringLiteral("—"); // none at this level
            }
        }
        h.extrapolated = isExtrapolated(level);
        return h;
    }

    std::optional<Table> hazardTableFor(const QString &statKey)
    {
        static const QHash<QString, Table> tables{
            {QStringLiteral("stealth"), Table::HazardStealthDisable},
            {QStringLiteral("minprof"), Table::HazardMinProficiency},
            {QStringLiteral("ac"), Table::HazardArmorClass},
            {QStringLiteral("fortitude"), Table::HazardSave},
            {QStringLiteral("reflex"), Table::HazardSave},
            {QStringLiteral("will"), Table::HazardSave},
            {QStringLiteral("hp"), Table::HazardHitPoints},
        };
        if (const auto it = tables.find(statKey); it != tables.end())
        {
            return *it;
        }
        return std::nullopt;
    }

    Hint statHint(const Monster &monster, const QString &statKey)
    {
        if (monster.isHazard())
        {
            const std::optional<Table> table = hazardTableFor(statKey);
            const Stat *stat = monster.findStat(statKey);
            if (!table || !stat)
            {
                return {};
            }
            const int level = monster.level.number().value_or(0);
            if (*table == Table::HazardMinProficiency)
            {
                // The rank to detect it is a Perception rank (GM Core's
                // "expert for Perception" at levels 1-4).
                const bool high = stat->benchmark.trimmed().compare(QLatin1String("high"),
                                                                    Qt::CaseInsensitive) == 0;
                return hint(*table, level, high ? QStringLiteral("highPerception")
                                                : QStringLiteral("moderate"));
            }
            Hint h = hint(*table, level, stat->benchmark);
            const bool stealthModifier = *table == Table::HazardStealthDisable && monster.hazard.complex;
            if (stealthModifier)
            {
                // A complex hazard lists its Stealth modifier: the DC - 10.
                h = lowered(h, 10, true);
            }
            if (monster.withoutLevel && (*table == Table::HazardStealthDisable ||
                                         *table == Table::HazardArmorClass || *table == Table::HazardSave))
            {
                h = lowered(h, monster.withoutLevel, stealthModifier || *table == Table::HazardSave);
            }
            return h;
        }
        const std::optional<Table> table = tableFor(statKey);
        const Stat *stat = monster.findStat(statKey);
        if (!table || !stat)
        {
            return {};
        }
        const Hint h = hint(*table, monster.hintLevel(statKey), stat->benchmark);
        if (monster.withoutLevel && (*table == Table::Perception || *table == Table::Skill ||
                                     *table == Table::ArmorClass || *table == Table::Save ||
                                     *table == Table::SpellDc || *table == Table::SpellAttack))
        {
            return lowered(h, monster.withoutLevel,
                           *table != Table::ArmorClass && *table != Table::SpellDc);
        }
        return h;
    }

    Hint lowered(Hint h, int by, bool withSign)
    {
        if (!h.valid)
        {
            return h;
        }
        h.min -= by;
        h.max -= by;
        if (h.value)
        {
            *h.value -= by;
        }
        h.text = h.min == h.max ? show(h.min, withSign)
                                : QStringLiteral("%1 to %2").arg(show(h.max, withSign), show(h.min, withSign));
        return h;
    }

    bool usesHint(const QString &statKey)
    {
        static const QStringList keys{
            "perception", "strength", "dexterity", "constitution", "intelligence",
            "wisdom", "charisma", "ac", "fortitude", "reflex", "will", "hp"};
        return keys.contains(statKey);
    }

    bool usesHint(const Monster &monster, const QString &statKey)
    {
        if (!monster.isHazard())
        {
            return usesHint(statKey);
        }
        return statKey == QLatin1String("stealth") || monster.hazard.defenses.contains(statKey);
    }

    int applyHints(Monster &monster)
    {
        int filled = 0;
        for (auto &[key, stat] : monster.stats)
        {
            if (!usesHint(monster, key) || !stat.value.isEmpty())
            {
                continue;
            }
            if (const Hint h = statHint(monster, key); h.valid && h.value)
            {
                stat.value = Value(*h.value);
                ++filled;
            }
        }
        return filled;
    }

    int levelDc(int level, const QString &difficulty)
    {
        struct Dcs
        {
            std::map<int, int> levels;
            QJsonObject adjustments;
        };
        static const Dcs dcs = [] {
            Dcs d;
            QFile file(QStringLiteral(":/data/dcs.json"));
            if (!file.open(QIODevice::ReadOnly))
            {
                qWarning("dcs.json is missing from the resources");
                return d;
            }
            const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
            const QJsonObject levels = root.value(QStringLiteral("levels")).toObject();
            for (auto it = levels.begin(); it != levels.end(); ++it)
            {
                d.levels[it.key().toInt()] = it.value().toInt();
            }
            d.adjustments = root.value(QStringLiteral("adjustments")).toObject();
            return d;
        }();
        if (dcs.levels.empty())
        {
            return 0;
        }
        level = std::clamp(level, dcs.levels.begin()->first, dcs.levels.rbegin()->first);
        return dcs.levels.at(level) + dcs.adjustments.value(difficulty.trimmed().toLower()).toInt();
    }

    int minLevel()
    {
        return data().minLevel;
    }

    int maxLevel()
    {
        return data().maxLevel;
    }

    bool isExtrapolated(int level)
    {
        return data().extrapolated.contains(level);
    }
} // namespace monster
