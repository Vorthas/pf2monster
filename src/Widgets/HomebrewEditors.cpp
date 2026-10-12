// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/HomebrewEditors.h"

#include "Model/Adjustments.h"
#include "Model/Benchmarks.h"
#include "Model/RecallKnowledge.h"
#include "Model/Roadmaps.h"
#include "Model/TraitNotes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>

namespace
{
    QLabel *hint(const QString &text)
    {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setEnabled(false); // grey, as the form's notes
        return label;
    }

    QStringList splitList(const QString &text)
    {
        QStringList items;
        for (const QString &item : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            if (const QString t = item.trimmed(); !t.isEmpty())
            {
                items << t;
            }
        }
        return items;
    }

    // The skills that identify creatures (Recall Knowledge), as checkboxes.
    const QStringList &knowledgeSkills()
    {
        static const QStringList skills{"Arcana", "Crafting", "Medicine", "Nature", "Occultism", "Religion", "Society"};
        return skills;
    }
} // namespace

// Domains.

DomainEditor::DomainEditor(QWidget *parent) : HomebrewEditor(parent)
{
    setUp(tr("Homebrew Domains"));
}

QSet<QString> DomainEditor::formKeys() const
{
    return {"name", "spell", "advanced", "source"};
}

void DomainEditor::buildForm(QFormLayout *form)
{
    form->addRow(hint(tr("A domain a Divine Warden can pick. It gets the domain spell, and the advanced "
                         "domain spell from 10th level.")));
    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("Storm"));
    spell_ = new QLineEdit;
    spell_->setPlaceholderText(tr("thunderclap"));
    advanced_ = new QLineEdit;
    advanced_->setPlaceholderText(tr("eye of the storm"));
    source_ = new QLineEdit;
    source_->setPlaceholderText(tr("for you: where it comes from"));
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Domain spell"), spell_);
    form->addRow(tr("Advanced domain spell"), advanced_);
    form->addRow(tr("Source"), source_);
    for (QLineEdit *edit : {name_, spell_, advanced_, source_})
    {
        connect(edit, &QLineEdit::textEdited, this, &DomainEditor::formEdited);
    }
}

void DomainEditor::fillForm(const QString &name, const QJsonObject &entry)
{
    name_->setText(name);
    spell_->setText(entry.value(QStringLiteral("spell")).toString());
    advanced_->setText(entry.value(QStringLiteral("advanced")).toString());
    source_->setText(entry.value(QStringLiteral("source")).toString());
}

QJsonObject DomainEditor::readForm(QString *name) const
{
    *name = name_->text().trimmed();
    QJsonObject entry{{QStringLiteral("name"), *name},
                      {QStringLiteral("spell"), spell_->text().trimmed()},
                      {QStringLiteral("advanced"), advanced_->text().trimmed()}};
    if (!source_->text().trimmed().isEmpty())
    {
        entry.insert(QStringLiteral("source"), source_->text().trimmed());
    }
    return entry;
}

QList<HomebrewEditor::FieldProblem> DomainEditor::checkForm() const
{
    QList<FieldProblem> problems;
    if (name_->text().trimmed().isEmpty())
    {
        problems.push_back({name_, tr("It needs a name (what the Divine Warden dialog lists).")});
    }
    if (spell_->text().trimmed().isEmpty())
    {
        problems.push_back({spell_, tr("It needs a domain spell.")});
    }
    if (advanced_->text().trimmed().isEmpty())
    {
        problems.push_back({advanced_, tr("It needs an advanced domain spell (a Divine Warden of 10th level "
                                          "or higher gets it).")});
    }
    for (const monster::Domain &d : monster::domains())
    {
        if (d.file.isEmpty() && d.name.compare(name_->text().trimmed(), Qt::CaseInsensitive) == 0)
        {
            problems.push_back({name_,
                                tr("A built-in domain is called %1 too; both will be listed, which may "
                                   "be confusing.")
                                    .arg(d.name),
                                false});
        }
    }
    return problems;
}

QStringList DomainEditor::builtInNames() const
{
    QStringList names;
    for (const monster::Domain &d : monster::domains())
    {
        if (d.file.isEmpty())
        {
            names << d.name;
        }
    }
    return names;
}

void DomainEditor::fillFromBuiltIn(const QString &name)
{
    for (const monster::Domain &d : monster::domains())
    {
        if (d.file.isEmpty() && d.name == name)
        {
            // A copy: the user renames it.
            fillForm(d.name + tr(" (homebrew)"), {{QStringLiteral("spell"), d.spell},
                                                  {QStringLiteral("advanced"), d.advanced}});
        }
    }
}

// Traits.

TraitEditor::TraitEditor(QWidget *parent) : HomebrewEditor(parent)
{
    setUp(tr("Homebrew Traits"));
}

QSet<QString> TraitEditor::formKeys() const
{
    return {"name", "lines", "recall", "ancestry", "source"};
}

void TraitEditor::buildForm(QFormLayout *form)
{
    form->addRow(hint(tr("A trait of your own (a homebrew ancestry or creature type). Its note shows in the "
                         "form for monsters with it, beside any built-in note; its skills identify those "
                         "monsters for Recall Knowledge.")));
    key_ = new QLineEdit;
    key_->setPlaceholderText(tr("stonekin"));
    key_->setToolTip(tr("The trait as you type it in a monster's Type or Traits (any capitals)"));
    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("Stonekin"));
    name_->setToolTip(tr("How the note and the Recall Knowledge line write it; empty: the trait with a "
                         "capital letter"));
    lines_ = new QPlainTextEdit;
    lines_->setPlaceholderText(tr("**Senses** low-light vision\n**Resistances** fire"));
    lines_->setToolTip(tr("One line each; **bold** for the labels, as the built-in notes do"));
    lines_->setMaximumHeight(fontMetrics().height() * 8);
    auto *skills = new QWidget;
    auto *grid = new QGridLayout(skills);
    grid->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < knowledgeSkills().size(); ++i)
    {
        auto *check = new QCheckBox(knowledgeSkills()[i]);
        grid->addWidget(check, i / 4, i % 4);
        skills_ << check;
        connect(check, &QCheckBox::toggled, this, &TraitEditor::formEdited);
    }
    otherSkills_ = new QLineEdit;
    otherSkills_->setPlaceholderText(tr("other skills, e.g. Stonekin Lore (commas between)"));
    grid->addWidget(otherSkills_, 2, 0, 1, 4);
    ancestry_ = new QCheckBox(tr("An ancestry"));
    ancestry_->setToolTip(tr("Replaced by an adjustment's \"any ancestry\", like the built-in ancestries. "
                             "Not needed for a trait such an adjustment gives: that counts already"));
    connect(ancestry_, &QCheckBox::toggled, this, &TraitEditor::formEdited);
    source_ = new QLineEdit;
    source_->setPlaceholderText(tr("for you: where it comes from"));
    form->addRow(tr("Trait"), key_);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Note"), lines_);
    form->addRow(tr("Recall Knowledge"), skills);
    form->addRow(QString(), ancestry_);
    form->addRow(tr("Source"), source_);
    for (QLineEdit *edit : {key_, name_, otherSkills_, source_})
    {
        connect(edit, &QLineEdit::textEdited, this, &TraitEditor::formEdited);
    }
    connect(lines_, &QPlainTextEdit::textChanged, this, &TraitEditor::formEdited);
}

void TraitEditor::fillForm(const QString &name, const QJsonObject &entry)
{
    key_->setText(name);
    name_->setText(entry.value(QStringLiteral("name")).toString());
    QStringList lines;
    for (const QJsonValue &v : entry.value(QStringLiteral("lines")).toArray())
    {
        lines << v.toString();
    }
    lines_->setPlainText(lines.join(QLatin1Char('\n')));
    QStringList other;
    for (QCheckBox *check : skills_)
    {
        check->setChecked(false);
    }
    for (const QJsonValue &v : entry.value(QStringLiteral("recall")).toArray())
    {
        const QString skill = v.toString().trimmed();
        bool found = false;
        for (QCheckBox *check : skills_)
        {
            if (check->text().compare(skill, Qt::CaseInsensitive) == 0)
            {
                check->setChecked(true);
                found = true;
            }
        }
        if (!found && !skill.isEmpty())
        {
            other << skill;
        }
    }
    otherSkills_->setText(other.join(QStringLiteral(", ")));
    ancestry_->setChecked(entry.value(QStringLiteral("ancestry")).toBool());
    source_->setText(entry.value(QStringLiteral("source")).toString());
}

QJsonObject TraitEditor::readForm(QString *name) const
{
    *name = key_->text().trimmed().toLower();
    QJsonObject entry;
    if (!name_->text().trimmed().isEmpty())
    {
        entry.insert(QStringLiteral("name"), name_->text().trimmed());
    }
    QJsonArray lines;
    for (const QString &line : lines_->toPlainText().split(QLatin1Char('\n')))
    {
        if (!line.trimmed().isEmpty())
        {
            lines.append(line.trimmed());
        }
    }
    if (!lines.isEmpty())
    {
        entry.insert(QStringLiteral("lines"), lines);
    }
    QJsonArray recall;
    for (QCheckBox *check : skills_)
    {
        if (check->isChecked())
        {
            recall.append(check->text());
        }
    }
    for (const QString &skill : splitList(otherSkills_->text()))
    {
        recall.append(skill);
    }
    if (!recall.isEmpty())
    {
        entry.insert(QStringLiteral("recall"), recall);
    }
    if (ancestry_->isChecked())
    {
        entry.insert(QStringLiteral("ancestry"), true);
    }
    if (!source_->text().trimmed().isEmpty())
    {
        entry.insert(QStringLiteral("source"), source_->text().trimmed());
    }
    return entry;
}

QList<HomebrewEditor::FieldProblem> TraitEditor::checkForm() const
{
    QList<FieldProblem> problems;
    const QString key = key_->text().trimmed();
    if (key.isEmpty())
    {
        problems.push_back({key_, tr("It needs the trait, as you'd type it in a monster's Traits.")});
    }
    else if (key.contains(QLatin1Char(',')))
    {
        problems.push_back({key_, tr("One trait only: commas separate traits in a monster's Traits.")});
    }
    bool anySkill = !splitList(otherSkills_->text()).isEmpty();
    for (QCheckBox *check : skills_)
    {
        anySkill = anySkill || check->isChecked();
    }
    if (lines_->toPlainText().trimmed().isEmpty() && !anySkill && !ancestry_->isChecked())
    {
        problems.push_back({lines_, tr("With no note and no skill it does nothing yet."), false});
    }
    if (!monster::builtInTraitNote(key.toLower()).name.isEmpty() ||
        !monster::builtInRecallSkills(key).isEmpty())
    {
        problems.push_back({key_,
                            tr("The app knows this trait already; your note shows beside its own, and "
                               "your skills are added to its own."),
                            false});
    }
    return problems;
}

QStringList TraitEditor::builtInNames() const
{
    return monster::builtInTraitKeys();
}

void TraitEditor::fillFromBuiltIn(const QString &name)
{
    const monster::TraitNote note = monster::builtInTraitNote(name);
    QJsonArray lines;
    for (const QString &line : note.lines)
    {
        lines.append(line);
    }
    QJsonArray recall;
    for (const QString &skill : monster::builtInRecallSkills(name))
    {
        recall.append(skill);
    }
    fillForm(name, {{QStringLiteral("name"), note.name}, {QStringLiteral("lines"), lines},
                    {QStringLiteral("recall"), recall}});
}

// Road maps.

namespace
{
    // The statistics a road map can set, by group, with their names.
    struct StatGroup
    {
        QString title;
        QList<std::pair<QString, QString>> stats;
    };

    const QList<StatGroup> &statGroups()
    {
        static const QList<StatGroup> groups{
            {QObject::tr("Perception"), {{"perception", QObject::tr("Perception")}}},
            {QObject::tr("Attribute Modifiers"),
             {{"strength", QObject::tr("Strength")}, {"dexterity", QObject::tr("Dexterity")},
              {"constitution", QObject::tr("Constitution")}, {"intelligence", QObject::tr("Intelligence")},
              {"wisdom", QObject::tr("Wisdom")}, {"charisma", QObject::tr("Charisma")}}},
            {QObject::tr("Defense"),
             {{"ac", QObject::tr("AC")}, {"fortitude", QObject::tr("Fortitude")}, {"reflex", QObject::tr("Reflex")},
              {"will", QObject::tr("Will")}, {"hp", QObject::tr("HP")}}},
            {QObject::tr("Spells"), {{"spelldc", QObject::tr("Spell DC")}, {"spellattack", QObject::tr("Spell attack")}}},
            {QObject::tr("Skills"),
             {{"acrobatics", QObject::tr("Acrobatics")}, {"arcana", QObject::tr("Arcana")},
              {"athletics", QObject::tr("Athletics")}, {"crafting", QObject::tr("Crafting")},
              {"deception", QObject::tr("Deception")}, {"diplomacy", QObject::tr("Diplomacy")},
              {"intimidation", QObject::tr("Intimidation")}, {"medicine", QObject::tr("Medicine")},
              {"nature", QObject::tr("Nature")}, {"occultism", QObject::tr("Occultism")},
              {"performance", QObject::tr("Performance")}, {"religion", QObject::tr("Religion")},
              {"society", QObject::tr("Society")}, {"stealth", QObject::tr("Stealth")},
              {"survival", QObject::tr("Survival")}, {"thievery", QObject::tr("Thievery")}}}};
        return groups;
    }

    // "Sage", "Wisdom" -> "sage-wisdom".
    QString idFrom(const QString &name, const QString &variant)
    {
        QString s = (name + QLatin1Char(' ') + variant).trimmed().toLower();
        s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        return s;
    }
} // namespace

RoadmapEditor::RoadmapEditor(QWidget *parent) : HomebrewEditor(parent)
{
    setUp(tr("Homebrew Road Maps"));
}

QSet<QString> RoadmapEditor::formKeys() const
{
    return {"format", "kind", "id", "name", "variant", "list", "rules", "benchmarks", "notes", "source", "license"};
}

void RoadmapEditor::buildForm(QFormLayout *form)
{
    form->addRow(hint(tr("A road map sets the benchmarks a creature of a role starts from (Monster > "
                         "Roadmap), and shows its notes in the form. Statistics left at \"not set\" go "
                         "to moderate (Perception, attributes, defenses) or stay as they are (skills).")));
    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("Sage"));
    variant_ = new QLineEdit;
    variant_->setPlaceholderText(tr("optional, e.g. Wisdom (variants share a submenu)"));
    list_ = new QComboBox;
    list_->addItem(tr("Base"), QStringLiteral("base"));
    list_->addItem(tr("Class"), QStringLiteral("class"));
    rules_ = new QPlainTextEdit;
    rules_->setPlaceholderText(tr("what the road map says, in a line or two (shown in the status bar)"));
    rules_->setMaximumHeight(fontMetrics().height() * 4);
    id_ = new QLabel;
    id_->setEnabled(false);
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Variant"), variant_);
    form->addRow(tr("Roadmap submenu"), list_);
    form->addRow(tr("Description"), rules_);
    form->addRow(tr("Id"), id_);
    connect(name_, &QLineEdit::textEdited, this, &RoadmapEditor::formEdited);
    connect(variant_, &QLineEdit::textEdited, this, &RoadmapEditor::formEdited);
    connect(list_, &QComboBox::activated, this, &RoadmapEditor::formEdited);
    connect(rules_, &QPlainTextEdit::textChanged, this, &RoadmapEditor::formEdited);

    // The benchmarks, by group.
    for (const StatGroup &group : statGroups())
    {
        auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(group.title.toHtmlEscaped()));
        form->addRow(title);
        for (const auto &[key, label] : group.stats)
        {
            const std::optional<monster::Table> table = monster::tableFor(key);
            if (!table)
            {
                continue;
            }
            StatRow row;
            row.key = key;
            row.main = new QComboBox;
            row.main->addItem(tr("not set"), QString());
            auto *line = new QWidget;
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->addWidget(row.main);
            auto *also = new QLabel(tr("or"));
            also->setEnabled(false);
            lineLayout->addWidget(also);
            for (const QString &benchmark : monster::benchmarks(*table))
            {
                row.main->addItem(benchmark, benchmark);
                auto *check = new QCheckBox(benchmark);
                check->setToolTip(tr("An alternative the rules allow; the form offers it in the hint"));
                lineLayout->addWidget(check);
                row.alternatives << check;
            }
            lineLayout->addStretch();
            form->addRow(label, line);
            rows_ << row;
        }
    }
    for (StatRow &row : rows_)
    {
        const QString key = row.key;
        connect(row.main, &QComboBox::activated, this, [this, key] {
            for (StatRow &r : rows_)
            {
                if (r.key == key)
                {
                    updateAlternatives(r);
                }
            }
            formEdited();
        });
        for (QCheckBox *check : row.alternatives)
        {
            connect(check, &QCheckBox::toggled, this, &RoadmapEditor::formEdited);
        }
    }

    // The notes, by the form's sections.
    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Notes").toHtmlEscaped())));
    form->addRow(hint(tr("Shown at the top of each section of the form while the road map is applied. "
                         "{maxrank} writes the highest spell rank for the monster's level, {maxrank-1} "
                         "the one below.")));
    for (const auto &[key, title] : monster::roadmapNoteSections())
    {
        auto *edit = new QPlainTextEdit;
        edit->setMaximumHeight(fontMetrics().height() * 3 + 8);
        form->addRow(title, edit);
        notes_ << std::pair{key, edit};
        connect(edit, &QPlainTextEdit::textChanged, this, &RoadmapEditor::formEdited);
    }
    source_ = new QLineEdit;
    source_->setPlaceholderText(tr("for you: where it comes from"));
    license_ = new QLineEdit;
    license_->setPlaceholderText(tr("for you: its license, if it's someone else's"));
    form->addRow(tr("Source"), source_);
    form->addRow(tr("License"), license_);
    connect(source_, &QLineEdit::textEdited, this, &RoadmapEditor::formEdited);
    connect(license_, &QLineEdit::textEdited, this, &RoadmapEditor::formEdited);
}

void RoadmapEditor::updateAlternatives(StatRow &row)
{
    const QString main = row.main->currentData().toString();
    for (QCheckBox *check : row.alternatives)
    {
        // Not the one set; none when nothing is set.
        check->setVisible(check->text() != main);
        check->setEnabled(!main.isEmpty());
        if (main.isEmpty() || check->text() == main)
        {
            const QSignalBlocker block(check);
            check->setChecked(false);
        }
    }
}

QString RoadmapEditor::idFor() const
{
    return savedId_.isEmpty() ? idFrom(name_->text(), variant_->text()) : savedId_;
}

void RoadmapEditor::fillForm(const QString &name, const QJsonObject &entry)
{
    savedId_ = name;
    name_->setText(entry.value(QStringLiteral("name")).toString());
    variant_->setText(entry.value(QStringLiteral("variant")).toString());
    list_->setCurrentIndex(entry.value(QStringLiteral("list")).toString() == QLatin1String("class") ? 1 : 0);
    rules_->setPlainText(entry.value(QStringLiteral("rules")).toString());
    const QJsonObject benchmarks = entry.value(QStringLiteral("benchmarks")).toObject();
    otherBenchmarks_ = QJsonObject();
    otherNotes_ = QJsonObject();
    badNames_.clear();
    for (auto it = benchmarks.begin(); it != benchmarks.end(); ++it)
    {
        const bool known = std::any_of(rows_.begin(), rows_.end(), [&](const StatRow &r) { return r.key == it.key(); });
        if (!known)
        {
            otherBenchmarks_.insert(it.key(), it.value());
        }
    }
    for (StatRow &row : rows_)
    {
        QStringList names;
        const QStringList allowed = monster::benchmarks(*monster::tableFor(row.key));
        QStringList bad;
        for (const QJsonValue &v : benchmarks.value(row.key).toArray())
        {
            (allowed.contains(v.toString()) ? names : bad) << v.toString();
        }
        if (!bad.isEmpty())
        {
            badNames_.insert(row.key, tr("\"%1\" isn't a benchmark for %2 (it can be %3); pick one here, or it's "
                                         "left out when you save.")
                                          .arg(bad.join(QStringLiteral("\", \"")), row.key,
                                               allowed.join(QStringLiteral(", "))));
        }
        row.main->setCurrentIndex(std::max(0, row.main->findData(names.value(0))));
        updateAlternatives(row);
        for (QCheckBox *check : row.alternatives)
        {
            const QSignalBlocker block(check);
            check->setChecked(names.mid(1).contains(check->text()));
        }
    }
    const QJsonObject notes = entry.value(QStringLiteral("notes")).toObject();
    for (const auto &[key, edit] : notes_)
    {
        edit->setPlainText(notes.value(key).toString());
    }
    for (auto it = notes.begin(); it != notes.end(); ++it)
    {
        const bool known = std::any_of(notes_.begin(), notes_.end(), [&](const auto &n) { return n.first == it.key(); });
        if (!known)
        {
            otherNotes_.insert(it.key(), it.value());
        }
    }
    source_->setText(entry.value(QStringLiteral("source")).toString());
    license_->setText(entry.value(QStringLiteral("license")).toString());
    id_->setText(savedId_.isEmpty() ? tr("made from the name when saved") : savedId_);
}

QJsonObject RoadmapEditor::readForm(QString *name) const
{
    *name = idFor();
    QJsonObject entry{{QStringLiteral("kind"), QStringLiteral("roadmap")},
                      {QStringLiteral("id"), *name},
                      {QStringLiteral("name"), name_->text().trimmed()},
                      {QStringLiteral("list"), list_->currentData().toString()}};
    if (!variant_->text().trimmed().isEmpty())
    {
        entry.insert(QStringLiteral("variant"), variant_->text().trimmed());
    }
    if (!rules_->toPlainText().trimmed().isEmpty())
    {
        entry.insert(QStringLiteral("rules"), rules_->toPlainText().trimmed());
    }
    QJsonObject benchmarks;
    for (const StatRow &row : rows_)
    {
        const QString main = row.main->currentData().toString();
        if (main.isEmpty())
        {
            continue;
        }
        QJsonArray names{main};
        for (QCheckBox *check : row.alternatives)
        {
            if (check->isChecked() && check->text() != main)
            {
                names.append(check->text());
            }
        }
        benchmarks.insert(row.key, names);
    }
    // What the form couldn't show, kept as it was.
    for (auto it = otherBenchmarks_.begin(); it != otherBenchmarks_.end(); ++it)
    {
        benchmarks.insert(it.key(), it.value());
    }
    entry.insert(QStringLiteral("benchmarks"), benchmarks);
    QJsonObject notes;
    for (const auto &[key, edit] : notes_)
    {
        if (!edit->toPlainText().trimmed().isEmpty())
        {
            notes.insert(key, edit->toPlainText().trimmed());
        }
    }
    for (auto it = otherNotes_.begin(); it != otherNotes_.end(); ++it)
    {
        notes.insert(it.key(), it.value());
    }
    if (!notes.isEmpty())
    {
        entry.insert(QStringLiteral("notes"), notes);
    }
    for (const auto &[key, edit] : {std::pair{"source", source_}, std::pair{"license", license_}})
    {
        if (!edit->text().trimmed().isEmpty())
        {
            entry.insert(QLatin1String(key), edit->text().trimmed());
        }
    }
    return entry;
}

QList<HomebrewEditor::FieldProblem> RoadmapEditor::checkForm() const
{
    QList<FieldProblem> problems;
    if (name_->text().trimmed().isEmpty())
    {
        problems.push_back({name_, tr("It needs a name (what the Roadmap menu shows).")});
    }
    bool any = false;
    for (const StatRow &row : rows_)
    {
        any = any || !row.main->currentData().toString().isEmpty();
    }
    for (const auto &[key, edit] : notes_)
    {
        any = any || !edit->toPlainText().trimmed().isEmpty();
    }
    if (!any)
    {
        problems.push_back({name_, tr("With no benchmark and no note it does nothing yet."), false});
    }
    for (const StatRow &row : rows_)
    {
        if (badNames_.contains(row.key) && row.main->currentData().toString().isEmpty())
        {
            problems.push_back({row.main, badNames_.value(row.key), false});
        }
    }
    for (auto it = otherBenchmarks_.begin(); it != otherBenchmarks_.end(); ++it)
    {
        problems.push_back({nullptr,
                            tr("\"%1\" in the benchmarks isn't a statistic a road map can set; it's kept in "
                               "the file as written, and ignored.")
                                .arg(it.key()),
                            false});
    }
    for (auto it = otherNotes_.begin(); it != otherNotes_.end(); ++it)
    {
        problems.push_back({nullptr,
                            tr("The note for \"%1\" isn't for a section of the form (it can be perception, "
                               "attributes, skills, defense, offense, spells, specials); it's kept in the "
                               "file as written, and not shown.")
                                .arg(it.key()),
                            false});
    }
    for (const monster::Roadmap &r : monster::roadmaps())
    {
        if (r.file.isEmpty() && r.title().compare(name_->text().trimmed() + (variant_->text().trimmed().isEmpty()
                                                                                 ? QString()
                                                                                 : QStringLiteral(" (%1)").arg(variant_->text().trimmed())),
                                                  Qt::CaseInsensitive) == 0)
        {
            problems.push_back({name_,
                                tr("A built-in road map is called %1 too; both will be in the menu.").arg(r.title()),
                                false});
        }
    }
    return problems;
}

QString RoadmapEditor::entryLabel(const QString &name, const QJsonObject &entry) const
{
    const QString title = entry.value(QStringLiteral("name")).toString();
    const QString variant = entry.value(QStringLiteral("variant")).toString();
    if (title.isEmpty())
    {
        return name;
    }
    return variant.isEmpty() ? title : QStringLiteral("%1 (%2)").arg(title, variant);
}

QStringList RoadmapEditor::builtInNames() const
{
    QStringList names;
    for (const monster::Roadmap &r : monster::roadmaps())
    {
        if (r.file.isEmpty())
        {
            names << r.title();
        }
    }
    return names;
}

void RoadmapEditor::fillFromBuiltIn(const QString &name)
{
    for (const monster::Roadmap &r : monster::roadmaps())
    {
        if (!r.file.isEmpty() || r.title() != name)
        {
            continue;
        }
        QJsonObject benchmarks;
        for (const auto &[key, names] : r.benchmarks)
        {
            benchmarks.insert(key, QJsonArray::fromStringList(names));
        }
        QJsonObject notes;
        for (const auto &[key, note] : r.notes)
        {
            notes.insert(key, note);
        }
        // A copy: a new id is made from its name when saved.
        fillForm(QString(), {{QStringLiteral("name"), r.name + tr(" (homebrew)")},
                             {QStringLiteral("variant"), r.variant},
                             {QStringLiteral("list"), r.list},
                             {QStringLiteral("rules"), r.rules},
                             {QStringLiteral("benchmarks"), benchmarks},
                             {QStringLiteral("notes"), notes},
                             {QStringLiteral("source"), QStringLiteral("Copied from the built-in %1").arg(r.title())}});
    }
}
