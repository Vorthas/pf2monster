// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/MonsterList.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    constexpr int kPathRole = Qt::UserRole;
    // Monster files are a few kilobytes; anything far bigger isn't one.
    constexpr qint64 kMaxFileSize = 4 * 1024 * 1024;

    QString levelText(const QJsonValue &level)
    {
        if (level.isDouble())
        {
            return QString::number(level.toInt());
        }
        return level.toString().trimmed();
    }
} // namespace

MonsterList::MonsterList(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Filter: name, trait or tag"));
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);

    // Tags as chips that wrap: selecting some shows only monsters with all
    // of them.
    tags_ = new QListWidget;
    tags_->setFlow(QListView::LeftToRight);
    tags_->setWrapping(true);
    tags_->setResizeMode(QListView::Adjust);
    tags_->setSelectionMode(QAbstractItemView::MultiSelection);
    tags_->setSpacing(2);
    tags_->setMaximumHeight(fontMetrics().height() * 4);
    tags_->setToolTip(tr("Show only monsters with all the selected tags"));
    layout->addWidget(tags_);

    tree_ = new QTreeWidget;
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Monster"), tr("Level")});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    layout->addWidget(tree_, 1);

    connect(filter_, &QLineEdit::textChanged, this, [this] { rebuildTree(); });
    // Enter in the filter opens the first monster it shows.
    connect(filter_, &QLineEdit::returnPressed, this, [this] {
        for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        {
            const QString path = (*it)->data(0, kPathRole).toString();
            if (!path.isEmpty())
            {
                emit monsterActivated(path);
                return;
            }
        }
    });
    connect(tags_, &QListWidget::itemSelectionChanged, this, [this] { rebuildTree(); });
    // A click or the keyboard picks a monster.
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!tree_->signalsBlocked() && item)
        {
            const QString path = item->data(0, kPathRole).toString();
            if (!path.isEmpty() && path != current_)
            {
                emit monsterActivated(path);
            }
        }
    });

    watcher_ = new QFileSystemWatcher(this);
    rescanTimer_ = new QTimer(this);
    rescanTimer_->setSingleShot(true);
    rescanTimer_->setInterval(300);
    connect(rescanTimer_, &QTimer::timeout, this, &MonsterList::rescan);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, rescanTimer_,
            qOverload<>(&QTimer::start));
}

void MonsterList::setFolder(const QString &folder)
{
    folder_ = QDir(folder).absolutePath();
    rescan();
}

bool MonsterList::readEntry(const QString &path, Entry *entry)
{
    QFile file(path);
    if (file.size() > kMaxFileSize || !file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject json = doc.object();
    // The website's monsters always have these.
    if (!doc.isObject() || !json.contains(QLatin1String("name")) ||
        !json.contains(QLatin1String("level")))
    {
        return false;
    }
    if (entry)
    {
        entry->path = QFileInfo(path).absoluteFilePath();
        entry->name = json.value(QLatin1String("name")).toString().trimmed();
        if (entry->name.isEmpty())
        {
            entry->name = QFileInfo(path).completeBaseName();
        }
        entry->level = levelText(json.value(QLatin1String("level")));
        entry->tags.clear();
        for (const QJsonValue &tag : json.value(QLatin1String("tags")).toArray())
        {
            if (const QString t = tag.toString().trimmed(); !t.isEmpty())
            {
                entry->tags << t;
            }
        }
        entry->traits = json.value(QLatin1String("traits")).toString();
    }
    return true;
}

void MonsterList::rescan()
{
    entries_.clear();
    if (!watcher_->directories().isEmpty())
    {
        watcher_->removePaths(watcher_->directories());
    }
    if (folder_.isEmpty() || !QDir(folder_).exists())
    {
        rebuildTags();
        rebuildTree();
        return;
    }
    QStringList folders{folder_};
    QDirIterator dirs(folder_, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext())
    {
        folders << dirs.next();
    }
    watcher_->addPaths(folders);

    QDirIterator files(folder_, {QStringLiteral("*.json"), QStringLiteral("*.JSON")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        Entry entry;
        if (readEntry(files.next(), &entry))
        {
            entries_.push_back(entry);
        }
    }
    rebuildTags();
    rebuildTree();
}

void MonsterList::setCurrent(const QString &path)
{
    current_ = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    const QSignalBlocker block(tree_);
    tree_->setCurrentItem(nullptr);
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
    {
        if ((*it)->data(0, kPathRole).toString() == current_)
        {
            tree_->setCurrentItem(*it);
            tree_->scrollToItem(*it);
            break;
        }
    }
}

QStringList MonsterList::selectedTags() const
{
    QStringList tags;
    for (const QListWidgetItem *item : tags_->selectedItems())
    {
        tags << item->text();
    }
    return tags;
}

void MonsterList::rebuildTags()
{
    const QStringList selected = selectedTags();
    QMap<QString, QString> tags; // lowercase -> as first written
    for (const Entry &entry : entries_)
    {
        for (const QString &tag : entry.tags)
        {
            tags.insert(tag.toLower(), tags.value(tag.toLower(), tag));
        }
    }
    const QSignalBlocker block(tags_);
    tags_->clear();
    for (const QString &tag : tags)
    {
        auto *item = new QListWidgetItem(tag, tags_);
        item->setSelected(selected.contains(tag, Qt::CaseInsensitive));
    }
    tags_->setVisible(!tags.isEmpty());
}

bool MonsterList::matches(const Entry &entry) const
{
    for (const QString &tag : selectedTags())
    {
        if (!entry.tags.contains(tag, Qt::CaseInsensitive))
        {
            return false;
        }
    }
    const QString haystack = QStringList{entry.name, entry.traits, entry.tags.join(QLatin1Char(' ')),
                                         QFileInfo(entry.path).fileName()}
                                 .join(QLatin1Char(' '));
    for (const QString &word : filter_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        if (!haystack.contains(word, Qt::CaseInsensitive))
        {
            return false;
        }
    }
    return true;
}

// A group per subfolder, then the monsters in the folder itself (folders
// first, as in a file manager), each sorted by name. Only those matching
// the filter and selected tags.
void MonsterList::rebuildTree()
{
    const QSignalBlocker block(tree_);
    tree_->clear();
    QMap<QString, std::vector<const Entry *>> groups; // relative folder -> entries
    const QDir root(folder_);
    for (const Entry &entry : entries_)
    {
        if (matches(entry))
        {
            groups[root.relativeFilePath(QFileInfo(entry.path).absolutePath())].push_back(&entry);
        }
    }
    QTreeWidgetItem *currentItem = nullptr;
    // The subfolders in order, then the folder itself (".").
    QStringList order = groups.keys();
    order.sort(Qt::CaseInsensitive);
    if (order.removeOne(QStringLiteral(".")))
    {
        order.append(QStringLiteral("."));
    }
    for (const QString &key : order)
    {
        std::vector<const Entry *> &group = groups[key];
        std::sort(group.begin(), group.end(), [](const Entry *a, const Entry *b) {
            return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
        });
        QTreeWidgetItem *parent = nullptr;
        if (key != QLatin1String("."))
        {
            parent = new QTreeWidgetItem(tree_, {key});
            QFont bold = parent->font(0);
            bold.setBold(true);
            parent->setFont(0, bold);
            parent->setFlags(Qt::ItemIsEnabled);
        }
        for (const Entry *entry : group)
        {
            auto *item = parent ? new QTreeWidgetItem(parent, {entry->name, entry->level})
                                : new QTreeWidgetItem(tree_, {entry->name, entry->level});
            item->setData(0, kPathRole, entry->path);
            item->setTextAlignment(1, Qt::AlignRight);
            QString tip = QDir::toNativeSeparators(entry->path);
            if (!entry->tags.isEmpty())
            {
                tip += QLatin1Char('\n') + tr("Tags: %1").arg(entry->tags.join(QStringLiteral(", ")));
            }
            item->setToolTip(0, tip);
            if (entry->path == current_)
            {
                currentItem = item;
            }
        }
        if (parent)
        {
            parent->setExpanded(true);
        }
    }
    if (currentItem)
    {
        tree_->setCurrentItem(currentItem);
    }
    // Lay out now. Left to Qt, the Level column (ResizeToContents) does it
    // from inside the header's paint, where the repaint of the list it asks
    // for gets lost: only rows already marked (the old current one) were
    // redrawn, and the rest kept the unfiltered list.
    tree_->doItemsLayout();
}

void MonsterList::contextMenuEvent(QContextMenuEvent *event)
{
    if (contextActions_.isEmpty())
    {
        return;
    }
    QMenu menu(this);
    menu.addActions(contextActions_);
    menu.exec(event->globalPos());
}
