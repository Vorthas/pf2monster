// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/HomebrewFoldersDialog.h"

#include "Model/Homebrew.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace
{
    // The item's folder (its text is shown with native separators).
    constexpr int kPathRole = Qt::UserRole;
} // namespace

HomebrewFoldersDialog::HomebrewFoldersDialog(const QString &collection, QWidget *parent)
    : QDialog(parent), collection_(collection)
{
    setWindowTitle(tr("Homebrew Folders"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Homebrew is read from these folders, top to bottom, after the "
                                "built-in data. Untick a folder to leave its homebrew out for now "
                                "without removing it. Each folder holds adjustments, roadmaps, "
                                "domains and traits folders."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *row = new QHBoxLayout;
    list_ = new QListWidget;
    list_->setMinimumWidth(fontMetrics().averageCharWidth() * 70);
    for (const monster::HomebrewFolder &folder : monster::homebrewFolders())
    {
        auto *item = new QListWidgetItem(QDir::toNativeSeparators(folder.path), list_);
        item->setData(kPathRole, folder.path);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(folder.enabled ? Qt::Checked : Qt::Unchecked);
        if (list_->count() == 1)
        {
            item->setToolTip(tr("The data folder: Homebrew > Open Homebrew Folder opens it. It can be "
                                "switched off, not removed."));
        }
    }
    row->addWidget(list_, 1);
    auto *side = new QVBoxLayout;
    auto *add = new QPushButton(tr("&Add..."));
    remove_ = new QPushButton(tr("&Remove"));
    up_ = new QPushButton(tr("Move &Up"));
    down_ = new QPushButton(tr("Move &Down"));
    for (QPushButton *button : {add, remove_, up_, down_})
    {
        side->addWidget(button);
    }
    side->addStretch();
    row->addLayout(side);
    layout->addLayout(row, 1);
    connect(add, &QPushButton::clicked, this, &HomebrewFoldersDialog::addFolder);
    connect(remove_, &QPushButton::clicked, this, &HomebrewFoldersDialog::removeFolder);
    connect(up_, &QPushButton::clicked, this, [this] { moveFolder(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveFolder(1); });
    connect(list_, &QListWidget::currentRowChanged, this, &HomebrewFoldersDialog::updateButtons);

    // The problem log's folder.
    auto *logRow = new QHBoxLayout;
    logRow->addWidget(new QLabel(tr("Problem log folder")));
    log_ = new QLineEdit(QDir::toNativeSeparators(monster::homebrewLogFolder(collection)));
    log_->setToolTip(tr("Where %1 is written when homebrew files have problems; empty: the "
                        "monster collection folder")
                         .arg(monster::kHomebrewLogName));
    log_->setPlaceholderText(QDir::toNativeSeparators(collection));
    logRow->addWidget(log_, 1);
    auto *browse = new QPushButton(tr("&Browse..."));
    logRow->addWidget(browse);
    layout->addLayout(logRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Problem Log Folder"), log_->text());
        if (!folder.isEmpty())
        {
            log_->setText(QDir::toNativeSeparators(folder));
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    list_->setCurrentRow(0);
    updateButtons();
}

void HomebrewFoldersDialog::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add Homebrew Folder"));
    if (folder.isEmpty())
    {
        return;
    }
    for (int i = 0; i < list_->count(); ++i)
    {
        if (QDir(list_->item(i)->data(kPathRole).toString()) == QDir(folder))
        {
            list_->setCurrentRow(i); // listed already
            return;
        }
    }
    auto *item = new QListWidgetItem(QDir::toNativeSeparators(folder), list_);
    item->setData(kPathRole, folder);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    list_->setCurrentItem(item);
}

void HomebrewFoldersDialog::removeFolder()
{
    // The data folder stays (it can be switched off); removing a folder
    // from the list leaves its files where they are.
    if (const int row = list_->currentRow(); row > 0)
    {
        delete list_->takeItem(row);
    }
}

void HomebrewFoldersDialog::moveFolder(int by)
{
    const int row = list_->currentRow();
    const int to = row + by;
    if (row < 0 || to < 0 || to >= list_->count())
    {
        return;
    }
    QListWidgetItem *item = list_->takeItem(row);
    list_->insertItem(to, item);
    list_->setCurrentRow(to);
}

void HomebrewFoldersDialog::updateButtons()
{
    const int row = list_->currentRow();
    const bool data = row >= 0 && QDir(list_->item(row)->data(kPathRole).toString()) ==
                                       QDir(monster::homebrewDataFolder());
    remove_->setEnabled(row >= 0 && !data);
    remove_->setToolTip(data ? tr("The data folder can't be removed; untick it to leave its homebrew out")
                             : tr("Take the folder off this list (its files stay where they are)"));
    // The data folder stays first.
    up_->setEnabled(row > 1);
    down_->setEnabled(row > 0 && row < list_->count() - 1);
}

void HomebrewFoldersDialog::accept()
{
    QList<monster::HomebrewFolder> folders;
    for (int i = 0; i < list_->count(); ++i)
    {
        const QListWidgetItem *item = list_->item(i);
        folders.push_back({item->data(kPathRole).toString(), item->checkState() == Qt::Checked});
    }
    monster::setHomebrewFolders(folders);
    const QString log = QDir::fromNativeSeparators(log_->text().trimmed());
    monster::setHomebrewLogFolder(QDir(log) == QDir(collection_) ? QString() : log);
    QDialog::accept();
}
