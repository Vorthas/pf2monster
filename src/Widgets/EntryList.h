// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QWidget>

#include <functional>

class QFrame;
class QVBoxLayout;

// A list of entries (Strikes, abilities, spell lists) shown one under the
// other, each with controls beside it: a grip to drag it to another place,
// buttons to move it up or down, and one to delete it. The list doesn't own
// the entries' data: it asks for a widget per entry and reports what the
// user did, and the owner changes its data and calls rebuild().
class EntryList : public QWidget
{
    Q_OBJECT

public:
    explicit EntryList(QWidget *parent = nullptr);

    // Makes the widget that shows entry `index`.
    using RowFactory = std::function<QWidget *(int index)>;
    void setRowFactory(RowFactory factory) { factory_ = std::move(factory); }
    // What the delete button's tooltip says ("Delete this Strike").
    void setDeleteToolTip(const QString &text) { deleteToolTip_ = text; }
    // Whether entry `index` can be deleted here (default: all); the others
    // get no delete button but can still be moved.
    void setRemovable(std::function<bool(int index)> removable) { removable_ = std::move(removable); }

    // Shows `count` entries, made anew.
    void rebuild(int count);

signals:
    // Entry `from` should go to place `to` (both in the list as it is now).
    void moveRequested(int from, int to);
    void removeRequested(int index);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // Where a drop at `y` would put the entry: before entry N (count: at
    // the end).
    int dropSlot(int y) const;
    void showDropLine(int slot);
    // Reports a request once the click or drop that made it is over (the
    // owner rebuilds the list, deleting the widget that was clicked).
    void later(std::function<void()> request);

    RowFactory factory_;
    QString deleteToolTip_;
    std::function<bool(int)> removable_;
    QVBoxLayout *layout_ = nullptr;
    QList<QWidget *> rows_;       // one per entry
    QList<QWidget *> separators_; // the lines between them
    QFrame *dropLine_ = nullptr;
};
