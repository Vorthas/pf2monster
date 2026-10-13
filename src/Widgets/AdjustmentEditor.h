// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Widgets/HomebrewEditor.h"

#include <QHash>
#include <QJsonArray>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;

// A list of JSON objects edited one at a time (an adjustment's abilities,
// Strikes, choices): the list with Add and Remove, and fields bound to keys
// of the one picked. Keys no field shows stay in each object as they were.
class ObjectListEditor : public QWidget
{
public:
    ObjectListEditor(const QString &noun, std::function<QString(const QJsonObject &)> label,
                     QWidget *parent = nullptr);

    // The fields, bound to keys of the picked object (empty text, or a
    // combo item with empty data, leaves the key out).
    QFormLayout *fields() const { return fields_; }
    void bind(const QString &key, QLineEdit *edit);
    void bind(const QString &key, QPlainTextEdit *edit);
    void bind(const QString &key, QComboBox *combo);
    // `extraFill` / `extraRead`: for a field that isn't one key's text (a
    // choice's items).
    void bindCustom(QWidget *field, std::function<void(const QJsonObject &)> fill,
                    std::function<void(QJsonObject &)> read);

    void setItems(const QJsonArray &items);
    // A bound field was edited: its value goes into the picked object (a
    // custom field's owner calls it).
    void fieldEdited();
    QJsonArray items() const { return items_; }
    // Called after any edit (adding, removing, a field).
    std::function<void()> onEdited;
    // What a new object starts as.
    std::function<QJsonObject()> newItem;

private:
    struct Binding
    {
        QString key;
        QWidget *field = nullptr;
        std::function<void(const QJsonObject &)> fill;
        std::function<void(QJsonObject &)> read;
    };
    void showCurrent();
    void refreshLabels();

    QString noun_;
    std::function<QString(const QJsonObject &)> label_;
    QListWidget *list_ = nullptr;
    QWidget *detail_ = nullptr;
    QFormLayout *fields_ = nullptr;
    QList<Binding> bindings_;
    QJsonArray items_;
    bool filling_ = false;
};

// Pairs of words edited as two boxes a row ("human" -> "stonekin"), with a
// remove button on each and an Add button below.
class PairListEditor : public QWidget
{
public:
    PairListEditor(const QString &fromPlaceholder, const QString &toPlaceholder, const QString &addLabel,
                   QWidget *parent = nullptr);

    void setPairs(const QJsonObject &pairs);
    // The complete pairs (both boxes filled).
    QJsonObject pairs() const;
    // The rows with only one box filled.
    int incompleteRows() const;
    std::function<void()> onEdited;
    // Offered while typing in the first box of rows added afterwards.
    QStringList fromSuggestions;

private:
    void addRow(const QString &from, const QString &to);

    QString fromPlaceholder_, toPlaceholder_;
    QVBoxLayout *rows_ = nullptr;
    QList<std::pair<QLineEdit *, QLineEdit *>> edits_;
};

// Homebrew > Adjustments and Templates...: an adjustment or template, the
// keys homebrew mostly uses as form fields (plan 9), every other key in the
// Advanced box. While it's open, the statblock shows the open monster with
// the entry being edited applied (previewChanged()).
class AdjustmentEditor : public HomebrewEditor
{
    Q_OBJECT

public:
    explicit AdjustmentEditor(QWidget *parent = nullptr);

signals:
    // The entry as it stands (empty when it can't be previewed).
    void previewChanged(const QJsonObject &entry);

protected:
    QString kind() const override { return QStringLiteral("adjustments"); }
    QString containerKey() const override { return QString(); }
    bool containerIsMap() const override { return false; }
    QString noun() const override { return tr("adjustment or template"); }
    QString pluralNoun() const override { return tr("adjustments and templates"); }
    QSet<QString> formKeys() const override;
    void buildForm(QFormLayout *form) override;
    void fillForm(const QString &name, const QJsonObject &entry) override;
    QJsonObject readForm(QString *name) const override;
    QList<FieldProblem> checkForm() const override;
    QString entryLabel(const QString &name, const QJsonObject &entry) const override;
    QStringList builtInNames() const override;
    void fillFromBuiltIn(const QString &name) override;
    QJsonObject advancedPart(const QJsonObject &entry) const override;
    QJsonObject mergeAdvanced(QJsonObject entry, const QJsonObject &advanced) const override;
    QStringList advancedProblems(const QJsonObject &advanced) const override;
    void entryChanged() override;

private:
    // Whether the form can show this key's value (else it stays in the
    // Advanced box and its field is disabled).
    static bool formShows(const QString &key, const QJsonValue &value, bool inChanges);
    QString idFor() const;
    void addRow(QFormLayout *form, const QString &label, QWidget *field, const QString &key,
                bool inChanges = true, const QString &tip = QString());

    // General.
    QComboBox *kindCombo_ = nullptr;
    QLineEdit *name_ = nullptr;
    QComboBox *group_ = nullptr;
    QLineEdit *prefix_ = nullptr;
    QPlainTextEdit *rules_ = nullptr;
    QPlainTextEdit *optional_ = nullptr;
    QSpinBox *minLevel_ = nullptr;
    QSpinBox *maxLevel_ = nullptr;
    QLabel *id_ = nullptr;
    QLineEdit *source_ = nullptr;
    // Numbers.
    QSpinBox *level_ = nullptr;
    QSpinBox *bonus_ = nullptr;
    QCheckBox *skipPerception_ = nullptr;
    QCheckBox *skipDcs_ = nullptr;
    QSpinBox *strikeDamage_ = nullptr;
    QSpinBox *hp_ = nullptr;
    QSpinBox *spellBonus_ = nullptr;
    // Traits.
    QLineEdit *traits_ = nullptr;
    QLineEdit *removeTraits_ = nullptr;
    PairListEditor *replaceOrAdd_ = nullptr;
    QComboBox *rarity_ = nullptr;
    // Senses and languages.
    QLineEdit *senses_ = nullptr;
    QLineEdit *languages_ = nullptr;
    QCheckBox *languagesAlways_ = nullptr;
    QLineEdit *languageExtras_ = nullptr;
    // Speeds and size.
    QComboBox *landMode_ = nullptr;
    QSpinBox *landFeet_ = nullptr;
    QPlainTextEdit *speeds_ = nullptr;
    QComboBox *size_ = nullptr;
    // Defenses.
    QLineEdit *immunities_ = nullptr;
    QPlainTextEdit *resistances_ = nullptr;
    QPlainTextEdit *weaknesses_ = nullptr;
    QPlainTextEdit *hpAbilities_ = nullptr;
    QLineEdit *saveNote_ = nullptr;
    // Items and skills.
    QLineEdit *items_ = nullptr;
    QPlainTextEdit *skills_ = nullptr;
    QPlainTextEdit *lores_ = nullptr;
    // Abilities, Strikes, choices.
    ObjectListEditor *abilities_ = nullptr;
    ObjectListEditor *strikes_ = nullptr;
    ObjectListEditor *choices_ = nullptr;

    QString savedId_;
    // The fields by the key they edit ("changes" ones by their own key),
    // and which keys this entry keeps in the Advanced box.
    QHash<QString, QWidget *> fieldOf_;
    QSet<QString> inAdvanced_;
};
