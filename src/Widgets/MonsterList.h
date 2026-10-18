// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QSet>
#include <QStringList>
#include <QWidget>

#include <vector>

class QFileSystemWatcher;
class QLineEdit;
class QListWidget;
class QMenu;
class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

// The collection: every monster file in a folder and its subfolders (one
// group per subfolder), with a filter box and tags to narrow it down
// (namespaced tags: Model/Tags.h). The tags show as a row of chips, or as a
// tree beside the monsters (Find Monster).
// Files that aren't monsters (the website's PNG, PDF and HTML exports, other
// JSON) are left out. Follows changes made to the folder from outside.
class MonsterList : public QWidget
{
    Q_OBJECT

public:
    explicit MonsterList(QWidget *parent = nullptr);

    // Shows the monsters in `folder`.
    void setFolder(const QString &folder);
    QString folder() const { return folder_; }
    // Reads the folder again.
    void rescan();

    // Marks the monster at `path` as the one open (none: empty).
    void setCurrent(const QString &path);

    // Shows the tags as a tree of checkboxes left of the monsters instead
    // of the chips above them (Find Monster). For good: call once.
    void useTagTree();
    // The tag tree | monsters split (after useTagTree), to remember.
    QSplitter *tagSplitter() const { return tagSplitter_; }

    // Actions for the list's right-click menu (New, Duplicate, ...).
    void setContextActions(const QList<QAction *> &actions) { contextActions_ = actions; }

    // A monster file as the list shows it.
    struct Entry
    {
        QString path;
        QString name;
        QString level;
        QStringList tags;
        QString traits;
    };
    const std::vector<Entry> &entries() const { return entries_; }

    // Whether `path` holds a monster (a JSON object in the website's
    // format), and what the list shows for it.
    static bool readEntry(const QString &path, Entry *entry);

signals:
    // The user picked the monster at `path` (clicked, chose it with the
    // keyboard, or pressed Enter in the filter for the first one shown).
    void monsterActivated(const QString &path);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void rebuildTree();
    void rebuildTags();
    void fillTagTree(const QStringList &all);
    void spreadTagChecks();
    void updateTagChecks();
    bool matches(const Entry &entry) const;

    QString folder_;
    QString current_;
    std::vector<Entry> entries_;
    QList<QAction *> contextActions_;
    QStringList checkedTags_;  // keys (monster::tagKey), chips or tree
    QSet<QString> tagKeys_;    // every tag and namespace in the collection
    QSet<QString> plainTagKeys_; // those some monster has as a tag itself
    QSet<QString> expandedTags_; // the tag tree's open namespaces

    QLineEdit *filter_ = nullptr;
    QListWidget *tags_ = nullptr;
    QTreeWidget *tagTree_ = nullptr; // with setTagTree(true)
    QSplitter *tagSplitter_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer *rescanTimer_ = nullptr; // a burst of file changes: one rescan
};
