// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/MonsterList.h"

#include "Model/Monster.h"
#include "Model/Tags.h"

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
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    constexpr int kPathRole = Qt::UserRole;
    constexpr int kTagKeyRole = Qt::UserRole + 1; // chips and the tag tree
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

    // Tags as chips that wrap: selecting some shows only monsters with
    // them (any one of a namespace's, Model/Tags.h).
    tags_ = new QListWidget;
    tags_->setFlow(QListView::LeftToRight);
    tags_->setWrapping(true);
    tags_->setResizeMode(QListView::Adjust);
    tags_->setSelectionMode(QAbstractItemView::MultiSelection);
    tags_->setSpacing(2);
    tags_->setMaximumHeight(fontMetrics().height() * 4);
    tags_->setToolTip(tr("Show only creatures and hazards with the selected tags\n"
                         "(of tags in one namespace, such as environment:..., any one will do)"));
    layout->addWidget(tags_);

    tree_ = new QTreeWidget;
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Creature or Hazard"), tr("Level")});
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
    connect(tags_, &QListWidget::itemSelectionChanged, this, [this] {
        checkedTags_.clear();
        for (const QListWidgetItem *item : tags_->selectedItems())
        {
            checkedTags_ << item->data(kTagKeyRole).toString();
        }
        rebuildTree();
    });
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
        // Hazards get a built-in tag for finding them ("hazard:simple",
        // "hazard:complex"; plan 13), kept here only, never in the file.
        if (json.value(QLatin1String("type")).toString().compare(monster::kHazardType, Qt::CaseInsensitive) == 0)
        {
            const QJsonObject own = json.value(QLatin1String("pf2monster")).toObject();
            const bool complex =
                monster::Hazard::fromJson(own.value(QLatin1String("hazard")).toObject(), entry->traits).complex;
            entry->tags << (complex ? QStringLiteral("hazard:complex") : QStringLiteral("hazard:simple"));
        }
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

void MonsterList::useTagTree()
{
    if (tagTree_)
    {
        return;
    }
    tagTree_ = new QTreeWidget;
    tagTree_->setHeaderHidden(true);
    tagTree_->setUniformRowHeights(true);
    tagTree_->setToolTip(tr("Show only creatures and hazards with the checked tags\n"
                            "(of tags in one namespace, such as environment:..., any one will do;\n"
                            "a namespace means any tag in it)"));
    tagTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tagSplitter_ = new QSplitter(Qt::Horizontal);
    tagSplitter_->setChildrenCollapsible(false);
    auto *box = static_cast<QVBoxLayout *>(layout());
    box->removeWidget(tree_);
    tagSplitter_->addWidget(tagTree_);
    tagSplitter_->addWidget(tree_);
    tagSplitter_->setStretchFactor(0, 1);
    tagSplitter_->setStretchFactor(1, 2);
    box->addWidget(tagSplitter_, 1);

    connect(tagTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        // As in a file tree: a namespace checks or unchecks everything
        // inside; unchecking something inside unchecks the namespaces
        // above it (they go partly checked).
        const QString key = item->data(0, kTagKeyRole).toString();
        const QString inside = key + QLatin1Char(':');
        checkedTags_.removeIf([&](const QString &checked) { return checked == key || checked.startsWith(inside); });
        if (item->checkState(0) == Qt::Checked)
        {
            checkedTags_ << key;
        }
        else
        {
            for (QTreeWidgetItem *above = item->parent(); above; above = above->parent())
            {
                checkedTags_.removeAll(above->data(0, kTagKeyRole).toString());
            }
        }
        spreadTagChecks();
        updateTagChecks();
        rebuildTree();
    });
    connect(tagTree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        expandedTags_.insert(item->data(0, kTagKeyRole).toString());
    });
    connect(tagTree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        expandedTags_.remove(item->data(0, kTagKeyRole).toString());
    });
    // Checks can hide in closed namespaces: one way to clear them all.
    connect(tagTree_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(tagTree_);
        QAction *clear = menu.addAction(tr("Clear Tag Selection"), this, [this] {
            checkedTags_.clear();
            updateTagChecks();
            rebuildTree();
        });
        clear->setEnabled(!checkedTags_.isEmpty());
        menu.exec(tagTree_->viewport()->mapToGlobal(pos));
    });
    tags_->hide();
    rebuildTags();
    rebuildTree();
}

void MonsterList::rebuildTags()
{
    QStringList all;
    for (const Entry &entry : entries_)
    {
        all << entry.tags;
    }
    tagKeys_ = monster::tagTreeKeys(all);
    plainTagKeys_.clear();
    for (const QString &tag : all)
    {
        plainTagKeys_.insert(monster::tagKey(tag));
    }
    // Checks on tags no monster has any more go.
    checkedTags_.removeIf([this](const QString &key) { return !tagKeys_.contains(key); });

    if (tagTree_)
    {
        spreadTagChecks(); // tags new inside a checked namespace
        fillTagTree(all);
        tagTree_->setVisible(!tagKeys_.isEmpty());
        return;
    }
    QMap<QString, QString> chips; // key -> as first written, tidied ("environment:cave")
    for (const QString &tag : all)
    {
        if (const QString key = monster::tagKey(tag); !key.isEmpty() && !chips.contains(key))
        {
            chips.insert(key, monster::tagPath(tag).join(QLatin1Char(':')));
        }
    }
    const QSignalBlocker block(tags_);
    tags_->clear();
    for (auto it = chips.begin(); it != chips.end(); ++it)
    {
        auto *item = new QListWidgetItem(it.value(), tags_);
        item->setData(kTagKeyRole, it.key());
        item->setSelected(checkedTags_.contains(it.key()));
    }
    tags_->setVisible(!chips.isEmpty());
}

// Each level sorted by name; namespaces open as the user left them.
void MonsterList::fillTagTree(const QStringList &all)
{
    const QSignalBlocker block(tagTree_);
    tagTree_->clear();
    const auto add = [this](const auto &self, QTreeWidgetItem *parent,
                            const std::vector<monster::TagNode> &nodes) -> void {
        for (const monster::TagNode &node : nodes)
        {
            auto *item = parent ? new QTreeWidgetItem(parent, {node.name})
                                : new QTreeWidgetItem(tagTree_, {node.name});
            item->setData(0, kTagKeyRole, node.key);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
            self(self, item, node.children);
            item->setExpanded(expandedTags_.contains(node.key));
        }
    };
    add(add, nullptr, monster::tagTree(all));
    updateTagChecks();
}

// A checked namespace checks everything inside it. A namespace no monster
// has as a tag itself is checked once everything inside it is ("cold" and
// "hot" check "ocean"); one that is a tag too isn't, as that would add the
// monsters tagged with it.
void MonsterList::spreadTagChecks()
{
    QSet<QString> checked(checkedTags_.begin(), checkedTags_.end());
    for (const QString &key : tagKeys_)
    {
        for (QString above = key; above.contains(QLatin1Char(':'));)
        {
            above = above.section(QLatin1Char(':'), 0, -2);
            if (checked.contains(above))
            {
                checked.insert(key);
                break;
            }
        }
    }
    // Deepest namespaces first, so a check can climb several levels.
    QStringList namespaces;
    for (const QString &key : tagKeys_)
    {
        if (!plainTagKeys_.contains(key))
        {
            namespaces << key;
        }
    }
    std::sort(namespaces.begin(), namespaces.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char(':')) > b.count(QLatin1Char(':'));
    });
    for (const QString &space : namespaces)
    {
        const QString inside = space + QLatin1Char(':');
        bool all = true;
        for (const QString &key : tagKeys_)
        {
            if (key.startsWith(inside) && !key.mid(inside.size()).contains(QLatin1Char(':')) &&
                !checked.contains(key))
            {
                all = false;
                break;
            }
        }
        if (all)
        {
            checked.insert(space);
        }
    }
    for (const QString &key : checked)
    {
        if (!checkedTags_.contains(key))
        {
            checkedTags_ << key;
        }
    }
}

// Checked for a checked tag or namespace, partly checked for a namespace
// with something checked inside it.
void MonsterList::updateTagChecks()
{
    if (!tagTree_)
    {
        return;
    }
    const QSignalBlocker block(tagTree_);
    for (QTreeWidgetItemIterator it(tagTree_); *it; ++it)
    {
        const QString key = (*it)->data(0, kTagKeyRole).toString();
        const QString inside = key + QLatin1Char(':');
        Qt::CheckState state = Qt::Unchecked;
        if (checkedTags_.contains(key))
        {
            state = Qt::Checked;
        }
        else if (std::any_of(checkedTags_.begin(), checkedTags_.end(),
                             [&](const QString &checked) { return checked.startsWith(inside); }))
        {
            state = Qt::PartiallyChecked;
        }
        (*it)->setCheckState(0, state);
    }
}

bool MonsterList::matches(const Entry &entry) const
{
    if (!monster::tagsMatch(entry.tags, checkedTags_))
    {
        return false;
    }
    const QString haystack = QStringList{entry.name, entry.traits, entry.tags.join(QLatin1Char(' ')),
                                         QFileInfo(entry.path).fileName()}
                                 .join(QLatin1Char(' '));
    for (const QString &word : filter_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        // A word naming a tag or namespace ("environment:", "environment:ocean")
        // matches it and everything inside; other words match any text.
        if (word.contains(QLatin1Char(':')))
        {
            if (const QString key = monster::tagKey(word); tagKeys_.contains(key))
            {
                if (std::none_of(entry.tags.begin(), entry.tags.end(),
                                 [&](const QString &tag) { return monster::tagWithin(tag, key); }))
                {
                    return false;
                }
                continue;
            }
        }
        if (!haystack.contains(word, Qt::CaseInsensitive))
        {
            return false;
        }
    }
    return true;
}

// A group per subfolder, then the monsters in the folder itself (folders
// first, as in a file manager), each sorted by name. Two levels deep, as in
// the Collection menu: a group per top folder, inside it one per folder
// below that, deeper folders named by their path ("Darkness/Owb"). Only
// those matching the filter and selected tags.
void MonsterList::rebuildTree()
{
    const QSignalBlocker block(tree_);
    tree_->clear();
    using Group = std::vector<const Entry *>;
    QMap<QString, QMap<QString, Group>> groups; // top folder -> rest of path -> entries
    const QDir root(folder_);
    for (const Entry &entry : entries_)
    {
        if (matches(entry))
        {
            const QString path = root.relativeFilePath(QFileInfo(entry.path).absolutePath());
            const qsizetype slash = path.indexOf(QLatin1Char('/'));
            groups[path.left(slash)][slash < 0 ? QString() : path.mid(slash + 1)].push_back(&entry);
        }
    }
    QTreeWidgetItem *currentItem = nullptr;
    const auto sortedKeys = [](QStringList keys) {
        keys.sort(Qt::CaseInsensitive);
        return keys;
    };
    // A folder's row: bold, not selectable, open.
    const auto addFolder = [](QTreeWidgetItem *item) {
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        item->setFlags(Qt::ItemIsEnabled);
        item->setExpanded(true);
        return item;
    };
    const auto addEntries = [&](QTreeWidgetItem *parent, Group &group) {
        std::sort(group.begin(), group.end(), [](const Entry *a, const Entry *b) {
            return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
        });
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
    };
    for (const QString &top : sortedKeys(groups.keys()))
    {
        if (top == QLatin1String("."))
        {
            continue; // the folder's own monsters go last
        }
        QMap<QString, Group> &subs = groups[top];
        QTreeWidgetItem *topItem = addFolder(new QTreeWidgetItem(tree_, {top}));
        for (const QString &sub : sortedKeys(subs.keys()))
        {
            if (!sub.isEmpty())
            {
                addEntries(addFolder(new QTreeWidgetItem(topItem, {sub})), subs[sub]);
            }
        }
        if (subs.contains(QString()))
        {
            addEntries(topItem, subs[QString()]);
        }
    }
    if (groups.contains(QStringLiteral(".")))
    {
        addEntries(nullptr, groups[QStringLiteral(".")][QString()]);
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
