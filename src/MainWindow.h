// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Model/Monster.h"
#include "Rendering/Statblock.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QThreadPool>
#include <QTimer>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class MonsterEditor;
class MonsterList;
class PreviewWidget;
class QAction;
class QMenu;
class HomebrewEditor;
class QSplitter;

// The main window: the editor (with the monster list in its lower-left
// corner) and the statblock preview, side by side.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    // Opens a monster file. False (after telling the user) if it can't.
    bool openFile(const QString &path);
    // Shows the monsters in `folder` in the list.
    void openFolder(const QString &folder);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void newMonster();
    void open();
    void chooseFolder();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    void duplicateMonster();
    void deleteMonster();
    // Asks what to do with unsaved changes; false to stay.
    bool maybeSave();
    // Opens the monster picked in the list (after maybeSave()).
    void switchTo(const QString &path);
    // Shows monster_ as just loaded or created: nothing to save.
    void showMonster();
    void updateModified();
    void updateTitle();

    // Undo: the monster's state before each burst of edits.
    void edited();
    void finishEdit();
    void undo();
    void redo();
    void updateUndoActions();
    struct History
    {
        std::vector<monster::Monster> undo;
        std::vector<monster::Monster> redo;
    };
    History &history() { return histories_[path_]; }

    // Records a change made from a menu (the monster was `before`) as one
    // undo step and shows it.
    void commitChange(monster::Monster before);

    // Fills the Collection menu with the folder's monsters.
    void fillCollectionMenu();
    // Find Monster: a search over the collection; opens the one picked.
    void findMonster();
    // Help > About: the version, the licences and the ORC credits.
    void about();

    void applyHints();
    // Gives the open monster theme `key` (an undoable edit) and makes it
    // the one new monsters get.
    void setTheme(const QString &key);
    // The open monster's theme, with the spacing chosen under View.
    scribe::Theme theme() const;
    void applyRoadmap(const QString &id);
    void clearRoadmap();
    void addModifier(const QString &id);
    // The Adjustments and Templates menus, from monster::modifiers().
    void fillModifierMenus();
    // The Roadmap menu, from monster::roadmaps().
    void fillRoadmapMenu();
    // Homebrew menu.
    void openHomebrewFolder();
    void editHomebrewFolders();
    // Reads all homebrew again; `showProblems`: the problems message too.
    void reloadHomebrew(bool showProblems = true);
    // Opens a homebrew editor (it deletes itself when closed).
    void editHomebrew(HomebrewEditor *editor);
    // While a homebrew adjustment/template is edited: the statblock shows
    // the open monster with `entry` applied (empty: back to the monster).
    void previewHomebrew(const QJsonObject &entry);
    // The problems the last homebrew load found, if any, in a dialog and
    // the log.
    void showHomebrewProblems();
    // Opens the Mythic dialog for the applied entry at `index`, or for the
    // monster's Mythic entry (a new one if it has none) when -1.
    void editMythic(int index = -1);
    // The options dialog of the applied adjustment at `index`, or of a new
    // one with this id (index -1); false if it was cancelled.
    bool editOptions(int index, const QString &id = QString());
    // The applied adjustment's dialog, whichever it has.
    void editModifier(int index);
    void exportScribe();
    void copyScribe();
    void exportPdf(bool a4);
    void exportPng();
    void importFromWebsite();
    // Copies these website downloads into the collection; returns the new
    // files.
    QStringList importFiles(const QStringList &paths);

    // Lays the statblock out on the build thread; the newest build is
    // shown when it's done.
    void startBuild();
    void buildFinished(int generation, std::shared_ptr<scribe::PageLayout> layout);

    void applyPanelShares();
    void storePanelShares();

    monster::Monster monster_;
    // The open monster with the homebrew entry being edited applied, shown
    // instead of it while the editor is open.
    std::optional<monster::Monster> previewMonster_;
    QString path_;      // empty until saved or opened
    QJsonObject saved_; // the monster as on disk, to tell whether it changed
    QString savedName_; // its name then: renaming the monster renames the file
    bool modified_ = false;

    // Undo history per monster file ("" for a new one) while the app runs.
    std::map<QString, History> histories_;
    monster::Monster beforeEdit_; // the state the current burst started from
    QTimer editBurst_;            // edits this close together undo as one
    QAction *undoAction_ = nullptr;
    QAction *redoAction_ = nullptr;

    QSplitter *splitter_ = nullptr;
    MonsterList *monsterList_ = nullptr;
    QMenu *collectionMenu_ = nullptr;
    QMenu *roadmapMenu_ = nullptr;
    QMenu *adjustmentMenu_ = nullptr;
    QMenu *templateMenu_ = nullptr;
    QAction *folderAction_ = nullptr;
    QAction *importAction_ = nullptr;
    MonsterEditor *editor_ = nullptr;
    PreviewWidget *preview_ = nullptr;

    // One build thread that never ends while layouts exist: they keep using
    // its font engines (see textEngineMutex()).
    QThreadPool builders_;
    QTimer buildTimer_; // a short pause after an edit before building
    std::shared_ptr<std::atomic<bool>> cancel_;
    int generation_ = 0;
    std::shared_ptr<scribe::PageLayout> layout_;
    QString newTheme_; // the theme last picked: new and imported monsters get it
    QAction *compactAction_ = nullptr;

    // Each panel's share of the window's width (editor, preview).
    std::array<double, 2> panelShares_{0.73, 0.27};

    static constexpr qreal kPngDpi = 300;

    static constexpr const char *kPanelSharesKey = "window/panelShares";
    static constexpr const char *kGeometryKey = "window/geometry";
    static constexpr const char *kListSplitKey = "window/listSplit";
    static constexpr const char *kShowListKey = "view/monsterList";
    static constexpr const char *kThemeKey = "view/lastTheme";
    static constexpr const char *kCompactKey = "view/compactSpacing";
    static constexpr const char *kLastFolderKey = "files/lastFolder";
    static constexpr const char *kCollectionKey = "collection/folder";
    static constexpr const char *kLastMonsterKey = "collection/lastMonster";
};
