// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Roadmaps.h"

#include "Model/Benchmarks.h"
#include "Model/Homebrew.h"
#include "Model/Monster.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSet>

#include <algorithm>

namespace monster
{
    namespace
    {
        // The statistics a road map sets, named or not.
        const QStringList &coreStats()
        {
            static const QStringList keys{
                "perception", "strength", "dexterity", "constitution", "intelligence",
                "wisdom", "charisma", "ac", "fortitude", "reflex", "will", "hp",
                "spelldc", "spellattack"};
            return keys;
        }

        Roadmap readRoadmap(const QJsonObject &root)
        {
            Roadmap r;
            r.id = root.value(QStringLiteral("id")).toString().trimmed().toLower();
            r.name = root.value(QStringLiteral("name")).toString();
            r.variant = root.value(QStringLiteral("variant")).toString();
            r.list = root.value(QStringLiteral("list")).toString(QStringLiteral("base"));
            r.rules = root.value(QStringLiteral("rules")).toString();
            r.order = root.value(QStringLiteral("order")).toInt(1000);
            const QJsonObject benchmarks = root.value(QStringLiteral("benchmarks")).toObject();
            for (auto it = benchmarks.begin(); it != benchmarks.end(); ++it)
            {
                QStringList names;
                for (const QJsonValue &v : it.value().toArray())
                {
                    names << v.toString();
                }
                if (!names.isEmpty())
                {
                    r.benchmarks.emplace_back(it.key(), names);
                }
            }
            const QJsonObject notes = root.value(QStringLiteral("notes")).toObject();
            for (auto it = notes.begin(); it != notes.end(); ++it)
            {
                r.notes.emplace_back(it.key(), it.value().toString());
            }
            return r;
        }

        // A homebrew road map checked for what the user might have got
        // wrong; nullopt (with problems recorded) if it can't be used.
        std::optional<Roadmap> readHomebrewRoadmap(const QString &path)
        {
            static const QSet<QString> topKeys{"format", "kind", "id", "name", "variant", "list", "order",
                                               "source", "license", "rules", "benchmarks", "notes"};
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
                problem(QStringLiteral("format"), QStringLiteral("\"format\": 1 is missing (add it as the first key)"));
            }
            if (root.value(QStringLiteral("kind")).toString() != QLatin1String("roadmap"))
            {
                problem(QStringLiteral("kind"), QStringLiteral("\"kind\" should be \"roadmap\", so this file wasn't loaded"));
                usable = false;
            }
            const QString id = root.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty() || id.contains(QLatin1Char(':')))
            {
                problem(QStringLiteral("id"), id.isEmpty()
                                                  ? QStringLiteral("it needs an \"id\" (a short name, like \"custom-sage\"), so this file wasn't loaded")
                                                  : QStringLiteral("its \"id\" can't contain a colon, so this file wasn't loaded"));
                usable = false;
            }
            if (root.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            {
                problem(QStringLiteral("name"), QStringLiteral("it needs a \"name\" (what the menu shows), so this file wasn't loaded"));
                usable = false;
            }
            const QString list = root.value(QStringLiteral("list")).toString(QStringLiteral("base"));
            if (list != QLatin1String("base") && list != QLatin1String("class"))
            {
                problem(QStringLiteral("list"), QStringLiteral("\"list\" should be \"base\" or \"class\" (the Roadmap submenu); "
                                                              "it goes in Base"));
            }
            checkKeys(path, text, root, topKeys);
            // Statistics the road map can set, and the benchmarks each has.
            const QJsonObject benchmarks = root.value(QStringLiteral("benchmarks")).toObject();
            for (auto it = benchmarks.begin(); it != benchmarks.end(); ++it)
            {
                const std::optional<Table> table = tableFor(it.key());
                if (!table || it.key() == QLatin1String("resistance") || it.key() == QLatin1String("weakness"))
                {
                    problem(it.key(), QStringLiteral("\"%1\" in \"benchmarks\" isn't a statistic a road map can set, so it's ignored")
                                          .arg(it.key()));
                    continue;
                }
                const QStringList allowed = monster::benchmarks(*table);
                for (const QJsonValue &v : it.value().toArray())
                {
                    if (!allowed.contains(v.toString()))
                    {
                        problem(it.key(), QStringLiteral("\"%1\" isn't a benchmark for %2 (it can be %3)")
                                              .arg(v.toString(), it.key(), allowed.join(QStringLiteral(", "))));
                    }
                }
            }
            QSet<QString> sections;
            for (const auto &[key, title] : roadmapNoteSections())
            {
                sections.insert(key);
            }
            checkKeys(path, text, root.value(QStringLiteral("notes")).toObject(), sections,
                      QStringLiteral("in \"notes\""));
            if (!usable)
            {
                return std::nullopt;
            }
            Roadmap r = readRoadmap(root);
            if (r.list != QLatin1String("class"))
            {
                r.list = QStringLiteral("base");
            }
            // Only what the app can use: statistics with a table, and their
            // benchmarks that table has (the others were reported above).
            std::erase_if(r.benchmarks, [](const auto &entry) {
                return !tableFor(entry.first) || entry.first == QLatin1String("resistance") ||
                       entry.first == QLatin1String("weakness");
            });
            for (auto &[key, names] : r.benchmarks)
            {
                const QStringList allowed = monster::benchmarks(*tableFor(key));
                names.removeIf([&](const QString &name) { return !allowed.contains(name); });
            }
            std::erase_if(r.benchmarks, [](const auto &entry) { return entry.second.isEmpty(); });
            r.id = kHomebrewPrefix + r.id;
            r.file = path;
            return r;
        }

        std::vector<Roadmap> load()
        {
            std::vector<std::pair<int, Roadmap>> loaded;
            const QDir dir(QStringLiteral(":/data/roadmaps"));
            for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
            {
                QFile file(dir.filePath(name));
                if (!file.open(QIODevice::ReadOnly))
                {
                    continue;
                }
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                if (root.value(QStringLiteral("kind")).toString() != QLatin1String("roadmap"))
                {
                    qWarning("%s is not a road map", qPrintable(name));
                    continue;
                }
                Roadmap r = readRoadmap(root);
                loaded.emplace_back(r.order, std::move(r));
            }
            // Homebrew after the built-in ones of the same order; an id an
            // earlier homebrew file has is reported.
            QHash<QString, QString> taken;
            for (const QString &path : homebrewFiles(QStringLiteral("roadmaps")))
            {
                std::optional<Roadmap> r = readHomebrewRoadmap(path);
                if (!r)
                {
                    continue;
                }
                if (const auto it = taken.constFind(r->id); it != taken.constEnd())
                {
                    QFile file(path);
                    const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
                    const auto [line, column] = keyPosition(text, QStringLiteral("id"));
                    addHomebrewProblem({path, line, column,
                                        QStringLiteral("its id \"%1\" is already used by %2, which was loaded "
                                                       "instead; give this one another id")
                                            .arg(r->id.mid(kHomebrewPrefix.size()), *it)});
                    continue;
                }
                taken.insert(r->id, path);
                loaded.emplace_back(r->order, std::move(*r));
            }
            std::stable_sort(loaded.begin(), loaded.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            std::vector<Roadmap> list;
            for (auto &entry : loaded)
            {
                list.push_back(std::move(entry.second));
            }
            return list;
        }

        std::optional<std::vector<Roadmap>> &store()
        {
            static std::optional<std::vector<Roadmap>> list;
            return list;
        }

        // The state of one statistic after applying the road map.
        Stat applied(const Monster &monster, const Roadmap &roadmap, const QString &key)
        {
            const Stat *before = monster.findStat(key);
            Stat stat = before ? *before : Stat();
            const QStringList *named = roadmap.benchmarksFor(key);
            if (coreStats().contains(key))
            {
                stat.benchmark = named ? named->first() : QStringLiteral("moderate");
                if (usesHint(key))
                {
                    stat.value = Value();
                }
            }
            else if (named)
            {
                stat.benchmark = named->first();
            }
            else
            {
                return stat;
            }
            // Spell DC and attack that were filled in, and skills it names,
            // take the new hint's number.
            if (!usesHint(key) && (!coreStats().contains(key) || !stat.value.isEmpty()))
            {
                const std::optional<Table> table = tableFor(key);
                const Hint h = table ? hint(*table, monster.hintLevel(key), stat.benchmark)
                                     : Hint();
                if (h.valid && h.value)
                {
                    stat.value = Value(*h.value);
                }
            }
            return stat;
        }
    } // namespace

    QString Roadmap::title() const
    {
        return variant.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, variant);
    }

    const QStringList *Roadmap::benchmarksFor(const QString &statKey) const
    {
        for (const auto &[key, names] : benchmarks)
        {
            if (key == statKey)
            {
                return &names;
            }
        }
        return nullptr;
    }

    const std::vector<Roadmap> &roadmaps()
    {
        if (!store())
        {
            store() = load();
        }
        return *store();
    }

    void reloadRoadmaps()
    {
        store() = load();
    }

    const std::vector<std::pair<QString, QString>> &roadmapNoteSections()
    {
        static const std::vector<std::pair<QString, QString>> sections{
            {QStringLiteral("perception"), QObject::tr("Perception")},
            {QStringLiteral("attributes"), QObject::tr("Attribute Modifiers")},
            {QStringLiteral("skills"), QObject::tr("Skills")},
            {QStringLiteral("defense"), QObject::tr("Defense")},
            {QStringLiteral("offense"), QObject::tr("Offense")},
            {QStringLiteral("spells"), QObject::tr("Spells")},
            {QStringLiteral("specials"), QObject::tr("Special Abilities")}};
        return sections;
    }

    const Roadmap *findRoadmap(const QString &id)
    {
        const QString wanted = id.trimmed().toLower();
        if (wanted.isEmpty())
        {
            return nullptr;
        }
        for (const Roadmap &r : roadmaps())
        {
            if (r.id == wanted)
            {
                return &r;
            }
        }
        return nullptr;
    }

    void applyRoadmap(Monster &monster, const Roadmap &roadmap)
    {
        for (auto &[key, stat] : monster.stats)
        {
            stat = applied(monster, roadmap, key);
        }
        monster.roadmap = roadmap.id;
    }

    bool roadmapReplacesValues(const Monster &monster, const Roadmap &roadmap)
    {
        for (const auto &[key, stat] : monster.stats)
        {
            const Stat after = applied(monster, roadmap, key);
            if (!stat.value.isEmpty() && after.value.toJson() != stat.value.toJson())
            {
                return true;
            }
            if (stat.benchmark.compare(QLatin1String("moderate"), Qt::CaseInsensitive) != 0 &&
                after.benchmark != stat.benchmark)
            {
                return true;
            }
        }
        return false;
    }

    QStringList roadmapAlternatives(const Monster &monster, const QString &statKey)
    {
        const Roadmap *roadmap = findRoadmap(monster.roadmap);
        const QStringList *named = roadmap ? roadmap->benchmarksFor(statKey) : nullptr;
        const Stat *stat = monster.findStat(statKey);
        if (!named || !stat || named->size() < 2)
        {
            return {};
        }
        const QString current = stat->benchmark.trimmed().toLower();
        if (!named->contains(current))
        {
            return {};
        }
        QStringList others = *named;
        others.removeAll(current);
        return others;
    }

    QString roadmapNote(const Monster &monster, const QString &section)
    {
        const Roadmap *roadmap = findRoadmap(monster.roadmap);
        if (!roadmap)
        {
            return {};
        }
        for (const auto &[key, note] : roadmap->notes)
        {
            if (key != section)
            {
                continue;
            }
            // Half the level, rounded up: the highest spell rank.
            const int maxRank = std::max(0, (monster.level.number().value_or(0) + 1) / 2);
            auto rank = [](int r) {
                return r < 1 ? QObject::tr("none, cantrips only")
                             : QObject::tr("rank %1 at this level").arg(r);
            };
            QString text = note;
            text.replace(QLatin1String("{maxrank-1}"), rank(maxRank - 1));
            text.replace(QLatin1String("{maxrank}"), rank(maxRank));
            return text;
        }
        return {};
    }
} // namespace monster
