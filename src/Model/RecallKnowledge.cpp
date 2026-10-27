// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/RecallKnowledge.h"

#include "Model/Benchmarks.h"
#include "Model/Monster.h"
#include "Model/TraitNotes.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <tuple>

namespace monster
{
    namespace
    {
        // Creature trait -> the skills that identify it.
        const QHash<QString, QStringList> &skillsByTrait()
        {
            static const QHash<QString, QStringList> table = [] {
                QHash<QString, QStringList> made;
                QFile file(QStringLiteral(":/data/recall-knowledge.json"));
                if (!file.open(QIODevice::ReadOnly))
                {
                    qWarning("recall-knowledge.json is missing from the resources");
                    return made;
                }
                const QJsonObject traits =
                    QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("traits")).toObject();
                for (auto it = traits.begin(); it != traits.end(); ++it)
                {
                    QStringList skills;
                    for (const QJsonValue &v : it.value().toArray())
                    {
                        skills << v.toString();
                    }
                    made.insert(it.key(), skills);
                }
                return made;
            }();
            return table;
        }

        // Its creature type, then its traits, lower case, each once.
        QStringList traitKeys(const Monster &m)
        {
            QStringList keys;
            for (const QString &trait : QStringList{m.type} + m.traits.split(QLatin1Char(',')))
            {
                const QString key = trait.trimmed().toLower();
                if (!key.isEmpty() && !keys.contains(key))
                {
                    keys << key;
                }
            }
            return keys;
        }
    } // namespace

    RecallKnowledge recallKnowledge(const Monster &m)
    {
        RecallKnowledge rk;
        const int level = m.showlevel.number().value_or(m.level.number().value_or(0));
        const int base = levelDc(level);
        int adjust = 0;
        if (!m.recallDifficulty.isEmpty())
        {
            // Rumored Cryptid: incredibly hard, in place of its rarity's.
            adjust = levelDc(level, m.recallDifficulty) - base;
            rk.note = m.recallDifficulty;
        }
        else
        {
            const QStringList keys = traitKeys(m);
            for (const auto &[rarity, label, amount] :
                 {std::tuple{"unique", "Unique", 10}, std::tuple{"rare", "Rare", 5},
                  std::tuple{"uncommon", "Uncommon", 2}})
            {
                if (keys.contains(QLatin1String(rarity)))
                {
                    adjust = amount;
                    rk.note = QStringLiteral("includes +%1 from %2").arg(amount).arg(QLatin1String(label));
                    break;
                }
            }
        }
        // Proficiency without Level takes the level off these DCs too (GM
        // Core: "subtract the level from the DC tables").
        adjust -= m.withoutLevel;
        rk.dc = base + adjust;
        rk.unspecificLore = levelDc(level, QStringLiteral("easy")) + adjust;
        rk.specificLore = levelDc(level, QStringLiteral("very easy")) + adjust;
        for (const QString &key : traitKeys(m))
        {
            // GM Core's skills, then any a homebrew trait adds.
            QStringList skills = skillsByTrait().value(key);
            QString name = key.left(1).toUpper() + key.mid(1);
            for (const HomebrewTrait &trait : homebrewTraits())
            {
                if (trait.key == key && !trait.recall.isEmpty())
                {
                    name = trait.name;
                    for (const QString &skill : trait.recall)
                    {
                        if (!skills.contains(skill, Qt::CaseInsensitive))
                        {
                            skills << skill;
                        }
                    }
                }
            }
            if (!skills.isEmpty())
            {
                rk.identifications << QStringLiteral("%1 (%2)").arg(name, skills.join(QStringLiteral(", ")));
            }
        }
        rk.otherSkills = m.recallSkills;
        return rk;
    }

    QString recallKnowledgeText(const Monster &m)
    {
        const RecallKnowledge rk = recallKnowledge(m);
        QStringList parts{rk.note.isEmpty() ? QStringLiteral("DC %1").arg(rk.dc)
                                            : QStringLiteral("DC %1 (%2)").arg(rk.dc).arg(rk.note)};
        parts << rk.identifications << rk.otherSkills;
        if (m.recallLore)
        {
            parts << QStringLiteral("Unspecific Lore DC %1").arg(rk.unspecificLore)
                  << QStringLiteral("Specific Lore DC %1").arg(rk.specificLore);
        }
        return parts.join(QStringLiteral(" • "));
    }

    bool hasCreatureTypeTrait(const Monster &m)
    {
        for (const QString &key : traitKeys(m))
        {
            if (skillsByTrait().contains(key))
            {
                return true;
            }
            // A homebrew trait that names skills to identify it counts.
            for (const HomebrewTrait &trait : homebrewTraits())
            {
                if (trait.key == key && !trait.recall.isEmpty())
                {
                    return true;
                }
            }
        }
        return false;
    }

    QStringList builtInRecallSkills(const QString &trait)
    {
        return skillsByTrait().value(trait.trimmed().toLower());
    }
} // namespace monster
