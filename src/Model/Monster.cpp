// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Monster.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace monster
{
    namespace
    {
        // The key that holds this app's own data in a monster file.
        const QString kOwnKey = QStringLiteral("pf2monster");

        // Reading: each known key is taken out of `rest` as it is read, so
        // what's left is the keys the model doesn't know. A key whose value
        // has a type the model can't read stays in `rest` as well, so it is
        // written back as it came (and the model's default isn't written
        // over it, see put()).

        QString takeText(QJsonObject &rest, const QString &key, const QString &fallback = {})
        {
            const auto it = rest.find(key);
            if (it == rest.end())
            {
                return fallback;
            }
            const QJsonValue value = *it;
            if (value.isString())
            {
                rest.erase(it);
                return value.toString();
            }
            if (value.isDouble())
            {
                rest.erase(it);
                return Value::fromJson(value).text();
            }
            return fallback;
        }

        Value takeValue(QJsonObject &rest, const QString &key, const Value &fallback = {})
        {
            const auto it = rest.find(key);
            if (it == rest.end() || !(it->isString() || it->isDouble()))
            {
                return fallback;
            }
            const Value value = Value::fromJson(*it);
            rest.erase(it);
            return value;
        }

        // Fills `ranks` from a JSON array of text; true if it was one.
        bool takeRanks(QJsonObject &rest, const QString &key,
                       std::array<QString, kSpellRanks> &ranks)
        {
            const auto it = rest.find(key);
            if (it == rest.end() || !it->isArray())
            {
                return false;
            }
            const QJsonArray array = it->toArray();
            rest.erase(it);
            for (int i = 0; i < kSpellRanks && i < array.size(); ++i)
            {
                ranks[static_cast<size_t>(i)] = Value::fromJson(array.at(i)).text();
            }
            return true;
        }

        QJsonArray ranksToJson(const std::array<QString, kSpellRanks> &ranks)
        {
            QJsonArray array;
            for (const QString &rank : ranks)
            {
                array.append(rank);
            }
            return array;
        }

        // Writing: starts from the keys the model didn't read, and doesn't
        // replace them.
        void put(QJsonObject &json, const QString &key, const QJsonValue &value)
        {
            if (!json.contains(key))
            {
                json.insert(key, value);
            }
        }

        // An object array's entries, each read by `read`. Other entries are
        // dropped.
        template <typename T, typename Read>
        bool takeObjects(QJsonObject &rest, const QString &key, std::vector<T> &out, Read read)
        {
            const auto it = rest.find(key);
            if (it == rest.end() || !it->isArray())
            {
                return false;
            }
            const QJsonArray array = it->toArray();
            rest.erase(it);
            out.clear();
            for (const QJsonValue &entry : array)
            {
                if (entry.isObject())
                {
                    out.push_back(read(entry.toObject()));
                }
            }
            return true;
        }

        template <typename T>
        QJsonArray objectsToJson(const std::vector<T> &items)
        {
            QJsonArray array;
            for (const T &item : items)
            {
                array.append(item.toJson());
            }
            return array;
        }
    } // namespace

    // ------------------------------------------------------------- Value

    Value Value::fromJson(const QJsonValue &json)
    {
        Value value;
        value.json_ = json;
        return value;
    }

    bool Value::isEmpty() const
    {
        return text().trimmed().isEmpty();
    }

    QString Value::text() const
    {
        if (json_.isDouble())
        {
            const double number = json_.toDouble();
            if (std::trunc(number) == number && std::abs(number) < 1e15)
            {
                return QString::number(static_cast<long long>(number));
            }
            return QString::number(number);
        }
        if (json_.isString())
        {
            return json_.toString();
        }
        return {};
    }

    std::optional<int> Value::number() const
    {
        if (json_.isDouble())
        {
            return static_cast<int>(std::lround(json_.toDouble()));
        }
        bool ok = false;
        const int number = text().trimmed().toInt(&ok);
        return ok ? std::optional<int>(number) : std::nullopt;
    }

    // ------------------------------------------------------------- Stat

    Stat Stat::fromJson(const QJsonValue &json, bool named)
    {
        Stat stat;
        if (named)
        {
            stat.name = QStringLiteral("lore");
        }
        QJsonObject rest = json.toObject();
        stat.value = takeValue(rest, QStringLiteral("value"));
        stat.benchmark = takeText(rest, QStringLiteral("benchmark"), stat.benchmark);
        stat.note = takeText(rest, QStringLiteral("note"));
        if (named)
        {
            stat.name = takeText(rest, QStringLiteral("name"), stat.name);
        }
        stat.extra = rest;
        return stat;
    }

    QJsonObject Stat::toJson(bool named) const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("value"), value.toJson());
        put(json, QStringLiteral("benchmark"), benchmark);
        put(json, QStringLiteral("note"), note);
        if (named)
        {
            put(json, QStringLiteral("name"), name);
        }
        return json;
    }

    // ------------------------------------------------------------- Strike

    Strike Strike::fromJson(const QJsonObject &json)
    {
        Strike strike;
        QJsonObject rest = json;
        strike.type = takeText(rest, QStringLiteral("type"), strike.type);
        strike.name = takeText(rest, QStringLiteral("name"), strike.name);
        strike.attack = takeValue(rest, QStringLiteral("attack"));
        strike.traits = takeText(rest, QStringLiteral("traits"));
        strike.damage = takeText(rest, QStringLiteral("damage"));
        strike.actions = takeText(rest, QStringLiteral("actions"));
        strike.extra = rest;
        return strike;
    }

    QJsonObject Strike::toJson() const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("type"), type);
        put(json, QStringLiteral("name"), name);
        put(json, QStringLiteral("attack"), attack.toJson());
        put(json, QStringLiteral("traits"), traits);
        put(json, QStringLiteral("damage"), damage);
        if (!actions.isEmpty())
        {
            put(json, QStringLiteral("actions"), actions);
        }
        return json;
    }

    // ------------------------------------------------------------- Special

    Special Special::fromJson(const QJsonObject &json)
    {
        Special special;
        QJsonObject rest = json;
        special.name = takeText(rest, QStringLiteral("name"), special.name);
        special.type = takeText(rest, QStringLiteral("type"), special.type);
        special.actions = takeText(rest, QStringLiteral("actions"), special.actions);
        special.traits = takeText(rest, QStringLiteral("traits"));
        special.range = takeText(rest, QStringLiteral("range"));
        special.description = takeText(rest, QStringLiteral("description"));
        special.extra = rest;
        return special;
    }

    QJsonObject Special::toJson() const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("name"), name);
        put(json, QStringLiteral("type"), type);
        put(json, QStringLiteral("actions"), actions);
        put(json, QStringLiteral("traits"), traits);
        put(json, QStringLiteral("range"), range);
        put(json, QStringLiteral("description"), description);
        return json;
    }

    // ------------------------------------------------------------- SpellList

    SpellList SpellList::fromJson(const QJsonObject &json)
    {
        SpellList list;
        QJsonObject rest = json;
        list.name = takeText(rest, QStringLiteral("name"));
        list.note = takeText(rest, QStringLiteral("note"));
        for (auto [key, stat] : {std::pair{QStringLiteral("spellattack"), &list.spellattack},
                                 std::pair{QStringLiteral("spelldc"), &list.spelldc}})
        {
            if (rest.value(key).isObject())
            {
                *stat = Stat::fromJson(rest.take(key));
            }
        }
        takeRanks(rest, QStringLiteral("spells"), list.spells);
        list.constant = takeText(rest, QStringLiteral("constant"));
        list.focuspoints = takeValue(rest, QStringLiteral("focuspoints"), list.focuspoints);
        list.cantriplevel = takeValue(rest, QStringLiteral("cantriplevel"));
        list.extra = rest;
        return list;
    }

    QJsonObject SpellList::toJson() const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("name"), name);
        put(json, QStringLiteral("note"), note);
        put(json, QStringLiteral("spellattack"), spellattack.toJson());
        put(json, QStringLiteral("spelldc"), spelldc.toJson());
        put(json, QStringLiteral("spells"), ranksToJson(spells));
        put(json, QStringLiteral("constant"), constant);
        put(json, QStringLiteral("focuspoints"), focuspoints.toJson());
        put(json, QStringLiteral("cantriplevel"), cantriplevel.toJson());
        return json;
    }

    // ------------------------------------------------------------- Monster

    int Monster::hintLevel(const QString &statKey) const
    {
        if (nonCombatLevel && nonCombatStats.contains(statKey))
        {
            return *nonCombatLevel;
        }
        return level.number().value_or(0);
    }

    HpPart HpPart::fromJson(const QJsonObject &json)
    {
        HpPart part;
        part.name = json.value(QStringLiteral("name")).toString();
        part.hp = json.value(QStringLiteral("hp")).toVariant().toString();
        part.note = json.value(QStringLiteral("note")).toString();
        part.immunities = json.value(QStringLiteral("immunities")).toString();
        part.resistances = json.value(QStringLiteral("resistances")).toString();
        part.weaknesses = json.value(QStringLiteral("weaknesses")).toString();
        part.hardness = json.value(QStringLiteral("hardness")).toVariant().toString();
        part.bt = json.value(QStringLiteral("bt")).toVariant().toString();
        part.noBt = json.value(QStringLiteral("noBt")).toBool();
        return part;
    }

    QJsonObject HpPart::toJson() const
    {
        QJsonObject json{{QStringLiteral("name"), name},
                         {QStringLiteral("hp"), hp},
                         {QStringLiteral("note"), note},
                         {QStringLiteral("immunities"), immunities},
                         {QStringLiteral("resistances"), resistances},
                         {QStringLiteral("weaknesses"), weaknesses}};
        if (!hardness.isEmpty())
        {
            json.insert(QStringLiteral("hardness"), hardness);
        }
        if (!bt.isEmpty())
        {
            json.insert(QStringLiteral("bt"), bt);
        }
        if (noBt)
        {
            json.insert(QStringLiteral("noBt"), true);
        }
        return json;
    }

    // ------------------------------------------------------------- Hazard

    Hazard Hazard::fromJson(const QJsonObject &json, const QString &traits)
    {
        Hazard hazard;
        QJsonObject rest = json;
        const QString complexity = takeText(rest, QStringLiteral("complexity"));
        if (complexity.isEmpty())
        {
            // The website's files, and ones written by hand: a "complex"
            // trait.
            const QStringList list = traits.split(QLatin1Char(','));
            hazard.complex = std::any_of(list.begin(), list.end(), [](const QString &trait) {
                return trait.trimmed().compare(QLatin1String("complex"), Qt::CaseInsensitive) == 0;
            });
        }
        else
        {
            hazard.complex = complexity == QLatin1String("complex");
        }
        hazard.disable = takeText(rest, QStringLiteral("disable"));
        hazard.routineActions = takeText(rest, QStringLiteral("routineActions"));
        hazard.routineTraits = takeText(rest, QStringLiteral("routineTraits"));
        hazard.routine = takeText(rest, QStringLiteral("routine"));
        hazard.reset = takeText(rest, QStringLiteral("reset"));
        hazard.bt = takeText(rest, QStringLiteral("bt"));
        if (rest.value(QStringLiteral("noBt")).isBool())
        {
            hazard.noBt = rest.take(QStringLiteral("noBt")).toBool();
        }
        hazard.acLabel = takeText(rest, QStringLiteral("acLabel"));
        if (rest.value(QStringLiteral("defenses")).isArray())
        {
            hazard.defenses.clear();
            for (const QJsonValue &v : rest.take(QStringLiteral("defenses")).toArray())
            {
                hazard.defenses << v.toString();
            }
        }
        hazard.extra = rest;
        return hazard;
    }

    QJsonObject Hazard::toJson() const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("complexity"),
            complex ? QStringLiteral("complex") : QStringLiteral("simple"));
        put(json, QStringLiteral("disable"), disable);
        put(json, QStringLiteral("routineActions"), routineActions);
        put(json, QStringLiteral("routineTraits"), routineTraits);
        put(json, QStringLiteral("routine"), routine);
        put(json, QStringLiteral("reset"), reset);
        put(json, QStringLiteral("bt"), bt);
        put(json, QStringLiteral("noBt"), noBt);
        put(json, QStringLiteral("acLabel"), acLabel);
        put(json, QStringLiteral("defenses"), QJsonArray::fromStringList(defenses));
        return json;
    }

    std::optional<HpPart> HpPart::fromNote(const QString &note)
    {
        // "50 (head)" then, optionally, the lists, each after its name.
        static const QRegularExpression whole(
            QStringLiteral(R"(^\s*(\d+)\s*\(([^)]+)\)\s*[,;]?\s*(.*)$)"));
        static const QRegularExpression list(
            QStringLiteral(R"(\b(Immunities|Resistances|Weaknesses)\b\s*(.*?)\s*[;,]?\s*)"
                           R"((?=\b(?:Immunities|Resistances|Weaknesses)\b|$))"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = whole.match(note);
        if (!match.hasMatch())
        {
            return std::nullopt;
        }
        HpPart part;
        part.hp = match.captured(1);
        part.name = match.captured(2).trimmed();
        const QString rest = match.captured(3).trimmed();
        qsizetype covered = 0;
        for (auto it = list.globalMatch(rest); it.hasNext();)
        {
            const QRegularExpressionMatch m = it.next();
            const QString kind = m.captured(1).toLower();
            const QString items = m.captured(2).trimmed();
            (kind == QLatin1String("immunities")    ? part.immunities
             : kind == QLatin1String("resistances") ? part.resistances
                                                    : part.weaknesses) = items;
            covered += m.capturedLength();
        }
        // Anything else in the note isn't the workaround.
        if (covered < rest.size())
        {
            return std::nullopt;
        }
        return part;
    }

    Monster::Monster()
    {
        auto add = [this](const auto &keys, bool named = false) {
            for (const char *key : keys)
            {
                Stat stat;
                if (named)
                {
                    stat.name = QStringLiteral("lore");
                }
                stats.emplace_back(QString::fromLatin1(key), stat);
            }
        };
        add(kAbilities);
        add(kSkills);
        add(kLores, true);
        add(kOtherStats);
    }

    Monster Monster::newHazard()
    {
        Monster m;
        m.type = kHazardType;
        m.size = QStringLiteral("-");
        m.mainPart.clear();
        // "Hazards are immune to anything an object is immune to unless
        // specifically noted otherwise" (GM Core); nearly every one lists
        // it. An ordinary value the user can remove.
        m.stat(QStringLiteral("immunity")).value = QStringLiteral("object immunities");
        // The hazard tables have extreme, high and low; high is "a
        // baseline" (GM Core, Building Hazards).
        for (const char *key : {"stealth", "ac", "fortitude", "reflex", "will"})
        {
            m.stat(QString::fromLatin1(key)).benchmark = QStringLiteral("high");
        }
        return m;
    }

    Stat &Monster::stat(const QString &key)
    {
        for (auto &[name, stat] : stats)
        {
            if (name == key)
            {
                return stat;
            }
        }
        stats.emplace_back(key, Stat());
        return stats.back().second;
    }

    const Stat *Monster::findStat(const QString &key) const
    {
        for (const auto &[name, stat] : stats)
        {
            if (name == key)
            {
                return &stat;
            }
        }
        return nullptr;
    }

    bool isLoreKey(const QString &key)
    {
        if (key == QLatin1String("lore") || key == QLatin1String("lorealt"))
        {
            return true;
        }
        bool number = false;
        return key.startsWith(QLatin1String("lore")) && key.mid(4).toInt(&number) >= 3 && number;
    }

    namespace
    {
        // An extra Lore skill: not one of the website's keys.
        bool isExtraLore(const QString &key)
        {
            return isLoreKey(key) && key != QLatin1String("lore") && key != QLatin1String("lorealt");
        }
    } // namespace

    QStringList Monster::loreKeys() const
    {
        QStringList keys;
        for (const auto &[key, stat] : stats)
        {
            if (isLoreKey(key))
            {
                keys << key;
            }
        }
        return keys;
    }

    QString Monster::addLore()
    {
        const QString key = QStringLiteral("lore%1").arg(loreKeys().size() + 1);
        Stat stat;
        stats.emplace_back(key, stat);
        return key;
    }

    void Monster::removeLore(const QString &key)
    {
        if (!isExtraLore(key))
        {
            return;
        }
        std::erase_if(stats, [&](const auto &entry) { return entry.first == key; });
        nonCombatStats.removeAll(key);
        // Renumber the ones after it.
        int n = 3;
        for (auto &[k, stat] : stats)
        {
            if (isExtraLore(k))
            {
                const QString renamed = QStringLiteral("lore%1").arg(n++);
                if (renamed != k)
                {
                    nonCombatStats.replaceInStrings(QRegularExpression(QStringLiteral("^%1$").arg(k)),
                                                    renamed);
                    k = renamed;
                }
            }
        }
    }

    Monster Monster::fromJson(const QJsonObject &json)
    {
        Monster m;
        QJsonObject rest = json;
        // The site's own bookkeeping, which its "Download JSON" leaves out
        // too: a browser-local id, its online sharing data and the list of
        // statistics shown with hints.
        rest.remove(QStringLiteral("id"));
        rest.remove(QStringLiteral("meta"));
        rest.remove(QStringLiteral("hints"));

        m.name = takeText(rest, QStringLiteral("name"));
        m.level = takeValue(rest, QStringLiteral("level"), m.level);
        m.showlevel = takeValue(rest, QStringLiteral("showlevel"));
        m.adjust = takeValue(rest, QStringLiteral("adjust"), m.adjust);
        // A new monster has no alignment; the site's files without the key
        // keep its old default, N.
        m.alignment = takeText(rest, QStringLiteral("alignment"), QStringLiteral("n"));
        m.size = takeText(rest, QStringLiteral("size"), m.size);
        m.type = takeText(rest, QStringLiteral("type"), m.type);
        m.creature = takeText(rest, QStringLiteral("creature"));
        m.traits = takeText(rest, QStringLiteral("traits"));
        if (rest.value(QStringLiteral("tags")).isArray())
        {
            for (const QJsonValue &tag : rest.take(QStringLiteral("tags")).toArray())
            {
                m.tags << Value::fromJson(tag).text();
            }
        }
        m.languages = takeText(rest, QStringLiteral("languages"));
        m.info = takeText(rest, QStringLiteral("info"));
        m.description = takeText(rest, QStringLiteral("description"));
        m.items = takeText(rest, QStringLiteral("items"));
        m.speed = takeText(rest, QStringLiteral("speed"));
        m.savenote = takeText(rest, QStringLiteral("savenote"));
        m.imgurl = takeText(rest, QStringLiteral("imgurl"));
        m.roadmap = takeText(rest, QStringLiteral("roadmap"));

        for (auto &[key, stat] : m.stats)
        {
            if (rest.value(key).isObject())
            {
                stat = Stat::fromJson(rest.take(key), isLoreKey(key));
            }
        }

        takeObjects(rest, QStringLiteral("strikes"), m.strikes, &Strike::fromJson);
        takeObjects(rest, QStringLiteral("specials"), m.specials, &Special::fromJson);

        m.spelltype = takeText(rest, QStringLiteral("spelltype"));
        m.focuspoints = takeValue(rest, QStringLiteral("focuspoints"), m.focuspoints);
        m.cantriplevel = takeValue(rest, QStringLiteral("cantriplevel"));
        m.ritualtype = takeText(rest, QStringLiteral("ritualtype"));
        m.rituals = takeText(rest, QStringLiteral("rituals"));
        takeRanks(rest, QStringLiteral("spells"), m.spells);
        m.constant = takeText(rest, QStringLiteral("constant"));
        takeObjects(rest, QStringLiteral("morespells"), m.morespells, &SpellList::fromJson);

        if (rest.value(kOwnKey).isObject())
        {
            m.own = rest.take(kOwnKey).toObject();
        }
        // Lore skills past the website's two.
        for (const QJsonValue &v : m.own.take(QStringLiteral("moreLores")).toArray())
        {
            const QString key = m.addLore();
            m.stat(key) = Stat::fromJson(v, true);
        }
        // The non-combat level.
        if (const QJsonObject nc = m.own.take(QStringLiteral("nonCombat")).toObject(); !nc.isEmpty())
        {
            if (nc.value(QStringLiteral("level")).isDouble())
            {
                m.nonCombatLevel = nc.value(QStringLiteral("level")).toInt();
            }
            m.nonCombatLabel = nc.value(QStringLiteral("label")).toString();
            for (const QJsonValue &v : nc.value(QStringLiteral("stats")).toArray())
            {
                m.nonCombatStats << v.toString();
            }
        }
        // Parts with Hit Points of their own.
        for (const QJsonValue &v : m.own.take(QStringLiteral("hpParts")).toArray())
        {
            m.hpParts.push_back(HpPart::fromJson(v.toObject()));
        }
        m.regeneration = m.own.take(QStringLiteral("regeneration")).toString();
        m.hardness = m.own.take(QStringLiteral("hardness")).toString();
        m.theme = m.own.take(QStringLiteral("theme")).toString();
        m.recallKnowledge = m.own.take(QStringLiteral("recallKnowledge")).toString();
        m.recallLore = m.own.take(QStringLiteral("recallLore")).toBool();
        if (m.own.contains(QStringLiteral("mainPart")))
        {
            m.mainPart = m.own.take(QStringLiteral("mainPart")).toString();
        }
        // A creature keeps a "hazard" key it has (written elsewhere) as it
        // came.
        if (m.isHazard())
        {
            m.hazard = Hazard::fromJson(m.own.take(QStringLiteral("hazard")).toObject(), m.traits);
            if (!json.value(kOwnKey).toObject().contains(QStringLiteral("mainPart")))
            {
                m.mainPart.clear();
            }
        }
        // The places of the abilities adjustments make, among the others.
        for (const QJsonValue &v : m.own.take(QStringLiteral("generatedSpecials")).toArray())
        {
            const QJsonObject o = v.toObject();
            Special place;
            place.generated = o.value(QStringLiteral("key")).toString();
            place.name = o.value(QStringLiteral("name")).toString();
            place.placeholder = true;
            const qsizetype at = std::clamp<qsizetype>(o.value(QStringLiteral("at")).toInt(), 0,
                                                       static_cast<qsizetype>(m.specials.size()));
            if (!place.generated.isEmpty())
            {
                m.specials.insert(m.specials.begin() + at, place);
            }
        }
        m.extra = rest;
        return m;
    }

    QJsonObject Monster::toJson() const
    {
        QJsonObject json = extra;
        put(json, QStringLiteral("name"), name);
        put(json, QStringLiteral("level"), level.toJson());
        // The site only has this key once the Display Level has been set.
        if (!showlevel.isEmpty())
        {
            put(json, QStringLiteral("showlevel"), showlevel.toJson());
        }
        put(json, QStringLiteral("adjust"), adjust.toJson());
        put(json, QStringLiteral("alignment"), alignment);
        put(json, QStringLiteral("size"), size);
        put(json, QStringLiteral("type"), type);
        put(json, QStringLiteral("creature"), creature);
        put(json, QStringLiteral("traits"), traits);
        put(json, QStringLiteral("tags"), QJsonArray::fromStringList(tags));
        put(json, QStringLiteral("languages"), languages);
        put(json, QStringLiteral("info"), info);
        put(json, QStringLiteral("description"), description);
        put(json, QStringLiteral("items"), items);
        put(json, QStringLiteral("speed"), speed);
        put(json, QStringLiteral("savenote"), savenote);
        put(json, QStringLiteral("imgurl"), imgurl);
        put(json, QStringLiteral("roadmap"), roadmap);

        QJsonArray moreLores;
        for (const auto &[key, stat] : stats)
        {
            if (isExtraLore(key))
            {
                moreLores.append(stat.toJson(true));
                continue;
            }
            put(json, key, stat.toJson(isLoreKey(key)));
        }

        put(json, QStringLiteral("strikes"), objectsToJson(strikes));
        // The website gets only the user's own abilities; the places of the
        // adjustments' ones go in this app's data.
        std::vector<Special> userSpecials;
        QJsonArray generatedPlaces;
        for (size_t i = 0; i < specials.size(); ++i)
        {
            if (specials[i].generated.isEmpty())
            {
                userSpecials.push_back(specials[i]);
            }
            else
            {
                generatedPlaces.append(QJsonObject{{QStringLiteral("key"), specials[i].generated},
                                                   {QStringLiteral("name"), specials[i].name},
                                                   {QStringLiteral("at"), static_cast<int>(i)}});
            }
        }
        put(json, QStringLiteral("specials"), objectsToJson(userSpecials));

        put(json, QStringLiteral("spelltype"), spelltype);
        put(json, QStringLiteral("focuspoints"), focuspoints.toJson());
        put(json, QStringLiteral("cantriplevel"), cantriplevel.toJson());
        put(json, QStringLiteral("ritualtype"), ritualtype);
        put(json, QStringLiteral("rituals"), rituals);
        put(json, QStringLiteral("spells"), ranksToJson(spells));
        put(json, QStringLiteral("constant"), constant);
        put(json, QStringLiteral("morespells"), objectsToJson(morespells));

        QJsonObject ownData = own;
        ownData.insert(QStringLiteral("version"), kFormatVersion);
        if (!generatedPlaces.isEmpty())
        {
            ownData.insert(QStringLiteral("generatedSpecials"), generatedPlaces);
        }
        if (!moreLores.isEmpty())
        {
            ownData.insert(QStringLiteral("moreLores"), moreLores);
        }
        if (nonCombatLevel || !nonCombatLabel.isEmpty() || !nonCombatStats.isEmpty())
        {
            QJsonObject nc{{QStringLiteral("label"), nonCombatLabel},
                           {QStringLiteral("stats"), QJsonArray::fromStringList(nonCombatStats)}};
            if (nonCombatLevel)
            {
                nc.insert(QStringLiteral("level"), *nonCombatLevel);
            }
            ownData.insert(QStringLiteral("nonCombat"), nc);
        }
        if (!hpParts.empty())
        {
            ownData.insert(QStringLiteral("hpParts"), objectsToJson(hpParts));
            ownData.insert(QStringLiteral("mainPart"), mainPart);
        }
        else if (isHazard() && !mainPart.isEmpty())
        {
            ownData.insert(QStringLiteral("mainPart"), mainPart);
        }
        if (!regeneration.isEmpty())
        {
            ownData.insert(QStringLiteral("regeneration"), regeneration);
        }
        if (!hardness.isEmpty())
        {
            ownData.insert(QStringLiteral("hardness"), hardness);
        }
        if (isHazard())
        {
            ownData.insert(QStringLiteral("hazard"), hazard.toJson());
        }
        if (!theme.isEmpty())
        {
            ownData.insert(QStringLiteral("theme"), theme);
        }
        if (!recallKnowledge.isEmpty())
        {
            ownData.insert(QStringLiteral("recallKnowledge"), recallKnowledge);
        }
        if (recallLore)
        {
            ownData.insert(QStringLiteral("recallLore"), true);
        }
        json.insert(kOwnKey, ownData);
        return json;
    }

    std::optional<Monster> Monster::load(const QString &path, QString *error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
            {
                *error = file.errorString();
            }
            return std::nullopt;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            if (error)
            {
                *error = parseError.error != QJsonParseError::NoError
                             ? QObject::tr("Not valid JSON: %1 (at character %2)")
                                   .arg(parseError.errorString())
                                   .arg(parseError.offset)
                             : QObject::tr("Not a monster: the file holds no JSON object");
            }
            return std::nullopt;
        }
        return fromJson(doc.object());
    }

    bool Monster::save(const QString &path, QString *error) const
    {
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
            if (file.commit())
            {
                return true;
            }
        }
        if (error)
        {
            *error = file.errorString();
        }
        return false;
    }
} // namespace monster
