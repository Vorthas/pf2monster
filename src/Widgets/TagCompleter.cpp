// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/TagCompleter.h"

#include "Model/Tags.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStringListModel>

#include <algorithm>

TagCompleter::TagCompleter(QLineEdit *edit, Mode mode)
    : QObject(edit), edit_(edit), mode_(mode), completer_(new QCompleter(this)),
      model_(new QStringListModel(this))
{
    completer_->setModel(model_);
    completer_->setWidget(edit_); // it doesn't change the text itself: insert() does
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setFilterMode(Qt::MatchStartsWith);
    completer_->setModelSorting(QCompleter::UnsortedModel); // sorted here
    // Typing only: text set by the program (loading a monster) offers
    // nothing.
    connect(edit_, &QLineEdit::textEdited, this, &TagCompleter::update);
    connect(completer_, qOverload<const QString &>(&QCompleter::activated), this, &TagCompleter::insert);
    // After QCompleter's own filter on the list, so this one sees keys
    // first.
    completer_->popup()->installEventFilter(this);
}

bool TagCompleter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == completer_->popup() && event->type() == QEvent::KeyPress)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool take = key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::KeypadModifier;
        if (take && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
        {
            const QModelIndex current = completer_->popup()->currentIndex();
            if (current.isValid())
            {
                completer_->popup()->hide();
                insert(current.data().toString());
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void TagCompleter::setTags(const QStringList &tags)
{
    tags_ = tags;
    tagKeys_.clear();
    for (const QString &tag : tags_)
    {
        if (const QString key = monster::tagKey(tag); !key.isEmpty())
        {
            tagKeys_.insert(key);
        }
    }
}

qsizetype TagCompleter::segmentStart() const
{
    const QString before = edit_->text().left(edit_->cursorPosition());
    const QChar separator = mode_ == Mode::Tags ? QLatin1Char(',') : QLatin1Char(' ');
    qsizetype start = before.lastIndexOf(separator) + 1;
    while (start < before.size() && before.at(start).isSpace())
    {
        ++start;
    }
    return start;
}

void TagCompleter::update()
{
    const QString before = edit_->text().left(edit_->cursorPosition());
    const QString segment = before.mid(segmentStart());
    // The namespace path typed so far, and the part after it.
    const qsizetype colon = segment.lastIndexOf(QLatin1Char(':'));
    const QString path = colon < 0 ? QString() : segment.left(colon);
    const QString part = segment.mid(colon + 1).trimmed();
    // Nothing typed yet for this tag: no list (only after a namespace's
    // colon).
    if (part.isEmpty() && colon < 0)
    {
        completer_->popup()->hide();
        return;
    }

    // The level of the tree the path names.
    const std::vector<monster::TagNode> tree = monster::tagTree(tags_);
    const std::vector<monster::TagNode> *level = &tree;
    for (const QString &name : monster::tagPath(path))
    {
        const auto it = std::find_if(level->begin(), level->end(), [&](const monster::TagNode &node) {
            return node.name.compare(name, Qt::CaseInsensitive) == 0;
        });
        if (it == level->end())
        {
            completer_->popup()->hide();
            return;
        }
        level = &it->children;
    }

    // In the Tags box, the tags it already has (in its other entries).
    QSet<QString> present;
    if (mode_ == Mode::Tags)
    {
        const QString text = edit_->text();
        const qsizetype start = segmentStart();
        qsizetype end = text.indexOf(QLatin1Char(','), edit_->cursorPosition());
        const QString others = text.left(start) + (end < 0 ? QString() : text.mid(end));
        for (const QString &tag : others.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            present.insert(monster::tagKey(tag));
        }
    }

    // Each entry of the level: a tag ("cave"), a namespace ("ocean:"), or
    // both, as two suggestions.
    QStringList suggestions;
    for (const monster::TagNode &node : *level)
    {
        if (tagKeys_.contains(node.key) && !present.contains(node.key))
        {
            suggestions << node.name;
        }
        if (!node.children.empty())
        {
            suggestions << node.name + QLatin1Char(':');
        }
    }
    model_->setStringList(suggestions);
    completer_->setCompletionPrefix(part);
    if (completer_->completionCount() == 0 ||
        (completer_->completionCount() == 1 &&
         completer_->currentCompletion().compare(part, Qt::CaseInsensitive) == 0))
    {
        completer_->popup()->hide(); // nothing to add to what's typed
        return;
    }
    completer_->complete();
    // The first is taken by Tab or Enter unless another is chosen.
    completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0, 0));
}

void TagCompleter::insert(const QString &choice)
{
    const QString text = edit_->text();
    const qsizetype cursor = edit_->cursorPosition();
    const QString before = text.left(cursor);
    const qsizetype start = segmentStart();
    // Replaces the part being typed (after the last colon of the segment).
    const qsizetype colon = before.mid(start).lastIndexOf(QLatin1Char(':'));
    const qsizetype partStart = colon < 0 ? start : start + colon + 1;
    const QString updated = text.left(partStart) + choice + text.mid(cursor);
    edit_->setText(updated);
    edit_->setCursorPosition(static_cast<int>(partStart + choice.size()));
    // The program's setText() isn't an edit: tell the owner, as typing
    // would.
    emit edit_->textEdited(edit_->text());
    // A namespace: straight on to what's inside it.
    if (choice.endsWith(QLatin1Char(':')))
    {
        update();
    }
    else
    {
        completer_->popup()->hide();
    }
}
