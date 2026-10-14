// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Appearance.h"
#include "MainWindow.h"
#include "Rendering/Fonts.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QPalette>

namespace
{
    // Some GTK themes give Qt's GTK platform theme fully transparent
    // background colours (window, base, button). Main windows still look
    // right, but popups such as a combo box's list then show the form
    // through them. Makes those colours opaque; does nothing for a palette
    // that already is.
    void makeBackgroundsOpaque()
    {
        QPalette palette = QApplication::palette();
        bool changed = false;
        for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        {
            for (const QPalette::ColorRole role :
                 {QPalette::Window, QPalette::Base, QPalette::AlternateBase, QPalette::Button,
                  QPalette::ToolTipBase})
            {
                QColor color = palette.color(group, role);
                if (color.alpha() < 255)
                {
                    color.setAlpha(255);
                    palette.setColor(group, role, color);
                    changed = true;
                }
            }
        }
        if (changed)
        {
            QApplication::setPalette(palette);
        }
    }

    // Some styles (Kvantum) make a combo box's list see-through, to draw
    // their own panel behind it; where that panel doesn't appear, the form
    // shows through the list. Each list in a popup gets the application's
    // opaque background when it is shown (after the style has set its own).
    class OpaquePopupLists : public QObject
    {
    public:
        using QObject::QObject;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::Show)
            {
                auto *view = qobject_cast<QAbstractItemView *>(watched);
                if (view && view->window() != view && view->window()->windowType() == Qt::Popup)
                {
                    QPalette palette = view->palette();
                    for (const QPalette::ColorRole role : {QPalette::Base, QPalette::Window})
                    {
                        if (palette.color(role).alpha() < 255)
                        {
                            QColor color = QApplication::palette().color(role);
                            color.setAlpha(255);
                            palette.setColor(role, color);
                        }
                    }
                    view->setPalette(palette);
                }
            }
            return QObject::eventFilter(watched, event);
        }
    };
} // namespace

int main(int argc, char *argv[])
{
    // Before the application: Qt reads the installed fonts when it starts.
    scribe::hideBrokenInstalledFonts();
    // Where QSettings keeps options (~/.config/pf2monster/pf2monster.conf on
    // Linux), and the name shown in window titles.
    QApplication::setOrganizationName(QStringLiteral("pf2monster"));
    QApplication::setApplicationName(QStringLiteral("pf2monster"));
    QApplication::setApplicationVersion(QStringLiteral(PF2MONSTER_VERSION));
    // Fusion with dark or light colours (its input boxes stand out from the
    // form, and the app looks the same however it is launched), or Kvantum:
    // View > Appearance.
    appearance::beforeApplication();
    QApplication app(argc, argv);
    appearance::afterApplication();
    QApplication::setApplicationDisplayName(
        QStringLiteral("Monster Builder for Pathfinder 2e"));
    makeBackgroundsOpaque();
    app.installEventFilter(new OpaquePopupLists(&app));

    scribe::loadFonts();

    MainWindow window;
    if (argc > 1)
    {
        window.openFile(QString::fromLocal8Bit(argv[1]));
    }
    window.show();

    return app.exec();
}
