// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/HazardEditor.h"

#include "Model/Benchmarks.h"
#include "Widgets/EntryList.h"
#include "Widgets/TagCompleter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

// The small helpers below are copies of the creature form's (MonsterEditor):
// the two forms are kept apart for now (the user); sharing them is a
// possible later refactor.
namespace
{
    using monster::Value;

    // What the site stores for a typed value: a number when it is one.
    Value valueFromText(const QString &text)
    {
        bool ok = false;
        const int number = text.trimmed().toInt(&ok);
        return ok ? Value(number) : Value(text);
    }

    // The choices of a combo box: what's stored, what's shown.
    using Choices = std::vector<std::pair<QString, QString>>;

    const Choices &strikeTypes()
    {
        static const Choices list{{"Melee", "Melee"}, {"Ranged", "Ranged"}};
        return list;
    }

    // Where an ability goes in a hazard's statblock (the website's
    // categories, by what they mean for a hazard).
    const Choices &categories()
    {
        static const Choices list{{"general", QT_TRANSLATE_NOOP("HazardEditor", "Top")},
                                  {"defense", QT_TRANSLATE_NOOP("HazardEditor", "Reactions")},
                                  {"offense", QT_TRANSLATE_NOOP("HazardEditor", "Actions")}};
        return list;
    }

    const Choices &actionChoices()
    {
        static const Choices list{
            {"none", "None"},
            {"one", "One"},
            {"two", "Two"},
            {"three", "Three"},
            {"one to two", "One to Two"},
            {"one to three", "One to Three"},
            {"two to three", "Two to Three"},
            {"free", "Free"},
            {"reaction", "Reaction"},
            {"1 minute", "1 minute"},
            {"10 minutes", "10 minutes"}};
        return list;
    }

    // A hazard Strike's icon: none unless one is chosen (GM Core prints
    // most without).
    const Choices &strikeActionChoices()
    {
        static const Choices list{{"", "(no icon)"},     {"one", "One"},
                                  {"two", "Two"},        {"three", "Three"},
                                  {"free", "Free"},      {"reaction", "Reaction"}};
        return list;
    }

    Choices benchmarkChoices(monster::Table table)
    {
        Choices choices;
        for (const QString &name : monster::benchmarks(table))
        {
            QString label = name;
            label[0] = label[0].toUpper();
            choices.emplace_back(name, label);
        }
        return choices;
    }

    // Shows a hint in `label`, in italics with a tooltip when it is
    // extrapolated (past the rules' tables).
    void showHint(QLabel *label, const monster::Hint &hint)
    {
        label->setText(hint.valid ? hint.text : QString());
        QFont font = label->font();
        font.setItalic(hint.extrapolated);
        label->setFont(font);
        label->setToolTip(hint.extrapolated
                              ? QObject::tr("Extrapolated: GM Core's tables stop at level 24")
                              : QString());
    }

    // The link that copies a hint into its value box.
    const QString copyHintLink = QStringLiteral("=copy");

    // Like showHint(), with the number as a link that copies it into the
    // value box (as on the website).
    void showCopyableHint(QLabel *label, const monster::Hint &hint)
    {
        showHint(label, hint);
        if (!hint.valid || !hint.value)
        {
            label->setTextFormat(Qt::PlainText);
            return;
        }
        label->setTextFormat(Qt::RichText);
        label->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(copyHintLink, hint.text.toHtmlEscaped()));
        const QString tip = QObject::tr("Click the number to put %1 in the value box").arg(*hint.value);
        label->setToolTip(label->toolTip().isEmpty() ? tip : label->toolTip() + QLatin1Char('\n') + tip);
    }

    // A combo box over `choices` that also keeps a value it doesn't list
    // (from a file), shown as it is.
    QComboBox *makeCombo(const Choices &choices)
    {
        auto *combo = new QComboBox;
        for (const auto &[key, label] : choices)
        {
            combo->addItem(QString(label), QString(key));
        }
        return combo;
    }

    void selectIn(QComboBox *combo, const QString &key)
    {
        int index = combo->findData(key.trimmed().toLower(), Qt::UserRole, Qt::MatchFixedString);
        if (index < 0)
        {
            combo->addItem(key, key);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    }

    template <typename T>
    void moveEntry(std::vector<T> &entries, int from, int to)
    {
        if (from < 0 || to < 0 || from >= static_cast<int>(entries.size()) ||
            to >= static_cast<int>(entries.size()) || from == to)
        {
            return;
        }
        T entry = std::move(entries[static_cast<size_t>(from)]);
        entries.erase(entries.begin() + from);
        entries.insert(entries.begin() + to, std::move(entry));
    }

    QLineEdit *valueEdit()
    {
        auto *edit = new QLineEdit;
        edit->setMaximumWidth(70);
        edit->setAlignment(Qt::AlignRight);
        return edit;
    }

    // A grey note under a row, as the creature form's hints.
    QLabel *hintLine()
    {
        auto *label = new QLabel;
        label->setWordWrap(true);
        label->setEnabled(false);
        return label;
    }

    // The defenses in the order GM Core prints them.
    const QStringList &defenseOrder()
    {
        static const QStringList order{QStringLiteral("ac"), QStringLiteral("fortitude"),
                                       QStringLiteral("reflex"), QStringLiteral("will"),
                                       QStringLiteral("hp")};
        return order;
    }
} // namespace

HazardEditor::HazardEditor(QWidget *parent) : QWidget(parent)
{
    // Three columns as the creature form, each scrolling on its own; the
    // first is split with the corner widget below it.
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < kColumns; ++i)
    {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *column = new QWidget;
        columnLayouts_[i] = new QVBoxLayout(column);
        scroll->setWidget(column);
        scrolls_[i] = scroll;
    }
    firstColumn_ = new QSplitter(Qt::Vertical);
    firstColumn_->setChildrenCollapsible(false);
    firstColumn_->addWidget(scrolls_[0]);
    row->addWidget(firstColumn_, 1);
    row->addWidget(scrolls_[1], 1);
    row->addWidget(scrolls_[2], 1);
    using monster::Monster;
    auto m = [this]() -> Monster & { return *monster_; };
    auto level = [this] { return monster_->level.number().value_or(0); };

    // General.
    {
        auto *grid = new QGridLayout(addSection(0, 0, tr("General")));
        addLine(grid, 0, tr("Name"), [m]() -> QString & { return m().name; });

        auto *levelBox = new QSpinBox;
        levelBox->setRange(-1, 25);
        levelBox->setToolTip(tr("The level the statistics are built for. GM Core's tables "
                                "stop at 24; level 25's hints are extrapolated."));
        auto *shown = new QLineEdit;
        shown->setPlaceholderText(tr("same"));
        shown->setMaximumWidth(70);
        shown->setToolTip(tr("The level the statblock shows, when it differs from the "
                             "level the statistics are built for"));
        auto *levelRow = new QHBoxLayout;
        levelRow->addWidget(levelBox);
        levelRow->addWidget(new QLabel(tr("Display level")));
        levelRow->addWidget(shown);
        levelRow->addStretch();
        grid->addWidget(new QLabel(tr("Level")), 1, 0);
        grid->addLayout(levelRow, 1, 1, 1, 4);
        connect(levelBox, &QSpinBox::valueChanged, this, [this](int value) {
            if (!loading_)
            {
                monster_->level = Value(value);
                refreshHints();
                emit changed();
            }
        });
        connect(shown, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->showlevel = valueFromText(text);
            emit changed();
        });
        loaders_.push_back([this, levelBox, shown] {
            levelBox->setValue(monster_->level.number().value_or(0));
            shown->setText(monster_->showlevel.text());
        });

        // Complexity and Rarity on one row.
        auto *complexity = makeCombo({{QStringLiteral("simple"), QT_TRANSLATE_NOOP("HazardEditor", "Simple")},
                                      {QStringLiteral("complex"), QT_TRANSLATE_NOOP("HazardEditor", "Complex")}});
        complexity->setToolTip(tr("Complex hazards roll initiative and follow a Routine; they list "
                                  "their Stealth modifier instead of a DC and get the Complex trait"));
        grid->addWidget(new QLabel(tr("Complexity")), 2, 0);
        grid->addWidget(complexity, 2, 1);
        connect(complexity, &QComboBox::activated, this, [this, complexity] {
            monster_->hazard.complex = complexity->currentData().toString() == QLatin1String("complex");
            updateVisibility();
            refreshHints();
            emit changed();
        });
        loaders_.push_back([this, complexity] {
            selectIn(complexity, monster_->hazard.complex ? QStringLiteral("complex") : QStringLiteral("simple"));
        });

        // Rarity: kept among the traits, as the website keeps it ("Rare,
        // trap"), and shown here instead of in the Traits box.
        auto *rarity = makeCombo({{QString(), QT_TRANSLATE_NOOP("HazardEditor", "(None)")},
                                  {QStringLiteral("uncommon"), QT_TRANSLATE_NOOP("HazardEditor", "Uncommon")},
                                  {QStringLiteral("rare"), QT_TRANSLATE_NOOP("HazardEditor", "Rare")},
                                  {QStringLiteral("unique"), QT_TRANSLATE_NOOP("HazardEditor", "Unique")}});
        grid->addWidget(new QLabel(tr("Rarity")), 2, 2);
        grid->addWidget(rarity, 2, 3);
        grid->setColumnStretch(4, 1); // the dropdowns keep their width
        auto *traits = new QLineEdit;
        traits->setPlaceholderText(tr("mechanical, trap, ..."));
        traits->setToolTip(tr("Complex is added for a complex hazard; Simple shows only if typed here"));
        grid->addWidget(new QLabel(tr("Traits")), 3, 0);
        grid->addWidget(traits, 3, 1, 1, 4);
        // The traits without the rarity, and the rarity found among them.
        auto split = [](const QString &all) {
            QStringList others;
            QString found;
            for (const QString &trait : all.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                const QString t = trait.trimmed();
                if (t.isEmpty())
                {
                    continue;
                }
                if (found.isEmpty() &&
                    QStringList{"uncommon", "rare", "unique"}.contains(t, Qt::CaseInsensitive))
                {
                    found = t.toLower();
                    continue;
                }
                others << t;
            }
            return std::pair{others.join(QStringLiteral(", ")), found};
        };
        // The rarity first, as the website writes it.
        auto store = [this](const QString &others, const QString &rare) {
            QString r = rare;
            if (!r.isEmpty())
            {
                r[0] = r[0].toUpper();
            }
            monster_->traits = r.isEmpty()        ? others
                               : others.isEmpty() ? r
                                                  : r + QStringLiteral(", ") + others;
            emit changed();
        };
        connect(rarity, &QComboBox::activated, this, [this, rarity, split, store] {
            store(split(monster_->traits).first, rarity->currentData().toString());
        });
        connect(traits, &QLineEdit::textEdited, this, [this, split, store](const QString &text) {
            store(text, split(monster_->traits).second);
        });
        loaders_.push_back([this, rarity, traits, split] {
            const auto [others, found] = split(monster_->traits);
            selectIn(rarity, found);
            traits->setText(others);
        });
        addLine(grid, 4, tr("Label"), [m]() -> QString & { return m().creature; })
            ->setPlaceholderText(tr("Hazard"));

        // Tags: for sorting the collection, not shown in the statblock.
        auto *tags = new QLineEdit;
        tags->setPlaceholderText(tr("campaign, dungeon, ... (for finding it in the list)"));
        tagCompleter_ = new TagCompleter(tags, TagCompleter::Mode::Tags);
        grid->addWidget(new QLabel(tr("Tags")), 5, 0);
        grid->addWidget(tags, 5, 1, 1, 4);
        connect(tags, &QLineEdit::textEdited, this, [this](const QString &text) {
            QStringList list;
            for (const QString &tag : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
            {
                if (const QString t = tag.trimmed(); !t.isEmpty())
                {
                    list << t;
                }
            }
            monster_->tags = list;
            emit changed();
        });
        loaders_.push_back([this, tags] { tags->setText(monster_->tags.join(QStringLiteral(", "))); });
        addLine(grid, 6, tr("Info"), [m]() -> QString & { return m().info; });
    }

    // Description: in the statblock, under Stealth.
    {
        auto *box = new QVBoxLayout(addSection(0, 1, tr("Description")));
        addText(box, [m]() -> QString & { return m().description; }, 4)
            ->setPlaceholderText(tr("What the hazard looks like, and any special rules. Markdown: "
                                    "**bold**, _italic_; % is its name."));
    }

    // Stealth and Disable.
    {
        auto *box = addSection(0, 2, tr("Stealth and Disable"));
        auto *layout = new QVBoxLayout(box);
        auto *grid = new QGridLayout;
        layout->addLayout(grid);
        auto *stealthLabel = new QLabel;
        addStat(grid, 0, stealthLabel, QStringLiteral("stealth"), false);
        // Its note is often long: a row of its own.
        auto *stealthNote = new QLineEdit;
        stealthNote->setPlaceholderText(tr("note: or DC 26 (expert) to notice the control panel"));
        stealthNote->setToolTip(tr("After the Stealth: starting in lower case or with \"(\" it follows "
                                   "a space, with \",\" or \";\" as typed, anything else after \"; \""));
        grid->addWidget(new QLabel(tr("Stealth note")), 2, 0);
        grid->addWidget(stealthNote, 2, 1, 1, 4);
        connect(stealthNote, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->stat(QStringLiteral("stealth")).note = text;
            emit changed();
        });
        loaders_.push_back([this, stealthNote] {
            stealthNote->setText(monster_->stat(QStringLiteral("stealth")).note);
        });
        hintUpdaters_.push_back([this, stealthLabel] {
            stealthLabel->setText(monster_->hazard.complex ? tr("Stealth modifier") : tr("Stealth DC"));
            stealthLabel->setToolTip(monster_->hazard.complex
                                         ? tr("A complex hazard lists its Stealth modifier, which it "
                                              "rolls for initiative: the DC - 10")
                                         : tr("The DC to find it"));
        });
        auto *rank = new QLabel(tr("Min. proficiency"));
        rank->setToolTip(tr("The Perception rank needed to find it, shown in parentheses after "
                            "Stealth (\"trained\"); empty: anyone can notice it"));
        addStat(grid, 1, rank, QStringLiteral("minprof"), false);

        auto *disableLabel = new QLabel(tr("Disable"));
        layout->addWidget(disableLabel);
        addText(layout, [m]() -> QString & { return m().hazard.disable; }, 3)
            ->setPlaceholderText(tr("DC 20 Thievery (trained) to ..., or *dispel magic* (2nd rank; "
                                    "counteract DC 18) to counteract the rune"));
        QLabel *disableHint = hintLine();
        layout->addWidget(disableHint);
        hintUpdaters_.push_back([disableHint, level] {
            using monster::Table;
            const int l = level();
            const monster::Hint extreme = monster::hint(Table::HazardStealthDisable, l, QStringLiteral("extreme"));
            const monster::Hint high = monster::hint(Table::HazardStealthDisable, l, QStringLiteral("high"));
            const monster::Hint low = monster::hint(Table::HazardStealthDisable, l, QStringLiteral("low"));
            const monster::Hint rankHigh = monster::hint(Table::HazardMinProficiency, l, QStringLiteral("high"));
            const monster::Hint rankModerate =
                monster::hint(Table::HazardMinProficiency, l, QStringLiteral("moderate"));
            disableHint->setText(tr("Disable DCs: extreme %1, high %2, low %3. Proficiency: high %4, "
                                    "moderate %5.")
                                     .arg(extreme.text, high.text, low.text, rankHigh.text, rankModerate.text));
        });
    }

    // Defenses: shown only for a hazard with a physical part.
    {
        auto *box = addSection(1, 0, tr("Defenses"));
        auto *layout = new QVBoxLayout(box);
        physical_ = new QCheckBox(tr("Physical component (AC, saves, Hardness, HP)"));
        physical_->setToolTip(tr("Untick for a purely magical or formless hazard: the statblock then "
                                 "shows no AC, saves, Hardness or HP (what's typed is kept)"));
        layout->addWidget(physical_);
        connect(physical_, &QCheckBox::toggled, this, [this](bool on) {
            if (loading_)
            {
                return;
            }
            if (on)
            {
                monster_->hazard.defenses = hiddenDefenses_.isEmpty()
                                                ? monster::Monster::newHazard().hazard.defenses
                                                : hiddenDefenses_;
            }
            else
            {
                hiddenDefenses_ = monster_->hazard.defenses;
                monster_->hazard.defenses.clear();
            }
            load();
            emit changed();
        });

        defenseRows_ = new QWidget;
        layout->addWidget(defenseRows_);
        auto *grid = new QGridLayout(defenseRows_);
        grid->setContentsMargins(0, 0, 0, 0);
        addLine(grid, 0, tr("AC label"), [m]() -> QString & { return m().hazard.acLabel; })
            ->setPlaceholderText(tr("sensor: \"Sensor AC 22\" (empty: none)"));
        int row = 1;
        for (const auto &[label, key] : {std::pair{tr("AC"), "ac"}, std::pair{tr("Fortitude"), "fortitude"},
                                         std::pair{tr("Reflex"), "reflex"}, std::pair{tr("Will"), "will"}})
        {
            const QString statKey = QString::fromLatin1(key);
            auto *show = new QCheckBox(label);
            show->setToolTip(tr("Shown in the statblock (usually only haunts have Will)"));
            addStat(grid, row++, show, statKey);
            connect(show, &QCheckBox::toggled, this, [this, statKey](bool on) {
                if (!loading_)
                {
                    setDefense(statKey, on);
                    emit changed();
                }
            });
            loaders_.push_back([this, show, statKey] {
                show->setChecked(monster_->hazard.defenses.contains(statKey));
            });
        }
        addLine(grid, row++, tr("Saves note"), [m]() -> QString & { return m().savenote; });

        // Hardness, HP and the Broken Threshold.
        auto *showHp = new QCheckBox(tr("HP"));
        showHp->setToolTip(tr("Hardness, HP and BT shown in the statblock"));
        connect(showHp, &QCheckBox::toggled, this, [this](bool on) {
            if (!loading_)
            {
                setDefense(QStringLiteral("hp"), on);
                emit changed();
            }
        });
        loaders_.push_back([this, showHp] {
            showHp->setChecked(monster_->hazard.defenses.contains(QStringLiteral("hp")));
        });
        grid->addWidget(new QLabel(tr("Hardness")), row, 0);
        auto *hardnessHint = new QLabel;
        hardnessHint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(hardnessHint, row, 2);
        QLineEdit *hardness = valueEdit();
        grid->addWidget(hardness, row++, 3);
        connect(hardness, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->hardness = text;
            emit changed();
        });
        loaders_.push_back([this, hardness] { hardness->setText(monster_->hardness); });
        hintUpdaters_.push_back([hardnessHint, level] {
            showHint(hardnessHint, monster::hint(monster::Table::HazardHardness, level()));
            hardnessHint->setToolTip(hardnessHint->toolTip().isEmpty()
                                         ? tr("Minimum–maximum for this level (some hazards don't "
                                              "make sense with a high Hardness: leave it empty)")
                                         : hardnessHint->toolTip());
        });
        addStat(grid, row++, showHp, QStringLiteral("hp"));

        // BT: half the HP unless typed, or none at all.
        grid->addWidget(new QLabel(tr("BT")), row, 0);
        QLineEdit *bt = valueEdit();
        bt->setToolTip(tr("The Broken Threshold; empty: half the HP"));
        grid->addWidget(bt, row, 3);
        auto *noBt = new QCheckBox(tr("None"));
        noBt->setToolTip(tr("No Broken Threshold: it can't be broken, only destroyed: \"HP 20; "
                            "Immunities ...\""));
        grid->addWidget(noBt, row++, 4);
        connect(bt, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->hazard.bt = text;
            emit changed();
        });
        connect(noBt, &QCheckBox::toggled, this, [this, bt](bool on) {
            bt->setEnabled(!on);
            if (!loading_)
            {
                monster_->hazard.noBt = on;
                emit changed();
            }
        });
        loaders_.push_back([this, bt, noBt] {
            bt->setText(monster_->hazard.bt);
            noBt->setChecked(monster_->hazard.noBt);
            bt->setEnabled(!monster_->hazard.noBt);
        });
        // The placeholder follows the HP as it's typed.
        auto updateBt = [this, bt] {
            QString hp = monster_->stat(QStringLiteral("hp")).value.text().trimmed();
            if (hp.isEmpty())
            {
                if (const monster::Hint h = monster::statHint(*monster_, QStringLiteral("hp")); h.value)
                {
                    hp = QString::number(*h.value);
                }
            }
            bool ok = false;
            const int value = hp.toInt(&ok);
            bt->setPlaceholderText(ok ? QString::number(value / 2) : QString());
        };
        hintUpdaters_.push_back(updateBt);
        connect(this, &HazardEditor::changed, this, updateBt);

        addLine(grid, row++, tr("HP label"), [m]() -> QString & { return m().mainPart; })
            ->setPlaceholderText(tr("trapdoor: \"Trapdoor Hardness 15; Trapdoor HP 60\" (empty: none)"));
        for (const auto &[label, key, placeholder] :
             {std::tuple{tr("Immunities"), "immunity", tr("object immunities, critical hits, ...")},
              std::tuple{tr("Weaknesses"), "weakness", tr("electricity 20, ...")},
              std::tuple{tr("Resistances"), "resistance", tr("fire 5, ...")}})
        {
            auto *edit = new QLineEdit;
            edit->setPlaceholderText(placeholder);
            const QString statKey = QString::fromLatin1(key);
            grid->addWidget(new QLabel(label), row, 0);
            grid->addWidget(edit, row++, 1, 1, 4);
            connect(edit, &QLineEdit::textEdited, this, [this, statKey](const QString &text) {
                monster_->stat(statKey).value = Value(text);
                emit changed();
            });
            loaders_.push_back([this, edit, statKey] { edit->setText(monster_->stat(statKey).value.text()); });
        }

        // More parts with Hardness and HP of their own (a spout beside a
        // trapdoor).
        hpParts_ = new EntryList;
        hpParts_->setDeleteToolTip(tr("Delete this part"));
        hpParts_->setRowFactory([this](int i) { return hpPartRow(i); });
        grid->addWidget(hpParts_, row++, 0, 1, 5);
        connect(hpParts_, &EntryList::moveRequested, this, [this](int from, int to) {
            moveEntry(monster_->hpParts, from, to);
            rebuildHpParts();
            emit changed();
        });
        connect(hpParts_, &EntryList::removeRequested, this, [this](int i) {
            monster_->hpParts.erase(monster_->hpParts.begin() + i);
            rebuildHpParts();
            emit changed();
        });
        auto *add = new QPushButton(tr("Add Part"));
        add->setToolTip(tr("Another part with its own Hardness and HP (the spouts beside a trapdoor)"));
        grid->addWidget(add, row++, 0, 1, 2, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            monster::HpPart part;
            part.name.clear();
            monster_->hpParts.push_back(part);
            rebuildHpParts();
            emit changed();
        });
    }

    // Routine: complex hazards.
    {
        auto *box = addSection(2, 0, tr("Routine"));
        routineSection_ = box;
        auto *layout = new QVBoxLayout(box);
        auto *grid = new QGridLayout;
        layout->addLayout(grid);
        grid->addWidget(new QLabel(tr("Actions")), 0, 0);
        QLineEdit *actions = valueEdit();
        actions->setPlaceholderText(tr("3"));
        actions->setToolTip(tr("How many actions it can use each turn: \"Routine (3 actions)\""));
        grid->addWidget(actions, 0, 1);
        auto *traits = new QLineEdit;
        traits->setPlaceholderText(tr("traits: auditory, mental, ..."));
        grid->addWidget(traits, 0, 2);
        grid->setColumnStretch(2, 1);
        connect(actions, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->hazard.routineActions = text;
            emit changed();
        });
        connect(traits, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->hazard.routineTraits = text;
            emit changed();
        });
        loaders_.push_back([this, actions, traits] {
            actions->setText(monster_->hazard.routineActions);
            traits->setText(monster_->hazard.routineTraits);
        });
        addText(layout, [m]() -> QString & { return m().hazard.routine; }, 4)
            ->setPlaceholderText(tr("What it does on each of its turns"));
    }

    // Strikes, with Speed and the offense table.
    {
        auto *box = addSection(2, 1, tr("Strikes"));
        auto *layout = new QVBoxLayout(box);
        auto *grid = new QGridLayout;
        layout->addLayout(grid);
        addLine(grid, 0, tr("Speed"), [m]() -> QString & { return m().speed; })
            ->setPlaceholderText(tr("10 feet (empty: it doesn't move)"));
        QLabel *offense = hintLine();
        layout->addWidget(offense);
        hintUpdaters_.push_back([this, offense, level] {
            using monster::Table;
            const int l = level();
            const QString kind = monster_->hazard.complex ? QStringLiteral("complex") : QStringLiteral("simple");
            const monster::Hint attack = monster::hint(Table::HazardAttack, l, kind);
            const monster::Hint damage = monster::hint(Table::HazardDamage, l, kind);
            const monster::Hint extreme = monster::hint(Table::HazardDc, l, QStringLiteral("extreme"));
            const monster::Hint hard = monster::hint(Table::HazardDc, l, QStringLiteral("hard"));
            offense->setText(tr("For a %1 hazard: attack %2, damage %3; save DCs: extreme %4, hard %5.")
                                 .arg(monster_->hazard.complex ? tr("complex") : tr("simple"),
                                      attack.text, damage.text, extreme.text, hard.text));
            offense->setToolTip(tr("Simple hazards hit harder and deal about twice the damage of "
                                   "complex ones (GM Core, Building Hazards)"));
        });
        strikes_ = new EntryList;
        strikes_->setDeleteToolTip(tr("Delete this Strike"));
        strikes_->setRowFactory([this](int i) { return strikeRow(i); });
        layout->addWidget(strikes_);
        connect(strikes_, &EntryList::moveRequested, this, [this](int from, int to) {
            moveEntry(monster_->strikes, from, to);
            rebuildStrikes();
            emit changed();
        });
        connect(strikes_, &EntryList::removeRequested, this, [this](int i) {
            monster_->strikes.erase(monster_->strikes.begin() + i);
            rebuildStrikes();
            emit changed();
        });
        auto *add = new QPushButton(tr("Add Strike"));
        layout->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            monster_->strikes.emplace_back();
            rebuildStrikes();
            emit changed();
        });
    }

    // Abilities: reactions, actions and the rest, in the order the
    // statblock shows them.
    {
        auto *box = addSection(2, 2, tr("Reactions and Actions"));
        auto *layout = new QVBoxLayout(box);
        specials_ = new EntryList;
        specials_->setDeleteToolTip(tr("Delete this ability"));
        specials_->setRowFactory([this](int i) { return specialRow(i); });
        layout->addWidget(specials_);
        connect(specials_, &EntryList::moveRequested, this, [this](int from, int to) {
            moveEntry(monster_->specials, from, to);
            rebuildSpecials();
            emit changed();
        });
        connect(specials_, &EntryList::removeRequested, this, [this](int i) {
            monster_->specials.erase(monster_->specials.begin() + i);
            rebuildSpecials();
            emit changed();
        });
        auto *add = new QPushButton(tr("Add Ability"));
        layout->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            // Most hazards' first ability is the reaction that sets them off.
            monster::Special special;
            special.type = QStringLiteral("defense");
            special.actions = QStringLiteral("reaction");
            monster_->specials.push_back(special);
            rebuildSpecials();
            emit changed();
        });
    }

    // Reset.
    {
        auto *box = new QVBoxLayout(addSection(2, 3, tr("Reset")));
        addText(box, [m]() -> QString & { return m().hazard.reset; }, 2)
            ->setPlaceholderText(tr("automatic; or how it can be reset (empty: it can't)"));
    }

    reflow(kColumns);
    setSectionsEnabled(false);
}

QWidget *HazardEditor::addSection(int group, int order, const QString &title)
{
    auto *box = new QGroupBox(title);
    // As tall as its contents: spare height goes below the last section.
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    sections_.push_back({group, order, box});
    std::stable_sort(sections_.begin(), sections_.end(), [](const Section &a, const Section &b) {
        return std::pair(a.group, a.order) < std::pair(b.group, b.order);
    });
    return box;
}

void HazardEditor::setCornerWidget(QWidget *widget)
{
    firstColumn_->addWidget(widget);
    // The form gets three quarters of the height, the corner the rest.
    firstColumn_->setSizes({3000, 1000});
}

QSplitter *HazardEditor::cornerSplitter() const
{
    return firstColumn_;
}

void HazardEditor::setKnownTags(const QStringList &tags)
{
    tagCompleter_->setTags(tags);
}

void HazardEditor::setSectionsEnabled(bool enabled)
{
    for (const Section &section : sections_)
    {
        section.box->setEnabled(enabled);
    }
}

// Puts the sections in `columns` columns: three, or with the later groups
// stacked when there's no room for three.
void HazardEditor::reflow(int columns)
{
    columns_ = columns;
    for (QVBoxLayout *layout : columnLayouts_)
    {
        while (QLayoutItem *item = layout->takeAt(0))
        {
            delete item; // the sections are moved, not deleted
        }
    }
    for (const Section &section : sections_)
    {
        columnLayouts_[std::min(section.group, columns - 1)]->addWidget(section.box);
    }
    for (int i = 0; i < kColumns; ++i)
    {
        columnLayouts_[i]->addStretch();
        scrolls_[i]->setVisible(i < columns);
    }
}

void HazardEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Room for a column: about 60 characters of the form's font.
    const int columnWidth = fontMetrics().averageCharWidth() * 60;
    const int columns = std::clamp(width() / std::max(columnWidth, 1), 1, kColumns);
    if (columns != columns_)
    {
        reflow(columns);
    }
}

void HazardEditor::setMonster(monster::Monster *monster)
{
    if (monster != monster_)
    {
        hiddenDefenses_.clear();
    }
    monster_ = monster;
    setSectionsEnabled(monster_ != nullptr);
    if (monster_)
    {
        load();
    }
}

void HazardEditor::load()
{
    loading_ = true;
    for (const auto &loader : loaders_)
    {
        loader();
    }
    physical_->setChecked(!monster_->hazard.defenses.isEmpty());
    rebuildStrikes();
    rebuildSpecials();
    rebuildHpParts();
    updateVisibility();
    refreshHints();
    loading_ = false;
}

void HazardEditor::refreshHints()
{
    if (!monster_)
    {
        return;
    }
    for (const auto &update : hintUpdaters_)
    {
        update();
    }
}

void HazardEditor::updateVisibility()
{
    if (!monster_)
    {
        return;
    }
    defenseRows_->setVisible(!monster_->hazard.defenses.isEmpty());
    // A Routine is for complex hazards; one typed for a simple hazard
    // still shows, so it can be seen and cleared.
    const bool routine = monster_->hazard.complex || !monster_->hazard.routine.trimmed().isEmpty() ||
                         !monster_->hazard.routineActions.trimmed().isEmpty();
    routineSection_->setVisible(routine);
}

void HazardEditor::setDefense(const QString &key, bool shown)
{
    QStringList &defenses = monster_->hazard.defenses;
    defenses.removeAll(key);
    if (shown)
    {
        defenses << key;
    }
    // GM Core's order; anything else kept after it.
    std::stable_sort(defenses.begin(), defenses.end(), [](const QString &a, const QString &b) {
        const auto rank = [](const QString &k) {
            const qsizetype at = defenseOrder().indexOf(k);
            return at < 0 ? defenseOrder().size() : at;
        };
        return rank(a) < rank(b);
    });
}

QLineEdit *HazardEditor::addLine(QGridLayout *grid, int row, const QString &label,
                                 std::function<QString &()> field)
{
    auto *edit = new QLineEdit;
    grid->addWidget(new QLabel(label), row, 0);
    grid->addWidget(edit, row, 1, 1, 4);
    grid->setColumnStretch(4, 1);
    connect(edit, &QLineEdit::textEdited, this, [this, field](const QString &text) {
        field() = text;
        emit changed();
    });
    loaders_.push_back([edit, field] { edit->setText(field()); });
    return edit;
}

void HazardEditor::addStat(QGridLayout *grid, int row, QWidget *label, const QString &key, bool withNote)
{
    grid->addWidget(label, row, 0);
    // Benchmark (extreme, high, low) and its hint from the hazard tables.
    const std::optional<monster::Table> table = monster::hazardTableFor(key);
    if (table && !monster::benchmarks(*table).isEmpty())
    {
        QComboBox *benchmark = makeCombo(benchmarkChoices(*table));
        grid->addWidget(benchmark, row, 1);
        connect(benchmark, &QComboBox::activated, this, [this, key, benchmark] {
            monster_->stat(key).benchmark = benchmark->currentData().toString();
            refreshHints();
            emit changed();
        });
        loaders_.push_back([this, benchmark, key] { selectIn(benchmark, monster_->stat(key).benchmark); });
    }
    auto *hint = new QLabel;
    hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hint->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("+88 to +88")));
    grid->addWidget(hint, row, 2);
    QLineEdit *value = valueEdit();
    grid->addWidget(value, row, 3);
    grid->setColumnStretch(4, 1);
    // An empty value stands for the hint in the statblock: shown greyed.
    hintUpdaters_.push_back([this, hint, value, key] {
        const monster::Hint h = monster::statHint(*monster_, key);
        showCopyableHint(hint, h);
        value->setPlaceholderText(monster::usesHint(*monster_, key) && h.value ? QString::number(*h.value)
                                                                               : QString());
    });
    connect(hint, &QLabel::linkActivated, this, [this, key, value](const QString &link) {
        if (link != copyHintLink)
        {
            return;
        }
        if (const monster::Hint h = monster::statHint(*monster_, key); h.value)
        {
            value->setText(QString::number(*h.value));
            monster_->stat(key).value = Value(*h.value);
            emit changed();
        }
    });
    connect(value, &QLineEdit::textEdited, this, [this, key](const QString &text) {
        monster_->stat(key).value = valueFromText(text);
        emit changed();
    });
    loaders_.push_back([this, value, key] { value->setText(monster_->stat(key).value.text()); });
    if (withNote)
    {
        auto *note = new QLineEdit;
        note->setPlaceholderText(key == QLatin1String("hp") ? tr("(per mannequin) or: each") : tr("note"));
        if (key == QLatin1String("hp"))
        {
            note->setToolTip(tr("Typed in parentheses it goes in the label: \"HP (per mannequin) 70 (BT "
                                "35)\"; otherwise after the BT: \"HP 7 (BT 3) each\""));
        }
        grid->addWidget(note, row, 4);
        connect(note, &QLineEdit::textEdited, this, [this, key](const QString &text) {
            monster_->stat(key).note = text;
            emit changed();
        });
        loaders_.push_back([this, note, key] { note->setText(monster_->stat(key).note); });
    }
}

QPlainTextEdit *HazardEditor::addText(QVBoxLayout *layout, std::function<QString &()> field, int lines)
{
    auto *edit = new QPlainTextEdit;
    edit->setTabChangesFocus(true);
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * (lines + 1) + 8);
    layout->addWidget(edit);
    connect(edit, &QPlainTextEdit::textChanged, this, [this, edit, field] {
        if (!loading_)
        {
            field() = edit->toPlainText();
            updateVisibility();
            emit changed();
        }
    });
    loaders_.push_back([edit, field] { edit->setPlainText(field()); });
    return edit;
}

void HazardEditor::rebuildStrikes()
{
    strikes_->rebuild(static_cast<int>(monster_->strikes.size()));
}

void HazardEditor::rebuildSpecials()
{
    specials_->rebuild(static_cast<int>(monster_->specials.size()));
}

void HazardEditor::rebuildHpParts()
{
    hpParts_->rebuild(static_cast<int>(monster_->hpParts.size()));
    hpParts_->setVisible(!monster_->hpParts.empty());
}

// A Strike: type, action icon, name, attack, traits, then the damage.
QWidget *HazardEditor::strikeRow(int i)
{
    const monster::Strike &strike = monster_->strikes[static_cast<size_t>(i)];
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);

    QComboBox *type = makeCombo(strikeTypes());
    selectIn(type, strike.type);
    QComboBox *action = makeCombo(strikeActionChoices());
    selectIn(action, strike.actions);
    action->setToolTip(tr("The action icon before it; most hazard Strikes have none"));
    auto *name = new QLineEdit(strike.name);
    name->setPlaceholderText(tr("name"));
    QLineEdit *attack = valueEdit();
    attack->setText(strike.attack.text());
    attack->setPlaceholderText(tr("+0"));
    auto *traits = new QLineEdit(strike.traits);
    traits->setPlaceholderText(tr("traits"));
    auto *damage = new QLineEdit(strike.damage);
    damage->setPlaceholderText(tr("damage, e.g. 2d10+5 slashing; no multiple attack penalty"));

    grid->addWidget(type, 0, 0);
    grid->addWidget(name, 0, 1);
    grid->addWidget(attack, 0, 2);
    grid->addWidget(traits, 0, 3);
    grid->addWidget(action, 1, 0);
    grid->addWidget(damage, 1, 1, 1, 3);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(3, 3);

    auto at = [this, i]() -> monster::Strike & { return monster_->strikes[static_cast<size_t>(i)]; };
    connect(type, &QComboBox::activated, this, [this, at, type] {
        at().type = type->currentData().toString();
        emit changed();
    });
    connect(action, &QComboBox::activated, this, [this, at, action] {
        at().actions = action->currentData().toString();
        emit changed();
    });
    connect(name, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().name = text;
        emit changed();
    });
    connect(attack, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().attack = valueFromText(text);
        emit changed();
    });
    connect(traits, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().traits = text;
        emit changed();
    });
    connect(damage, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().damage = text;
        emit changed();
    });
    return row;
}

// An ability: name, where it goes, actions, traits, then the description.
QWidget *HazardEditor::specialRow(int i)
{
    const monster::Special &special = monster_->specials[static_cast<size_t>(i)];
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);

    auto *name = new QLineEdit(special.name);
    name->setPlaceholderText(tr("name"));
    QComboBox *category = makeCombo(categories());
    selectIn(category, special.type);
    category->setToolTip(tr("Where it goes in the statblock: Top, under the description; Reactions, "
                            "after the defenses; Actions, after the Routine and Strikes"));
    QComboBox *action = makeCombo(actionChoices());
    selectIn(action, special.actions);
    auto *traits = new QLineEdit(special.traits);
    traits->setPlaceholderText(tr("traits"));
    auto *description = new QPlainTextEdit(special.description);
    description->setTabChangesFocus(true);
    description->setFixedHeight(description->fontMetrics().lineSpacing() * 4 + 8);
    description->setPlaceholderText(tr("Trigger A creature ...; Effect ... (Trigger, Effect, Critical "
                                       "Success, ... are made bold)"));

    grid->addWidget(name, 0, 0);
    grid->addWidget(category, 0, 1);
    grid->addWidget(action, 0, 2);
    grid->addWidget(traits, 0, 3);
    grid->addWidget(description, 1, 0, 1, 4);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(3, 2);

    auto at = [this, i]() -> monster::Special & { return monster_->specials[static_cast<size_t>(i)]; };
    connect(name, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().name = text;
        emit changed();
    });
    connect(category, &QComboBox::activated, this, [this, at, category] {
        at().type = category->currentData().toString();
        emit changed();
    });
    connect(action, &QComboBox::activated, this, [this, at, action] {
        at().actions = action->currentData().toString();
        emit changed();
    });
    connect(traits, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().traits = text;
        emit changed();
    });
    connect(description, &QPlainTextEdit::textChanged, this, [this, at, description] {
        at().description = description->toPlainText();
        emit changed();
    });
    return row;
}

// Another part: its name, Hardness, HP and BT (or none), a note, its lists.
QWidget *HazardEditor::hpPartRow(int i)
{
    const monster::HpPart &part = monster_->hpParts[static_cast<size_t>(i)];
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);
    auto at = [this, i]() -> monster::HpPart & { return monster_->hpParts[static_cast<size_t>(i)]; };
    auto field = [&](int r, int c, int span, const QString &label, const QString &text,
                     const QString &placeholder, QString monster::HpPart::*member) {
        auto *edit = new QLineEdit(text);
        edit->setPlaceholderText(placeholder);
        grid->addWidget(new QLabel(label), r, c);
        grid->addWidget(edit, r, c + 1, 1, span);
        connect(edit, &QLineEdit::textEdited, this, [this, at, member](const QString &value) {
            at().*member = value;
            emit changed();
        });
        return edit;
    };
    field(0, 0, 3, tr("Part"), part.name, tr("spout"), &monster::HpPart::name);
    field(1, 0, 1, tr("Hardness"), part.hardness, tr("8"), &monster::HpPart::hardness)->setMaximumWidth(70);
    field(1, 2, 1, tr("HP"), part.hp, tr("32"), &monster::HpPart::hp)->setMaximumWidth(70);
    QLineEdit *bt = field(2, 0, 1, tr("BT"), part.bt, QString(), &monster::HpPart::bt);
    bt->setMaximumWidth(70);
    bool ok = false;
    const int hp = part.hp.trimmed().toInt(&ok);
    bt->setPlaceholderText(ok ? QString::number(hp / 2) : tr("half"));
    bt->setEnabled(!part.noBt);
    auto *noBt = new QCheckBox(tr("None"));
    noBt->setToolTip(tr("No Broken Threshold: it can't be broken, only destroyed"));
    noBt->setChecked(part.noBt);
    grid->addWidget(noBt, 2, 2, 1, 2);
    connect(noBt, &QCheckBox::toggled, this, [this, at, bt](bool on) {
        at().noBt = on;
        bt->setEnabled(!on);
        emit changed();
    });
    field(3, 0, 3, tr("Note"), part.note, tr("(per spout), or after the BT: each"), &monster::HpPart::note);
    field(4, 0, 3, tr("Immunities"), part.immunities, QString(), &monster::HpPart::immunities);
    field(5, 0, 3, tr("Weaknesses"), part.weaknesses, QString(), &monster::HpPart::weaknesses);
    field(6, 0, 3, tr("Resistances"), part.resistances, QString(), &monster::HpPart::resistances);
    grid->setColumnStretch(3, 1);
    return row;
}
