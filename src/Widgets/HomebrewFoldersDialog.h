// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;

// Homebrew > Homebrew Folders...: the folders homebrew is read from, in
// order (the data folder first, always listed and never moved), each with a checkbox to
// switch it off; and the folder the problem log goes in. Saved to the
// settings when accepted.
class HomebrewFoldersDialog : public QDialog
{
    Q_OBJECT

public:
    // `collection`: the monster collection folder, the log's default.
    explicit HomebrewFoldersDialog(const QString &collection, QWidget *parent = nullptr);

    void accept() override;

private:
    void addFolder();
    void removeFolder();
    void moveFolder(int by);
    void updateButtons();

    QString collection_;
    QListWidget *list_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *up_ = nullptr;
    QPushButton *down_ = nullptr;
    QLineEdit *log_ = nullptr;
};
