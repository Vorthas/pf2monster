// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Model/Monster.h"

#include <QWidget>

#include <array>
#include <functional>
#include <vector>

class EntryList;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QToolButton;
class QVBoxLayout;

// The form that edits a monster: its identity, statistics, strikes and
// special abilities, each statistic with its benchmark and the hint for it
// (GM Core's tables), and the spells. Laid out in the website's three columns, each
// scrolling on its own, with fewer columns when the form is narrow; a
// corner widget (the monster list) sits under the first column. Sections
// are placed as on the website: identity, attributes, defense and
// Perception; skills, items, offense and spells; additional spells and
// special abilities.
class MonsterEditor : public QWidget
{
    Q_OBJECT

public:
    explicit MonsterEditor(QWidget *parent = nullptr);

    // The monster to edit, which must outlive the editor or the next call.
    // The form shows it and writes every change straight into it.
    void setMonster(monster::Monster *monster);

    // Puts `widget` under the first column, with a handle between them.
    void setCornerWidget(QWidget *widget);
    // The first column's splitter, to save and restore the handle.
    QSplitter *cornerSplitter() const;

signals:
    void changed(); // the user changed the monster
    // The applied adjustment at `index` should be edited in its dialog.
    void editModifierRequested(int index);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int kColumns = 3;

    // A section and where it goes: its column on the website (`group`) and
    // its place in that column.
    struct Section
    {
        int group = 0;
        int order = 0;
        QGroupBox *box = nullptr;
    };

    // A section; returns the widget its contents go in. `noteKey` names it
    // for road map notes ("defense", ...), shown at its top.
    QWidget *addSection(int group, int order, const QString &title,
                        const QString &noteKey = QString());
    void reflow(int columns);
    void setSectionsEnabled(bool enabled);

    // Fills the form from the monster. Setting fields doesn't count as an
    // edit.
    void load();
    // Shows each statistic's hint for the monster's level and benchmark.
    void refreshHints();
    void rebuildStrikes();
    void rebuildSpecials();
    void rebuildSpellLists();
    void rebuildHpParts();
    // GM Core's notes for the monster's type and traits.
    void updateTraitNotes();
    void updateRecallWarning();
    // Shows the "NC" boxes while the monster has a non-combat level.
    void updateNonCombat();
    // The rows of the Lore skills past the website's two, and the button to
    // add one.
    void rebuildLores();
    // Shows the parts' controls when there are parts, and the note
    // conversion when the HP note reads like one.
    void updateHpPartControls();
    QWidget *hpPartRow(int index);
    // The applied adjustments, the stacking warning and the adjusted level.
    void rebuildModifiers();
    // The widgets showing one entry of a list.
    QWidget *strikeRow(int index);
    QWidget *specialRow(int index);
    QWidget *generatedRow(const monster::Special &place);
    QWidget *spellListRow(int index);

    QLineEdit *addLine(QGridLayout *grid, int row, const QString &label,
                       std::function<QString &()> field);
    // A statistic row: label, value, note.
    void addStat(QGridLayout *grid, int row, const QString &label, const QString &key,
                 bool loreName = false);
    QPlainTextEdit *addText(QVBoxLayout *layout, std::function<QString &()> field,
                            int lines);

    monster::Monster *monster_ = nullptr;
    bool loading_ = false;
    // Puts the monster's values in the form; one per field.
    std::vector<std::function<void()>> loaders_;
    // Show the hints for the current level and benchmarks.
    std::vector<std::function<void()>> hintUpdaters_;

    std::vector<Section> sections_;
    // Road map notes closed by the user, while the road map is
    // `notesRoadmap_`.
    QStringList closedNotes_;
    QString notesRoadmap_;
    std::array<QScrollArea *, kColumns> scrolls_{};
    std::array<QVBoxLayout *, kColumns> columnLayouts_{};
    QSplitter *firstColumn_ = nullptr;
    int columns_ = 0;

    QSpinBox *level_ = nullptr;
    EntryList *strikes_ = nullptr;
    EntryList *specials_ = nullptr;
    EntryList *spellLists_ = nullptr;
    EntryList *hpParts_ = nullptr;
    QWidget *traitNotes_ = nullptr;
    QLineEdit *nonCombat_ = nullptr;
    std::vector<QCheckBox *> nonCombatBoxes_;

    // Where addStat() puts a row's loaders, hint updaters and "NC" box: the
    // form's own lists, or those of the extra Lore rows (remade with them).
    std::vector<std::function<void()>> *statLoaders_ = &loaders_;
    std::vector<std::function<void()>> *statHints_ = &hintUpdaters_;
    std::vector<QCheckBox *> *statNcBoxes_ = &nonCombatBoxes_;
    QGridLayout *skillsGrid_ = nullptr;
    int skillsFixedItems_ = 0;
    int firstExtraLoreRow_ = 0;
    std::vector<std::function<void()>> extraLoaders_;
    std::vector<std::function<void()>> extraHints_;
    std::vector<QCheckBox *> extraNcBoxes_;
    QToolButton *traitNotesToggle_ = nullptr;
    QLabel *traitNotesText_ = nullptr;
    QLabel *mainPartLabel_ = nullptr;
    QLineEdit *mainPart_ = nullptr;
    QPushButton *convertNote_ = nullptr;
    QVBoxLayout *modifiers_ = nullptr; // one row per applied adjustment
    QWidget *modifiersRow_ = nullptr;
    QLabel *stackWarning_ = nullptr;
    QLabel *recallWarning_ = nullptr;
    QLabel *adjustedLevel_ = nullptr;
    // The hints in the additional spell lists' rows, made with the rows.
    std::vector<std::function<void()>> listHintUpdaters_;
};
