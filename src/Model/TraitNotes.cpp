// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/TraitNotes.h"

#include "Model/Homebrew.h"
#include "Model/Monster.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace monster
{
    namespace
    {
        // The built-in notes: read once, on first use.
        const QHash<QString, TraitNote> &notes()
        {
            static const QHash<QString, TraitNote> loaded = [] {
                QHash<QString, TraitNote> all;
                QFile file(QStringLiteral(":/data/traits.json"));
                if (!file.open(QIODevice::ReadOnly))
                {
                    qWarning("traits.json is missing from the resources");
                    return all;
                }
                const QJsonObject traits =
                    QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("traits")).toObject();
                for (auto it = traits.begin(); it != traits.end(); ++it)
                {
                    const QJsonObject o = it.value().toObject();
                    TraitNote note;
                    note.name = o.value(QStringLiteral("name")).toString();
                    for (const QJsonValue &line : o.value(QStringLiteral("lines")).toArray())
                    {
                        note.lines << line.toString();
                    }
                    all.insert(it.key(), note);
                }
                return all;
            }();
            return loaded;
        }

        QStringList strings(const QJsonValue &json)
        {
            QStringList list;
            for (const QJsonValue &v : json.toArray())
            {
                if (const QString s = v.toString().trimmed(); !s.isEmpty())
                {
                    list << s;
                }
            }
            return list;
        }

        std::vector<HomebrewTrait> loadHomebrewTraits()
        {
            static const QSet<QString> topKeys{"format", "traits", "source", "license", "note"};
            static const QSet<QString> traitKeys{"name", "lines", "recall", "ancestry", "source"};
            std::vector<HomebrewTrait> made;
            QHash<QString, QString> taken;
            for (const QString &path : homebrewFiles(QStringLiteral("traits")))
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
                const QJsonObject traits = root->value(QStringLiteral("traits")).toObject();
                for (auto it = traits.begin(); it != traits.end(); ++it)
                {
                    const QString key = it.key().trimmed().toLower();
                    const auto [line, column] = keyPosition(text, it.key());
                    const QJsonObject o = it.value().toObject();
                    checkKeys(path, text, o, traitKeys, QStringLiteral("in the trait \"%1\"").arg(it.key()));
                    if (key.isEmpty())
                    {
                        addHomebrewProblem({path, line, column, QStringLiteral("a trait has no name, so it wasn't loaded")});
                        continue;
                    }
                    if (const auto found = taken.constFind(key); found != taken.constEnd())
                    {
                        addHomebrewProblem({path, line, column,
                                            QStringLiteral("the trait \"%1\" is already in %2, which was loaded "
                                                           "instead")
                                                .arg(key, *found)});
                        continue;
                    }
                    taken.insert(key, path);
                    HomebrewTrait trait;
                    trait.key = key;
                    trait.name = o.value(QStringLiteral("name")).toString().trimmed();
                    if (trait.name.isEmpty())
                    {
                        trait.name = key.left(1).toUpper() + key.mid(1);
                    }
                    trait.lines = strings(o.value(QStringLiteral("lines")));
                    trait.recall = strings(o.value(QStringLiteral("recall")));
                    trait.ancestry = o.value(QStringLiteral("ancestry")).toBool();
                    trait.file = path;
                    made.push_back(trait);
                }
            }
            return made;
        }

        // Loaded on first use, or by reloadTraitNotes() (once, not twice).
        std::optional<std::vector<HomebrewTrait>> &traitStore()
        {
            static std::optional<std::vector<HomebrewTrait>> list;
            return list;
        }

        std::vector<HomebrewTrait> &loadedHomebrewTraits()
        {
            if (!traitStore())
            {
                traitStore() = loadHomebrewTraits();
            }
            return *traitStore();
        }
    } // namespace

    std::vector<TraitNote> traitNotes(const Monster &monster)
    {
        QStringList keys{monster.type.trimmed().toLower()};
        for (const QString &trait : monster.traits.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            keys << trait.trimmed().toLower();
        }
        std::vector<TraitNote> found;
        QStringList seen;
        for (const QString &key : keys)
        {
            if (seen.contains(key))
            {
                continue;
            }
            seen << key;
            if (notes().contains(key))
            {
                found.push_back(notes().value(key));
            }
            for (const HomebrewTrait &trait : homebrewTraits())
            {
                if (trait.key == key && !trait.lines.isEmpty())
                {
                    found.push_back({trait.name, trait.lines, trait.file});
                }
            }
        }
        return found;
    }

    const std::vector<HomebrewTrait> &homebrewTraits()
    {
        return loadedHomebrewTraits();
    }

    QStringList builtInTraitKeys()
    {
        QStringList keys = notes().keys();
        keys.sort();
        return keys;
    }

    TraitNote builtInTraitNote(const QString &key)
    {
        return notes().value(key);
    }

    void reloadTraitNotes()
    {
        traitStore() = loadHomebrewTraits();
    }
} // namespace monster
