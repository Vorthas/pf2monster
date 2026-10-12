// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Model/Adjustments.h"

#include <QDialog>
#include <QJsonObject>

#include <functional>
#include <vector>

class QScrollArea;

// The choices of an adjustment whose file has "dialog": "options" (Mutant
// Cryptid's abilities, Secret Society Member's skills, ...): a group of
// checkboxes per "pick", a list per "choice", a checkbox per "check" and a
// line per "text", each with the file's note. Starts from the choices
// already made (or the file's suggestions) and hands back the new ones as
// the applied entry's options.
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    // `level`: the creature's level before this adjustment (items offered
    // from a level look at the level after it).
    OptionsDialog(const monster::Modifier &modifier, int level, const QJsonObject &options,
                  QWidget *parent = nullptr);

    QJsonObject options() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Enables the choices offered only when another is made.
    void updateEnabled();
    // Makes the dialog as tall as its rows, once it is shown.
    void fitHeight();

    const monster::Modifier &modifier_;
    QScrollArea *scroll_ = nullptr;
    QWidget *content_ = nullptr;
    QJsonObject options_;
    // Per choice: reads it back into options_, and the widget to enable.
    std::vector<std::function<void(QJsonObject &)>> readers_;
    std::vector<std::pair<QString, QWidget *>> conditional_; // when, widget
};
