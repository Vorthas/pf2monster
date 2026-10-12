// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Model/Monster.h"

#include <QDialog>
#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;

// Monster > Adjustments > Mythic: the template (or none), the basic mythic
// abilities of each level-table row up to the creature's level, the skills
// for Mythic Skill and the template's abilities to include. Starts from the
// choices already made (or the defaults) and hands back the new ones as the
// Mythic entry's options (see Model/Mythic.h).
class MythicDialog : public QDialog
{
    Q_OBJECT

public:
    // `monster` as Mythic will see it (after the adjustments before it),
    // at `level`; `name` is its name without the adjustments' prefixes.
    MythicDialog(const monster::Monster &monster, int level, const QJsonObject &options,
                 const QString &name, QWidget *parent = nullptr);

    // Every choice shown, so they stay as they are if the creature changes.
    QJsonObject options() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Makes the dialog as tall as its rows, once it is shown.
    void fitHeight();
    // Remakes the rows that depend on the choices, once the current signal
    // is over.
    void rebuildLater();
    void rebuild();

    monster::Monster monster_;
    int level_ = 0;
    QJsonObject options_;

    QComboBox *template_ = nullptr;
    QLabel *summary_ = nullptr;
    QComboBox *tradition_ = nullptr;
    QLabel *traditionLabel_ = nullptr;
    QGridLayout *slots_ = nullptr;
    QVBoxLayout *abilities_ = nullptr;
    QCheckBox *earlyPoints_ = nullptr;
    QCheckBox *fullText_ = nullptr;
    QCheckBox *prefixName_ = nullptr;
    QLineEdit *callIt_ = nullptr;
    QString name_;
    bool rebuildPending_ = false;
};
