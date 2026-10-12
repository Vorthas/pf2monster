// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

// Homebrew: the user's own adjustments, templates, road maps, domains and
// traits, read from homebrew folders after the built-in data (plan 9). Each
// folder has the same layout: adjustments/, roadmaps/, domains/, traits/.
// The data folder (~/.local/share/pf2monster/homebrew) comes first, then
// the extra folders the user lists; each can be switched off.
namespace monster
{
    // The prefix a homebrew entry's id gets, so it never mixes with a
    // built-in one of the same id ("homebrew:custom-stonekin").
    inline const QString kHomebrewPrefix = QStringLiteral("homebrew:");

    struct HomebrewFolder
    {
        QString path;
        bool enabled = true;
    };

    // The data folder, whether or not it exists yet.
    QString homebrewDataFolder();

    // Every homebrew folder in order (the data folder first), as set in
    // Homebrew Folders...; saved in the settings.
    QList<HomebrewFolder> homebrewFolders();
    void setHomebrewFolders(const QList<HomebrewFolder> &folders);

    // Where the problem log goes (default: the monster collection folder,
    // `collection`).
    QString homebrewLogFolder(const QString &collection);
    void setHomebrewLogFolder(const QString &folder);
    inline const QString kHomebrewLogName = QStringLiteral("pf2monster-homebrew.log");

    // The .json files in `kind` ("adjustments", ...) of the switched-on
    // folders, in folder order, subfolders included, sorted within each.
    QStringList homebrewFiles(const QString &kind);

    // A problem with a homebrew file, as the user sees it: the file, and
    // where in it when known (line and column from 1; 0: unknown).
    struct HomebrewProblem
    {
        QString file;
        int line = 0;
        int column = 0;
        QString message;

        // "/home/.../stonekin.json, line 12, column 5: ..."
        QString text() const;
    };

    // The problems the last load of homebrew found (every kind).
    const QList<HomebrewProblem> &homebrewProblems();
    void clearHomebrewProblems();
    void addHomebrewProblem(const HomebrewProblem &problem);

    // A homebrew file read as JSON: its object, or nullopt with a problem
    // recorded (unreadable, not valid JSON with the line and column, not an
    // object). `text` gets the file's text, for finding keys' lines.
    std::optional<QJsonObject> readHomebrewJson(const QString &path, QString *text = nullptr);

    // A homebrew file's text as JSON, without recording anything: its
    // object, or nullopt and the problem (with the line and column) in
    // `problem` (the editors use it).
    std::optional<QJsonObject> parseHomebrewText(const QString &path, const QString &text,
                                                 HomebrewProblem *problem);

    // The keys of `object` not in `known`, each with its message ("\"x\"
    // isn't a key the app knows ... did you mean ...").
    QStringList unknownKeyMessages(const QJsonObject &object, const QSet<QString> &known);

    // The line and column of the first `"key"` in `text` at or after
    // `from` (a character index), for a problem with that key; {0, 0} if
    // it isn't there.
    std::pair<int, int> keyPosition(const QString &text, const QString &key, qsizetype from = 0);

    // Records a problem for each key of `object` not in `known` ("unknown
    // key"), with a suggestion when one known key is close ("did you mean
    // \"languages\"?"). `where` names the part ("in \"changes\"").
    void checkKeys(const QString &path, const QString &text, const QJsonObject &object,
                   const QSet<QString> &known, const QString &where = QString());

    // Writes the problems to the log in `folder` (replacing it), dated.
    // Returns the log's path, or empty if it couldn't be written.
    QString writeHomebrewLog(const QString &folder);
} // namespace monster
