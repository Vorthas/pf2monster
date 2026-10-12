// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <QSet>

#include <optional>

class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

// What the Homebrew editors share (plan 9): the homebrew entries of one
// kind on the left, with the folder each is in; the form for the one
// picked on the right, with its problems listed above it and the fields
// they concern outlined; keys the form doesn't know in an Advanced box;
// New, Copy from Built-in, Delete and Save. A file that isn't valid JSON
// opens as text at the problem's line, and goes back to the form once it
// can be read.
//
// Files hold several entries in a "container" (domains: {"domains":
// [{"name": ...}]}, traits: {"traits": {"stonekin": {...}}}), or are one entry
// each (road maps, adjustments: an empty containerKey(), the entry's name
// is its "id"); each subclass gives the form and the keys.
class HomebrewEditor : public QDialog
{
    Q_OBJECT

public:
    explicit HomebrewEditor(QWidget *parent = nullptr);

signals:
    // A homebrew file was written or moved to the trash: the app reloads.
    void saved();

protected:
    // A problem with a field of the form: the field is outlined and the
    // message listed. `blocking`: it can't be saved like this.
    struct FieldProblem
    {
        QWidget *field = nullptr;
        QString message;
        bool blocking = true;
    };

    // The folder of each homebrew folder its files are in ("domains").
    virtual QString kind() const = 0;
    // The key the file keeps its entries under ("domains", "traits"), or
    // empty when the file is the entry.
    virtual QString containerKey() const = 0;
    // Whether that's an object by entry name (traits) rather than a list
    // of objects with a "name" (domains).
    virtual bool containerIsMap() const = 0;
    // "domain", "trait": in the dialog's texts; the list's title in the
    // plural ("Your homebrew domains").
    virtual QString noun() const = 0;
    virtual QString pluralNoun() const { return noun() + QStringLiteral("s"); }
    // The keys the form edits; others go to the Advanced box.
    virtual QSet<QString> formKeys() const = 0;
    // The form's fields, added to `form`.
    virtual void buildForm(QFormLayout *form) = 0;
    // Fills the form from an entry (`name`: its key in a map container,
    // else its "name"); empty for a new one.
    virtual void fillForm(const QString &name, const QJsonObject &entry) = 0;
    // The entry the form describes, and its name in `name`.
    virtual QJsonObject readForm(QString *name) const = 0;
    // What's wrong with the form as it is.
    virtual QList<FieldProblem> checkForm() const = 0;
    // How the list shows an entry (default: its name).
    virtual QString entryLabel(const QString &name, const QJsonObject &entry) const;
    // The part of an entry the Advanced box holds (default: the top-level
    // keys not in formKeys()), and putting it back into what the form
    // gives (default: the keys the form left out).
    virtual QJsonObject advancedPart(const QJsonObject &entry) const;
    virtual QJsonObject mergeAdvanced(QJsonObject entry, const QJsonObject &advanced) const;
    // What's wrong with the Advanced box's keys (default: any key at all is
    // one the app doesn't know).
    virtual QStringList advancedProblems(const QJsonObject &advanced) const;
    // Called after the form shows another entry or is edited (the
    // adjustment editor's preview).
    virtual void entryChanged() {}
    // The entry as the form and the Advanced box describe it now (nullopt
    // if the Advanced box isn't valid JSON), and its name.
    std::optional<QJsonObject> currentEntry(QString *name = nullptr) const;
    // The built-in entries to copy from, and filling the form from one.
    virtual QStringList builtInNames() const = 0;
    virtual void fillFromBuiltIn(const QString &name) = 0;

    // Builds the dialog; subclasses call it at the end of their
    // constructor.
    void setUp(const QString &title);
    // Puts `advanced` in the Advanced box (Copy from Built-in).
    void setAdvanced(const QJsonObject &advanced);
    // The subclass's fields call it when the user edits them.
    void formEdited();

    // Outlines `field` (or clears it with an empty message).
    static void markField(QWidget *field, const QString &message);

    // Closing (the button, Escape, the window's close) asks about unsaved
    // edits first.
    void reject() override;

private:
    // An entry in the list: a file, and the entry's name in it (empty for
    // a file that can't be read as JSON).
    struct Entry
    {
        QString file;
        QString name;
    };

    void refreshList(const Entry &select = Entry());
    void showEntry(int row);
    void newEntry();
    void copyFromBuiltIn();
    void deleteEntry();
    bool save();
    // Asks whether to save the edits first; false if the user cancelled.
    bool maybeSave();
    void updateProblems();
    void setDirty(bool dirty);
    // The file's object, or an empty one if it doesn't exist.
    QJsonObject readFile(const QString &path) const;
    bool writeFile(const QString &path, const QJsonObject &root);
    // Where a new entry called `name` goes in the chosen folder.
    QString newFilePath(const QString &name) const;

    QListWidget *list_ = nullptr;
    QList<Entry> entries_;
    QLabel *problems_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QFormLayout *form_ = nullptr;
    QPlainTextEdit *advanced_ = nullptr;
    QPlainTextEdit *text_ = nullptr; // a file that isn't valid JSON
    QLabel *where_ = nullptr;
    QComboBox *folder_ = nullptr;
    QPushButton *delete_ = nullptr;
    QPushButton *save_ = nullptr;

    Entry current_;    // file empty: a new entry
    QString loadedName_; // its name when opened (a rename moves it)
    bool dirty_ = false;
    bool filling_ = false; // filling the form isn't an edit
    QList<QPointer<QWidget>> marked_; // the fields outlined now
};
