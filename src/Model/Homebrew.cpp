// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Model/Homebrew.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace monster
{
    namespace
    {
        const QString kFoldersKey = QStringLiteral("homebrew/folders");
        const QString kDataEnabledKey = QStringLiteral("homebrew/dataEnabled");
        const QString kLogFolderKey = QStringLiteral("homebrew/logFolder");

        QList<HomebrewProblem> &problems()
        {
            static QList<HomebrewProblem> list;
            return list;
        }

        // Line and column (from 1) of character `offset` in `text`.
        std::pair<int, int> lineColumn(const QString &text, qsizetype offset)
        {
            offset = std::clamp<qsizetype>(offset, 0, text.size());
            int line = 1;
            qsizetype lineStart = 0;
            for (qsizetype i = 0; i < offset; ++i)
            {
                if (text.at(i) == QLatin1Char('\n'))
                {
                    ++line;
                    lineStart = i + 1;
                }
            }
            return {line, static_cast<int>(offset - lineStart) + 1};
        }

        // The parser's complaint in plain words.
        QString parseProblem(QJsonParseError::ParseError error)
        {
            switch (error)
            {
            case QJsonParseError::UnterminatedObject:
                return QStringLiteral("a { has no matching } (or a comma is missing before this point)");
            case QJsonParseError::MissingNameSeparator:
                return QStringLiteral("a : is missing after a name in quotes");
            case QJsonParseError::UnterminatedArray:
                return QStringLiteral("a [ has no matching ] (or a comma is missing before this point)");
            case QJsonParseError::MissingValueSeparator:
                return QStringLiteral("a comma is missing between two entries");
            case QJsonParseError::IllegalValue:
                return QStringLiteral("this value isn't valid: text needs \"double quotes\", and "
                                      "only numbers, true, false and null go without them");
            case QJsonParseError::TerminationByNumber:
            case QJsonParseError::IllegalNumber:
                return QStringLiteral("this number isn't valid");
            case QJsonParseError::IllegalEscapeSequence:
                return QStringLiteral("a \\ in text starts an escape the format doesn't know "
                                      "(write \\\\ for a backslash)");
            case QJsonParseError::IllegalUTF8String:
                return QStringLiteral("the text has characters that aren't valid UTF-8 (save the file as UTF-8)");
            case QJsonParseError::UnterminatedString:
                return QStringLiteral("a \" has no closing \"");
            case QJsonParseError::MissingObject:
                return QStringLiteral("the file should start with {");
            case QJsonParseError::DeepNesting:
                return QStringLiteral("too many { or [ inside each other");
            case QJsonParseError::DocumentTooLarge:
                return QStringLiteral("the file is too large");
            case QJsonParseError::GarbageAtEnd:
                return QStringLiteral("there's more text after the final }");
            default:
                return QStringLiteral("the file can't be read as JSON");
            }
        }

        // How many single-character edits make `a` into `b`.
        int editDistance(const QString &a, const QString &b)
        {
            std::vector<int> row(static_cast<size_t>(b.size()) + 1);
            for (size_t j = 0; j < row.size(); ++j)
            {
                row[j] = static_cast<int>(j);
            }
            for (qsizetype i = 1; i <= a.size(); ++i)
            {
                int diagonal = row[0];
                row[0] = static_cast<int>(i);
                for (qsizetype j = 1; j <= b.size(); ++j)
                {
                    const int above = row[static_cast<size_t>(j)];
                    const int cost = a.at(i - 1).toLower() == b.at(j - 1).toLower() ? 0 : 1;
                    row[static_cast<size_t>(j)] =
                        std::min({above + 1, row[static_cast<size_t>(j - 1)] + 1, diagonal + cost});
                    diagonal = above;
                }
            }
            return row.back();
        }
    } // namespace

    QString homebrewDataFolder()
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("pf2monster/homebrew"));
    }

    QList<HomebrewFolder> homebrewFolders()
    {
        QSettings settings;
        QList<HomebrewFolder> folders{{homebrewDataFolder(), settings.value(kDataEnabledKey, true).toBool()}};
        for (const QVariant &v : settings.value(kFoldersKey).toList())
        {
            const QVariantMap map = v.toMap();
            const QString path = map.value(QStringLiteral("path")).toString();
            if (!path.isEmpty())
            {
                folders.push_back({path, map.value(QStringLiteral("enabled"), true).toBool()});
            }
        }
        return folders;
    }

    void setHomebrewFolders(const QList<HomebrewFolder> &folders)
    {
        QSettings settings;
        QVariantList extra;
        for (const HomebrewFolder &f : folders)
        {
            if (QDir(f.path) == QDir(homebrewDataFolder()))
            {
                settings.setValue(kDataEnabledKey, f.enabled);
                continue;
            }
            extra << QVariantMap{{QStringLiteral("path"), f.path}, {QStringLiteral("enabled"), f.enabled}};
        }
        settings.setValue(kFoldersKey, extra);
    }

    QString homebrewLogFolder(const QString &collection)
    {
        const QString chosen = QSettings().value(kLogFolderKey).toString();
        return chosen.isEmpty() ? collection : chosen;
    }

    void setHomebrewLogFolder(const QString &folder)
    {
        QSettings().setValue(kLogFolderKey, folder);
    }

    QStringList homebrewFiles(const QString &kind)
    {
        QStringList files;
        for (const HomebrewFolder &folder : homebrewFolders())
        {
            if (!folder.enabled)
            {
                continue;
            }
            QStringList here;
            QDirIterator it(QDir(folder.path).filePath(kind), {QStringLiteral("*.json")}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                here << it.next();
            }
            here.sort(Qt::CaseInsensitive);
            files << here;
        }
        return files;
    }

    QString HomebrewProblem::text() const
    {
        QString where = file;
        if (line > 0)
        {
            where += column > 0 ? QStringLiteral(", line %1, column %2").arg(line).arg(column)
                                : QStringLiteral(", line %1").arg(line);
        }
        return where + QStringLiteral(": ") + message;
    }

    const QList<HomebrewProblem> &homebrewProblems()
    {
        return problems();
    }

    void clearHomebrewProblems()
    {
        problems().clear();
    }

    void addHomebrewProblem(const HomebrewProblem &problem)
    {
        problems().push_back(problem);
    }

    std::optional<QJsonObject> parseHomebrewText(const QString &path, const QString &content,
                                                 HomebrewProblem *problem)
    {
        const QByteArray bytes = content.toUtf8();
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError)
        {
            // The parser's offset is in bytes; as characters for the line.
            const qsizetype offset = QString::fromUtf8(bytes.left(error.offset)).size();
            const auto [line, column] = lineColumn(content, offset);
            QString message = parseProblem(error.error);
            // The parser stops where the next entry starts; a missing comma
            // is usually at the end of the last line with text before it.
            if (error.error == QJsonParseError::UnterminatedObject ||
                error.error == QJsonParseError::UnterminatedArray ||
                error.error == QJsonParseError::MissingValueSeparator)
            {
                const QStringList lines = content.left(offset).split(QLatin1Char('\n'));
                for (qsizetype i = lines.size() - 2; i >= 0; --i)
                {
                    if (!lines[i].trimmed().isEmpty())
                    {
                        message = QStringLiteral("probably a comma is missing at the end of line %1 "
                                                 "(or a { or [ has no matching } or ])")
                                      .arg(i + 1);
                        break;
                    }
                }
            }
            if (problem)
            {
                *problem = {path, line, column,
                            QStringLiteral("isn't valid JSON, so nothing in it was loaded: %1").arg(message)};
            }
            return std::nullopt;
        }
        if (!doc.isObject())
        {
            if (problem)
            {
                *problem = {path, 1, 1, QStringLiteral("should be one { ... } with the entry's keys in it")};
            }
            return std::nullopt;
        }
        return doc.object();
    }

    std::optional<QJsonObject> readHomebrewJson(const QString &path, QString *text)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            addHomebrewProblem({path, 0, 0, QStringLiteral("the file can't be opened: %1").arg(file.errorString())});
            return std::nullopt;
        }
        const QString content = QString::fromUtf8(file.readAll());
        if (text)
        {
            *text = content;
        }
        HomebrewProblem problem;
        std::optional<QJsonObject> object = parseHomebrewText(path, content, &problem);
        if (!object)
        {
            addHomebrewProblem(problem);
        }
        return object;
    }

    std::pair<int, int> keyPosition(const QString &text, const QString &key, qsizetype from)
    {
        const qsizetype at = text.indexOf(QLatin1Char('"') + key + QLatin1Char('"'), from);
        return at < 0 ? std::pair{0, 0} : lineColumn(text, at);
    }

    namespace
    {
        // "(did you mean \"languages\"?)" for a key a couple of letters
        // from one known, else empty.
        QString suggestion(const QString &key, const QSet<QString> &known)
        {
            QString closest;
            int best = 3;
            for (const QString &k : known)
            {
                if (const int d = editDistance(key, k); d < best)
                {
                    best = d;
                    closest = k;
                }
            }
            return closest.isEmpty() ? QString() : QStringLiteral(" (did you mean \"%1\"?)").arg(closest);
        }
    } // namespace

    QStringList unknownKeyMessages(const QJsonObject &object, const QSet<QString> &known)
    {
        QStringList messages;
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!known.contains(it.key()))
            {
                messages << QStringLiteral("\"%1\" isn't a key the app knows, so it's ignored").arg(it.key()) +
                                suggestion(it.key(), known);
            }
        }
        return messages;
    }

    void checkKeys(const QString &path, const QString &text, const QJsonObject &object,
                   const QSet<QString> &known, const QString &where)
    {
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (known.contains(it.key()))
            {
                continue;
            }
            const QString message =
                (where.isEmpty()
                     ? QStringLiteral("\"%1\" isn't a key the app knows, so it's ignored").arg(it.key())
                     : QStringLiteral("\"%1\" %2 isn't a key the app knows, so it's ignored").arg(it.key(), where)) +
                suggestion(it.key(), known);
            const auto [line, column] = keyPosition(text, it.key());
            addHomebrewProblem({path, line, column, message});
        }
    }

    QString writeHomebrewLog(const QString &folder)
    {
        if (folder.isEmpty() || !QDir().mkpath(folder))
        {
            return QString();
        }
        const QString path = QDir(folder).filePath(kHomebrewLogName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            return QString();
        }
        QStringList lines{
            QStringLiteral("Monster Builder for Pathfinder 2e: homebrew problems, %1")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate)),
            QStringLiteral("%1 problem(s). The files without problems loaded as usual.").arg(problems().size()),
            QString()};
        for (const HomebrewProblem &p : problems())
        {
            lines << p.text();
        }
        file.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n');
        return path;
    }
} // namespace monster
