// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/MonsterEditor.h"

#include "Model/Adjustments.h"
#include "Model/Benchmarks.h"
#include "Model/Mythic.h"
#include "Model/RecallKnowledge.h"
#include "Model/Roadmaps.h"
#include "Model/TraitNotes.h"
#include "Widgets/EntryList.h"
#include "Widgets/TagCompleter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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

    const Choices &alignments()
    {
        static const Choices list{
            {"-", "(none)"}, {"lg", "LG"}, {"ng", "NG"}, {"cg", "CG"},
            {"ln", "LN"},    {"n", "N"},   {"cn", "CN"}, {"le", "LE"},
            {"ne", "NE"},    {"ce", "CE"}};
        return list;
    }

    const Choices &sizes()
    {
        static const Choices list{{"tiny", "Tiny"},   {"small", "Small"},
                                  {"medium", "Medium"}, {"large", "Large"},
                                  {"huge", "Huge"},   {"gargantuan", "Gargantuan"},
                                  {"-", "(none)"}};
        return list;
    }

    const Choices &types()
    {
        static const Choices list{
            {"-", "(none)"},       {"aberration", "Aberration"}, {"animal", "Animal"},
            {"astral", "Astral"},  {"beast", "Beast"},           {"celestial", "Celestial"},
            {"construct", "Construct"}, {"dragon", "Dragon"},    {"elemental", "Elemental"},
            {"ethereal", "Ethereal"}, {"fey", "Fey"},            {"fiend", "Fiend"},
            {"fungus", "Fungus"},  {"humanoid", "Humanoid"},     {"monitor", "Monitor"},
            {"ooze", "Ooze"},      {"plant", "Plant"},           {"spirit", "Spirit"},
            {"undead", "Undead"}};
        return list;
    }

    const Choices &strikeTypes()
    {
        static const Choices list{{"Melee", "Melee"}, {"Ranged", "Ranged"}};
        return list;
    }

    const Choices &categories()
    {
        static const Choices list{
            {"general", "General"}, {"offense", "Offense"}, {"defense", "Defense"}};
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

    // The link that copies a hint into its value box (benchmark names are
    // the other links in a hint label).
    const QString copyHintLink = QStringLiteral("=copy");

    // Like showHint(), with the number as a link that copies it into the
    // value box (as on the website) and `extraHtml` after it.
    void showCopyableHint(QLabel *label, const monster::Hint &hint, const QString &extraHtml = {})
    {
        showHint(label, hint);
        if (!hint.valid || (!hint.value && extraHtml.isEmpty()))
        {
            label->setTextFormat(Qt::PlainText);
            return;
        }
        const QString text = hint.value ? QStringLiteral("<a href=\"%1\">%2</a>")
                                              .arg(copyHintLink, hint.text.toHtmlEscaped())
                                        : hint.text.toHtmlEscaped();
        label->setTextFormat(Qt::RichText);
        label->setText(extraHtml.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, extraHtml));
        if (hint.value)
        {
            const QString tip = QObject::tr("Click the number to put %1 in the value box")
                                    .arg(*hint.value);
            label->setToolTip(label->toolTip().isEmpty() ? tip
                                                         : label->toolTip() + QLatin1Char('\n') + tip);
        }
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

    // "10th" ... "1st" for the rank rows (index 0 is 10th rank).
    QString rankName(int index)
    {
        static const QStringList names{"10th", "9th", "8th", "7th", "6th",
                                       "5th",  "4th", "3rd", "2nd", "1st"};
        return names.value(index);
    }

    // The cantrips' rank when none is given: half the level, rounded up
    // (at least 1), as the website works it out.
    int defaultCantripRank(int level)
    {
        return std::max(1, (std::max(level, 2) + 1) / 2);
    }

    QLineEdit *valueEdit()
    {
        auto *edit = new QLineEdit;
        edit->setMaximumWidth(70);
        edit->setAlignment(Qt::AlignRight);
        return edit;
    }

} // namespace

MonsterEditor::MonsterEditor(QWidget *parent) : QWidget(parent)
{
    // Three columns as on the website, each scrolling on its own; the
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

    // General.
    {
        auto *grid = new QGridLayout(addSection(0, 0, tr("General")));
        addLine(grid, 0, tr("Name"), [m]() -> QString & { return m().name; });

        level_ = new QSpinBox;
        level_->setRange(-1, 25);
        grid->addWidget(new QLabel(tr("Level")), 1, 0);
        auto *levelRow = new QHBoxLayout;
        levelRow->addWidget(level_);
        auto *shown = new QLineEdit;
        shown->setPlaceholderText(tr("same"));
        shown->setMaximumWidth(70);
        shown->setToolTip(tr("The level the statblock shows, when it differs from the "
                             "level the statistics are built for"));
        levelRow->addWidget(new QLabel(tr("Display level")));
        levelRow->addWidget(shown);
        // The level after the applied adjustments, which the statblock shows.
        adjustedLevel_ = new QLabel;
        adjustedLevel_->setToolTip(tr("The level after the applied adjustments: the one the "
                                      "statblock shows"));
        levelRow->addWidget(adjustedLevel_);
        levelRow->addStretch();
        grid->addLayout(levelRow, 1, 1, 1, 4);

        // The non-combat level (GM Core, Building NPCs): the skills, and
        // Perception or Will, ticked on their rows take their hints from it.
        nonCombat_ = new QLineEdit;
        nonCombat_->setMaximumWidth(50);
        nonCombat_->setPlaceholderText(tr("none"));
        nonCombat_->setValidator(new QIntValidator(-1, 25, nonCombat_));
        nonCombat_->setToolTip(tr("A level for a kind of non-combat challenge (a barrister: -1 in "
                                  "combat, 4 for legal matters). The rows ticked \"NC\" take their "
                                  "hints from it; not shown in the statblock."));
        auto *nonCombatLabel = new QLineEdit;
        nonCombatLabel->setPlaceholderText(tr("what for, e.g. legal matters (for you)"));
        auto *nonCombatRow = new QHBoxLayout;
        nonCombatRow->addWidget(nonCombat_);
        nonCombatRow->addWidget(nonCombatLabel, 1);
        grid->addWidget(new QLabel(tr("Non-combat level")), 2, 0);
        grid->addLayout(nonCombatRow, 2, 1, 1, 4);
        connect(nonCombat_, &QLineEdit::textEdited, this, [this](const QString &text) {
            bool ok = false;
            const int level = text.trimmed().toInt(&ok);
            monster_->nonCombatLevel = ok ? std::optional<int>(level) : std::nullopt;
            updateNonCombat();
            refreshHints();
            emit changed();
        });
        connect(nonCombatLabel, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->nonCombatLabel = text;
            emit changed();
        });
        loaders_.push_back([this, nonCombatLabel] {
            nonCombat_->setText(monster_->nonCombatLevel ? QString::number(*monster_->nonCombatLevel)
                                                         : QString());
            nonCombatLabel->setText(monster_->nonCombatLabel);
            updateNonCombat();
        });
        level_->setToolTip(tr("The level the statistics are built for. GM Core's tables "
                              "stop at 24; level 25's hints are extrapolated."));
        connect(level_, &QSpinBox::valueChanged, this, [this](int value) {
            if (!loading_)
            {
                monster_->level = Value(value);
                rebuildModifiers(); // whether each applies can change; refreshes the hints
                emit changed();
            }
        });
        connect(shown, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->showlevel = valueFromText(text);
            rebuildModifiers();
            emit changed();
        });
        loaders_.push_back([this, shown] {
            level_->setValue(monster_->level.number().value_or(0));
            shown->setText(monster_->showlevel.text());
        });
        hintUpdaters_.push_back([this] {
            const std::optional<int> level = monster::adjustedLevel(*monster_);
            adjustedLevel_->setText(level ? tr("adjusted %1").arg(*level) : QString());
            adjustedLevel_->setVisible(level.has_value());
        });

        // Alignment and Size, Type and Rarity: two to a row.
        auto addCombo = [&](int row, int column, const QString &label, const Choices &choices,
                            QString Monster::*field) {
            QComboBox *combo = makeCombo(choices);
            grid->addWidget(new QLabel(label), row, column);
            grid->addWidget(combo, row, column + 1);
            connect(combo, &QComboBox::activated, this, [this, combo, field] {
                monster_->*field = combo->currentData().toString();
                emit changed();
            });
            loaders_.push_back([this, combo, field] { selectIn(combo, monster_->*field); });
        };
        addCombo(3, 0, tr("Alignment"), alignments(), &Monster::alignment);
        addCombo(3, 2, tr("Size"), sizes(), &Monster::size);
        addCombo(4, 0, tr("Type"), types(), &Monster::type);

        // Rarity: kept among the traits, as the website keeps it ("Rare,
        // fire"), and shown here instead of in the Traits box.
        auto *rarity = makeCombo({{QString(), tr("(None)")},
                                  {QStringLiteral("uncommon"), tr("Uncommon")},
                                  {QStringLiteral("rare"), tr("Rare")},
                                  {QStringLiteral("unique"), tr("Unique")}});
        grid->addWidget(new QLabel(tr("Rarity")), 4, 2);
        grid->addWidget(rarity, 4, 3);
        grid->setColumnStretch(4, 1); // the dropdowns keep their width
        auto *traits = new QLineEdit;
        traits->setPlaceholderText(tr("fire, undead, ..."));
        grid->addWidget(new QLabel(tr("Traits")), 6, 0);
        grid->addWidget(traits, 6, 1, 1, 4);
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
            monster_->traits = r.isEmpty()           ? others
                               : others.isEmpty()    ? r
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
        addLine(grid, 5, tr("Creature label"), [m]() -> QString & { return m().creature; })
            ->setPlaceholderText(tr("Creature"));
        // GM Core's notes on the creature type and traits ("Undead: Senses
        // darkvision; ..."), under the traits, folded away on request.
        traitNotes_ = new QWidget;
        auto *notesLayout = new QVBoxLayout(traitNotes_);
        notesLayout->setContentsMargins(0, 0, 0, 0);
        notesLayout->setSpacing(2);
        traitNotesToggle_ = new QToolButton;
        traitNotesToggle_->setCheckable(true);
        traitNotesToggle_->setChecked(true);
        traitNotesToggle_->setAutoRaise(true);
        traitNotesToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        traitNotesToggle_->setToolTip(tr("What creatures with these traits typically have (GM Core, "
                                         "Trait Abilities)"));
        notesLayout->addWidget(traitNotesToggle_);
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("traitNotes"));
        frame->setStyleSheet(QStringLiteral(
            "#traitNotes { background: palette(alternate-base); border: 1px solid palette(mid); "
            "border-radius: 4px; }"));
        auto *frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(6, 4, 6, 4);
        traitNotesText_ = new QLabel;
        traitNotesText_->setWordWrap(true);
        traitNotesText_->setTextFormat(Qt::RichText);
        traitNotesText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QFont small = traitNotesText_->font();
        small.setPointSizeF(small.pointSizeF() * 0.9);
        traitNotesText_->setFont(small);
        frameLayout->addWidget(traitNotesText_);
        notesLayout->addWidget(frame);
        grid->addWidget(traitNotes_, 7, 1, 1, 4);
        connect(traitNotesToggle_, &QToolButton::toggled, this, [this, frame](bool open) {
            frame->setVisible(open);
            traitNotesToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        traitNotesToggle_->setArrowType(Qt::DownArrow);
        connect(this, &MonsterEditor::changed, this, &MonsterEditor::updateTraitNotes);

        // Tags: for sorting the collection, not shown in the statblock.
        auto *tags = new QLineEdit;
        tags->setPlaceholderText(tr("campaign, undead, ... (for finding monsters in the list)"));
        tagCompleter_ = new TagCompleter(tags, TagCompleter::Mode::Tags);
        grid->addWidget(new QLabel(tr("Tags")), 8, 0);
        grid->addWidget(tags, 8, 1, 1, 4);
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
        addLine(grid, 9, tr("Languages"), [m]() -> QString & { return m().languages; });
        addLine(grid, 10, tr("Info"), [m]() -> QString & { return m().info; });

        // Recall Knowledge: worked out from the level, rarity and traits;
        // where it goes, and whether with the Lore DCs.
        auto *recall = makeCombo({{QString(), tr("In the statblock")},
                                  {QStringLiteral("above"), tr("Above the description")},
                                  {QStringLiteral("below"), tr("Below the description")},
                                  {QStringLiteral("off"), tr("Off")}});
        recall->setToolTip(tr("The DC to identify it (GM Core's DC for its level, adjusted for "
                              "rarity) and the skills its creature traits call for"));
        auto *recallLore = new QCheckBox(tr("Lore DCs"));
        recallLore->setToolTip(tr("Also the DCs for an applicable Lore: unspecific (easy) and "
                                  "specific (very easy), before rarity"));
        auto *recallRow = new QHBoxLayout;
        recallRow->addWidget(recall);
        recallRow->addWidget(recallLore);
        recallRow->addStretch();
        grid->addWidget(new QLabel(tr("Recall Knowledge")), 11, 0);
        grid->addLayout(recallRow, 11, 1, 1, 4);
        // A creature without a creature type trait (aberration ... undead)
        // has no skill to identify it with.
        recallWarning_ = new QLabel(tr("It has no creature type trait (aberration, animal, beast, "
                                       "construct, dragon, humanoid, undead, ...): set its Type or "
                                       "add one to its Traits, or Recall Knowledge lists no skill."));
        recallWarning_->setWordWrap(true);
        grid->addWidget(recallWarning_, 12, 1, 1, 4);
        connect(recall, &QComboBox::activated, this, [this, recall, recallLore] {
            monster_->recallKnowledge = recall->currentData().toString();
            recallLore->setEnabled(monster_->recallKnowledge != QLatin1String("off"));
            emit changed();
        });
        connect(recallLore, &QCheckBox::toggled, this, [this](bool on) {
            if (!loading_ && monster_->recallLore != on)
            {
                monster_->recallLore = on;
                emit changed();
            }
        });
        loaders_.push_back([this, recall, recallLore] {
            selectIn(recall, monster_->recallKnowledge);
            recallLore->setChecked(monster_->recallLore);
            recallLore->setEnabled(monster_->recallKnowledge != QLatin1String("off"));
        });
        hintUpdaters_.push_back([this] { updateRecallWarning(); });
        connect(this, &MonsterEditor::changed, this, &MonsterEditor::updateRecallWarning);

        // Adjustments (Elite, Weak, ...), added from the Monster menu,
        // applied in this order.
        modifiersRow_ = new QWidget;
        auto *modifiersLayout = new QVBoxLayout(modifiersRow_);
        modifiersLayout->setContentsMargins(0, 0, 0, 0);
        modifiers_ = new QVBoxLayout;
        modifiers_->setSpacing(0);
        modifiersLayout->addLayout(modifiers_);
        stackWarning_ = new QLabel(tr("More than one adjustment: the rules advise against "
                                      "stacking them, so the creature's real difficulty may no "
                                      "longer match its level."));
        stackWarning_->setWordWrap(true);
        modifiersLayout->addWidget(stackWarning_);
        auto *modifiersLabel = new QLabel(tr("Adjustments"));
        grid->addWidget(modifiersLabel, 13, 0, Qt::AlignTop);
        grid->addWidget(modifiersRow_, 13, 1, 1, 4);
        // Only shown while some are applied.
        hintUpdaters_.push_back([this, modifiersLabel] {
            const bool any = !monster::appliedModifiers(*monster_).isEmpty();
            modifiersLabel->setVisible(any);
            modifiersRow_->setVisible(any);
        });
    }

    // Perception and senses.
    {
        auto *grid = new QGridLayout(addSection(0, 4, tr("Perception"), QStringLiteral("perception")));
        addStat(grid, 0, tr("Perception"), QStringLiteral("perception"));
    }

    // Attribute modifiers.
    {
        auto *grid = new QGridLayout(addSection(0, 2, tr("Attribute Modifiers"), QStringLiteral("attributes")));
        const QStringList names{tr("Strength"), tr("Dexterity"), tr("Constitution"),
                                tr("Intelligence"), tr("Wisdom"), tr("Charisma")};
        for (int i = 0; i < names.size(); ++i)
        {
            addStat(grid, i, names[i], QString::fromLatin1(monster::kAbilities[static_cast<size_t>(i)]));
        }
    }

    // Skills.
    {
        auto *grid = new QGridLayout(addSection(1, 0, tr("Skills"), QStringLiteral("skills")));
        int row = 0;
        for (const char *key : monster::kSkills)
        {
            QString name = QString::fromLatin1(key);
            name[0] = name[0].toUpper();
            addStat(grid, row++, name, QString::fromLatin1(key));
        }
        for (const char *key : monster::kLores)
        {
            addStat(grid, row++, tr("Lore"), QString::fromLatin1(key), true);
        }
        // More Lore skills (rare), after the website's two: rows made by
        // rebuildLores(), then the button to add one.
        skillsGrid_ = grid;
        skillsFixedItems_ = grid->count();
        firstExtraLoreRow_ = row;
    }

    // Items.
    {
        auto *box = new QVBoxLayout(addSection(1, 1, tr("Items")));
        auto *safe = new QLabel;
        safe->setToolTip(tr("The highest item level a creature of this level can carry "
                            "without giving the PCs too much treasure (GM Core, Safe Items)"));
        box->addWidget(safe);
        hintUpdaters_.push_back([this, safe] {
            const monster::Hint h = monster::hint(monster::Table::SafeItem,
                                                  monster_->level.number().value_or(0));
            safe->setText(tr("Safe item level: %1").arg(h.text));
        });
        addText(box, [m]() -> QString & { return m().items; }, 2);
    }

    // Defense.
    {
        auto *grid = new QGridLayout(addSection(0, 3, tr("Defense"), QStringLiteral("defense")));
        addStat(grid, 0, tr("Armor Class"), QStringLiteral("ac"));
        addStat(grid, 1, tr("Fortitude"), QStringLiteral("fortitude"));
        addStat(grid, 2, tr("Reflex"), QStringLiteral("reflex"));
        addStat(grid, 3, tr("Will"), QStringLiteral("will"));
        addLine(grid, 4, tr("Saves note"), [m]() -> QString & { return m().savenote; });
        addStat(grid, 5, tr("Hit Points"), QStringLiteral("hp"));
        addLine(grid, 6, tr("Hardness"), [m]() -> QString & { return m().hardness; })
            ->setPlaceholderText(tr("9 (empty: none)"));
        int row = 7;
        for (const auto &[label, key] : {std::pair{tr("Resistances"), "resistance"},
                                         std::pair{tr("Weaknesses"), "weakness"},
                                         std::pair{tr("Immunities"), "immunity"}})
        {
            auto *edit = new QLineEdit;
            const QString statKey = QString::fromLatin1(key);
            grid->addWidget(new QLabel(label), row, 0);
            if (monster::tableFor(statKey))
            {
                auto *range = new QLabel;
                range->setAlignment(Qt::AlignCenter);
                grid->addWidget(range, row, 1, 1, 2);
                grid->addWidget(edit, row, 3, 1, 2);
                edit->setPlaceholderText(tr("fire 10, ..."));
                hintUpdaters_.push_back([this, range, statKey] {
                    showHint(range, monster::statHint(*monster_, statKey));
                    range->setToolTip(range->toolTip().isEmpty()
                                          ? tr("Minimum–maximum for this level")
                                          : range->toolTip());
                });
            }
            else
            {
                grid->addWidget(edit, row, 1, 1, 4);
            }
            connect(edit, &QLineEdit::textEdited, this, [this, statKey](const QString &text) {
                monster_->stat(statKey).value = Value(text);
                emit changed();
            });
            loaders_.push_back([this, edit, statKey] { edit->setText(monster_->stat(statKey).value.text()); });
            ++row;
        }

        // Parts with Hit Points of their own (a hydra's heads): the label
        // of the creature's own line once there are any, then the parts.
        mainPartLabel_ = new QLabel(tr("Main part"));
        mainPart_ = new QLineEdit;
        mainPart_->setPlaceholderText(tr("body (empty: no label)"));
        mainPart_->setToolTip(tr("What the creature's own HP line is labelled once it has parts: "
                                 "\"HP 150 (body)\""));
        grid->addWidget(mainPartLabel_, row, 0);
        grid->addWidget(mainPart_, row++, 1, 1, 4);
        connect(mainPart_, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->mainPart = text;
            emit changed();
        });
        loaders_.push_back([this] { mainPart_->setText(monster_->mainPart); });
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
        auto *buttons = new QHBoxLayout;
        auto *add = new QPushButton(tr("Add HP Part"));
        add->setToolTip(tr("A part with Hit Points of its own, such as a hydra's head"));
        buttons->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this] {
            monster_->hpParts.emplace_back();
            rebuildHpParts();
            emit changed();
        });
        // A website file that put a part in the HP note: "50 (head),
        // Immunities ...".
        convertNote_ = new QPushButton(tr("Make the HP Note a Part"));
        convertNote_->setToolTip(tr("The HP note reads like a part (\"50 (head), Immunities "
                                    "...\", the website's workaround): move it into a part"));
        buttons->addWidget(convertNote_);
        buttons->addStretch();
        grid->addLayout(buttons, row++, 0, 1, 5);
        connect(convertNote_, &QPushButton::clicked, this, [this] {
            monster::Stat &hp = monster_->stat(QStringLiteral("hp"));
            if (const std::optional<monster::HpPart> part = monster::HpPart::fromNote(hp.note))
            {
                monster_->hpParts.push_back(*part);
                hp.note.clear();
                load(); // the note box and the parts
                emit changed();
            }
        });
        connect(this, &MonsterEditor::changed, this, &MonsterEditor::updateHpPartControls);
    }

    // Offense.
    {
        auto *box = addSection(1, 2, tr("Offense"), QStringLiteral("offense"));
        auto *layout = new QVBoxLayout(box);
        auto *grid = new QGridLayout;
        layout->addLayout(grid);
        addLine(grid, 0, tr("Speed"), [m]() -> QString & { return m().speed; })
            ->setPlaceholderText(tr("25 feet, fly 30 feet"));

        auto *table = new QGridLayout;
        layout->addLayout(table);
        // Low to extreme from left to right, as on the website.
        QStringList columns = monster::benchmarks(monster::Table::StrikeAttack);
        std::reverse(columns.begin(), columns.end());
        for (int c = 0; c < columns.size(); ++c)
        {
            QString name = columns[c];
            name[0] = name[0].toUpper();
            auto *head = new QLabel(name);
            head->setAlignment(Qt::AlignCenter);
            table->addWidget(head, 0, c + 1);
        }
        for (const auto &[row, label, kind] :
             {std::tuple{1, tr("Attack bonus"), monster::Table::StrikeAttack},
              std::tuple{2, tr("Strike damage"), monster::Table::StrikeDamage}})
        {
            table->addWidget(new QLabel(label), row, 0);
            for (int c = 0; c < columns.size(); ++c)
            {
                auto *cell = new QLabel;
                cell->setAlignment(Qt::AlignCenter);
                table->addWidget(cell, row, c + 1);
                hintUpdaters_.push_back([this, cell, kind, name = columns[c]] {
                    showHint(cell, monster::hint(kind, monster_->level.number().value_or(0), name));
                });
            }
        }

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

    // Spells: DC and attack (with the area damage to aim for), the kind of
    // spellcasting, focus points, rituals and the spells by rank.
    {
        auto *grid = new QGridLayout(addSection(1, 3, tr("Spells"), QStringLiteral("spells")));
        addStat(grid, 0, tr("Spell attack"), QStringLiteral("spellattack"));
        addStat(grid, 1, tr("Spell DC"), QStringLiteral("spelldc"));
        grid->addWidget(new QLabel(tr("Area damage")), 2, 0);
        auto *area = new QLabel;
        area->setToolTip(tr("Unlimited: an ability usable at will. Limited: one usable "
                            "only once in a while (GM Core, Area Damage). Moderate DC: "
                            "the moderate spell DC, for its save"));
        grid->addWidget(area, 2, 1, 1, 4);
        hintUpdaters_.push_back([this, area] {
            const int level = monster_->level.number().value_or(0);
            const monster::Hint unlimited = monster::hint(monster::Table::AreaDamage, level, "unlimited");
            const monster::Hint limited = monster::hint(monster::Table::AreaDamage, level, "limited");
            const monster::Hint dc = monster::hint(monster::Table::SpellDc, level, "moderate");
            area->setText(tr("unlimited %1, limited %2, moderate DC %3")
                              .arg(unlimited.text, limited.text, dc.text));
        });

        addLine(grid, 3, tr("Spell type"), [m]() -> QString & { return m().spelltype; })
            ->setPlaceholderText(tr("Arcane Innate, Divine Prepared, ..."));
        // Cantrip rank and focus points on one line.
        grid->addWidget(new QLabel(tr("Cantrip rank")), 4, 0);
        auto *numbers = new QHBoxLayout;
        QLineEdit *cantrips = valueEdit();
        cantrips->setToolTip(tr("The rank cantrips are cast at; empty: half the level, rounded up"));
        numbers->addWidget(cantrips);
        numbers->addWidget(new QLabel(tr("Focus points")));
        auto *focus = new QSpinBox;
        focus->setRange(0, 3);
        numbers->addWidget(focus);
        numbers->addStretch();
        grid->addLayout(numbers, 4, 1, 1, 4);
        connect(cantrips, &QLineEdit::textEdited, this, [this](const QString &text) {
            monster_->cantriplevel = valueFromText(text);
            emit changed();
        });
        connect(focus, &QSpinBox::valueChanged, this, [this](int value) {
            if (!loading_)
            {
                monster_->focuspoints = Value(value);
                emit changed();
            }
        });
        loaders_.push_back([this, cantrips, focus] {
            cantrips->setText(monster_->cantriplevel.text());
            focus->setValue(monster_->focuspoints.number().value_or(0));
        });
        hintUpdaters_.push_back([this, cantrips] {
            cantrips->setPlaceholderText(
                QString::number(defaultCantripRank(monster_->level.number().value_or(0))));
        });
        addLine(grid, 5, tr("Ritual type"), [m]() -> QString & { return m().ritualtype; })
            ->setPlaceholderText(tr("Arcane, Divine, ... (empty: no rituals)"));
        addLine(grid, 6, tr("Rituals"), [m]() -> QString & { return m().rituals; });
        for (int i = 0; i < monster::kSpellRanks; ++i)
        {
            const QString label = i == monster::kCantrips ? tr("Cantrips") : rankName(i);
            addLine(grid, 7 + i, label, [m, i]() -> QString & { return m().spells[static_cast<size_t>(i)]; });
        }
        addLine(grid, 7 + monster::kSpellRanks, tr("Constant"),
                [m]() -> QString & { return m().constant; });
    }

    // Additional spells: more spellcasting entries, such as innate spells
    // beside prepared ones.
    {
        auto *box = addSection(2, 0, tr("Additional Spells"));
        auto *layout = new QVBoxLayout(box);
        spellLists_ = new EntryList;
        spellLists_->setDeleteToolTip(tr("Delete this spell list"));
        spellLists_->setRowFactory([this](int i) { return spellListRow(i); });
        layout->addWidget(spellLists_);
        connect(spellLists_, &EntryList::moveRequested, this, [this](int from, int to) {
            moveEntry(monster_->morespells, from, to);
            rebuildSpellLists();
            emit changed();
        });
        connect(spellLists_, &EntryList::removeRequested, this, [this](int i) {
            monster_->morespells.erase(monster_->morespells.begin() + i);
            rebuildSpellLists();
            emit changed();
        });
        auto *add = new QPushButton(tr("Add Spell List"));
        layout->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            monster_->morespells.emplace_back();
            rebuildSpellLists();
            emit changed();
        });
    }

    // Special abilities.
    {
        auto *box = addSection(2, 1, tr("Special Abilities"), QStringLiteral("specials"));
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
        // Mythic's abilities sit among the others, to be put in order, but
        // are changed in its dialog.
        specials_->setRemovable([this](int i) {
            return monster_->specials[static_cast<size_t>(i)].generated.isEmpty();
        });
        connect(specials_, &EntryList::removeRequested, this, [this](int i) {
            if (!monster_->specials[static_cast<size_t>(i)].generated.isEmpty())
            {
                return;
            }
            monster_->specials.erase(monster_->specials.begin() + i);
            rebuildSpecials();
            emit changed();
        });
        auto *add = new QPushButton(tr("Add Ability"));
        layout->addWidget(add, 0, Qt::AlignLeft);
        connect(add, &QPushButton::clicked, this, [this] {
            monster_->specials.emplace_back();
            rebuildSpecials();
            emit changed();
        });
    }

    // Description.
    {
        auto *box = new QVBoxLayout(addSection(0, 1, tr("Description")));
        addText(box, [m]() -> QString & { return m().description; }, 4)
            ->setPlaceholderText(tr("Markdown: **bold**, _italic_. [ac], [dex], [dc], ... become "
                                    "the monster's values, % its name (\\% for a percent sign)."));
    }

    reflow(kColumns);
    setSectionsEnabled(false);
}

QWidget *MonsterEditor::addSection(int group, int order, const QString &title,
                                   const QString &noteKey)
{
    auto *box = new QGroupBox(title);
    // As tall as its contents: spare height goes below the last section.
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    sections_.push_back({group, order, box});
    std::stable_sort(sections_.begin(), sections_.end(), [](const Section &a, const Section &b) {
        return std::pair(a.group, a.order) < std::pair(b.group, b.order);
    });
    if (noteKey.isEmpty())
    {
        return box;
    }

    // The road map's note for this section, at its top in a grey box with
    // a close button, as on the website.
    auto *outer = new QVBoxLayout(box);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *holder = new QWidget;
    auto *holderLayout = new QHBoxLayout(holder);
    holderLayout->setContentsMargins(holderLayout->contentsMargins().left(),
                                     holderLayout->contentsMargins().top(),
                                     holderLayout->contentsMargins().right(), 0);
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("roadmapNote"));
    frame->setStyleSheet(QStringLiteral(
        "#roadmapNote { background: palette(alternate-base); border: 1px solid palette(mid); "
        "border-radius: 4px; }"));
    holderLayout->addWidget(frame);
    auto *grid = new QGridLayout(frame);
    grid->setContentsMargins(6, 4, 4, 4);
    auto *heading = new QLabel;
    QFont small = heading->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    QFont bold = small;
    bold.setBold(true);
    heading->setFont(bold);
    auto *text = new QLabel;
    text->setFont(small);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *close = new QToolButton;
    close->setText(QStringLiteral("×"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Hide this note (Creature or Hazard > Roadmap > Clear Roadmap Notes removes them all)"));
    grid->addWidget(heading, 0, 0);
    grid->addWidget(close, 0, 1, Qt::AlignRight | Qt::AlignTop);
    grid->addWidget(text, 1, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    outer->addWidget(holder);
    auto *contents = new QWidget;
    outer->addWidget(contents);

    hintUpdaters_.push_back([this, holder, heading, text, noteKey] {
        if (monster_->roadmap != notesRoadmap_)
        {
            notesRoadmap_ = monster_->roadmap;
            closedNotes_.clear();
        }
        const monster::Roadmap *roadmap = monster::findRoadmap(monster_->roadmap);
        const QString note = monster::roadmapNote(*monster_, noteKey);
        if (roadmap && !note.isEmpty() && !closedNotes_.contains(noteKey))
        {
            heading->setText(roadmap->title().toUpper());
            text->setText(note);
            holder->show();
        }
        else
        {
            holder->hide();
        }
    });
    connect(close, &QToolButton::clicked, this, [this, holder, noteKey] {
        closedNotes_ << noteKey;
        holder->hide();
    });
    return contents;
}

void MonsterEditor::setCornerWidget(QWidget *widget)
{
    firstColumn_->addWidget(widget);
    // The form gets three quarters of the height, the corner the rest.
    firstColumn_->setSizes({3000, 1000});
}

QSplitter *MonsterEditor::cornerSplitter() const
{
    return firstColumn_;
}

void MonsterEditor::setKnownTags(const QStringList &tags)
{
    tagCompleter_->setTags(tags);
}

void MonsterEditor::setSectionsEnabled(bool enabled)
{
    for (const Section &section : sections_)
    {
        section.box->setEnabled(enabled);
    }
}

// Puts the sections in `columns` columns: the website's three, or with the
// later groups stacked when there's no room for three.
void MonsterEditor::reflow(int columns)
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

void MonsterEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Room for a column: about 60 characters of the form's font (the
    // special abilities need about that much).
    const int columnWidth = fontMetrics().averageCharWidth() * 60;
    const int columns = std::clamp(width() / std::max(columnWidth, 1), 1, kColumns);
    if (columns != columns_)
    {
        reflow(columns);
    }
}

void MonsterEditor::setMonster(monster::Monster *monster)
{
    monster_ = monster;
    setSectionsEnabled(monster_ != nullptr);
    if (monster_)
    {
        load();
    }
}

void MonsterEditor::load()
{
    loading_ = true;
    for (const auto &loader : loaders_)
    {
        loader();
    }
    rebuildStrikes();
    monster::syncGeneratedSpecials(*monster_);
    rebuildSpecials();
    rebuildHpParts();
    rebuildLores();
    updateTraitNotes();
    rebuildSpellLists();
    rebuildModifiers();
    refreshHints();
    loading_ = false;
}

void MonsterEditor::rebuildModifiers()
{
    // The abilities the adjustments make can change with them (and with
    // the level).
    if (monster::syncGeneratedSpecials(*monster_))
    {
        rebuildSpecials();
    }
    while (QLayoutItem *item = modifiers_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    const QStringList ids = monster::appliedModifiers(*monster_);
    const QList<bool> inEffect = monster::modifiersInEffect(*monster_);
    // Changes the list once the click is over (the rebuild deletes the
    // button that was clicked).
    auto change = [this](std::function<void(monster::Monster &)> edit) {
        QTimer::singleShot(0, this, [this, edit] {
            edit(*monster_);
            rebuildModifiers();
            emit changed();
        });
    };
    for (int i = 0; i < ids.size(); ++i)
    {
        const monster::Modifier *modifier = monster::findModifier(ids[i]);
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        QString text = modifier ? modifier->name : tr("%1 (not in this version)").arg(ids[i]);
        if (const monster::MythicTemplate *t = monster::findMythicTemplate(
                monster::modifierOptions(*monster_, i).value(QStringLiteral("template")).toString());
            t && modifier && modifier->dialog == QLatin1String("mythic"))
        {
            text = tr("%1 (%2)").arg(modifier->name, t->name);
        }
        if (modifier && !inEffect.value(i))
        {
            text = tr("%1 (no effect at this level)").arg(modifier->name);
        }
        auto *name = new QLabel(text);
        name->setEnabled(inEffect.value(i));
        if (modifier)
        {
            name->setToolTip(modifier->rules);
        }
        layout->addWidget(name, 1);
        auto button = [&](const QString &text, const QString &tip, bool enabled) {
            auto *b = new QToolButton;
            b->setText(text);
            b->setToolTip(tip);
            b->setAutoRaise(true);
            b->setEnabled(enabled);
            layout->addWidget(b);
            return b;
        };
        if (modifier && !modifier->dialog.isEmpty())
        {
            connect(button(QStringLiteral("…"), tr("Change its choices"), true), &QToolButton::clicked,
                    this, [this, i] { emit editModifierRequested(i); });
        }
        connect(button(QStringLiteral("▲"), tr("Apply earlier"), i > 0), &QToolButton::clicked, this,
                [change, i] { change([i](monster::Monster &m) { monster::moveAppliedModifier(m, i, i - 1); }); });
        connect(button(QStringLiteral("▼"), tr("Apply later"), i + 1 < ids.size()),
                &QToolButton::clicked, this,
                [change, i] { change([i](monster::Monster &m) { monster::moveAppliedModifier(m, i, i + 1); }); });
        connect(button(QStringLiteral("✕"), tr("Remove this adjustment"), true), &QToolButton::clicked,
                this, [change, i] { change([i](monster::Monster &m) { monster::removeAppliedModifier(m, i); }); });
        modifiers_->addWidget(row);
        // What its rules leave to the GM, in a grey box like the road map
        // notes.
        if (modifier && !modifier->optional.isEmpty() && inEffect.value(i))
        {
            auto *note = new QLabel(modifier->optional);
            note->setObjectName(QStringLiteral("adjustmentNote"));
            note->setStyleSheet(QStringLiteral(
                "#adjustmentNote { background: palette(alternate-base); border: 1px solid "
                "palette(mid); border-radius: 4px; padding: 4px 6px; }"));
            QFont small = note->font();
            small.setPointSizeF(small.pointSizeF() * 0.9);
            note->setFont(small);
            note->setWordWrap(true);
            note->setTextInteractionFlags(Qt::TextSelectableByMouse);
            modifiers_->addWidget(note);
        }
    }
    stackWarning_->setVisible(ids.size() > 1);
    refreshHints();
}

void MonsterEditor::updateRecallWarning()
{
    // As the statblock shows it: a template can give it its type (Ghost:
    // undead).
    recallWarning_->setVisible(monster_ && monster_->recallKnowledge != QLatin1String("off") &&
                               !monster::hasCreatureTypeTrait(monster::adjusted(*monster_)));
}

void MonsterEditor::refreshHints()
{
    if (!monster_)
    {
        return;
    }
    for (const auto &update : hintUpdaters_)
    {
        update();
    }
    for (const auto &update : listHintUpdaters_)
    {
        update();
    }
    for (const auto &update : extraHints_)
    {
        update();
    }
}

QLineEdit *MonsterEditor::addLine(QGridLayout *grid, int row, const QString &label,
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

void MonsterEditor::addStat(QGridLayout *grid, int row, const QString &label,
                            const QString &key, bool loreName)
{
    QLineEdit *value = valueEdit();
    auto *note = new QLineEdit;
    note->setPlaceholderText(key == QLatin1String("perception") ? tr("senses") : tr("note"));
    if (loreName)
    {
        auto *name = new QLineEdit;
        name->setPlaceholderText(tr("Lore"));
        grid->addWidget(name, row, 0);
        connect(name, &QLineEdit::textEdited, this, [this, key](const QString &text) {
            monster_->stat(key).name = text;
            emit changed();
        });
        statLoaders_->push_back([this, name, key] { name->setText(monster_->stat(key).name); });
    }
    else
    {
        grid->addWidget(new QLabel(label), row, 0);
    }
    // Benchmark and its hint, as on the website.
    const std::optional<monster::Table> table = monster::tableFor(key);
    auto *hint = new QLabel;
    hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hint->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("+88 to +88")));
    QComboBox *benchmark = nullptr;
    if (table)
    {
        benchmark = makeCombo(benchmarkChoices(*table));
        grid->addWidget(benchmark, row, 1);
        connect(benchmark, &QComboBox::activated, this, [this, key, benchmark] {
            monster_->stat(key).benchmark = benchmark->currentData().toString();
            refreshHints();
            emit changed();
        });
        statLoaders_->push_back([this, benchmark, key] { selectIn(benchmark, monster_->stat(key).benchmark); });
    }
    grid->addWidget(hint, row, 2);
    grid->addWidget(value, row, 3);
    grid->addWidget(note, row, 4);
    grid->setColumnStretch(4, 1);
    // Follows the non-combat level? (Skills, Perception and Will; shown once
    // the monster has one.)
    if (key == QLatin1String("perception") || key == QLatin1String("will") ||
        monster::tableFor(key) == monster::Table::Skill)
    {
        auto *follow = new QCheckBox(tr("NC"));
        follow->setToolTip(tr("Use the non-combat level's tables for this hint"));
        grid->addWidget(follow, row, 5);
        statNcBoxes_->push_back(follow);
        connect(follow, &QCheckBox::toggled, this, [this, key](bool on) {
            if (loading_)
            {
                return;
            }
            monster_->nonCombatStats.removeAll(key);
            if (on)
            {
                monster_->nonCombatStats << key;
            }
            refreshHints();
            emit changed();
        });
        statLoaders_->push_back([this, follow, key] {
            follow->setChecked(monster_->nonCombatStats.contains(key));
        });
    }
    // An empty value stands for the hint in the statblock (core statistics):
    // shown greyed in the box.
    statHints_->push_back([this, hint, value, key] {
        const monster::Hint h = monster::statHint(*monster_, key);
        // The other benchmarks the road map allows ("high or extreme"), as
        // links that switch to them.
        QString alternatives;
        QString roadmapTip;
        if (const QStringList others = monster::roadmapAlternatives(*monster_, key);
            !others.isEmpty() && h.valid)
        {
            const monster::Roadmap *roadmap = monster::findRoadmap(monster_->roadmap);
            QStringList links;
            QStringList tips;
            for (const QString &other : others)
            {
                links << QStringLiteral("<a href=\"%1\">%1</a>").arg(other.toHtmlEscaped());
                monster::Hint alt = monster::hint(*monster::tableFor(key),
                                                  monster_->level.number().value_or(0), other);
                tips << QStringLiteral("%1 (%2)").arg(other, alt.valid ? alt.text : tr("none"));
            }
            alternatives = QStringLiteral("<small>or %1</small>").arg(links.join(QStringLiteral(", ")));
            roadmapTip = tr("The %1 roadmap also allows: %2. Click one to switch to it.")
                             .arg(roadmap ? roadmap->title() : QString(), tips.join(QStringLiteral(", ")));
        }
        showCopyableHint(hint, h, alternatives);
        if (!roadmapTip.isEmpty())
        {
            hint->setToolTip(hint->toolTip().isEmpty() ? roadmapTip
                                                       : roadmapTip + QLatin1Char('\n') + hint->toolTip());
        }
        value->setPlaceholderText(monster::usesHint(key) && h.value ? QString::number(*h.value)
                                                                    : QString());
    });
    connect(hint, &QLabel::linkActivated, this, [this, key, benchmark, value](const QString &name) {
        if (name == copyHintLink)
        {
            if (const monster::Hint h = monster::statHint(*monster_, key); h.value)
            {
                value->setText(QString::number(*h.value));
                monster_->stat(key).value = Value(*h.value);
                emit changed();
            }
            return;
        }
        monster_->stat(key).benchmark = name;
        if (benchmark)
        {
            selectIn(benchmark, name);
        }
        refreshHints();
        emit changed();
    });
    connect(value, &QLineEdit::textEdited, this, [this, key](const QString &text) {
        monster_->stat(key).value = valueFromText(text);
        emit changed();
    });
    connect(note, &QLineEdit::textEdited, this, [this, key](const QString &text) {
        monster_->stat(key).note = text;
        emit changed();
    });
    statLoaders_->push_back([this, value, note, key] {
        const monster::Stat &stat = monster_->stat(key);
        value->setText(stat.value.text());
        note->setText(stat.note);
    });
}

QPlainTextEdit *MonsterEditor::addText(QVBoxLayout *layout, std::function<QString &()> field,
                                       int lines)
{
    auto *edit = new QPlainTextEdit;
    edit->setTabChangesFocus(true);
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * (lines + 1) + 8);
    layout->addWidget(edit);
    connect(edit, &QPlainTextEdit::textChanged, this, [this, edit, field] {
        if (!loading_)
        {
            field() = edit->toPlainText();
            emit changed();
        }
    });
    loaders_.push_back([edit, field] { edit->setPlainText(field()); });
    return edit;
}

void MonsterEditor::rebuildStrikes()
{
    strikes_->rebuild(static_cast<int>(monster_->strikes.size()));
}

void MonsterEditor::rebuildSpecials()
{
    specials_->rebuild(static_cast<int>(monster_->specials.size()));
}

void MonsterEditor::updateNonCombat()
{
    const bool on = monster_ && monster_->nonCombatLevel.has_value();
    for (const auto *boxes : {&nonCombatBoxes_, &extraNcBoxes_})
    {
        for (QCheckBox *box : *boxes)
        {
            box->setVisible(on);
        }
    }
}

void MonsterEditor::rebuildLores()
{
    // The rows past the fixed ones go, with what updated them.
    while (skillsGrid_->count() > skillsFixedItems_)
    {
        QLayoutItem *item = skillsGrid_->takeAt(skillsGrid_->count() - 1);
        delete item->widget();
        delete item;
    }
    extraLoaders_.clear();
    extraHints_.clear();
    extraNcBoxes_.clear();

    // A row like the website's two for each extra one, with a delete button.
    statLoaders_ = &extraLoaders_;
    statHints_ = &extraHints_;
    statNcBoxes_ = &extraNcBoxes_;
    int row = firstExtraLoreRow_;
    const QStringList keys = monster_->loreKeys().mid(2);
    for (const QString &key : keys)
    {
        addStat(skillsGrid_, row, tr("Lore"), key, true);
        auto *remove = new QToolButton;
        remove->setText(QStringLiteral("✕"));
        remove->setAutoRaise(true);
        remove->setToolTip(tr("Delete this Lore skill"));
        skillsGrid_->addWidget(remove, row++, 6);
        // Once the click is over: the rebuild deletes the button.
        connect(remove, &QToolButton::clicked, this, [this, key] {
            QTimer::singleShot(0, this, [this, key] {
                monster_->removeLore(key);
                rebuildLores();
                emit changed();
            });
        });
    }
    statLoaders_ = &loaders_;
    statHints_ = &hintUpdaters_;
    statNcBoxes_ = &nonCombatBoxes_;

    auto *add = new QPushButton(tr("Add Lore"));
    add->setToolTip(tr("Another Lore skill, past the two the website has"));
    skillsGrid_->addWidget(add, row, 0, 1, 2, Qt::AlignLeft);
    connect(add, &QPushButton::clicked, this, [this] {
        QTimer::singleShot(0, this, [this] {
            monster_->addLore();
            rebuildLores();
            emit changed();
        });
    });

    const bool wasLoading = loading_;
    loading_ = true;
    for (const auto &load : extraLoaders_)
    {
        load();
    }
    loading_ = wasLoading;
    for (const auto &update : extraHints_)
    {
        update();
    }
    updateNonCombat();
}

void MonsterEditor::updateTraitNotes()
{
    if (!monster_)
    {
        return;
    }
    const std::vector<monster::TraitNote> found = monster::traitNotes(*monster_);
    traitNotes_->setVisible(!found.empty());
    QStringList names;
    QString html;
    // The notes' Markdown: **label**, _book title_.
    static const QRegularExpression bold(QStringLiteral(R"(\*\*(.+?)\*\*)"));
    static const QRegularExpression italic(QStringLiteral(R"(\b_(.+?)_\b)"));
    for (const monster::TraitNote &note : found)
    {
        // A homebrew note says so (it can sit beside a built-in one).
        const QString title = note.file.isEmpty() ? note.name.toUpper() : note.name.toUpper() + tr(" (homebrew)");
        if (!names.contains(note.name))
        {
            names << note.name;
        }
        html += QStringLiteral("<p style='margin:0 0 4px 0'><b>%1</b>").arg(title.toHtmlEscaped());
        for (const QString &line : note.lines)
        {
            QString text = line.toHtmlEscaped();
            text.replace(bold, QStringLiteral("<b>\\1</b>")).replace(italic, QStringLiteral("<i>\\1</i>"));
            html += QStringLiteral("<br>") + text;
        }
        html += QStringLiteral("</p>");
    }
    traitNotesToggle_->setText(tr("Trait notes: %1").arg(names.join(QStringLiteral(", "))));
    traitNotesText_->setText(html);
}

void MonsterEditor::rebuildHpParts()
{
    hpParts_->rebuild(static_cast<int>(monster_->hpParts.size()));
    updateHpPartControls();
}

void MonsterEditor::updateHpPartControls()
{
    if (!monster_)
    {
        return;
    }
    const bool parts = !monster_->hpParts.empty();
    mainPartLabel_->setVisible(parts);
    mainPart_->setVisible(parts);
    hpParts_->setVisible(parts);
    convertNote_->setVisible(
        monster::HpPart::fromNote(monster_->stat(QStringLiteral("hp")).note).has_value());
}

// A part with Hit Points of its own: name, HP and note, then its lists.
QWidget *MonsterEditor::hpPartRow(int i)
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
    field(0, 0, 1, tr("Part"), part.name, tr("head"), &monster::HpPart::name);
    field(0, 2, 1, tr("HP"), part.hp, tr("50"), &monster::HpPart::hp)->setMaximumWidth(70);
    field(1, 0, 3, tr("Note"), part.note, tr("head regrowth, ..."), &monster::HpPart::note);
    field(2, 0, 3, tr("Immunities"), part.immunities, tr("area damage, ..."), &monster::HpPart::immunities);
    field(3, 0, 3, tr("Resistances"), part.resistances, tr("fire 5, ..."), &monster::HpPart::resistances);
    field(4, 0, 3, tr("Weaknesses"), part.weaknesses, tr("slashing 5, ..."), &monster::HpPart::weaknesses);
    grid->setColumnStretch(1, 1);
    return row;
}

void MonsterEditor::rebuildSpellLists()
{
    listHintUpdaters_.clear(); // their labels go with the old rows
    spellLists_->rebuild(static_cast<int>(monster_->morespells.size()));
    for (const auto &update : listHintUpdaters_)
    {
        update();
    }
    for (const auto &update : extraHints_)
    {
        update();
    }
}

// A Strike: type, name, attack, traits, then the damage.
QWidget *MonsterEditor::strikeRow(int i)
{
    const monster::Strike &strike = monster_->strikes[static_cast<size_t>(i)];
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);

    QComboBox *type = makeCombo(strikeTypes());
    selectIn(type, strike.type);
    auto *name = new QLineEdit(strike.name);
    name->setPlaceholderText(tr("name"));
    QLineEdit *attack = valueEdit();
    attack->setText(strike.attack.text());
    attack->setPlaceholderText(tr("+0"));
    auto *traits = new QLineEdit(strike.traits);
    traits->setPlaceholderText(tr("traits"));
    auto *damage = new QLineEdit(strike.damage);
    damage->setPlaceholderText(tr("damage, e.g. 2d8+6 slashing"));

    grid->addWidget(type, 0, 0);
    grid->addWidget(name, 0, 1);
    grid->addWidget(attack, 0, 2);
    grid->addWidget(traits, 0, 3);
    grid->addWidget(damage, 1, 1, 1, 3);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(3, 3);

    auto at = [this, i]() -> monster::Strike & { return monster_->strikes[static_cast<size_t>(i)]; };
    connect(type, &QComboBox::activated, this, [this, at, type] {
        at().type = type->currentData().toString();
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

// An ability: name, category, actions, traits, then the description.
QWidget *MonsterEditor::specialRow(int i)
{
    const monster::Special &special = monster_->specials[static_cast<size_t>(i)];
    if (!special.generated.isEmpty())
    {
        return generatedRow(special);
    }
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);

    auto *name = new QLineEdit(special.name);
    name->setPlaceholderText(tr("name"));
    QComboBox *category = makeCombo(categories());
    selectIn(category, special.type);
    category->setToolTip(tr("Where it goes in the statblock: with the senses and "
                            "skills, with the defenses, or with the attacks"));
    QComboBox *action = makeCombo(actionChoices());
    selectIn(action, special.actions);
    auto *traits = new QLineEdit(special.traits);
    traits->setPlaceholderText(tr("traits"));
    auto *description = new QPlainTextEdit(special.description);
    description->setTabChangesFocus(true);
    description->setFixedHeight(description->fontMetrics().lineSpacing() * 4 + 8);
    description->setPlaceholderText(tr("[ac], [dex], [dc], ... become the monster's values, "
                                       "% its name (\\% for a percent sign)"));

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

// An ability an adjustment makes (Mythic's): shown as the statblock has it,
// greyed, to be moved but changed only through the adjustment.
QWidget *MonsterEditor::generatedRow(const monster::Special &place)
{
    monster::Special shown = place;
    for (const monster::Special &s : monster::adjusted(*monster_).specials)
    {
        if (s.generated == place.generated)
        {
            shown = s;
            break;
        }
    }
    auto *row = new QWidget;
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    const QString category = [&] {
        for (const auto &[key, label] : categories())
        {
            if (key == shown.type)
            {
                return label;
            }
        }
        return shown.type;
    }();
    QString kind = category;
    for (const auto &[key, label] : actionChoices())
    {
        if (key == shown.actions && key != QLatin1String("none"))
        {
            kind += QStringLiteral(", ") + label.toLower();
        }
    }
    if (!shown.under.isEmpty())
    {
        kind += tr(", listed under Mythic Power");
    }
    const QString source = monster::generatedSource(place.generated);
    auto *title = new QLabel(tr("<b>%1</b> &nbsp;<i>%2, from %3</i>")
                                 .arg(shown.name.toHtmlEscaped(), kind.toHtmlEscaped(), source.toHtmlEscaped()));
    layout->addWidget(title);
    QString text = shown.description;
    text.remove(QStringLiteral("**")).replace(QStringLiteral("\\%"), QStringLiteral("%"));
    if (!shown.traits.isEmpty())
    {
        text.prepend(QStringLiteral("(%1) ").arg(shown.traits));
    }
    if (!text.isEmpty())
    {
        auto *body = new QLabel(text);
        body->setTextFormat(Qt::PlainText);
        body->setWordWrap(true);
        layout->addWidget(body);
    }
    row->setEnabled(false);
    const bool mythic = !place.generated.contains(QLatin1Char(':'));
    row->setToolTip(mythic ? tr("Made by Mythic: change it in Creature or Hazard > Adjustments > Mythic... It can be "
                                "moved among the other abilities.")
                           : tr("Made by the %1 adjustment: removing the adjustment removes it. It can "
                                "be moved among the other abilities.")
                                 .arg(source));
    return row;
}

// An additional spell list: its name, DC and attack (with benchmarks and
// hints, as the main list), cantrip rank, focus points, a note and the
// spells by rank.
QWidget *MonsterEditor::spellListRow(int i)
{
    const monster::SpellList &list = monster_->morespells[static_cast<size_t>(i)];
    auto at = [this, i]() -> monster::SpellList & { return monster_->morespells[static_cast<size_t>(i)]; };
    auto *row = new QWidget;
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setColumnStretch(4, 1);

    // A line spanning the value columns.
    auto line = [&](int r, const QString &label, const QString &text,
                    std::function<QString &()> field) {
        auto *edit = new QLineEdit(text);
        grid->addWidget(new QLabel(label), r, 0);
        grid->addWidget(edit, r, 1, 1, 4);
        connect(edit, &QLineEdit::textEdited, this, [this, field](const QString &value) {
            field() = value;
            emit changed();
        });
        return edit;
    };

    line(0, tr("Spell type"), list.name, [at]() -> QString & { return at().name; })
        ->setPlaceholderText(tr("Occult Innate, ..."));

    // Spell attack and DC.
    for (const auto &[r, label, table, member] :
         {std::tuple{1, tr("Spell attack"), monster::Table::SpellAttack, &monster::SpellList::spellattack},
          std::tuple{2, tr("Spell DC"), monster::Table::SpellDc, &monster::SpellList::spelldc}})
    {
        const monster::Stat &stat = list.*member;
        QComboBox *benchmark = makeCombo(benchmarkChoices(table));
        selectIn(benchmark, stat.benchmark);
        auto *hint = new QLabel;
        hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hint->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("+88 to +88")));
        QLineEdit *value = valueEdit();
        value->setText(stat.value.text());
        auto *note = new QLineEdit(stat.note);
        note->setPlaceholderText(tr("note"));
        grid->addWidget(new QLabel(label), r, 0);
        grid->addWidget(benchmark, r, 1);
        grid->addWidget(hint, r, 2);
        grid->addWidget(value, r, 3);
        grid->addWidget(note, r, 4);

        auto stat_ = [at, member]() -> monster::Stat & { return at().*member; };
        const bool isDc = table == monster::Table::SpellDc;
        listHintUpdaters_.push_back([this, hint, value, table, stat_, isDc] {
            const monster::Hint h =
                monster::hint(table, monster_->level.number().value_or(0), stat_().benchmark);
            showCopyableHint(hint, h);
            // An empty DC prints its hint; an empty attack is left out.
            value->setPlaceholderText(isDc && h.value ? QString::number(*h.value) : QString());
        });
        connect(benchmark, &QComboBox::activated, this, [this, benchmark, stat_] {
            stat_().benchmark = benchmark->currentData().toString();
            refreshHints();
            emit changed();
        });
        connect(hint, &QLabel::linkActivated, this, [this, value, table, stat_] {
            if (const monster::Hint h =
                    monster::hint(table, monster_->level.number().value_or(0), stat_().benchmark);
                h.value)
            {
                value->setText(QString::number(*h.value));
                stat_().value = Value(*h.value);
                emit changed();
            }
        });
        connect(value, &QLineEdit::textEdited, this, [this, stat_](const QString &text) {
            stat_().value = valueFromText(text);
            emit changed();
        });
        connect(note, &QLineEdit::textEdited, this, [this, stat_](const QString &text) {
            stat_().note = text;
            emit changed();
        });
    }

    // Cantrip rank and focus points.
    grid->addWidget(new QLabel(tr("Cantrip rank")), 3, 0);
    auto *numbers = new QHBoxLayout;
    QLineEdit *cantrips = valueEdit();
    cantrips->setText(list.cantriplevel.text());
    cantrips->setToolTip(tr("The rank cantrips are cast at; empty: half the level, rounded up"));
    numbers->addWidget(cantrips);
    numbers->addWidget(new QLabel(tr("Focus points")));
    auto *focus = new QSpinBox;
    focus->setRange(0, 3);
    focus->setValue(list.focuspoints.number().value_or(0));
    numbers->addWidget(focus);
    numbers->addStretch();
    grid->addLayout(numbers, 3, 1, 1, 4);
    connect(cantrips, &QLineEdit::textEdited, this, [this, at](const QString &text) {
        at().cantriplevel = valueFromText(text);
        emit changed();
    });
    connect(focus, &QSpinBox::valueChanged, this, [this, at](int value) {
        at().focuspoints = Value(value);
        emit changed();
    });
    listHintUpdaters_.push_back([this, cantrips] {
        cantrips->setPlaceholderText(
            QString::number(defaultCantripRank(monster_->level.number().value_or(0))));
    });

    line(4, tr("Note"), list.note, [at]() -> QString & { return at().note; })
        ->setPlaceholderText(tr("shown after the DC and attack"));
    for (int r = 0; r < monster::kSpellRanks; ++r)
    {
        line(5 + r, r == monster::kCantrips ? tr("Cantrips") : rankName(r),
             list.spells[static_cast<size_t>(r)],
             [at, r]() -> QString & { return at().spells[static_cast<size_t>(r)]; });
    }
    line(5 + monster::kSpellRanks, tr("Constant"), list.constant,
         [at]() -> QString & { return at().constant; });
    return row;
}
