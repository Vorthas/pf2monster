// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/ScribeExport.h"

#include "Model/Adjustments.h"
#include "Model/Benchmarks.h"
#include "Model/Monster.h"
#include "Model/RecallKnowledge.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace monster
{
    namespace
    {
        // "acrobatics" -> "Acrobatics", "stonekin lore" -> "Stonekin Lore".
        QString titleCase(const QString &text)
        {
            QString out = text;
            bool start = true;
            for (QChar &c : out)
            {
                if (start && c.isLetter())
                {
                    c = c.toUpper();
                }
                start = c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('(');
            }
            return out;
        }

        // 1 -> "1st", 2 -> "2nd", 12 -> "12th".
        QString ordinal(int n)
        {
            const int lastTwo = n % 100;
            const char *suffix = "th";
            if (lastTwo < 11 || lastTwo > 13)
            {
                switch (n % 10)
                {
                case 1: suffix = "st"; break;
                case 2: suffix = "nd"; break;
                case 3: suffix = "rd"; break;
                default: break;
                }
            }
            return QString::number(n) + QLatin1String(suffix);
        }

        // The site's text fields can hold a little HTML (its own exports
        // write <i>...</i>); markdown says the same.
        QString htmlToMarkdown(QString text)
        {
            static const QRegularExpression italic(
                QStringLiteral(R"(</?\s*(i|em)\s*>)"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression bold(
                QStringLiteral(R"(</?\s*(b|strong)\s*>)"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression lineBreak(
                QStringLiteral(R"(<\s*br\s*/?\s*>)"), QRegularExpression::CaseInsensitiveOption);
            text.replace(italic, QStringLiteral("*"));
            text.replace(bold, QStringLiteral("**"));
            text.replace(lineBreak, QStringLiteral("\n"));
            text.replace(QLatin1String("&nbsp;"), QStringLiteral(" "));
            return text;
        }

        // Text typed into a field, ready to go inside the item block: HTML
        // turned into markdown, "(x2)" into "(×2)", and lines that mean
        // something to Scribe inside a block (a lone "-", "|", ")", "%",
        // "/" or "=", or a leading ";") escaped so they show as typed.
        QString fieldText(const QString &raw)
        {
            static const QRegularExpression times(QStringLiteral(R"(\(x(\d+)\))"));
            QString text = htmlToMarkdown(raw);
            text.replace(times, QStringLiteral("(×\\1)"));
            // The site's _italic_; Qt's markdown would underline it.
            static const QRegularExpression underscores(
                QStringLiteral(R"((?<![\w_])_([^_\n]+?)_(?![\w_]))"));
            text.replace(underscores, QStringLiteral("*\\1*"));
            text.remove(QLatin1Char('\r'));

            static const QSet<QString> special{
                QStringLiteral("-"), QStringLiteral("|"), QStringLiteral(")"),
                QStringLiteral("%"), QStringLiteral("/"), QStringLiteral("=")};
            QStringList lines = text.split(QLatin1Char('\n'));
            for (QString &line : lines)
            {
                const QString trimmed = line.trimmed();
                if (special.contains(trimmed) || line.startsWith(QLatin1Char(';')))
                {
                    const qsizetype at = line.indexOf(trimmed.at(0));
                    line.insert(at, QLatin1Char('\\'));
                }
            }
            return lines.join(QLatin1Char('\n')).trimmed();
        }

        // A Resistances list from fieldText(): "ghost touch" in an exception
        // names a property rune, so it's italic as Paizo prints it ("all
        // damage 5 (except force, *ghost touch*, spirit, or vitality)"),
        // unless it already is.
        QString resistanceText(const QString &raw)
        {
            static const QRegularExpression ghostTouch(
                QStringLiteral(R"((?<![*\w])(ghost touch)(?![*\w]))"),
                QRegularExpression::CaseInsensitiveOption);
            QString text = fieldText(raw).simplified();
            text.replace(ghostTouch, QStringLiteral("*\\1*"));
            return text;
        }

        // Text that goes outside any block (the description): like
        // fieldText(), and also lines that would mean something to Scribe
        // there are escaped: a block opening ("info(", "left(", ...), a
        // content definition ("name {"), a reference ("{{name}}") or a
        // directive alone on its line ("pagenumbers", "reset").
        QString bodyText(const QString &raw)
        {
            static const QRegularExpression blockOpen(
                QStringLiteral(R"(^(\s*(?:watermark|title|head|css|fonts|sticky|info|rules|)"
                               R"(note|math|left|right|item)\s*)\()"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression definition(
                QStringLiteral(R"(^(\s*[A-Za-z][A-Za-z0-9_-]*\s*)\{\s*$)"));
            static const QRegularExpression reference(QStringLiteral(R"(^(\s*)\{\{)"));
            static const QRegularExpression directive(
                QStringLiteral(R"(^\s*(pagenumbers|reset)\s*$)"),
                QRegularExpression::CaseInsensitiveOption);
            QStringList lines = fieldText(raw).split(QLatin1Char('\n'));
            for (QString &line : lines)
            {
                line.replace(blockOpen, QStringLiteral("\\1\\("));
                line.replace(definition, QStringLiteral("\\1\\{"));
                line.replace(reference, QStringLiteral("\\1\\{{"));
                if (directive.match(line).hasMatch())
                {
                    line += QChar(0x200B); // no longer the directive alone
                }
            }
            return lines.join(QLatin1Char('\n'));
        }

        // An ability's description: like fieldText(), with the words Paizo
        // sets in bold ("Trigger", "Critical Success", ...) made bold, as
        // the website does. Its lines stay line breaks inside the entry, so
        // the degrees of success under a saving throw are indented under
        // the ability as Paizo prints them (the user, 2026-10-26).
        QString descriptionText(const QString &raw)
        {
            static const QRegularExpression keywords(QStringLiteral(
                R"((?<![\w*])(Saving Throw|Maximum Duration|Stage [1-6]|Trigger|Effect|)"
                R"(Critical Success|Success|Critical Failure|Failure|Requirements|Requirement|)"
                R"(Frequency|Activate|Heightened \(\+\d+\)|\((?:1st|2nd|3rd|[4-9]th|10th)\))(?![\w*]))"));
            QString text = fieldText(raw);
            text.replace(keywords, QStringLiteral("**\\1**"));
            return text;
        }

        // A note in parentheses after a value, if there is one.
        QString note(const QString &text)
        {
            const QString trimmed = text.trimmed();
            return trimmed.isEmpty() ? QString()
                                     : QStringLiteral(" (%1)").arg(fieldText(trimmed));
        }

        const Stat &statOf(const Monster &m, const char *key)
        {
            static const Stat empty;
            const Stat *stat = m.findStat(QString::fromLatin1(key));
            return stat ? *stat : empty;
        }

        // What the statblock shows for a statistic: its value, or when that's
        // empty its benchmark's hint (core statistics, see usesHint(); spell
        // DC and attack while the monster has spells), else `fallback`.
        QString shown(const Monster &m, const char *key, const QString &fallback,
                      bool hinted = false)
        {
            const QString statKey = QString::fromLatin1(key);
            const Stat *stat = m.findStat(statKey);
            if (stat && !stat->value.isEmpty())
            {
                return stat->value.text().trimmed();
            }
            if (hinted || usesHint(m, statKey))
            {
                if (const Hint h = statHint(m, statKey); h.valid && h.value)
                {
                    return QString::number(*h.value);
                }
            }
            return fallback;
        }

        bool hasSpells(const Monster &m)
        {
            return std::any_of(m.spells.begin(), m.spells.end(),
                               [](const QString &s) { return !s.trimmed().isEmpty(); }) ||
                   !m.constant.trimmed().isEmpty();
        }

        QString actionIcon(const QString &actions)
        {
            const QString key = actions.trimmed().toLower();
            if (key == QLatin1String("one")) return QStringLiteral(":a:");
            if (key == QLatin1String("two")) return QStringLiteral(":aa:");
            if (key == QLatin1String("three")) return QStringLiteral(":aaa:");
            if (key == QLatin1String("one to two")) return QStringLiteral(":a: to :aa:");
            if (key == QLatin1String("one to three")) return QStringLiteral(":a: to :aaa:");
            if (key == QLatin1String("two to three")) return QStringLiteral(":aa: to :aaa:");
            if (key == QLatin1String("free")) return QStringLiteral(":f:");
            if (key == QLatin1String("reaction")) return QStringLiteral(":r:");
            if (key.isEmpty() || key == QLatin1String("none")) return {};
            return actions.trimmed(); // "1 minute", "10 minutes": as text
        }

        // "**Name** :aa: (traits) Description".
        // `bullet`: one of the entries listed under another (Mythic Power's
        // actions), as Paizo prints them: "* *Name* :a: ...".
        QString specialLine(const Special &special, bool bullet = false)
        {
            QString line = bullet ? QStringLiteral("* *%1*").arg(fieldText(special.name).simplified())
                                  : QStringLiteral("**%1**").arg(fieldText(special.name).simplified());
            const QString icon = actionIcon(special.actions);
            if (!icon.isEmpty())
            {
                line += QLatin1Char(' ') + icon;
            }
            const QString description = descriptionText(special.description);
            // "(concentrate) **Cost** ...", "(move) **Trigger** ...": no
            // semicolon after the traits, as the Remaster books print it
            // (War of Immortals pg. 168-174).
            if (!special.traits.trimmed().isEmpty())
            {
                line += QStringLiteral(" (%1)").arg(fieldText(special.traits).simplified());
            }
            if (!description.isEmpty())
            {
                line += QLatin1Char(' ') + description;
            }
            return line;
        }

        // `hazard`: no action icon unless the Strike has one set (GM Core
        // prints most hazard Strikes without); a creature's default is one
        // action.
        QString strikeLine(const Strike &strike, bool hazard = false)
        {
            const QString type = strike.type.trimmed().isEmpty()
                                     ? QStringLiteral("Melee")
                                     : titleCase(strike.type.trimmed());
            // The name upright, as Paizo prints it; italics only where typed
            // (a magical weapon: "*+1 striking longsword*" or the site's
            // <i>...</i>).
            QString icon = actionIcon(strike.actions);
            if (strike.actions.trimmed().isEmpty() && !hazard)
            {
                icon = QStringLiteral(":a:");
            }
            QString line = QStringLiteral("**%1**%2 %3 %4")
                               .arg(type, icon.isEmpty() ? QString() : QLatin1Char(' ') + icon,
                                    fieldText(strike.name).simplified(),
                                    withSign(strike.attack.isEmpty()
                                                 ? QStringLiteral("0")
                                                 : strike.attack.text().trimmed()));
            if (!strike.traits.trimmed().isEmpty())
            {
                line += QStringLiteral(" (%1)").arg(fieldText(strike.traits).simplified());
            }
            line += QStringLiteral(", **Damage** %1").arg(fieldText(strike.damage).simplified());
            return line;
        }

        // A list of spells as Paizo sets it: each name in italics, notes in
        // parentheses upright ("*translocate* (at will), *phase bolt*").
        // Italics typed around single spells (the site writes <i>...</i>)
        // are dropped first, so they don't double up.
        QString spellList(const QString &raw)
        {
            QString list = fieldText(raw).simplified();
            list.remove(QLatin1Char('*'));

            // Split at commas outside parentheses.
            QStringList items;
            QString item;
            int depth = 0;
            for (const QChar c : list)
            {
                if (c == QLatin1Char('('))
                {
                    ++depth;
                }
                else if (c == QLatin1Char(')') && depth > 0)
                {
                    --depth;
                }
                if (c == QLatin1Char(',') && depth == 0)
                {
                    items << item.trimmed();
                    item.clear();
                    continue;
                }
                item += c;
            }
            items << item.trimmed();

            QStringList out;
            for (QString entry : items)
            {
                if (entry.isEmpty())
                {
                    continue;
                }
                // A note before the first spell stays upright too:
                // "(4 slots) *disintegrate*".
                QString lead;
                if (entry.startsWith(QLatin1Char('(')))
                {
                    const qsizetype close = entry.indexOf(QLatin1Char(')'));
                    if (close > 0)
                    {
                        lead = entry.left(close + 1) + QLatin1Char(' ');
                        entry = entry.mid(close + 1).trimmed();
                    }
                }
                if (entry.isEmpty())
                {
                    out << lead.trimmed();
                    continue;
                }
                const qsizetype paren = entry.indexOf(QLatin1Char('('));
                const QString name = (paren < 0 ? entry : entry.left(paren)).trimmed();
                const QString rest = paren < 0 ? QString() : entry.mid(paren);
                out << lead + (name.isEmpty() ? rest
                                              : QStringLiteral("*%1*").arg(name) +
                                                    (rest.isEmpty() ? QString() : QLatin1Char(' ') + rest));
            }
            return out.join(QStringLiteral(", "));
        }

        QString rankLabel(int index)
        {
            return ordinal(kSpellRanks - 1 - index); // index 0 is 10th rank
        }

        // "**Arcane Innate Spells** DC 29, attack +21; **5th** *fireball*;
        // **Cantrips (6th)** *light*; **Constant (3rd)** *see the unseen*".
        QString spellLine(const QString &title, const Value &focusPoints, const Stat &dc,
                          const QString &dcValue, const Stat &attack,
                          const QString &attackValue, const QString &extraNote,
                          const std::array<QString, kSpellRanks> &spells,
                          const Value &cantripLevel, const QString &constant, int level)
        {
            QString line = QStringLiteral("**%1**").arg(title);
            QStringList head;
            if (const int points = focusPoints.number().value_or(0); points > 0)
            {
                head << (points == 1 ? QStringLiteral("1 Focus Point")
                                     : QStringLiteral("%1 Focus Points").arg(points));
            }
            head << QStringLiteral("DC %1%2").arg(dcValue.isEmpty() ? QStringLiteral("0") : dcValue,
                                                  note(dc.note));
            if (!attackValue.isEmpty())
            {
                head << QStringLiteral("attack %1%2").arg(withSign(attackValue), note(attack.note));
            }
            line += QLatin1Char(' ') + head.join(QStringLiteral(", "));
            if (!extraNote.trimmed().isEmpty())
            {
                line += QLatin1Char(' ') + fieldText(extraNote).simplified();
            }

            QStringList ranks;
            for (int i = 0; i < kSpellRanks; ++i)
            {
                const QString list = spellList(spells[static_cast<size_t>(i)]);
                if (list.isEmpty())
                {
                    continue;
                }
                QString label;
                if (i == kCantrips)
                {
                    const int rank = cantripLevel.number().value_or(
                        static_cast<int>(std::ceil(std::max(level, 2) / 2.0)));
                    label = QStringLiteral("Cantrips (%1)").arg(ordinal(rank));
                }
                else
                {
                    label = rankLabel(i);
                }
                ranks << QStringLiteral("**%1** %2").arg(label, list);
            }
            if (!constant.trimmed().isEmpty())
            {
                ranks << QStringLiteral("**Constant** %1").arg(spellList(constant));
            }
            if (!ranks.isEmpty())
            {
                line += QStringLiteral("; ") + ranks.join(QStringLiteral("; "));
            }
            return line;
        }

        // The trait row: rarity, alignment, size, creature type, then the
        // other traits, each once.
        QString traitRow(const Monster &m)
        {
            QStringList traits;
            for (const QString &trait : m.traits.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                const QString t = fieldText(trait).simplified();
                if (!t.isEmpty() && t != QLatin1String("-"))
                {
                    traits << t;
                }
            }
            QStringList row;
            QSet<QString> seen;
            auto add = [&](const QString &trait) {
                if (!trait.isEmpty() && trait != QLatin1String("-") &&
                    !seen.contains(trait.toLower()))
                {
                    seen.insert(trait.toLower());
                    row << trait;
                }
            };
            for (const char *rarity : {"unique", "rare", "uncommon"})
            {
                for (const QString &t : traits)
                {
                    if (t.compare(QLatin1String(rarity), Qt::CaseInsensitive) == 0)
                    {
                        add(titleCase(t.toLower()));
                    }
                }
            }
            if (!m.isHazard())
            {
                add(m.alignment.trimmed().toUpper());
                add(titleCase(m.size.trimmed()));
            }
            const qsizetype others = row.size(); // the type and the other traits
            if (!m.isHazard())
            {
                add(titleCase(m.type.trimmed()));
            }
            else if (m.hazard.complex)
            {
                // Hazards with initiative and a routine have the complex
                // trait (GM Core). Simple is a trait only where typed (the
                // user: Paizo is inconsistent about it).
                add(QStringLiteral("Complex"));
            }
            for (const QString &t : traits)
            {
                add(t);
            }
            // The type and the other traits in alphabetical order, as Paizo
            // prints them ("Earth Mythic Undead Unholy"), whatever order
            // they were typed in. Those adjustments add ("mythic", "fire")
            // are capitalized like the rest.
            QStringList added{QStringLiteral("mythic")};
            added += m.addedTraits;
            for (qsizetype at = others; at < row.size(); ++at)
            {
                if (added.contains(row[at], Qt::CaseInsensitive))
                {
                    row[at] = titleCase(row[at].toLower());
                }
            }
            std::stable_sort(row.begin() + others, row.end(), [](const QString &a, const QString &b) {
                return a.compare(b, Qt::CaseInsensitive) < 0;
            });
            return row.join(QLatin1Char(','));
        }
        // The item block: name, label and level, traits, then the entries,
        // each its own paragraph; "-" is a rule.
        QStringList itemBlock(const Monster &m, const QString &label, const QStringList &entries)
        {
            const QString shownLevel = m.showlevel.isEmpty() ? m.level.text().trimmed()
                                                             : m.showlevel.text().trimmed();
            QStringList lines;
            lines << QStringLiteral("item(")
                  << QStringLiteral("# ") + fieldText(m.name).simplified()
                  << QStringLiteral("## %1 %2").arg(label, shownLevel) << QStringLiteral("-");
            if (const QString traits = traitRow(m); !traits.isEmpty())
            {
                lines << QLatin1Char(';') + traits;
            }
            // Rules sit on their own line; other entries are separated by a
            // blank line, so each is its own paragraph.
            bool afterRule = true;
            for (const QString &entry : entries)
            {
                const bool rule = entry == QLatin1String("-");
                if (!afterRule && !rule)
                {
                    lines << QString();
                }
                lines << entry;
                afterRule = rule;
            }
            lines << QStringLiteral(")");
            return lines;
        }

        // "trapdoor" -> "Trapdoor": a part's name leading its labels.
        QString capitalized(const QString &text)
        {
            QString out = text.trimmed();
            if (!out.isEmpty())
            {
                out[0] = out[0].toUpper();
            }
            return out;
        }

        // Half the HP, rounded down (the Broken Threshold is "usually half
        // the hazard's HP", GM Core); empty when the HP isn't a number.
        QString halfHp(const QString &hp)
        {
            bool ok = false;
            const int value = hp.trimmed().toInt(&ok);
            return ok ? QString::number(static_cast<int>(std::floor(value / 2.0))) : QString();
        }

        // A hazard's statblock entries, as GM Core prints them (plan 13):
        // Complexity, Stealth, Description; a rule; Disable, AC and saves,
        // Hardness and HP, the reactions; a rule and the Routine, Speed,
        // Strikes and actions (complex hazards, or any with a Routine); a
        // rule and Reset. Abilities keep the order they're listed in.
        QStringList hazardEntries(const Monster &m)
        {
            const Hazard &hz = m.hazard;
            QStringList entries;
            auto specialsOf = [&](const char *type) {
                for (const Special &special : m.specials)
                {
                    if (special.type.compare(QLatin1String(type), Qt::CaseInsensitive) == 0)
                    {
                        entries << specialLine(special);
                    }
                }
            };

            if (!m.info.trimmed().isEmpty())
            {
                entries << fieldText(m.info);
            }
            entries << QStringLiteral("**Complexity** ") +
                           (hz.complex ? QStringLiteral("Complex") : QStringLiteral("Simple"));

            // "DC 20 (trained)" for a simple hazard, "+10 (trained)" for a
            // complex one; then the note: one starting in lower case or "("
            // follows after a space ("or DC 31 (master) to notice the
            // control panel", "(or -10 ..."), one starting with "," or ";"
            // as typed, anything else after "; " ("DC 22 (expert) to notice
            // the water spouts").
            const QString stealth = shown(m, "stealth", QString());
            if (!stealth.isEmpty())
            {
                QString line = QStringLiteral("**Stealth** ") +
                               (hz.complex ? withSign(stealth) : QStringLiteral("DC ") + stealth);
                const QString rank = fieldText(statOf(m, "minprof").value.text()).simplified();
                if (!rank.isEmpty())
                {
                    line += QStringLiteral(" (%1)").arg(rank);
                }
                const QString extra = fieldText(statOf(m, "stealth").note).simplified();
                if (!extra.isEmpty())
                {
                    const QChar first = extra.at(0);
                    if (first == QLatin1Char(',') || first == QLatin1Char(';'))
                    {
                        line += extra;
                    }
                    else if (first.isLower() || first == QLatin1Char('(') || first == QLatin1Char('*'))
                    {
                        line += QLatin1Char(' ') + extra;
                    }
                    else
                    {
                        line += QStringLiteral("; ") + extra;
                    }
                }
                entries << line;
            }
            if (!m.description.trimmed().isEmpty())
            {
                entries << QStringLiteral("**Description** ") + fieldText(m.description);
            }
            specialsOf("general");
            entries << QStringLiteral("-");

            // Disabling and defenses.
            if (!hz.disable.trimmed().isEmpty())
            {
                entries << QStringLiteral("**Disable** ") + fieldText(hz.disable);
            }
            QStringList saves;
            for (const auto &[label, key] : {std::pair{"Fort", "fortitude"}, std::pair{"Ref", "reflex"},
                                             std::pair{"Will", "will"}})
            {
                if (hz.defenses.contains(QLatin1String(key)))
                {
                    saves << QStringLiteral("**%1** %2%3").arg(QLatin1String(label),
                                                               withSign(shown(m, key, QStringLiteral("0"))),
                                                               note(statOf(m, key).note));
                }
            }
            QStringList defense;
            if (hz.defenses.contains(QLatin1String("ac")))
            {
                const QString acLead = hz.acLabel.trimmed().isEmpty()
                                           ? QString()
                                           : capitalized(fieldText(hz.acLabel).simplified()) + QLatin1Char(' ');
                defense << QStringLiteral("**%1AC** %2%3").arg(acLead, shown(m, "ac", QStringLiteral("0")),
                                                               note(statOf(m, "ac").note));
            }
            if (!saves.isEmpty())
            {
                defense << saves.join(QStringLiteral(", "));
            }
            if (!defense.isEmpty() && !m.savenote.trimmed().isEmpty())
            {
                defense << fieldText(m.savenote).simplified();
            }
            if (!defense.isEmpty())
            {
                entries << defense.join(QStringLiteral("; "));
            }
            if (hz.defenses.contains(QLatin1String("hp")))
            {
                // "**Hardness** 14; **HP** 56 (BT 28); **Immunities** ...";
                // with parts each is labelled by its name ("**Trapdoor
                // Hardness** 15; **Trapdoor HP** 60 (BT 30); **Spout
                // Hardness** 8; ..."). An HP note typed in parentheses goes in
                // the label ("**HP (per mannequin)** 70 (BT 35)"), any other
                // after the BT ("**Sensor HP** 7 (BT 3) each"). No BT when
                // there's none (`noBt`: "**HP** 20; **Immunities** ...").
                QStringList items;
                auto addPart = [&](const QString &name, const QString &hardness, const QString &hp,
                                   const QString &hpNote, const QString &bt, bool noBt) {
                    const QString lead = name.trimmed().isEmpty()
                                             ? QString()
                                             : capitalized(fieldText(name).simplified()) + QLatin1Char(' ');
                    if (!hardness.trimmed().isEmpty())
                    {
                        items << QStringLiteral("**%1Hardness** %2").arg(lead, fieldText(hardness).simplified());
                    }
                    if (!hp.trimmed().isEmpty())
                    {
                        const QString threshold = noBt                    ? QString()
                                                  : bt.trimmed().isEmpty() ? halfHp(hp)
                                                                           : fieldText(bt).simplified();
                        const QString noteText = fieldText(hpNote).simplified();
                        const bool inLabel = noteText.startsWith(QLatin1Char('('));
                        items << QStringLiteral("**%1HP%2** %3%4%5")
                                     .arg(lead, inLabel ? QLatin1Char(' ') + noteText : QString(),
                                          fieldText(hp).simplified(),
                                          threshold.isEmpty() ? QString() : QStringLiteral(" (BT %1)").arg(threshold),
                                          inLabel || noteText.isEmpty() ? QString() : QLatin1Char(' ') + noteText);
                    }
                };
                auto addLists = [&](const QString &immunities, const QString &weaknesses,
                                    const QString &resistances) {
                    // GM Core's hazard format: Immunities, Weaknesses,
                    // Resistances.
                    for (const auto &[label, list] : {std::pair{"Immunities", immunities},
                                                      std::pair{"Weaknesses", weaknesses},
                                                      std::pair{"Resistances", resistances}})
                    {
                        if (!list.trimmed().isEmpty())
                        {
                            items << QStringLiteral("**%1** %2").arg(
                                QLatin1String(label), qstrcmp(label, "Resistances") == 0
                                                          ? resistanceText(list)
                                                          : fieldText(list).simplified());
                        }
                    }
                };
                addPart(m.mainPart, m.hardness, shown(m, "hp", QString()),
                        statOf(m, "hp").note, hz.bt, hz.noBt);
                for (const HpPart &part : m.hpParts)
                {
                    addPart(part.name, part.hardness, part.hp, part.note, part.bt, part.noBt);
                    addLists(part.immunities, part.weaknesses, part.resistances);
                }
                addLists(statOf(m, "immunity").value.text(), statOf(m, "weakness").value.text(),
                         statOf(m, "resistance").value.text());
                if (!items.isEmpty())
                {
                    entries << items.join(QStringLiteral("; "));
                }
            }
            specialsOf("defense");

            // What it does on its turns. A simple hazard's Strikes and
            // actions follow its reaction with no rule (GM Core's Spear
            // Launcher).
            QStringList offense;
            if (!hz.routine.trimmed().isEmpty() || !hz.routineActions.trimmed().isEmpty())
            {
                QStringList head;
                const QString count = hz.routineActions.trimmed();
                bool number = false;
                const int n = count.toInt(&number);
                if (number)
                {
                    head << (n == 1 ? QStringLiteral("1 action") : QStringLiteral("%1 actions").arg(n));
                }
                else if (!count.isEmpty())
                {
                    head << fieldText(count).simplified();
                }
                if (!hz.routineTraits.trimmed().isEmpty())
                {
                    head << fieldText(hz.routineTraits).simplified();
                }
                QString line = QStringLiteral("**Routine**");
                if (!head.isEmpty())
                {
                    line += QStringLiteral(" (%1)").arg(head.join(QStringLiteral("; ")));
                }
                if (const QString text = descriptionText(hz.routine); !text.isEmpty())
                {
                    line += QLatin1Char(' ') + text;
                }
                offense << line;
            }
            if (!m.speed.trimmed().isEmpty())
            {
                offense << QStringLiteral("**Speed** ") + fieldText(m.speed).simplified();
            }
            for (const Strike &strike : m.strikes)
            {
                offense << strikeLine(strike, true);
            }
            for (const Special &special : m.specials)
            {
                if (special.type.compare(QLatin1String("offense"), Qt::CaseInsensitive) == 0)
                {
                    offense << specialLine(special);
                }
            }
            if (!offense.isEmpty())
            {
                if (hz.complex || !hz.routine.trimmed().isEmpty() || !hz.routineActions.trimmed().isEmpty())
                {
                    entries << QStringLiteral("-");
                }
                entries << offense;
            }
            if (!hz.reset.trimmed().isEmpty())
            {
                entries << QStringLiteral("-") << QStringLiteral("**Reset** ") + fieldText(hz.reset);
            }
            // No rule left hanging at the end (a hazard with nothing after
            // its description).
            while (!entries.isEmpty() && entries.last() == QLatin1String("-"))
            {
                entries.removeLast();
            }
            return entries;
        }
    } // namespace

    QString withSign(const QString &value)
    {
        const QString trimmed = value.trimmed();
        bool ok = false;
        const int number = trimmed.toInt(&ok);
        if (!ok || trimmed.startsWith(QLatin1Char('+')) || trimmed.startsWith(QLatin1Char('-')))
        {
            return trimmed;
        }
        return number >= 0 ? QStringLiteral("+%1").arg(number) : QString::number(number);
    }

    QString expandReferences(const Monster &monster, const QString &text)
    {
        if (!text.contains(QLatin1Char('%')) && !text.contains(QLatin1Char('[')))
        {
            return text;
        }
        // The website's short names.
        static const QHash<QString, QString> aliases{
            {"dc", "spelldc"},     {"fort", "fortitude"},  {"ref", "reflex"},
            {"str", "strength"},   {"dex", "dexterity"},   {"con", "constitution"},
            {"int", "intelligence"}, {"wis", "wisdom"},    {"cha", "charisma"}};
        // "\%", an escaped bracket ("\[ac]" stays as it is: markdown shows
        // "[ac]"), "%", or a reference not followed by "(" (a markdown link).
        static const QRegularExpression token(
            QStringLiteral(R"(\\%|\\\[|%|\[([A-Za-z][A-Za-z ]*)\](?!\())"));

        QString out;
        qsizetype at = 0;
        for (auto it = token.globalMatch(text); it.hasNext();)
        {
            const QRegularExpressionMatch match = it.next();
            out += QStringView(text).mid(at, match.capturedStart() - at);
            at = match.capturedEnd();
            const QString found = match.captured();
            if (found == QLatin1String("\\%"))
            {
                out += QLatin1Char('%');
                continue;
            }
            if (found == QLatin1String("\\["))
            {
                out += found;
                continue;
            }
            if (found == QLatin1String("%"))
            {
                out += monster.name;
                continue;
            }
            QString key = match.captured(1).toLower().remove(QLatin1Char(' '));
            key = aliases.value(key, key);
            const Stat *stat = monster.findStat(key);
            if (!stat)
            {
                out += found; // unknown: kept as typed
                continue;
            }
            QString value = stat->value.text().trimmed();
            if (value.isEmpty())
            {
                if (const Hint h = statHint(monster, key); h.valid && h.value)
                {
                    value = QString::number(*h.value);
                }
            }
            // Bonuses with their sign, as the statblock shows them; AC, DCs
            // and HP without.
            const std::optional<Table> table = tableFor(key);
            const bool withPlus = table && (*table == Table::Attribute || *table == Table::Perception ||
                                            *table == Table::Skill || *table == Table::Save ||
                                            *table == Table::SpellAttack);
            const bool hazardDc = monster.isHazard() && key == QLatin1String("stealth") &&
                                  !monster.hazard.complex;
            out += withPlus && !hazardDc && !value.isEmpty() ? withSign(value) : value;
        }
        out += QStringView(text).mid(at);
        return out;
    }

    QString toScribe(const Monster &input)
    {
        // The applied adjustments first, then the references and % (in the
        // fields the website works them out in) from the adjusted numbers;
        // % is the name with the adjustments' prefixes in lower case
        // ("experimental flame drake"), as their abilities say it.
        Monster m = adjusted(input);
        Monster references = m;
        references.name = spokenName(m.name, input.name);
        auto expand = [&references](QString &field) { field = expandReferences(references, field); };
        expand(m.description);
        expand(m.items);
        expand(m.languages);
        expand(m.constant);
        for (QString &spells : m.spells)
        {
            expand(spells);
        }
        for (SpellList &list : m.morespells)
        {
            expand(list.constant);
            for (QString &spells : list.spells)
            {
                expand(spells);
            }
        }
        for (Strike &strike : m.strikes)
        {
            expand(strike.name);
        }
        for (Special &special : m.specials)
        {
            expand(special.description);
        }
        if (m.isHazard())
        {
            for (QString *field : {&m.hazard.disable, &m.hazard.routine, &m.hazard.reset})
            {
                expand(*field);
            }
            // The description is in the statblock.
            const QString label = m.creature.trimmed().isEmpty() ? QStringLiteral("Hazard")
                                                                 : fieldText(m.creature).simplified();
            return itemBlock(m, label, hazardEntries(m)).join(QLatin1Char('\n')) + QLatin1Char('\n');
        }

        const int level = m.level.number().value_or(0);
        const QString label = m.creature.trimmed().isEmpty() ? QStringLiteral("Creature")
                                                             : fieldText(m.creature).simplified();

        // Each entry is its own paragraph; "-" is a rule.
        QStringList entries;
        // A section's abilities in alphabetical order, whatever order they're
        // listed in; Mythic Power first, and in defense the reactions after
        // the automatic abilities, as Paizo prints them (War of
        // Immortals' Mythic Lich: Frightful Presence, then Counterspell).
        auto specialsOf = [&](const char *type) {
            std::vector<const Special *> section;
            for (const Special &special : m.specials)
            {
                if (special.type.compare(QLatin1String(type), Qt::CaseInsensitive) == 0 &&
                    special.under.isEmpty())
                {
                    section.push_back(&special);
                }
            }
            const bool defense = QLatin1String(type) == QLatin1String("defense");
            auto rank = [defense](const Special *s) {
                if (s->generated == QLatin1String("mythic-power"))
                {
                    return 0;
                }
                return defense && s->actions == QLatin1String("reaction") ? 2 : 1;
            };
            std::stable_sort(section.begin(), section.end(), [&](const Special *a, const Special *b) {
                if (rank(a) != rank(b))
                {
                    return rank(a) < rank(b);
                }
                return fieldText(a->name).simplified().compare(fieldText(b->name).simplified(),
                                                               Qt::CaseInsensitive) < 0;
            });
            for (const Special *entry : section)
            {
                const Special &special = *entry;
                // The entries listed under this one follow it as bullets,
                // wherever they are in the list.
                QStringList lines{specialLine(special)};
                for (const Special &sub : m.specials)
                {
                    if (!special.generated.isEmpty() && sub.under == special.generated)
                    {
                        lines << specialLine(sub, true);
                    }
                }
                entries << lines.join(QLatin1Char('\n'));
            }
        };

        // Identity and senses.
        if (!m.info.trimmed().isEmpty())
        {
            entries << fieldText(m.info);
        }
        // Recall Knowledge, where Archives of Nethys prints it: above
        // Perception (or with the description, or not at all).
        const QString recall = QStringLiteral("**Recall Knowledge** ") + recallKnowledgeText(m);
        if (m.recallKnowledge.isEmpty())
        {
            entries << recall;
        }
        const Stat &perception = statOf(m, "perception");
        const QString perceptionValue = shown(m, "perception", QString());
        if (!perceptionValue.isEmpty())
        {
            QString line = QStringLiteral("**Perception** %1").arg(withSign(perceptionValue));
            if (!perception.note.trimmed().isEmpty())
            {
                line += QStringLiteral("; ") + fieldText(perception.note).simplified();
            }
            entries << line;
        }
        if (!m.languages.trimmed().isEmpty())
        {
            entries << QStringLiteral("**Languages** ") + fieldText(m.languages).simplified();
        }
        // Lores sorted in among the other skills by name, as Paizo prints
        // them.
        std::vector<std::pair<QString, QString>> skillEntries; // name, entry
        auto addSkill = [&](const QString &name, const Stat &stat) {
            if (!stat.value.isEmpty())
            {
                const QString title = titleCase(name);
                skillEntries.emplace_back(title, QStringLiteral("%1 %2%3").arg(
                                                     title, withSign(stat.value.text()), note(stat.note)));
            }
        };
        for (const char *key : kSkills)
        {
            addSkill(QString::fromLatin1(key), statOf(m, key));
        }
        for (const QString &key : m.loreKeys())
        {
            const Stat &lore = *m.findStat(key);
            addSkill(lore.name.trimmed().isEmpty() ? QStringLiteral("lore") : fieldText(lore.name).simplified(),
                     lore);
        }
        std::stable_sort(skillEntries.begin(), skillEntries.end(), [](const auto &a, const auto &b) {
            return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
        });
        QStringList skills;
        for (const auto &entry : skillEntries)
        {
            skills << entry.second;
        }
        if (!skills.isEmpty())
        {
            entries << QStringLiteral("**Skills** ") + skills.join(QStringLiteral(", "));
        }
        QStringList attributes;
        const std::array<const char *, 6> shortNames{"Str", "Dex", "Con", "Int", "Wis", "Cha"};
        for (size_t i = 0; i < kAbilities.size(); ++i)
        {
            const Stat &stat = statOf(m, kAbilities[i]);
            attributes << QStringLiteral("**%1** %2%3").arg(
                QLatin1String(shortNames[i]), withSign(shown(m, kAbilities[i], QStringLiteral("0"))),
                note(stat.note));
        }
        entries << attributes.join(QStringLiteral(", "));
        if (!m.items.trimmed().isEmpty())
        {
            entries << QStringLiteral("**Items** ") + fieldText(m.items).simplified();
        }
        // After Items, as Paizo prints them.
        specialsOf("general");
        entries << QStringLiteral("-");

        // Defense.
        auto save = [&](const char *label, const char *key) {
            const Stat &stat = statOf(m, key);
            return QStringLiteral("**%1** %2%3").arg(QLatin1String(label),
                                                     withSign(shown(m, key, QStringLiteral("0"))),
                                                     note(stat.note));
        };
        const Stat &ac = statOf(m, "ac");
        QString defense = QStringLiteral("**AC** %1%2; %3, %4, %5")
                              .arg(shown(m, "ac", QStringLiteral("0")), note(ac.note),
                                   save("Fort", "fortitude"), save("Ref", "reflex"),
                                   save("Will", "will"));
        if (!m.savenote.trimmed().isEmpty())
        {
            defense += QStringLiteral("; ") + fieldText(m.savenote).simplified();
        }
        entries << defense;
        const Stat &hp = statOf(m, "hp");
        // With parts, the creature's own line is labelled ("(body, head
        // regrowth)") and each part follows with its own: "**HP** 50 (head);
        // **Immunities** area damage; ...", as in the user's Scribe documents.
        auto labelled = [](const QString &label, const QString &text) {
            QStringList parts;
            for (const QString &p : {label.trimmed(), text.trimmed()})
            {
                if (!p.isEmpty())
                {
                    parts << p;
                }
            }
            return note(parts.join(QStringLiteral(", ")));
        };
        // Templates' "fast healing 7", "void healing" join the note in its
        // parentheses, as Monster Core prints them ("HP 65 (coffin
        // restoration, fast healing 7, void healing)").
        QStringList inside;
        if (!hp.note.trimmed().isEmpty())
        {
            inside << hp.note.trimmed();
        }
        inside << m.hpAbilities;
        QString hpLine = QStringLiteral("**HP** %1%2").arg(
            shown(m, "hp", QStringLiteral("1")),
            labelled(m.hpParts.empty() ? QString() : m.mainPart, inside.join(QStringLiteral(", "))));
        // "**HP** 125, regeneration 20 (deactivated by electricity or fire);
        // **Weaknesses** ...", as Monster Core prints a troll: after
        // the note, not inside it.
        if (!m.regeneration.trimmed().isEmpty())
        {
            hpLine += QStringLiteral(", regeneration ") + fieldText(m.regeneration).simplified();
        }
        // "**HP** 20; **Hardness** 9; **Immunities** ...", as Monster Core
        // prints an animated armor.
        if (!m.hardness.trimmed().isEmpty())
        {
            hpLine += QStringLiteral("; **Hardness** ") + fieldText(m.hardness).simplified();
        }
        for (const auto &[label, key] : {std::pair{"Immunities", "immunity"},
                                         std::pair{"Resistances", "resistance"},
                                         std::pair{"Weaknesses", "weakness"}})
        {
            const Stat &stat = statOf(m, key);
            if (!stat.value.isEmpty())
            {
                const QString text = qstrcmp(key, "resistance") == 0
                                         ? resistanceText(stat.value.text())
                                         : fieldText(stat.value.text()).simplified();
                hpLine += QStringLiteral("; **%1** %2").arg(QLatin1String(label), text);
            }
        }
        entries << hpLine;
        for (const HpPart &part : m.hpParts)
        {
            QString line = QStringLiteral("**HP** %1%2").arg(fieldText(part.hp).simplified(),
                                                             labelled(part.name, part.note));
            for (const auto &[label, list] : {std::pair{"Immunities", part.immunities},
                                              std::pair{"Resistances", part.resistances},
                                              std::pair{"Weaknesses", part.weaknesses}})
            {
                if (!list.trimmed().isEmpty())
                {
                    const QString text = qstrcmp(label, "Resistances") == 0
                                             ? resistanceText(list)
                                             : fieldText(list).simplified();
                    line += QStringLiteral("; **%1** %2").arg(QLatin1String(label), text);
                }
            }
            entries << line;
        }
        specialsOf("defense");
        entries << QStringLiteral("-");

        // Offense.
        entries << QStringLiteral("**Speed** ") +
                       (m.speed.trimmed().isEmpty() ? QStringLiteral("5 feet")
                                                    : fieldText(m.speed).simplified());
        for (const Strike &strike : m.strikes)
        {
            entries << strikeLine(strike);
        }
        const Stat &spellDc = statOf(m, "spelldc");
        const Stat &spellAttack = statOf(m, "spellattack");
        // Spellcasting: shown once it has a DC or attack, or spells (then
        // with the benchmarks' DC and attack where those are empty). A DC
        // set without an attack is shown alone.
        const bool spells = hasSpells(m);
        if (spells || !spellDc.value.isEmpty() || !spellAttack.value.isEmpty())
        {
            const QString type = fieldText(m.spelltype).simplified();
            const bool dcOnly = spellAttack.value.isEmpty() && !spellDc.value.isEmpty();
            entries << spellLine(titleCase(type.isEmpty() ? QStringLiteral("Spells")
                                                          : type + QStringLiteral(" Spells")),
                                 m.focuspoints, spellDc, shown(m, "spelldc", QString(), spells),
                                 spellAttack, dcOnly ? QString() : shown(m, "spellattack", QString(), spells),
                                 QString(), m.spells, m.cantriplevel, m.constant, level);
        }
        for (const SpellList &list : m.morespells)
        {
            // Additional spell lists: an empty DC is its benchmark's hint;
            // an empty attack is left out.
            QString dc = list.spelldc.value.text().trimmed();
            if (dc.isEmpty())
            {
                if (const Hint h = hint(Table::SpellDc, level, list.spelldc.benchmark); h.valid && h.value)
                {
                    dc = QString::number(*h.value);
                }
            }
            const QString name = fieldText(list.name).simplified();
            entries << spellLine(titleCase(name.isEmpty() ? QStringLiteral("Spells")
                                                          : name + QStringLiteral(" Spells")),
                                 list.focuspoints, list.spelldc, dc,
                                 list.spellattack, list.spellattack.value.text().trimmed(),
                                 list.note, list.spells, list.cantriplevel, list.constant, level);
        }
        if (!m.ritualtype.trimmed().isEmpty())
        {
            entries << QStringLiteral("**%1 Rituals** DC %2; %3")
                           .arg(titleCase(fieldText(m.ritualtype).simplified()),
                                shown(m, "spelldc", QStringLiteral("0"), true),
                                spellList(m.rituals));
        }
        specialsOf("offense");


        QStringList lines = itemBlock(m, label, entries);

        // The description follows the statblock as ordinary text, as on the
        // website (and in the book's body font), below a horizontal rule
        // (a lone "-" in Scribe) so the two read apart.
        // Recall Knowledge can go above or below it.
        QStringList after;
        if (m.recallKnowledge == QLatin1String("above"))
        {
            after << recall;
        }
        if (!m.description.trimmed().isEmpty())
        {
            after << bodyText(m.description);
        }
        if (m.recallKnowledge == QLatin1String("below"))
        {
            after << recall;
        }
        if (!after.isEmpty())
        {
            lines << QString() << QStringLiteral("-") << QString() << after.join(QStringLiteral("\n\n"));
        }
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
} // namespace monster
