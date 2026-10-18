// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

// Tags for finding monsters in the collection. A tag is a plain string as
// on the website; a colon makes a namespace, and namespaces nest
// ("environment:ocean:cold"). Parts are trimmed, empty ones dropped and
// compared ignoring case, part by part: "environment:ocean" holds
// "environment:ocean:cold" but not "environment:oceanic".
namespace monster
{
    // "environment : ocean::cold" -> {"environment", "ocean", "cold"}.
    QStringList tagPath(const QString &tag);

    // What tags are compared by: the path in lower case joined by colons
    // ("environment:ocean:cold"); empty for a tag with no parts (":").
    QString tagKey(const QString &tag);

    // Whether the tag is the one `key` names or inside it.
    bool tagWithin(const QString &tag, const QString &key);

    // Whether a monster with `tags` passes the checked tags (keys). Checked
    // tags group by their top namespace (a plain tag is its own group, and a
    // plain "environment" joins the environment group): any one of a group
    // will do, and every group must match.
    bool tagsMatch(const QStringList &tags, const QStringList &checkedKeys);

    // A level of the tag tree: a tag, a namespace or both.
    struct TagNode
    {
        QString name; // this part, as first written ("ocean")
        QString key;  // the whole path ("environment:ocean")
        std::vector<TagNode> children; // sorted by name, ignoring case
    };

    // Every tag in `tags` as a tree, sorted at each level ignoring case.
    std::vector<TagNode> tagTree(const QStringList &tags);

    // Every key in the tree: each tag and each namespace above it.
    QSet<QString> tagTreeKeys(const QStringList &tags);
} // namespace monster
