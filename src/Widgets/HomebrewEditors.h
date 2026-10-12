// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Widgets/HomebrewEditor.h"

#include <QMap>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

// Homebrew > Domains...: a domain's name, its domain spell and advanced
// domain spell (for Divine Warden).
class DomainEditor : public HomebrewEditor
{
    Q_OBJECT

public:
    explicit DomainEditor(QWidget *parent = nullptr);

protected:
    QString kind() const override { return QStringLiteral("domains"); }
    QString containerKey() const override { return QStringLiteral("domains"); }
    bool containerIsMap() const override { return false; }
    QString noun() const override { return tr("domain"); }
    QSet<QString> formKeys() const override;
    void buildForm(QFormLayout *form) override;
    void fillForm(const QString &name, const QJsonObject &entry) override;
    QJsonObject readForm(QString *name) const override;
    QList<FieldProblem> checkForm() const override;
    QStringList builtInNames() const override;
    void fillFromBuiltIn(const QString &name) override;

private:
    QLineEdit *name_ = nullptr;
    QLineEdit *spell_ = nullptr;
    QLineEdit *advanced_ = nullptr;
    QLineEdit *source_ = nullptr;
};

// Homebrew > Traits...: a trait's note (shown in the form for monsters
// with it) and the skills that identify a creature with it (Recall
// Knowledge).
class TraitEditor : public HomebrewEditor
{
    Q_OBJECT

public:
    explicit TraitEditor(QWidget *parent = nullptr);

protected:
    QString kind() const override { return QStringLiteral("traits"); }
    QString containerKey() const override { return QStringLiteral("traits"); }
    bool containerIsMap() const override { return true; }
    QString noun() const override { return tr("trait"); }
    QSet<QString> formKeys() const override;
    void buildForm(QFormLayout *form) override;
    void fillForm(const QString &name, const QJsonObject &entry) override;
    QJsonObject readForm(QString *name) const override;
    QList<FieldProblem> checkForm() const override;
    QStringList builtInNames() const override;
    void fillFromBuiltIn(const QString &name) override;

private:
    QLineEdit *key_ = nullptr;
    QLineEdit *name_ = nullptr;
    QPlainTextEdit *lines_ = nullptr;
    QList<QCheckBox *> skills_;
    QLineEdit *otherSkills_ = nullptr;
    QCheckBox *ancestry_ = nullptr;
    QLineEdit *source_ = nullptr;
};

// Homebrew > Road Maps...: a road map's name (and variant), its submenu,
// the benchmark it sets for each statistic (with the alternatives the
// rules allow) and its notes for the form's sections.
class RoadmapEditor : public HomebrewEditor
{
    Q_OBJECT

public:
    explicit RoadmapEditor(QWidget *parent = nullptr);

protected:
    QString kind() const override { return QStringLiteral("roadmaps"); }
    QString containerKey() const override { return QString(); }
    bool containerIsMap() const override { return false; }
    QString noun() const override { return tr("road map"); }
    QSet<QString> formKeys() const override;
    void buildForm(QFormLayout *form) override;
    void fillForm(const QString &name, const QJsonObject &entry) override;
    QJsonObject readForm(QString *name) const override;
    QList<FieldProblem> checkForm() const override;
    QString entryLabel(const QString &name, const QJsonObject &entry) const override;
    QStringList builtInNames() const override;
    void fillFromBuiltIn(const QString &name) override;

private:
    // One statistic's row: the benchmark it sets, and the alternatives.
    struct StatRow
    {
        QString key;
        QComboBox *main = nullptr;
        QList<QCheckBox *> alternatives;
    };
    void updateAlternatives(StatRow &row);
    // Its id: the one it was saved with, else made from the name.
    QString idFor() const;

    QLineEdit *name_ = nullptr;
    QLineEdit *variant_ = nullptr;
    QComboBox *list_ = nullptr;
    QPlainTextEdit *rules_ = nullptr;
    QLabel *id_ = nullptr;
    QList<StatRow> rows_;
    QList<std::pair<QString, QPlainTextEdit *>> notes_;
    QLineEdit *source_ = nullptr;
    QLineEdit *license_ = nullptr;
    QString savedId_; // empty for a new one
    // From the file, what the form can't show: statistics and note
    // sections the app doesn't know (kept as written when saved), and
    // benchmark names that aren't valid for their statistic (by key).
    QJsonObject otherBenchmarks_;
    QJsonObject otherNotes_;
    QMap<QString, QString> badNames_;
};

