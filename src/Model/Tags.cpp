// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Tags.h"

#include <QHash>
#include <QMap>

#include <algorithm>

namespace monster
{
    QStringList tagPath(const QString &tag)
    {
        QStringList path;
        for (const QString &part : tag.split(QLatin1Char(':')))
        {
            if (const QString t = part.trimmed(); !t.isEmpty())
            {
                path << t;
            }
        }
        return path;
    }

    QString tagKey(const QString &tag)
    {
        return tagPath(tag).join(QLatin1Char(':')).toLower();
    }

    bool tagWithin(const QString &tag, const QString &key)
    {
        const QString own = tagKey(tag);
        return !key.isEmpty() && own.startsWith(key) &&
               (own.size() == key.size() || own.at(key.size()) == QLatin1Char(':'));
    }

    bool tagsMatch(const QStringList &tags, const QStringList &checkedKeys)
    {
        QHash<QString, QStringList> groups; // top namespace -> checked keys
        for (const QString &key : checkedKeys)
        {
            groups[key.section(QLatin1Char(':'), 0, 0)] << key;
        }
        for (const QStringList &group : std::as_const(groups))
        {
            const bool any = std::any_of(tags.begin(), tags.end(), [&](const QString &tag) {
                return std::any_of(group.begin(), group.end(),
                                   [&](const QString &key) { return tagWithin(tag, key); });
            });
            if (!any)
            {
                return false;
            }
        }
        return true;
    }

    namespace
    {
        // Built with maps by lower-case part, which keeps each level
        // sorted ignoring case.
        struct Building
        {
            QString name;
            QMap<QString, Building> children;
        };

        std::vector<TagNode> finish(const QMap<QString, Building> &level, const QString &parentKey)
        {
            std::vector<TagNode> nodes;
            for (auto it = level.begin(); it != level.end(); ++it)
            {
                TagNode node;
                node.name = it->name;
                node.key = parentKey.isEmpty() ? it.key() : parentKey + QLatin1Char(':') + it.key();
                node.children = finish(it->children, node.key);
                nodes.push_back(std::move(node));
            }
            return nodes;
        }
    } // namespace

    std::vector<TagNode> tagTree(const QStringList &tags)
    {
        QMap<QString, Building> top;
        for (const QString &tag : tags)
        {
            QMap<QString, Building> *level = &top;
            for (const QString &part : tagPath(tag))
            {
                Building &node = (*level)[part.toLower()];
                if (node.name.isEmpty())
                {
                    node.name = part;
                }
                level = &node.children;
            }
        }
        return finish(top, QString());
    }

    QSet<QString> tagTreeKeys(const QStringList &tags)
    {
        QSet<QString> keys;
        for (const QString &tag : tags)
        {
            QString key;
            for (const QString &part : tagPath(tag))
            {
                key += (key.isEmpty() ? QString() : QStringLiteral(":")) + part.toLower();
                keys.insert(key);
            }
        }
        return keys;
    }
} // namespace monster
