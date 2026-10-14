// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "MainWindow.h"

#include "Appearance.h"
#include "Model/Adjustments.h"
#include "Model/Benchmarks.h"
#include "Model/Homebrew.h"
#include "Model/Roadmaps.h"
#include "Model/ScribeExport.h"
#include "Model/TraitNotes.h"
#include "Rendering/Theme.h"
#include "Widgets/AdjustmentEditor.h"
#include "Widgets/HomebrewEditors.h"
#include "Widgets/HomebrewFoldersDialog.h"
#include "Widgets/MonsterEditor.h"
#include "Widgets/MonsterList.h"
#include "Widgets/MythicDialog.h"
#include "Widgets/OptionsDialog.h"
#include "Widgets/PreviewWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace
{
    // A pause after an edit before the statblock is laid out again, so a
    // burst of typing costs one build.
    constexpr int kBuildDelay = 100;
    // Edits closer together than this undo as one step (a word typed, a
    // number changed).
    constexpr int kEditBurst = 700;
    // Undo steps kept per monster.
    constexpr size_t kMaxUndo = 200;

    // How a monster stores theme `key`: empty for Pathfinder (the first
    // preset), and for keys this build doesn't know, which show as it.
    QString themeKey(const QString &key)
    {
        const auto &presets = scribe::Theme::presets();
        const bool known = std::any_of(presets.begin() + 1, presets.end(),
                                       [&](const scribe::Theme::Preset &p) { return key == QLatin1String(p.key); });
        return known ? key : QString();
    }

    // Where file dialogs start.
    QString dialogFolder()
    {
        return QSettings()
            .value(QStringLiteral("files/lastFolder"),
                   QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .toString();
    }

    // The collection's folder until the user picks one.
    // A menu entry's status tip: its rules, and for homebrew where it's from.
    QString statusTipOf(const monster::Modifier &m)
    {
        if (m.file.isEmpty())
        {
            return m.rules;
        }
        return QCoreApplication::translate("MainWindow", "Homebrew, from %1. %2")
            .arg(QDir::toNativeSeparators(m.file), m.rules);
    }

    QString defaultCollection()
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("PF2e Monsters"));
    }

    // A file name for a monster called `name`: "Temple Guard.json", with the
    // characters file systems refuse replaced. In `dir`, and not taken by
    // another file than `except` ("Temple Guard 2.json" if it is).
    QString fileFor(const QString &dir, const QString &name, const QString &except = QString())
    {
        static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
        QString base = QString(name).replace(unsafe, QStringLiteral("_")).simplified();
        while (base.startsWith(QLatin1Char('.')))
        {
            base.remove(0, 1);
        }
        if (base.isEmpty())
        {
            base = QObject::tr("Unnamed");
        }
        const QDir folder(dir);
        const QString exceptPath = except.isEmpty() ? QString() : QFileInfo(except).absoluteFilePath();
        for (int n = 1;; ++n)
        {
            const QString candidate = folder.absoluteFilePath(
                (n == 1 ? base : QStringLiteral("%1 %2").arg(base).arg(n)) + QStringLiteral(".json"));
            if (!QFileInfo::exists(candidate) || candidate == exceptPath)
            {
                return candidate;
            }
        }
    }

    bool matchesAny(const QKeySequence &pressed, QKeySequence::StandardKey key)
    {
        const QList<QKeySequence> bindings = QKeySequence::keyBindings(key);
        return std::any_of(bindings.begin(), bindings.end(),
                           [&](const QKeySequence &binding) { return binding == pressed; });
    }
} // namespace

MainWindow::MainWindow()
{
    monsterList_ = new MonsterList;
    connect(monsterList_, &MonsterList::monsterActivated, this, &MainWindow::switchTo);

    editor_ = new MonsterEditor;
    editor_->setCornerWidget(monsterList_);
    editor_->cornerSplitter()->restoreState(QSettings().value(kListSplitKey).toByteArray());
    connect(editor_->cornerSplitter(), &QSplitter::splitterMoved, this, [this] {
        QSettings().setValue(kListSplitKey, editor_->cornerSplitter()->saveState());
    });
    connect(editor_, &MonsterEditor::changed, this, &MainWindow::edited);
    connect(editor_, &MonsterEditor::editModifierRequested, this, [this](int index) { editModifier(index); });

    preview_ = new PreviewWidget;
    preview_->setFitWidth(true);
    // Never smaller than this on screen: about 11 px body text at 96 dpi.
    preview_->setMinimumFitZoom(0.85);
    auto *copyAction = new QAction(tr("&Copy"), preview_);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyAction, &QAction::triggered, preview_, &PreviewWidget::copy);
    auto *selectAllAction = new QAction(tr("Select &All"), preview_);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllAction, &QAction::triggered, preview_, &PreviewWidget::selectAll);
    preview_->addActions({copyAction, selectAllAction});
    preview_->setContextActions({copyAction, selectAllAction});

    splitter_ = new QSplitter;
    splitter_->addWidget(editor_);
    splitter_->addWidget(preview_);
    splitter_->setChildrenCollapsible(false);
    setCentralWidget(splitter_);

    // The panels keep their shares of the width through resizes, maximising
    // included. Dragging a handle changes the shares, which are saved.
    const QVariantList saved = QSettings().value(kPanelSharesKey).toList();
    if (saved.size() == static_cast<int>(panelShares_.size()))
    {
        std::array<double, 2> shares{};
        double sum = 0;
        bool ok = true;
        for (size_t i = 0; i < shares.size() && ok; ++i)
        {
            shares[i] = saved.at(static_cast<int>(i)).toDouble(&ok);
            ok = ok && shares[i] > 0 && shares[i] < 1;
            sum += shares[i];
        }
        if (ok && sum > 0.99 && sum < 1.01)
        {
            panelShares_ = shares;
        }
    }
    splitter_->installEventFilter(this);
    connect(splitter_, &QSplitter::splitterMoved, this, [this] { storePanelShares(); });

    // File menu.
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *newAction = fileMenu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newMonster);
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::open);
    QAction *folderAction = fileMenu->addAction(tr("Open &Folder..."), this, &MainWindow::chooseFolder);
    QAction *importAction = fileMenu->addAction(tr("&Import from Website..."), this,
                                                &MainWindow::importFromWebsite);
    importAction->setStatusTip(tr("Copy monsters downloaded from the website (JSON) into the "
                                  "collection; the downloaded files stay as they are"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    fileMenu->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    fileMenu->addSeparator();
    QAction *duplicateAction = fileMenu->addAction(tr("&Duplicate"), this, &MainWindow::duplicateMonster);
    QAction *deleteAction = fileMenu->addAction(tr("De&lete..."), this, &MainWindow::deleteMonster);
    deleteAction->setStatusTip(tr("Move this monster's file to the trash"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export Scribe &Markdown..."), this, &MainWindow::exportScribe);
    fileMenu->addAction(tr("Cop&y Scribe Markdown"), QKeySequence(tr("Ctrl+Shift+C")), this,
                        &MainWindow::copyScribe);
    fileMenu->addAction(tr("Export &PDF (Statblock Size)..."), this, [this] { exportPdf(false); })
        ->setStatusTip(tr("A PDF whose page is the statblock"));
    fileMenu->addAction(tr("Export PDF (A&4 Page)..."), this, [this] { exportPdf(true); })
        ->setStatusTip(tr("A PDF with the statblock at the top of an A4 page"));
    fileMenu->addAction(tr("Export P&NG..."), this, &MainWindow::exportPng)
        ->setStatusTip(tr("The statblock as a 300 dpi image on white"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    monsterList_->setContextActions({newAction, duplicateAction, deleteAction, folderAction, importAction});
    folderAction_ = folderAction;
    importAction_ = importAction;

    // Edit menu: undo and redo across the whole form.
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, &MainWindow::undo);
    redoAction_ = editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, &MainWindow::redo);
    // Text fields have their own Ctrl+Z; the form's undo goes first (see
    // eventFilter()).
    qApp->installEventFilter(this);

    // View menu: the monster list panel, hidden unless asked for (the
    // Collection menu and Find Monster do its job without the room).
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *showList = viewMenu->addAction(tr("Monster &List"));
    showList->setCheckable(true);
    showList->setStatusTip(tr("Show the collection as a list under the form"));
    showList->setChecked(QSettings().value(kShowListKey, false).toBool());
    monsterList_->setVisible(showList->isChecked());
    connect(showList, &QAction::toggled, this, [this](bool on) {
        monsterList_->setVisible(on);
        QSettings().setValue(kShowListKey, on);
    });
    // The open monster's look, kept in its file. New and imported
    // monsters get the one last picked.
    QMenu *themeMenu = viewMenu->addMenu(tr("&Themes"));
    auto *themes = new QActionGroup(this);
    newTheme_ = QSettings().value(kThemeKey).toString();
    for (const scribe::Theme::Preset &preset : scribe::Theme::presets())
    {
        QAction *action = themeMenu->addAction(QCoreApplication::translate("Theme", preset.name));
        action->setCheckable(true);
        action->setData(themeKey(QString::fromLatin1(preset.key)));
        themes->addAction(action);
        connect(action, &QAction::triggered, this, [this, action] { setTheme(action->data().toString()); });
    }
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themes] {
        const QString current = themeKey(monster_.theme);
        for (QAction *action : themes->actions())
        {
            action->setChecked(action->data().toString() == current);
        }
    });
    // The program's own colours (not the stat blocks'): dark, light or
    // the desktop's Kvantum style.
    QMenu *appearanceMenu = viewMenu->addMenu(tr("&Appearance"));
    auto *appearances = new QActionGroup(this);
    const std::pair<appearance::Mode, QString> modes[] = {
        {appearance::Mode::Dark, tr("&Dark")},
        {appearance::Mode::Light, tr("&Light")},
        {appearance::Mode::Kvantum, tr("&Kvantum")},
    };
    for (const auto &[mode, name] : modes)
    {
        QAction *action = appearanceMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(mode == appearance::saved());
        appearances->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            if (appearance::choose(mode))
            {
                QMessageBox::information(this, tr("Appearance"),
                                         tr("The new appearance takes effect the next time "
                                            "the program starts."));
            }
        });
        if (mode == appearance::Mode::Kvantum)
        {
            action->setEnabled(appearance::kvantumInstalled());
            action->setStatusTip(action->isEnabled()
                                     ? tr("The desktop's Kvantum style; takes effect at the next start")
                                     : tr("Kvantum isn't installed"));
        }
    }
    // As in pf2scribe: for every monster, whatever its theme.
    compactAction_ = viewMenu->addAction(tr("&Compact Line Spacing"));
    compactAction_->setCheckable(true);
    compactAction_->setStatusTip(tr("Stat blocks with the line spacing of the printed rulebooks, "
                                    "tighter than the Scribe website's"));
    compactAction_->setChecked(QSettings().value(kCompactKey, false).toBool());
    connect(compactAction_, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(kCompactKey, on);
        startBuild();
    });

    // Collection menu: the monsters of the folder, filled when opened.
    collectionMenu_ = menuBar()->addMenu(tr("&Collection"));
    connect(collectionMenu_, &QMenu::aboutToShow, this, &MainWindow::fillCollectionMenu);
    fillCollectionMenu(); // so Find Monster's shortcut works before it's opened

    // Monster menu.
    QMenu *monsterMenu = menuBar()->addMenu(tr("&Monster"));
    QAction *applyHints = monsterMenu->addAction(tr("Apply &Hints"), this, &MainWindow::applyHints);
    applyHints->setStatusTip(tr("Fill each empty Perception, attribute, AC, save and HP value "
                                "with its benchmark's hint"));

    // Roadmap: Base (GM Core's creature road maps) and Class (its class
    // road maps), each with a submenu for those with variants (Spellcaster,
    // Fighter, ...).
    roadmapMenu_ = monsterMenu->addMenu(tr("&Roadmap"));
    fillRoadmapMenu();

    // Homebrew is all read here, on this thread, before anything uses it.
    monster::domains();
    monster::homebrewTraits();
    // Adjustments and Templates: filled by fillModifierMenus(), again when
    // homebrew is reloaded.
    adjustmentMenu_ = monsterMenu->addMenu(tr("&Adjustments"));
    templateMenu_ = monsterMenu->addMenu(tr("&Templates"));
    fillModifierMenus();

    // Homebrew: the user's own data (plan 9).
    QMenu *homebrewMenu = menuBar()->addMenu(tr("Home&brew"));
    homebrewMenu->addAction(tr("&Adjustments and Templates..."), this, [this] {
        auto *editor = new AdjustmentEditor(this);
        // The statblock shows the open monster with the entry applied while
        // it's edited.
        connect(editor, &AdjustmentEditor::previewChanged, this, &MainWindow::previewHomebrew);
        editHomebrew(editor);
        previewHomebrew(QJsonObject());
    })->setStatusTip(tr("Make and edit homebrew adjustments and templates, previewed on the open monster"));
    homebrewMenu->addAction(tr("&Domains..."), this, [this] { editHomebrew(new DomainEditor(this)); })
        ->setStatusTip(tr("Make and edit homebrew domains (for Divine Warden)"));
    homebrewMenu->addAction(tr("&Road Maps..."), this, [this] { editHomebrew(new RoadmapEditor(this)); })
        ->setStatusTip(tr("Make and edit homebrew road maps (Monster > Roadmap)"));
    homebrewMenu->addAction(tr("&Traits..."), this, [this] { editHomebrew(new TraitEditor(this)); })
        ->setStatusTip(tr("Make and edit homebrew traits: their notes, and the skills that identify them"));
    homebrewMenu->addSeparator();
    homebrewMenu->addAction(tr("&Open Homebrew Folder"), this, &MainWindow::openHomebrewFolder)
        ->setStatusTip(tr("Open the folder your homebrew files go in (made if it doesn't exist yet)"));
    homebrewMenu->addAction(tr("Homebrew &Folders..."), this, &MainWindow::editHomebrewFolders)
        ->setStatusTip(tr("Choose which folders homebrew is read from, switch them on or off, and "
                          "where the problem log goes"));
    homebrewMenu->addSeparator();
    homebrewMenu->addAction(tr("&Reload Homebrew"), this, [this] { reloadHomebrew(); })
        ->setStatusTip(tr("Read every homebrew file again, after you've changed or added some"));

    builders_.setMaxThreadCount(1);
    builders_.setExpiryTimeout(-1);
    buildTimer_.setSingleShot(true);
    buildTimer_.setInterval(kBuildDelay);
    connect(&buildTimer_, &QTimer::timeout, this, &MainWindow::startBuild);
    editBurst_.setSingleShot(true);
    editBurst_.setInterval(kEditBurst);
    connect(&editBurst_, &QTimer::timeout, this, &MainWindow::finishEdit);

    // Help menu.
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::about);

    if (!restoreGeometry(QSettings().value(kGeometryKey).toByteArray()))
    {
        resize(1600, 900);
    }

    // The collection, and the monster that was open last time.
    openFolder(QSettings().value(kCollectionKey, defaultCollection()).toString());
    saved_ = monster_.toJson(); // a new, empty monster: nothing to save
    showMonster();
    const QString last = QSettings().value(kLastMonsterKey).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
    {
        openFile(last);
    }
    // Problems with homebrew files, once the window is up.
    QTimer::singleShot(0, this, &MainWindow::showHomebrewProblems);
}

MainWindow::~MainWindow()
{
    if (cancel_)
    {
        cancel_->store(true);
    }
    builders_.waitForDone();
    // Layouts go before the build thread does (with builders_), as they use
    // its font engines; the preview is only deleted after that.
    preview_->setPageLayout(nullptr);
    layout_.reset();
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    std::optional<monster::Monster> loaded = monster::Monster::load(path, &error);
    if (!loaded)
    {
        QMessageBox::warning(this, tr("Open Monster"),
                             tr("Couldn't open %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), error));
        monsterList_->setCurrent(path_);
        return false;
    }
    monster_ = std::move(*loaded);
    // Places for the abilities its adjustments make (Mythic's), if the file
    // has none yet: not a change to save.
    monster::syncGeneratedSpecials(monster_);
    path_ = QFileInfo(path).absoluteFilePath();
    saved_ = monster_.toJson();
    savedName_ = monster_.name;
    QSettings().setValue(kLastMonsterKey, path_);
    showMonster();
    monsterList_->setCurrent(path_);
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

void MainWindow::openFolder(const QString &folder)
{
    monsterList_->setFolder(folder);
    monsterList_->setCurrent(path_);
    QSettings().setValue(kCollectionKey, monsterList_->folder());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave())
    {
        event->ignore();
        return;
    }
    QSettings().setValue(kGeometryKey, saveGeometry());
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == splitter_ && event->type() == QEvent::Resize)
    {
        applyPanelShares();
    }
    // Ctrl+Z / Ctrl+Shift+Z in a field of the form: the form's undo, which
    // covers what was typed, instead of the field's own.
    if (event->type() == QEvent::ShortcutOverride)
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && editor_->isAncestorOf(widget))
        {
            const QKeySequence pressed(static_cast<QKeyEvent *>(event)->keyCombination());
            if (matchesAny(pressed, QKeySequence::Undo) || matchesAny(pressed, QKeySequence::Redo))
            {
                event->ignore(); // not for the field: the shortcut runs
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::newMonster()
{
    if (!maybeSave())
    {
        return;
    }
    monster_ = monster::Monster();
    monster_.theme = themeKey(newTheme_);
    path_.clear();
    saved_ = monster_.toJson();
    savedName_.clear();
    histories_.erase(QString());
    showMonster();
    monsterList_->setCurrent(QString());
}

void MainWindow::open()
{
    if (!maybeSave())
    {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Monster"), dialogFolder(), tr("Monsters (*.json);;All files (*)"));
    if (!path.isEmpty())
    {
        QSettings().setValue(kLastFolderKey, QFileInfo(path).absolutePath());
        openFile(path);
    }
}

void MainWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder"),
                                                             monsterList_->folder());
    if (!folder.isEmpty())
    {
        openFolder(folder);
    }
}

void MainWindow::switchTo(const QString &path)
{
    if (QFileInfo(path).absoluteFilePath() == path_)
    {
        return;
    }
    if (!maybeSave())
    {
        monsterList_->setCurrent(path_); // stay on this one
        return;
    }
    openFile(path);
}

// A monster that has a file is saved to it, renamed along with the monster
// (see savedName_). A new one goes in the collection's folder, named after
// it.
bool MainWindow::save()
{
    finishEdit();
    QString target = path_;
    if (path_.isEmpty())
    {
        const QString folder = monsterList_->folder();
        if (!QDir().mkpath(folder))
        {
            QMessageBox::warning(this, tr("Save Monster"),
                                 tr("Couldn't create the folder %1.").arg(QDir::toNativeSeparators(folder)));
            return false;
        }
        target = fileFor(folder, monster_.name);
    }
    else if (monster_.name.trimmed() != savedName_.trimmed())
    {
        const QString renamed = fileFor(QFileInfo(path_).absolutePath(), monster_.name, path_);
        if (renamed != path_ && QFile::rename(path_, renamed))
        {
            histories_[renamed] = std::move(histories_[path_]);
            histories_.erase(path_);
            target = renamed;
            path_ = renamed;
        }
    }
    return saveTo(target);
}

bool MainWindow::saveAs()
{
    finishEdit();
    const QString start = path_.isEmpty() ? fileFor(dialogFolder(), monster_.name) : path_;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Monster"), start, tr("Monsters (*.json)"));
    if (path.isEmpty())
    {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty())
    {
        path += QStringLiteral(".json");
    }
    QSettings().setValue(kLastFolderKey, QFileInfo(path).absolutePath());
    return saveTo(path);
}

bool MainWindow::saveTo(const QString &path)
{
    QString error;
    if (!monster_.save(path, &error))
    {
        QMessageBox::warning(this, tr("Save Monster"),
                             tr("Couldn't save %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute != path_)
    {
        // The undo history goes with the monster to its new file.
        histories_[absolute] = std::move(histories_[path_]);
        histories_.erase(path_);
        path_ = absolute;
    }
    saved_ = monster_.toJson();
    savedName_ = monster_.name;
    QSettings().setValue(kLastMonsterKey, path_);
    updateModified();
    monsterList_->rescan();
    monsterList_->setCurrent(path_);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path_)), 5000);
    return true;
}

// A copy of the monster, as saved, in a new file next to it (or in the
// collection), then opened.
void MainWindow::duplicateMonster()
{
    if (!maybeSave())
    {
        return;
    }
    monster::Monster copy = monster_;
    copy.name = monster_.name.trimmed().isEmpty() ? tr("Unnamed")
                                                  : tr("%1 (copy)").arg(monster_.name.trimmed());
    const QString folder = path_.isEmpty() ? monsterList_->folder() : QFileInfo(path_).absolutePath();
    QDir().mkpath(folder);
    const QString path = fileFor(folder, copy.name);
    QString error;
    if (!copy.save(path, &error))
    {
        QMessageBox::warning(this, tr("Duplicate Monster"),
                             tr("Couldn't save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    monsterList_->rescan();
    openFile(path);
}

void MainWindow::deleteMonster()
{
    const QString name = monster_.name.trimmed().isEmpty() ? tr("this monster") : monster_.name.trimmed();
    if (path_.isEmpty())
    {
        // Never saved: nothing on disk, so only the unsaved monster goes.
        if (modified_ && QMessageBox::question(this, tr("Delete Monster"),
                                               tr("Discard %1? It was never saved.").arg(name)) !=
                             QMessageBox::Yes)
        {
            return;
        }
        modified_ = false;
        newMonster();
        return;
    }
    if (QMessageBox::question(this, tr("Delete Monster"),
                              tr("Move %1 (%2) to the trash?")
                                  .arg(name, QFileInfo(path_).fileName())) != QMessageBox::Yes)
    {
        return;
    }
    if (!QFile::moveToTrash(path_))
    {
        QMessageBox::warning(this, tr("Delete Monster"),
                             tr("Couldn't move %1 to the trash.").arg(QDir::toNativeSeparators(path_)));
        return;
    }
    statusBar()->showMessage(tr("Moved %1 to the trash").arg(QFileInfo(path_).fileName()), 5000);
    histories_.erase(path_);
    QSettings().remove(kLastMonsterKey);
    monster_ = monster::Monster();
    monster_.theme = themeKey(newTheme_);
    path_.clear();
    saved_ = monster_.toJson();
    savedName_.clear();
    histories_.erase(QString());
    showMonster();
    monsterList_->rescan();
    monsterList_->setCurrent(QString());
}

bool MainWindow::maybeSave()
{
    finishEdit();
    if (!modified_)
    {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("%1 has unsaved changes. Save them?")
            .arg(monster_.name.trimmed().isEmpty() ? tr("This monster") : monster_.name.trimmed()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
    {
        return save();
    }
    if (answer == QMessageBox::Discard)
    {
        // The undo steps lead to the discarded changes: they go too.
        histories_.erase(path_);
        modified_ = false;
        return true;
    }
    return false;
}

void MainWindow::showMonster()
{
    editBurst_.stop();
    beforeEdit_ = monster_;
    editor_->setMonster(&monster_);
    updateModified();
    updateUndoActions();
    startBuild();
}

void MainWindow::updateModified()
{
    // Compared with the file, so undoing back to it counts as unchanged.
    modified_ = monster_.toJson() != saved_;
    updateTitle();
}

void MainWindow::updateTitle()
{
    setWindowFilePath(path_);
    const QString name = path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).fileName();
    setWindowTitle(name + QStringLiteral("[*]"));
    setWindowModified(modified_);
}

// The first edit of a burst records the state before it as an undo step.
void MainWindow::edited()
{
    if (!editBurst_.isActive())
    {
        History &h = history();
        h.undo.push_back(beforeEdit_);
        if (h.undo.size() > kMaxUndo)
        {
            h.undo.erase(h.undo.begin());
        }
        h.redo.clear();
    }
    editBurst_.start();
    updateModified();
    updateUndoActions();
    buildTimer_.start();
}

void MainWindow::finishEdit()
{
    editBurst_.stop();
    beforeEdit_ = monster_;
}

void MainWindow::undo()
{
    finishEdit();
    History &h = history();
    if (h.undo.empty())
    {
        return;
    }
    h.redo.push_back(monster_);
    monster_ = std::move(h.undo.back());
    h.undo.pop_back();
    beforeEdit_ = monster_;
    editor_->setMonster(&monster_);
    updateModified();
    updateUndoActions();
    startBuild();
}

void MainWindow::redo()
{
    finishEdit();
    History &h = history();
    if (h.redo.empty())
    {
        return;
    }
    h.undo.push_back(monster_);
    monster_ = std::move(h.redo.back());
    h.redo.pop_back();
    beforeEdit_ = monster_;
    editor_->setMonster(&monster_);
    updateModified();
    updateUndoActions();
    startBuild();
}

void MainWindow::updateUndoActions()
{
    const History &h = history();
    undoAction_->setEnabled(!h.undo.empty());
    redoAction_->setEnabled(!h.redo.empty());
}

void MainWindow::commitChange(monster::Monster before)
{
    History &h = history();
    h.undo.push_back(std::move(before));
    if (h.undo.size() > kMaxUndo)
    {
        h.undo.erase(h.undo.begin());
    }
    h.redo.clear();
    beforeEdit_ = monster_;
    editor_->setMonster(&monster_); // show the new values
    updateModified();
    updateUndoActions();
    startBuild();
}

void MainWindow::fillCollectionMenu()
{
    collectionMenu_->clear(); // deletes its own actions, not File's shared ones
    collectionMenu_->addAction(tr("&Find Monster..."), QKeySequence(tr("Ctrl+P")), this,
                               &MainWindow::findMonster)
        ->setStatusTip(tr("Search the collection by name, trait or tag"));
    collectionMenu_->addSeparator();

    // A submenu per subfolder, by name, then the folder's own monsters
    // (folders first, as in a file manager). Two levels deep: a submenu per
    // top folder, inside it one per folder below that, deeper folders named
    // by their path ("Darkness/Owb").
    using Group = std::vector<const MonsterList::Entry *>;
    QMap<QString, QMap<QString, Group>> groups; // top folder -> rest of path -> monsters
    const QDir root(monsterList_->folder());
    for (const MonsterList::Entry &entry : monsterList_->entries())
    {
        const QString path = root.relativeFilePath(QFileInfo(entry.path).absolutePath());
        const qsizetype slash = path.indexOf(QLatin1Char('/'));
        groups[path.left(slash)][slash < 0 ? QString() : path.mid(slash + 1)].push_back(&entry);
    }
    const auto sortedKeys = [](QStringList keys) {
        keys.sort(Qt::CaseInsensitive);
        return keys;
    };
    const auto escaped = [](QString text) { return text.replace(QLatin1Char('&'), QStringLiteral("&&")); };
    const auto addMonsters = [&](QMenu *into, Group &group) {
        std::sort(group.begin(), group.end(), [](const auto *a, const auto *b) {
            return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
        });
        for (const MonsterList::Entry *entry : group)
        {
            const QString label = entry->level.isEmpty()
                                      ? entry->name
                                      : tr("%1 (%2)").arg(entry->name, entry->level);
            QAction *action = into->addAction(escaped(label), this,
                                              [this, path = entry->path] { switchTo(path); });
            action->setCheckable(true);
            action->setChecked(QFileInfo(entry->path).absoluteFilePath() == path_);
            if (!entry->tags.isEmpty())
            {
                action->setStatusTip(tr("Tags: %1").arg(entry->tags.join(QStringLiteral(", "))));
            }
        }
    };
    for (const QString &top : sortedKeys(groups.keys()))
    {
        if (top == QLatin1String("."))
        {
            continue; // the folder's own monsters go last
        }
        QMap<QString, Group> &subs = groups[top];
        QMenu *topMenu = collectionMenu_->addMenu(escaped(top));
        for (const QString &sub : sortedKeys(subs.keys()))
        {
            if (!sub.isEmpty())
            {
                addMonsters(topMenu->addMenu(escaped(sub)), subs[sub]);
            }
        }
        if (subs.contains(QString()))
        {
            addMonsters(topMenu, subs[QString()]);
        }
    }
    if (groups.contains(QStringLiteral(".")))
    {
        addMonsters(collectionMenu_, groups[QStringLiteral(".")][QString()]);
    }
    if (groups.isEmpty())
    {
        collectionMenu_->addAction(tr("(No monsters in this folder)"))->setEnabled(false);
    }
    collectionMenu_->addSeparator();
    collectionMenu_->addAction(folderAction_);
    collectionMenu_->addAction(importAction_);
}

void MainWindow::findMonster()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Find Monster"));
    auto *layout = new QVBoxLayout(&dialog);
    // The list itself: its filter (name, trait, tag), tags and groups.
    auto *list = new MonsterList;
    list->setFolder(monsterList_->folder());
    list->setCurrent(path_);
    layout->addWidget(list);
    QString picked;
    connect(list, &MonsterList::monsterActivated, &dialog, [&](const QString &path) {
        picked = path;
        dialog.accept();
    });
    dialog.resize(fontMetrics().averageCharWidth() * 50, fontMetrics().height() * 28);
    if (auto *filter = list->findChild<QLineEdit *>())
    {
        filter->setFocus();
    }
    if (dialog.exec() == QDialog::Accepted && !picked.isEmpty())
    {
        switchTo(picked);
    }
}

void MainWindow::about()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser;
    text->setOpenExternalLinks(true);
    QFile credits(QStringLiteral(":/data/credits.html")); // built in: always there
    const QString creditsHtml = credits.open(QIODevice::ReadOnly) ? QString::fromUtf8(credits.readAll()) : QString();
    text->setHtml(
        tr("<h2>%1</h2><p>Version %2</p>"
           "<p>A creature builder for the Pathfinder Second Edition roleplaying game, after "
           "monster.pf2.tools. An unofficial fan project.</p>"
           "<p>Copyright &copy; 2026 Vorthas. This program is free software: you can "
           "redistribute it and/or modify it under the terms of the "
           "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU General Public License</a> "
           "as published by the Free Software Foundation, either version 3 of the License, or "
           "(at your option) any later version. It comes with no warranty.</p>"
           "<p>Built with Qt %3 (LGPL 3).</p>")
            .arg(QGuiApplication::applicationDisplayName(), QCoreApplication::applicationVersion(),
                 QString::fromLatin1(qVersion())) +
        creditsHtml);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(fontMetrics().averageCharWidth() * 80, fontMetrics().height() * 32);
    dialog.exec();
}

void MainWindow::applyHints()
{
    finishEdit();
    monster::Monster before = monster_;
    const int filled = monster::applyHints(monster_);
    if (filled == 0)
    {
        statusBar()->showMessage(tr("No empty values to fill"), 5000);
        return;
    }
    commitChange(std::move(before));
    statusBar()->showMessage(tr("Filled %n value(s) from their hints", nullptr, filled), 5000);
}

void MainWindow::setTheme(const QString &key)
{
    newTheme_ = key;
    QSettings().setValue(kThemeKey, key);
    if (key == themeKey(monster_.theme))
    {
        return;
    }
    finishEdit();
    monster::Monster before = monster_;
    monster_.theme = key;
    commitChange(std::move(before));
}

scribe::Theme MainWindow::theme() const
{
    scribe::Theme theme = scribe::Theme::preset(monster_.theme);
    theme.compactSpacing = compactAction_ && compactAction_->isChecked();
    return theme;
}

void MainWindow::applyRoadmap(const QString &id)
{
    const monster::Roadmap *roadmap = monster::findRoadmap(id);
    if (!roadmap)
    {
        return;
    }
    finishEdit();
    // Unlike the website, ask before replacing what's there.
    if (monster::roadmapReplacesValues(monster_, *roadmap) &&
        QMessageBox::question(
            this, tr("Apply Roadmap"),
            tr("Apply the %1 roadmap?\n\nIt sets the benchmarks of Perception, the attributes, "
               "AC, saves, HP and spells (moderate where it names none) and clears their values "
               "so they follow the hints; the skills it names get its benchmarks and their "
               "hints. Undo puts the old values back.")
                .arg(roadmap->title()),
            QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Apply) != QMessageBox::Apply)
    {
        return;
    }
    monster::Monster before = monster_;
    monster::applyRoadmap(monster_, *roadmap);
    commitChange(std::move(before));
    statusBar()->showMessage(tr("Applied the %1 roadmap").arg(roadmap->title()), 5000);
}

void MainWindow::fillRoadmapMenu()
{
    QMenu *roadmapMenu = roadmapMenu_;
    roadmapMenu->clear();
    {
        QMenu *baseMenu = roadmapMenu->addMenu(tr("&Base"));
        QMenu *classMenu = roadmapMenu->addMenu(tr("&Class"));
        QMenu *variants = nullptr;
        QString variantsOf;
        for (const monster::Roadmap &r : monster::roadmaps())
        {
            QMenu *list = r.list == QLatin1String("class") ? classMenu : baseMenu;
            QMenu *into = list;
            QString label = r.name;
            if (!r.variant.isEmpty())
            {
                if (!variants || variantsOf != r.list + r.name)
                {
                    variants = list->addMenu(r.name);
                    variantsOf = r.list + r.name;
                }
                into = variants;
                label = r.variant;
            }
            QAction *action = into->addAction(label, this, [this, id = r.id] { applyRoadmap(id); });
            QString rules = r.rules;
            if (!rules.isEmpty())
            {
                rules[0] = rules[0].toUpper();
            }
            if (!r.file.isEmpty())
            {
                rules = tr("Homebrew, from %1. %2").arg(QDir::toNativeSeparators(r.file), rules);
            }
            action->setStatusTip(rules);
        }
        roadmapMenu->addSeparator();
        QAction *clear = roadmapMenu->addAction(tr("&Clear Roadmap Notes"), this, &MainWindow::clearRoadmap);
        clear->setStatusTip(tr("Remove the roadmap's notes from the form; the statistics stay as "
                               "they are"));
    }
}

void MainWindow::fillModifierMenus()
{
    // Adjustments: each adds one to the monster (they stack); the form lists
    // those applied. Those of a group (Elemental, ...) are in its submenu,
    // below the others. A group can hold groups ("Ancestry/Homebrew"): those
    // come after its entries, the same way. Groups are in the order their
    // first entries come.
    QMenu *adjustmentMenu = adjustmentMenu_;
    adjustmentMenu->clear();
    {
        QHash<QString, QMenu *> groups; // by path
        QList<QPair<QString, QMenu *>> ordered; // a submenu goes into the one of its path's parent
        std::function<QMenu *(const QString &)> menuFor = [&](const QString &path) -> QMenu * {
            if (path.isEmpty())
            {
                return adjustmentMenu;
            }
            QMenu *menu = groups.value(path);
            if (!menu)
            {
                const QString parent = path.section(QLatin1Char('/'), 0, -2);
                menuFor(parent); // made first, so parents come before their children
                menu = new QMenu(path.section(QLatin1Char('/'), -1), adjustmentMenu);
                groups.insert(path, menu);
                ordered.push_back({parent, menu});
            }
            return menu;
        };
        for (const monster::Modifier &m : monster::modifiers())
        {
            if (m.kind != QLatin1String("adjustment"))
            {
                continue;
            }
            if (m.dialog == QLatin1String("mythic"))
            {
                adjustmentMenu->addAction(m.name + QStringLiteral("..."), this, [this] { editMythic(); })
                    ->setStatusTip(statusTipOf(m));
                continue;
            }
            const QString label = m.dialog.isEmpty() ? m.name : m.name + QStringLiteral("...");
            menuFor(m.group)->addAction(label, this, [this, id = m.id] { addModifier(id); })
                ->setStatusTip(statusTipOf(m));
        }
        // Each menu's submenus after its entries, below a separator.
        QSet<QMenu *> separated;
        for (const auto &[parent, menu] : ordered)
        {
            QMenu *into = parent.isEmpty() ? adjustmentMenu : groups.value(parent);
            if (!separated.contains(into))
            {
                separated.insert(into);
                if (!into->isEmpty())
                {
                    into->addSeparator();
                }
            }
            into->addMenu(menu);
        }
    }

    // Templates (creature families' "Creating a ..."): alphabetical, the
    // undead in their own submenu below the rest. A group can hold groups
    // ("Undead/Vampire"): those sit among its entries, alphabetically, and
    // list first the one named like them (Vampire, then Nosferatu, ...).
    // They stack with the adjustments, in one list.
    QMenu *templateMenu = templateMenu_;
    templateMenu->clear();
    {
        std::vector<const monster::Modifier *> templates;
        for (const monster::Modifier &m : monster::modifiers())
        {
            if (m.kind == QLatin1String("template"))
            {
                templates.push_back(&m);
            }
        }
        auto addEntry = [this](QMenu *menu, const monster::Modifier *m) {
            const QString label = m->dialog.isEmpty() ? m->name : m->name + QStringLiteral("...");
            menu->addAction(label, this, [this, id = m->id] { addModifier(id); })->setStatusTip(statusTipOf(*m));
        };
        // The menu for the group `path` ("" the Templates menu itself).
        std::function<void(QMenu *, const QString &)> fill = [&](QMenu *menu, const QString &path) {
            const QString name = path.section(QLatin1Char('/'), -1);
            // Its entries and its groups' names, each sorted by name.
            struct Item
            {
                QString label;
                const monster::Modifier *entry = nullptr; // else a group
            };
            QList<Item> items;
            QStringList subgroups;
            const QString below = path.isEmpty() ? QString() : path + QLatin1Char('/');
            for (const monster::Modifier *m : templates)
            {
                if (m->group == path)
                {
                    items.push_back({m->name, m});
                }
                else if (m->group.startsWith(below) && (!path.isEmpty() || !m->group.isEmpty()))
                {
                    const QString child = m->group.mid(below.size()).section(QLatin1Char('/'), 0, 0);
                    if (!subgroups.contains(child))
                    {
                        subgroups << child;
                    }
                }
            }
            auto byLabel = [&](const Item &a, const Item &b) {
                // The entry named like its group first.
                const bool aFirst = a.entry && a.label.compare(name, Qt::CaseInsensitive) == 0;
                const bool bFirst = b.entry && b.label.compare(name, Qt::CaseInsensitive) == 0;
                if (aFirst != bFirst)
                {
                    return aFirst;
                }
                return QString::compare(a.label, b.label, Qt::CaseInsensitive) < 0;
            };
            subgroups.sort(Qt::CaseInsensitive);
            if (path.isEmpty())
            {
                // The top: its entries, then its groups below a separator.
                std::sort(items.begin(), items.end(), byLabel);
                for (const Item &item : items)
                {
                    addEntry(menu, item.entry);
                }
                if (!subgroups.isEmpty() && !items.isEmpty())
                {
                    menu->addSeparator();
                }
                for (const QString &group : subgroups)
                {
                    fill(menu->addMenu(group), group);
                }
                return;
            }
            for (const QString &group : subgroups)
            {
                items.push_back({group, nullptr});
            }
            std::sort(items.begin(), items.end(), byLabel);
            for (const Item &item : items)
            {
                if (item.entry)
                {
                    addEntry(menu, item.entry);
                }
                else
                {
                    fill(menu->addMenu(item.label), below + item.label);
                }
            }
        };
        fill(templateMenu, QString());
    }
}

void MainWindow::addModifier(const QString &id)
{
    const monster::Modifier *modifier = monster::findModifier(id);
    if (!modifier)
    {
        return;
    }
    finishEdit();
    if (modifier->dialog == QLatin1String("options"))
    {
        if (!editOptions(-1, id))
        {
            return;
        }
    }
    else
    {
        monster::Monster before = monster_;
        monster::addAppliedModifier(monster_, id);
        commitChange(std::move(before));
    }
    // Kept even when it changes nothing at this level (Weak at level 0):
    // the level may change later.
    statusBar()->showMessage(monster::modifiersInEffect(monster_).back()
                                 ? tr("Applied %1").arg(modifier->name)
                                 : tr("Added %1, but it has no effect at this level").arg(modifier->name),
                             5000);
}

void MainWindow::editModifier(int index)
{
    const QStringList ids = monster::appliedModifiers(monster_);
    const monster::Modifier *modifier = monster::findModifier(ids.value(index));
    if (!modifier)
    {
        return;
    }
    if (modifier->dialog == QLatin1String("mythic"))
    {
        editMythic(index);
    }
    else if (modifier->dialog == QLatin1String("options"))
    {
        editOptions(index);
    }
}

bool MainWindow::editOptions(int index, const QString &id)
{
    finishEdit();
    const QStringList ids = monster::appliedModifiers(monster_);
    const monster::Modifier *modifier = monster::findModifier(index >= 0 ? ids.value(index) : id);
    if (!modifier)
    {
        return false;
    }
    // The level it comes to: after those before it.
    int level = 0;
    monster::adjustedUpTo(monster_, index >= 0 ? index : static_cast<int>(ids.size()), &level);
    OptionsDialog dialog(*modifier, level,
                         index >= 0 ? monster::modifierOptions(monster_, index) : QJsonObject(), this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }
    monster::Monster before = monster_;
    if (index >= 0)
    {
        monster::setModifierOptions(monster_, index, dialog.options());
    }
    else
    {
        monster::addAppliedModifier(monster_, modifier->id, dialog.options());
    }
    monster::syncGeneratedSpecials(monster_); // its abilities' places in the form
    if (monster_.toJson() != before.toJson())
    {
        commitChange(std::move(before));
    }
    return true;
}

void MainWindow::editMythic(int index)
{
    finishEdit();
    const QStringList ids = monster::appliedModifiers(monster_);
    if (index < 0)
    {
        index = static_cast<int>(ids.indexOf(QStringLiteral("mythic"))); // one per creature
    }
    // The dialog sees the creature as Mythic will: after those before it.
    int level = 0;
    const monster::Monster soFar =
        monster::adjustedUpTo(monster_, index >= 0 ? index : static_cast<int>(ids.size()), &level);
    MythicDialog dialog(soFar, level,
                        index >= 0 ? monster::modifierOptions(monster_, index) : QJsonObject(),
                        monster_.name, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    monster::Monster before = monster_;
    if (index >= 0)
    {
        monster::setModifierOptions(monster_, index, dialog.options());
    }
    else
    {
        monster::addAppliedModifier(monster_, QStringLiteral("mythic"), dialog.options());
    }
    monster::syncGeneratedSpecials(monster_); // its abilities' places in the form
    if (monster_.toJson() == before.toJson())
    {
        return; // nothing changed: no undo step
    }
    commitChange(std::move(before));
}

void MainWindow::clearRoadmap()
{
    finishEdit();
    if (monster_.roadmap.isEmpty())
    {
        statusBar()->showMessage(tr("No roadmap notes to clear"), 5000);
        return;
    }
    monster::Monster before = monster_;
    monster_.roadmap.clear();
    commitChange(std::move(before));
}

void MainWindow::exportScribe()
{
    const QString base = path_.isEmpty()
                             ? QDir(dialogFolder()).filePath(
                                   monster_.name.trimmed().isEmpty() ? tr("monster")
                                                                     : monster_.name.trimmed())
                             : QFileInfo(path_).absoluteDir().filePath(QFileInfo(path_).completeBaseName());
    QString path = QFileDialog::getSaveFileName(this, tr("Export Scribe Markdown"),
                                                base + QStringLiteral(".md"),
                                                tr("Scribe markdown (*.md);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(monster::toScribe(monster_).toUtf8()) < 0 || !file.commit())
    {
        QMessageBox::warning(this, tr("Export Scribe Markdown"),
                             tr("Couldn't write %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 5000);
}

namespace
{
    // Where export dialogs start, and the file name they suggest.
    QString exportPath(const QString &name, const QString &suffix)
    {
        const QString folder = QSettings()
                                   .value(QStringLiteral("files/lastExportFolder"), dialogFolder())
                                   .toString();
        QString base = name.trimmed().isEmpty() ? QObject::tr("monster") : name.trimmed();
        static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
        base.replace(unsafe, QStringLiteral("_"));
        return QDir(folder).filePath(base + suffix);
    }

    void rememberExportFolder(const QString &path)
    {
        QSettings().setValue(QStringLiteral("files/lastExportFolder"), QFileInfo(path).absolutePath());
    }
} // namespace

// Laid out again here, on this thread: the PDF writer reads font data when
// it finishes, outside any paintPage(), which the background layouts' fonts
// don't allow (see textEngineMutex()). It's also always the latest monster.
void MainWindow::exportPdf(bool a4)
{
    const QString path = QFileDialog::getSaveFileName(
        this, a4 ? tr("Export PDF (A4 Page)") : tr("Export PDF (Statblock Size)"),
        exportPath(monster_.name, QStringLiteral(".pdf")), tr("PDF (*.pdf)"));
    if (path.isEmpty())
    {
        return;
    }
    rememberExportFolder(path);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString text = monster::toScribe(monster_);
    const scribe::Theme theme = this->theme();
    const std::shared_ptr<scribe::PageLayout> layout =
        a4 ? monster::layoutStatblockOnA4(text, theme) : monster::layoutStatblock(text, theme);
    const bool ok = layout && monster::writePdf(*layout, path, monster_.name.trimmed());
    QApplication::restoreOverrideCursor();
    if (!ok)
    {
        QMessageBox::warning(this, tr("Export PDF"),
                             tr("Couldn't write %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::exportPng()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"), exportPath(monster_.name, QStringLiteral(".png")), tr("PNG (*.png)"));
    if (path.isEmpty())
    {
        return;
    }
    rememberExportFolder(path);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::shared_ptr<scribe::PageLayout> layout =
        monster::layoutStatblock(monster::toScribe(monster_), theme());
    const bool ok = layout && !layout->pages().empty() &&
                    monster::renderPage(*layout, 0, kPngDpi).save(path, "PNG");
    QApplication::restoreOverrideCursor();
    if (!ok)
    {
        QMessageBox::warning(this, tr("Export PNG"),
                             tr("Couldn't write %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 5000);
}

// Copies monsters downloaded from the website into the collection, each as
// "Name.json" (or "Name 2.json" if that's taken). The downloads are only
// read. The last one imported is opened.
void MainWindow::importFromWebsite()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Import from Website"), dialogFolder(), tr("Monsters (*.json);;All files (*)"));
    if (paths.isEmpty())
    {
        return;
    }
    QSettings().setValue(kLastFolderKey, QFileInfo(paths.first()).absolutePath());
    const QStringList imported = importFiles(paths);
    if (!imported.isEmpty() && maybeSave())
    {
        openFile(imported.last());
    }
}

QStringList MainWindow::importFiles(const QStringList &paths)
{
    const QString folder = monsterList_->folder();
    QStringList imported;
    QStringList skipped;
    if (!QDir().mkpath(folder))
    {
        QMessageBox::warning(this, tr("Import from Website"),
                             tr("Couldn't create the folder %1.").arg(QDir::toNativeSeparators(folder)));
        return imported;
    }
    for (const QString &path : paths)
    {
        std::optional<monster::Monster> loaded;
        if (MonsterList::readEntry(path, nullptr))
        {
            loaded = monster::Monster::load(path);
        }
        if (loaded && loaded->theme.isEmpty())
        {
            loaded->theme = themeKey(newTheme_);
        }
        const QString target = loaded ? fileFor(folder, loaded->name) : QString();
        if (!loaded || !loaded->save(target))
        {
            skipped << QFileInfo(path).fileName();
            continue;
        }
        imported << target;
    }
    monsterList_->rescan();
    monsterList_->setCurrent(path_);
    if (!skipped.isEmpty())
    {
        QMessageBox::warning(this, tr("Import from Website"),
                             tr("These files aren't monsters from the website and weren't "
                                "imported:\n%1").arg(skipped.join(QLatin1Char('\n'))));
    }
    statusBar()->showMessage(tr("Imported %n monster(s)", nullptr, static_cast<int>(imported.size())), 5000);
    return imported;
}

void MainWindow::copyScribe()
{
    QApplication::clipboard()->setText(monster::toScribe(monster_));
    statusBar()->showMessage(tr("Scribe markdown copied to the clipboard"), 5000);
}

// Lays out a snapshot of the monster on the build thread, cancelling the
// build before it.
void MainWindow::startBuild()
{
    buildTimer_.stop();
    if (cancel_)
    {
        cancel_->store(true);
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const int generation = ++generation_;

    builders_.start([this, generation, cancel = cancel_,
                     text = monster::toScribe(previewMonster_ ? *previewMonster_ : monster_),
                     theme = theme()] {
        std::shared_ptr<scribe::PageLayout> layout =
            monster::layoutStatblock(text, theme, cancel.get());
        if (!layout)
        {
            return; // cancelled
        }
        layout->moveToThread(QCoreApplication::instance()->thread());
        QMetaObject::invokeMethod(
            this, [this, generation, layout] { buildFinished(generation, layout); },
            Qt::QueuedConnection);
    });
}

void MainWindow::buildFinished(int generation, std::shared_ptr<scribe::PageLayout> layout)
{
    if (generation != generation_)
    {
        return; // a newer build is on its way
    }
    layout_ = std::move(layout); // the previous layout is freed here
    preview_->setPageLayout(layout_);
}

// Sizes the panels from their shares of the width.
void MainWindow::applyPanelShares()
{
    const int handles = splitter_->count() - 1;
    const int width = splitter_->width() - std::max(handles, 0) * splitter_->handleWidth();
    QList<int> sizes;
    for (double share : panelShares_)
    {
        sizes << qRound(share * width);
    }
    splitter_->setSizes(sizes);
}

// After a drag: the panels' new shares of the width, saved.
void MainWindow::storePanelShares()
{
    const QList<int> sizes = splitter_->sizes();
    int total = 0;
    for (int size : sizes)
    {
        total += size;
    }
    if (total <= 0 || sizes.size() != static_cast<int>(panelShares_.size()))
    {
        return;
    }
    QVariantList saved;
    for (size_t i = 0; i < panelShares_.size(); ++i)
    {
        panelShares_[i] = static_cast<double>(sizes[static_cast<int>(i)]) / total;
        saved << panelShares_[i];
    }
    QSettings().setValue(kPanelSharesKey, saved);
}

void MainWindow::openHomebrewFolder()
{
    const QString folder = monster::homebrewDataFolder();
    if (!QDir().mkpath(folder))
    {
        QMessageBox::warning(this, tr("Open Homebrew Folder"),
                             tr("The homebrew folder couldn't be made:\n%1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    // The kinds' folders, so it's clear where each file goes.
    for (const char *kind : {"adjustments", "roadmaps", "domains", "traits"})
    {
        QDir(folder).mkpath(QString::fromLatin1(kind));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::editHomebrewFolders()
{
    HomebrewFoldersDialog dialog(monsterList_->folder(), this);
    if (dialog.exec() == QDialog::Accepted)
    {
        reloadHomebrew();
    }
}

void MainWindow::editHomebrew(HomebrewEditor *editor)
{
    editor->setAttribute(Qt::WA_DeleteOnClose);
    // Each save reloads homebrew, without the problems message: the editor
    // shows its own.
    connect(editor, &HomebrewEditor::saved, this, [this] { reloadHomebrew(false); });
    editor->exec();
}

void MainWindow::previewHomebrew(const QJsonObject &entry)
{
    if (entry.isEmpty())
    {
        monster::setPreviewModifier(std::nullopt);
        if (previewMonster_)
        {
            previewMonster_.reset();
            startBuild();
        }
        return;
    }
    monster::setPreviewModifier(monster::modifierFromJson(entry, monster::kPreviewModifierId));
    // The monster as it is, with the entry in the place of its saved
    // version if it has that one applied, else added last.
    monster::Monster m = monster_;
    const QString saved = monster::kHomebrewPrefix + entry.value(QStringLiteral("id")).toString();
    if (!monster::replaceAppliedModifier(m, saved, monster::kPreviewModifierId))
    {
        monster::addAppliedModifier(m, monster::kPreviewModifierId);
    }
    monster::syncGeneratedSpecials(m);
    previewMonster_ = std::move(m);
    startBuild();
}

void MainWindow::reloadHomebrew(bool showProblems)
{
    finishEdit();
    monster::reloadHomebrew();
    fillRoadmapMenu();
    fillModifierMenus();
    // The form's list of applied adjustments and the statblock, with the
    // new data.
    showMonster();
    const qsizetype count = monster::homebrewProblems().size();
    statusBar()->showMessage(count == 0 ? tr("Homebrew reloaded")
                                        : tr("Homebrew reloaded, with %n problem(s)", nullptr, static_cast<int>(count)),
                             5000);
    if (showProblems)
    {
        showHomebrewProblems();
    }
}

void MainWindow::showHomebrewProblems()
{
    const QList<monster::HomebrewProblem> &problems = monster::homebrewProblems();
    if (problems.isEmpty())
    {
        return;
    }
    // Every problem goes to the log; the first ones show here.
    constexpr int kShown = 20;
    const QString log = monster::writeHomebrewLog(monster::homebrewLogFolder(monsterList_->folder()));

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Homebrew Problems"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(tr("Some homebrew files have problems. The parts that could be read "
                                "were loaded; fix these and use Homebrew > Reload Homebrew."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    // The file's name and the problem first; its folder below.
    QStringList lines;
    for (qsizetype i = 0; i < std::min<qsizetype>(problems.size(), kShown); ++i)
    {
        const QFileInfo file(problems[i].file);
        lines << file.fileName() + problems[i].text().mid(problems[i].file.size()) + QStringLiteral("\n    in ") +
                     QDir::toNativeSeparators(file.absolutePath());
    }
    auto *list = new QPlainTextEdit(lines.join(QStringLiteral("\n\n")));
    list->setReadOnly(true);
    list->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    list->setMinimumSize(fontMetrics().averageCharWidth() * 90, fontMetrics().height() * 16);
    layout->addWidget(list, 1);
    QString more;
    if (problems.size() > kShown)
    {
        more = tr("%n more problem(s) not shown here.", nullptr, static_cast<int>(problems.size() - kShown)) +
               QLatin1Char(' ');
    }
    auto *where = new QLabel(log.isEmpty()
                                 ? more + tr("The problem log couldn't be written.")
                                 : more + tr("The full list is in the log: %1").arg(QDir::toNativeSeparators(log)));
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(where);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    if (!log.isEmpty())
    {
        QPushButton *open = buttons->addButton(tr("Open Log"), QDialogButtonBox::ActionRole);
        connect(open, &QPushButton::clicked, &dialog,
                [log] { QDesktopServices::openUrl(QUrl::fromLocalFile(log)); });
    }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

