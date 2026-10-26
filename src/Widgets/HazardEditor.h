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
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QVBoxLayout;
class TagCompleter;

// The form that edits a hazard (a monster whose type is "hazard", plan
// section 13): its identity, Stealth and how to disable it, its defenses,
// Routine, Strikes, abilities and Reset, each statistic with its benchmark
// and the hint for it from GM Core's Building Hazards tables. Laid out in
// three columns as the creature form (MonsterEditor), each scrolling on its
// own, with fewer when narrow, and the same corner widget (the monster
// list) under the first. Kept apart from the creature form on purpose
// (the user); what they share may become common code later.
class HazardEditor : public QWidget
{
    Q_OBJECT

public:
    explicit HazardEditor(QWidget *parent = nullptr);

    // The hazard to edit, which must outlive the editor or the next call.
    // The form shows it and writes every change straight into it.
    void setMonster(monster::Monster *monster);

    // Puts `widget` under the first column, with a handle between them.
    void setCornerWidget(QWidget *widget);
    // The first column's splitter, to save and restore the handle.
    QSplitter *cornerSplitter() const;

    // The collection's tags, suggested while typing in the Tags box.
    void setKnownTags(const QStringList &tags);

signals:
    void changed(); // the user changed the hazard

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int kColumns = 3;

    struct Section
    {
        int group = 0;
        int order = 0;
        QGroupBox *box = nullptr;
    };

    QWidget *addSection(int group, int order, const QString &title);
    void reflow(int columns);
    void setSectionsEnabled(bool enabled);

    void load();
    void refreshHints();
    void rebuildStrikes();
    void rebuildSpecials();
    void rebuildHpParts();
    // Shows what the hazard's complexity and defenses call for: the
    // Routine section, the defense rows.
    void updateVisibility();
    // Sets whether `key` ("ac", "will", "hp", ...) is among the defenses
    // the statblock shows, keeping GM Core's order.
    void setDefense(const QString &key, bool shown);

    QWidget *strikeRow(int index);
    QWidget *specialRow(int index);
    QWidget *hpPartRow(int index);

    QLineEdit *addLine(QGridLayout *grid, int row, const QString &label,
                       std::function<QString &()> field);
    // A statistic row: label (or the box that shows it in the statblock),
    // benchmark, hint, value, note.
    void addStat(QGridLayout *grid, int row, QWidget *label, const QString &key, bool withNote = true);
    QPlainTextEdit *addText(QVBoxLayout *layout, std::function<QString &()> field, int lines);

    monster::Monster *monster_ = nullptr;
    bool loading_ = false;
    std::vector<std::function<void()>> loaders_;
    std::vector<std::function<void()>> hintUpdaters_;

    std::vector<Section> sections_;
    std::array<QScrollArea *, kColumns> scrolls_{};
    std::array<QVBoxLayout *, kColumns> columnLayouts_{};
    QSplitter *firstColumn_ = nullptr;
    TagCompleter *tagCompleter_ = nullptr;
    int columns_ = 0;

    QWidget *routineSection_ = nullptr;
    QCheckBox *physical_ = nullptr;
    QWidget *defenseRows_ = nullptr;
    // The defenses as they were before "Physical component" was unticked,
    // to bring back when it's ticked again.
    QStringList hiddenDefenses_;
    EntryList *strikes_ = nullptr;
    EntryList *specials_ = nullptr;
    EntryList *hpParts_ = nullptr;
};
