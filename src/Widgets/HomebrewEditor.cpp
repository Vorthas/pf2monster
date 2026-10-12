// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/HomebrewEditor.h"

#include "Model/Homebrew.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBlock>
#include <QVBoxLayout>

namespace
{
    // Which homebrew folder a file is in, for the list: "data folder" or
    // the folder's own name.
    QString folderLabel(const QString &file)
    {
        for (const monster::HomebrewFolder &folder : monster::homebrewFolders())
        {
            const QString root = QDir(folder.path).absolutePath() + QLatin1Char('/');
            if (QFileInfo(file).absoluteFilePath().startsWith(root))
            {
                return QDir(folder.path) == QDir(monster::homebrewDataFolder())
                           ? HomebrewEditor::tr("data folder")
                           : QDir(folder.path).dirName();
            }
        }
        return QFileInfo(file).absolutePath();
    }

    // A file name from an entry's name: "Storm Lord" -> "storm-lord".
    QString slug(const QString &name)
    {
        QString s = name.trimmed().toLower();
        s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        return s.isEmpty() ? QStringLiteral("homebrew") : s;
    }
} // namespace

HomebrewEditor::HomebrewEditor(QWidget *parent) : QDialog(parent)
{
}

QString HomebrewEditor::entryLabel(const QString &name, const QJsonObject &) const
{
    return name;
}

void HomebrewEditor::setAdvanced(const QJsonObject &advanced)
{
    advanced_->setPlainText(advanced.isEmpty() ? QString()
                                               : QString::fromUtf8(QJsonDocument(advanced).toJson(QJsonDocument::Indented)));
}

QJsonObject HomebrewEditor::advancedPart(const QJsonObject &entry) const
{
    QJsonObject other;
    const QSet<QString> known = formKeys();
    for (auto it = entry.begin(); it != entry.end(); ++it)
    {
        if (!known.contains(it.key()))
        {
            other.insert(it.key(), it.value());
        }
    }
    return other;
}

QJsonObject HomebrewEditor::mergeAdvanced(QJsonObject entry, const QJsonObject &advanced) const
{
    for (auto it = advanced.begin(); it != advanced.end(); ++it)
    {
        if (!entry.contains(it.key()))
        {
            entry.insert(it.key(), it.value());
        }
    }
    return entry;
}

QStringList HomebrewEditor::advancedProblems(const QJsonObject &advanced) const
{
    return monster::unknownKeyMessages(advanced, formKeys());
}

std::optional<QJsonObject> HomebrewEditor::currentEntry(QString *name) const
{
    QJsonObject other;
    if (const QString typed = advanced_->toPlainText().trimmed(); !typed.isEmpty())
    {
        monster::HomebrewProblem problem;
        const std::optional<QJsonObject> parsed = monster::parseHomebrewText(QString(), typed, &problem);
        if (!parsed)
        {
            return std::nullopt;
        }
        other = *parsed;
    }
    QString entryName;
    const QJsonObject entry = mergeAdvanced(readForm(&entryName), other);
    if (name)
    {
        *name = entryName;
    }
    return entry;
}

void HomebrewEditor::setUp(const QString &title)
{
    setWindowTitle(title);
    auto *outer = new QHBoxLayout(this);

    // The entries.
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Your homebrew %1").arg(pluralNoun())));
    list_ = new QListWidget;
    list_->setMinimumWidth(fontMetrics().averageCharWidth() * 32);
    left->addWidget(list_, 1);
    auto *newButton = new QPushButton(tr("&New"));
    newButton->setToolTip(tr("Start a new %1 from an empty form").arg(noun()));
    auto *copy = new QPushButton(tr("&Copy from Built-in..."));
    copy->setToolTip(tr("Start a new %1 from one of the app's own; the built-in one doesn't change").arg(noun()));
    delete_ = new QPushButton(tr("&Delete..."));
    delete_->setToolTip(tr("Move this %1 to the trash (after asking)").arg(noun()));
    for (QPushButton *button : {newButton, copy, delete_})
    {
        left->addWidget(button);
    }
    outer->addLayout(left);
    connect(newButton, &QPushButton::clicked, this, &HomebrewEditor::newEntry);
    connect(copy, &QPushButton::clicked, this, &HomebrewEditor::copyFromBuiltIn);
    connect(delete_, &QPushButton::clicked, this, &HomebrewEditor::deleteEntry);

    // The form, or a file's text when it can't be read.
    auto *right = new QVBoxLayout;
    problems_ = new QLabel;
    problems_->setWordWrap(true);
    problems_->setTextFormat(Qt::RichText);
    problems_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    problems_->setFrameShape(QFrame::StyledPanel);
    problems_->setMargin(6);
    right->addWidget(problems_);

    pages_ = new QStackedWidget;
    auto *formPage = new QWidget;
    auto *formLayout = new QVBoxLayout(formPage);
    formLayout->setContentsMargins(0, 0, 0, 0);
    form_ = new QFormLayout;
    buildForm(form_);
    formLayout->addLayout(form_);
    auto *advancedBox = new QGroupBox(tr("Advanced"));
    auto *advancedLayout = new QVBoxLayout(advancedBox);
    auto *advancedNote = new QLabel(tr("Anything in the file that the form above doesn't show, kept as "
                                       "written (JSON). Usually empty."));
    advancedNote->setWordWrap(true);
    advancedNote->setEnabled(false);
    advancedLayout->addWidget(advancedNote);
    advanced_ = new QPlainTextEdit;
    advanced_->setPlaceholderText(QStringLiteral("{ }"));
    advanced_->setMaximumHeight(fontMetrics().height() * 6);
    advancedLayout->addWidget(advanced_);
    formLayout->addWidget(advancedBox);
    formLayout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(formPage);
    pages_->addWidget(scroll);
    connect(advanced_, &QPlainTextEdit::textChanged, this, &HomebrewEditor::formEdited);

    text_ = new QPlainTextEdit;
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    text_->setFont(mono);
    pages_->addWidget(text_);
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        if (!filling_)
        {
            setDirty(true);
        }
    });
    right->addWidget(pages_, 1);

    // Where it's saved, and the buttons.
    auto *bottom = new QHBoxLayout;
    where_ = new QLabel;
    where_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottom->addWidget(where_);
    folder_ = new QComboBox;
    folder_->setToolTip(tr("The homebrew folder a new %1 is saved in").arg(noun()));
    for (const monster::HomebrewFolder &folder : monster::homebrewFolders())
    {
        if (folder.enabled)
        {
            folder_->addItem(QDir(folder.path) == QDir(monster::homebrewDataFolder())
                                 ? tr("Homebrew data folder")
                                 : QDir::toNativeSeparators(folder.path),
                             folder.path);
        }
    }
    bottom->addWidget(folder_, 1);
    save_ = new QPushButton(tr("&Save"));
    auto *close = new QPushButton(tr("Close"));
    bottom->addWidget(save_);
    bottom->addWidget(close);
    right->addLayout(bottom);
    connect(save_, &QPushButton::clicked, this, &HomebrewEditor::save);
    connect(close, &QPushButton::clicked, this, &HomebrewEditor::reject);
    outer->addLayout(right, 1);

    // Picking another entry asks about unsaved edits first.
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        static bool reverting = false;
        if (reverting || row < 0)
        {
            return;
        }
        if (!maybeSave())
        {
            // Back to the one being edited.
            reverting = true;
            const int back = static_cast<int>(std::distance(
                entries_.begin(), std::find_if(entries_.begin(), entries_.end(), [this](const Entry &e) {
                    return e.file == current_.file && e.name == current_.name;
                })));
            list_->setCurrentRow(back < entries_.size() ? back : -1);
            reverting = false;
            return;
        }
        showEntry(row);
    });

    resize(fontMetrics().averageCharWidth() * 110, fontMetrics().height() * 34);
    refreshList();
    if (entries_.isEmpty())
    {
        newEntry();
    }
    else
    {
        list_->setCurrentRow(0);
    }
}

void HomebrewEditor::refreshList(const Entry &select)
{
    const QSignalBlocker block(list_);
    list_->clear();
    entries_.clear();
    int selectRow = -1;
    for (const QString &path : monster::homebrewFiles(kind()))
    {
        QFile file(path);
        const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        monster::HomebrewProblem problem;
        const std::optional<QJsonObject> root = monster::parseHomebrewText(path, text, &problem);
        QStringList names;
        QList<QJsonObject> objects;
        if (root && containerKey().isEmpty())
        {
            // The file is the entry.
            names << root->value(QStringLiteral("id")).toString();
            objects << *root;
        }
        else if (root)
        {
            const QJsonValue container = root->value(containerKey());
            if (containerIsMap())
            {
                const QJsonObject map = container.toObject();
                for (auto it = map.begin(); it != map.end(); ++it)
                {
                    names << it.key();
                    objects << it.value().toObject();
                }
            }
            else
            {
                for (const QJsonValue &v : container.toArray())
                {
                    names << v.toObject().value(QStringLiteral("name")).toString();
                    objects << v.toObject();
                }
            }
        }
        const QString where = folderLabel(path);
        if (!root)
        {
            auto *item = new QListWidgetItem(tr("%1 (can't be read)").arg(QFileInfo(path).fileName()), list_);
            item->setToolTip(QDir::toNativeSeparators(path));
            entries_.push_back({path, QString()});
            if (select.file == path)
            {
                selectRow = list_->count() - 1;
            }
            continue;
        }
        for (qsizetype i = 0; i < names.size(); ++i)
        {
            const QString &name = names[i];
            const QString label = entryLabel(name, objects[i]);
            const QString shown = label.trimmed().isEmpty() ? tr("(no name)") : label;
            auto *item = new QListWidgetItem(QStringLiteral("%1  —  %2").arg(shown, where), list_);
            item->setToolTip(QDir::toNativeSeparators(path));
            entries_.push_back({path, name});
            if (select.file == path && (select.name == name || select.name.isEmpty()) && selectRow < 0)
            {
                selectRow = list_->count() - 1;
            }
        }
    }
    if (selectRow >= 0)
    {
        list_->setCurrentRow(selectRow);
    }
}

void HomebrewEditor::showEntry(int row)
{
    if (row < 0 || row >= entries_.size())
    {
        return;
    }
    current_ = entries_[row];
    QFile file(current_.file);
    const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    monster::HomebrewProblem problem;
    const std::optional<QJsonObject> root = monster::parseHomebrewText(current_.file, text, &problem);
    folder_->setVisible(false);
    where_->setText(tr("In %1").arg(QDir::toNativeSeparators(current_.file)));
    filling_ = true;
    if (!root)
    {
        // Not valid JSON: the text, at the problem's line.
        text_->setPlainText(text);
        pages_->setCurrentIndex(1);
        if (problem.line > 0)
        {
            QTextCursor cursor(text_->document()->findBlockByNumber(problem.line - 1));
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, std::max(0, problem.column - 1));
            text_->setTextCursor(cursor);
            text_->setFocus();
        }
        problems_->setText(tr("<b>This file can't be read</b>, so it can't be shown as a form. "
                              "Fix it here and save; it opens as a form once it reads.<br>%1")
                               .arg(problem.text().mid(problem.file.size() + 2).toHtmlEscaped()));
        problems_->setVisible(true);
        filling_ = false;
        setDirty(false);
        return;
    }
    QJsonObject entry;
    const QJsonValue container = root->value(containerKey());
    if (containerKey().isEmpty())
    {
        entry = *root;
    }
    else if (containerIsMap())
    {
        entry = container.toObject().value(current_.name).toObject();
    }
    else
    {
        for (const QJsonValue &v : container.toArray())
        {
            if (v.toObject().value(QStringLiteral("name")).toString() == current_.name)
            {
                entry = v.toObject();
                break;
            }
        }
    }
    pages_->setCurrentIndex(0);
    fillForm(current_.name, entry);
    // What the form doesn't edit, kept as written.
    const QJsonObject other = advancedPart(entry);
    advanced_->setPlainText(other.isEmpty() ? QString()
                                            : QString::fromUtf8(QJsonDocument(other).toJson(QJsonDocument::Indented)));
    loadedName_ = current_.name;
    filling_ = false;
    setDirty(false);
    updateProblems();
    entryChanged();
}

void HomebrewEditor::newEntry()
{
    if (!maybeSave())
    {
        return;
    }
    {
        const QSignalBlocker block(list_);
        list_->setCurrentRow(-1);
    }
    current_ = Entry();
    loadedName_.clear();
    filling_ = true;
    pages_->setCurrentIndex(0);
    fillForm(QString(), QJsonObject());
    advanced_->clear();
    filling_ = false;
    where_->setText(tr("Save to"));
    folder_->setVisible(true);
    setDirty(false);
    updateProblems();
    entryChanged();
}

void HomebrewEditor::copyFromBuiltIn()
{
    if (!maybeSave())
    {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("Copy from Built-in"),
                                               tr("Start a new %1 from:").arg(noun()), builtInNames(), 0,
                                               false, &ok);
    if (!ok || name.isEmpty())
    {
        return;
    }
    newEntry();
    filling_ = true;
    fillFromBuiltIn(name);
    filling_ = false;
    setDirty(true);
    updateProblems();
    entryChanged();
}

void HomebrewEditor::deleteEntry()
{
    if (current_.file.isEmpty())
    {
        // A new one not saved yet: just start over.
        setDirty(false);
        newEntry();
        return;
    }
    const QString shown = current_.name.isEmpty() ? QFileInfo(current_.file).fileName() : current_.name;
    QJsonObject root = readFile(current_.file);
    int others = 0;
    if (!current_.name.isEmpty() && !containerKey().isEmpty())
    {
        const QJsonValue container = root.value(containerKey());
        others = static_cast<int>(containerIsMap() ? container.toObject().size() : container.toArray().size()) - 1;
    }
    const QString question =
        others > 0 ? tr("Delete the %1 \"%2\"? It's taken out of this file; the rest of the file stays "
                        "as it is:\n%3")
                         .arg(noun(), shown, QDir::toNativeSeparators(current_.file))
                   : tr("Delete the %1 \"%2\"? Its file goes to the trash:\n%3")
                         .arg(noun(), shown, QDir::toNativeSeparators(current_.file));
    if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes)
    {
        return;
    }
    if (others > 0)
    {
        if (containerIsMap())
        {
            QJsonObject map = root.value(containerKey()).toObject();
            map.remove(current_.name);
            root.insert(containerKey(), map);
        }
        else
        {
            QJsonArray list;
            for (const QJsonValue &v : root.value(containerKey()).toArray())
            {
                if (v.toObject().value(QStringLiteral("name")).toString() != current_.name)
                {
                    list.append(v);
                }
            }
            root.insert(containerKey(), list);
        }
        if (!writeFile(current_.file, root))
        {
            return;
        }
    }
    else if (!QFile::moveToTrash(current_.file))
    {
        QMessageBox::warning(this, tr("Delete"),
                             tr("The file couldn't be moved to the trash:\n%1")
                                 .arg(QDir::toNativeSeparators(current_.file)));
        return;
    }
    setDirty(false);
    emit saved();
    refreshList();
    if (entries_.isEmpty())
    {
        newEntry();
    }
    else
    {
        list_->setCurrentRow(0);
    }
}

bool HomebrewEditor::save()
{
    // A file that couldn't be read: its text as typed.
    if (pages_->currentIndex() == 1)
    {
        QSaveFile file(current_.file);
        if (!file.open(QIODevice::WriteOnly) || file.write(text_->toPlainText().toUtf8()) < 0 || !file.commit())
        {
            QMessageBox::warning(this, tr("Save"), tr("The file couldn't be saved:\n%1")
                                                       .arg(QDir::toNativeSeparators(current_.file)));
            return false;
        }
        setDirty(false);
        emit saved();
        refreshList({current_.file, QString()});
        return true;
    }

    QStringList blocking;
    for (const FieldProblem &p : checkForm())
    {
        if (p.blocking)
        {
            blocking << p.message;
        }
    }
    QJsonObject other;
    if (const QString typed = advanced_->toPlainText().trimmed(); !typed.isEmpty())
    {
        monster::HomebrewProblem problem;
        if (const std::optional<QJsonObject> parsed = monster::parseHomebrewText(QString(), typed, &problem))
        {
            other = *parsed;
        }
        else
        {
            blocking << tr("The Advanced box isn't valid JSON (line %1): %2")
                            .arg(problem.line)
                            .arg(problem.message.section(QStringLiteral(": "), 1));
        }
    }
    if (!blocking.isEmpty())
    {
        QMessageBox::warning(this, tr("Save"),
                             tr("It can't be saved yet:\n\n• %1").arg(blocking.join(QStringLiteral("\n• "))));
        return false;
    }
    QString name;
    const QJsonObject entry = mergeAdvanced(readForm(&name), other);
    if (current_.file.isEmpty() && folder_->count() == 0)
    {
        QMessageBox::warning(this, tr("Save"),
                             tr("Every homebrew folder is switched off. Switch one on in Homebrew > "
                                "Homebrew Folders... first."));
        return false;
    }
    const QString path = current_.file.isEmpty() ? newFilePath(name) : current_.file;
    // The file is the entry, or holds it with others.
    QJsonObject root = containerKey().isEmpty() ? entry : readFile(path);
    root.insert(QStringLiteral("format"), 1);
    if (containerKey().isEmpty())
    {
        // Nothing more to put in.
    }
    else if (containerIsMap())
    {
        QJsonObject map = root.value(containerKey()).toObject();
        if (!loadedName_.isEmpty() && loadedName_ != name)
        {
            map.remove(loadedName_); // renamed
        }
        map.insert(name, entry);
        root.insert(containerKey(), map);
    }
    else
    {
        QJsonArray list = root.value(containerKey()).toArray();
        bool replaced = false;
        for (qsizetype i = 0; i < list.size(); ++i)
        {
            if (!loadedName_.isEmpty() && list[i].toObject().value(QStringLiteral("name")).toString() == loadedName_)
            {
                list[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            list.append(entry);
        }
        root.insert(containerKey(), list);
    }
    if (!writeFile(path, root))
    {
        return false;
    }
    current_ = {path, name};
    loadedName_ = name;
    setDirty(false);
    emit saved();
    refreshList(current_);
    return true;
}

bool HomebrewEditor::maybeSave()
{
    if (!dirty_)
    {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, windowTitle(), tr("Save the changes to this %1 first?").arg(noun()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
    {
        return save();
    }
    if (answer == QMessageBox::Discard)
    {
        setDirty(false);
        return true;
    }
    return false;
}

void HomebrewEditor::formEdited()
{
    if (filling_)
    {
        return;
    }
    setDirty(true);
    updateProblems();
    entryChanged();
}

void HomebrewEditor::markField(QWidget *field, const QString &message)
{
    if (!field)
    {
        return;
    }
    field->setStyleSheet(message.isEmpty() ? QString()
                                           : QStringLiteral("%1 { border: 2px solid #c62828; }")
                                                 .arg(QLatin1String(field->metaObject()->className())));
    field->setToolTip(message);
}

void HomebrewEditor::updateProblems()
{
    if (pages_->currentIndex() == 1)
    {
        return;
    }
    // Clear the marks, then mark what's wrong now.
    for (const QPointer<QWidget> &field : marked_)
    {
        markField(field, QString());
    }
    marked_.clear();
    marked_ << advanced_;
    markField(advanced_, QString());
    QStringList blocking;
    QStringList warnings;
    for (const FieldProblem &p : checkForm())
    {
        markField(p.field, p.message);
        marked_ << p.field;
        (p.blocking ? blocking : warnings) << p.message;
    }
    if (const QString typed = advanced_->toPlainText().trimmed(); !typed.isEmpty())
    {
        monster::HomebrewProblem problem;
        if (const std::optional<QJsonObject> parsed = monster::parseHomebrewText(QString(), typed, &problem))
        {
            const QStringList unknown = advancedProblems(*parsed);
            if (!unknown.isEmpty())
            {
                markField(advanced_, unknown.join(QLatin1Char('\n')));
                warnings << unknown;
            }
        }
        else
        {
            const QString message = tr("The Advanced box isn't valid JSON (line %1)").arg(problem.line);
            markField(advanced_, message);
            blocking << message;
        }
    }
    // Another entry with this name.
    QString name;
    readForm(&name);
    for (const Entry &e : entries_)
    {
        if (!name.isEmpty() && e.name.compare(name, Qt::CaseInsensitive) == 0 &&
            !(e.file == current_.file && e.name == loadedName_))
        {
            warnings << tr("Another homebrew %1 is called \"%2\" (in %3); only the first one loads.")
                            .arg(noun(), name, QDir::toNativeSeparators(e.file));
            break;
        }
    }
    QStringList parts;
    auto bullets = [](const QStringList &lines) {
        QStringList escaped;
        for (const QString &line : lines)
        {
            escaped << line.toHtmlEscaped();
        }
        return QStringLiteral("<ul style=\"margin: 0\"><li>%1</li></ul>").arg(escaped.join(QStringLiteral("</li><li>")));
    };
    if (!blocking.isEmpty())
    {
        parts << tr("<b>To fix before saving:</b>") + bullets(blocking);
    }
    if (!warnings.isEmpty())
    {
        parts << tr("<b>Worth checking:</b>") + bullets(warnings);
    }
    problems_->setText(parts.join(QString()));
    problems_->setVisible(!parts.isEmpty());
}

void HomebrewEditor::setDirty(bool dirty)
{
    dirty_ = dirty;
    delete_->setEnabled(!current_.file.isEmpty() || dirty);
    QString title = windowTitle();
    title.remove(QStringLiteral(" *"));
    setWindowTitle(dirty ? title + QStringLiteral(" *") : title);
}

QJsonObject HomebrewEditor::readFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool HomebrewEditor::writeFile(const QString &path, const QJsonObject &root)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit())
    {
        QMessageBox::warning(this, tr("Save"),
                             tr("The file couldn't be saved:\n%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}

QString HomebrewEditor::newFilePath(const QString &name) const
{
    const QDir dir(QDir(folder_->currentData().toString()).filePath(kind()));
    const QString base = slug(name);
    QString path = dir.filePath(base + QStringLiteral(".json"));
    for (int n = 2; QFileInfo::exists(path); ++n)
    {
        path = dir.filePath(QStringLiteral("%1-%2.json").arg(base).arg(n));
    }
    return path;
}

void HomebrewEditor::reject()
{
    if (maybeSave())
    {
        QDialog::reject();
    }
}
