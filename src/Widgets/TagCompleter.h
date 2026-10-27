// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>

class QCompleter;
class QLineEdit;
class QStringListModel;

// Suggests the collection's tags while typing in a line edit, one level of
// a namespace at a time (Model/Tags.h): "env" offers "environment:" (a
// namespace) and plain tags starting so; picking "environment:" fills it in
// and offers what's inside it ("cave", "ocean:", ...). Matches the start of
// the part being typed, ignoring case; shows each part as first written.
// Tab, Enter or a click takes a suggestion (the first unless another is
// chosen), Escape closes the list, typing on ignores it.
class TagCompleter : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        Tags,   // the form's Tags box: tags separated by commas; ones the
                // box already has aren't offered again
        Filter, // a filter box: words separated by spaces
    };

    // Works on `edit` (and is deleted with it).
    TagCompleter(QLineEdit *edit, Mode mode);

    // The tags to suggest from (each as some file writes it).
    void setTags(const QStringList &tags);

protected:
    // Tab and Enter on the open list take the suggestion (QCompleter
    // would hand them to the line edit first: Tab moves the focus away,
    // Enter in a filter box opens the first monster).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Shows the suggestions for the text before the cursor, or hides them.
    void update();
    // Puts `choice` in place of the part being typed.
    void insert(const QString &choice);
    // Where the tag (or word) being typed starts, before the cursor.
    qsizetype segmentStart() const;

    QLineEdit *edit_;
    Mode mode_;
    QStringList tags_;
    QSet<QString> tagKeys_; // the keys of the tags themselves (not namespaces)
    QCompleter *completer_;
    QStringListModel *model_;
};
