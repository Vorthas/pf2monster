// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Appearance.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace appearance
{
    namespace
    {
        const QString kAppearanceKey = QStringLiteral("appearance");

        Mode runningMode = Mode::Dark;

        // Fusion's own palette follows the colour scheme the desktop
        // reports, which many setups (e.g. QT_QPA_PLATFORMTHEME=generic)
        // don't, so both palettes are spelled out.
        QPalette darkPalette()
        {
            QPalette palette;
            const QColor text(0xf0, 0xf0, 0xf0);
            const QColor disabledText(0x80, 0x80, 0x80);
            const QColor highlight(0x2f, 0x6e, 0xd6);
            palette.setColor(QPalette::Window, QColor(0x32, 0x32, 0x32));
            palette.setColor(QPalette::WindowText, text);
            palette.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
            palette.setColor(QPalette::AlternateBase, QColor(0x2a, 0x2a, 0x2a));
            palette.setColor(QPalette::ToolTipBase, QColor(0x3c, 0x3c, 0x3c));
            palette.setColor(QPalette::ToolTipText, text);
            palette.setColor(QPalette::PlaceholderText, QColor(0xa0, 0xa0, 0xa0));
            palette.setColor(QPalette::Text, text);
            palette.setColor(QPalette::Button, QColor(0x3c, 0x3c, 0x3c));
            palette.setColor(QPalette::ButtonText, text);
            palette.setColor(QPalette::BrightText, Qt::white);
            palette.setColor(QPalette::Light, QColor(0x50, 0x50, 0x50));
            palette.setColor(QPalette::Midlight, QColor(0x46, 0x46, 0x46));
            palette.setColor(QPalette::Mid, QColor(0x2d, 0x2d, 0x2d));
            palette.setColor(QPalette::Dark, QColor(0x23, 0x23, 0x23));
            palette.setColor(QPalette::Shadow, QColor(0x14, 0x14, 0x14));
            palette.setColor(QPalette::Highlight, highlight);
            palette.setColor(QPalette::HighlightedText, Qt::white);
            palette.setColor(QPalette::Link, QColor(0x6e, 0xa8, 0xfe));
            palette.setColor(QPalette::LinkVisited, QColor(0xb1, 0x9c, 0xd9));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            palette.setColor(QPalette::Accent, highlight);
#endif
            for (const QPalette::ColorRole role :
                 {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            {
                palette.setColor(QPalette::Disabled, role, disabledText);
            }
            palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50, 0x50, 0x50));
            palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            palette.setColor(QPalette::Disabled, QPalette::Accent, QColor(0x50, 0x50, 0x50));
#endif
            return palette;
        }

        // Fusion's light palette (Qt 6.12).
        QPalette lightPalette()
        {
            QPalette palette;
            const QColor highlight(0x30, 0x8c, 0xc6);
            palette.setColor(QPalette::Window, QColor(0xef, 0xef, 0xef));
            palette.setColor(QPalette::WindowText, Qt::black);
            palette.setColor(QPalette::Base, Qt::white);
            palette.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
            palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
            palette.setColor(QPalette::ToolTipText, Qt::black);
            palette.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 0x80));
            palette.setColor(QPalette::Text, Qt::black);
            palette.setColor(QPalette::Button, QColor(0xef, 0xef, 0xef));
            palette.setColor(QPalette::ButtonText, Qt::black);
            palette.setColor(QPalette::BrightText, Qt::white);
            palette.setColor(QPalette::Light, Qt::white);
            palette.setColor(QPalette::Midlight, QColor(0xca, 0xca, 0xca));
            palette.setColor(QPalette::Mid, QColor(0xb8, 0xb8, 0xb8));
            palette.setColor(QPalette::Dark, QColor(0x9f, 0x9f, 0x9f));
            palette.setColor(QPalette::Shadow, QColor(0x76, 0x76, 0x76));
            palette.setColor(QPalette::Highlight, highlight);
            palette.setColor(QPalette::HighlightedText, Qt::white);
            palette.setColor(QPalette::Link, Qt::blue);
            palette.setColor(QPalette::LinkVisited, Qt::magenta);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            palette.setColor(QPalette::Accent, highlight);
#endif
            const QColor disabledText(0xbe, 0xbe, 0xbe);
            for (const QPalette::ColorRole role :
                 {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::Dark})
            {
                palette.setColor(QPalette::Disabled, role, disabledText);
            }
            palette.setColor(QPalette::Disabled, QPalette::Base, QColor(0xef, 0xef, 0xef));
            palette.setColor(QPalette::Disabled, QPalette::Shadow, QColor(0xb1, 0xb1, 0xb1));
            palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x91, 0x91, 0x91));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            palette.setColor(QPalette::Disabled, QPalette::Accent, QColor(0x91, 0x91, 0x91));
#endif
            return palette;
        }

        QString modeName(Mode mode)
        {
            switch (mode)
            {
            case Mode::Light:
                return QStringLiteral("light");
            case Mode::Kvantum:
                return QStringLiteral("kvantum");
            case Mode::Dark:
                break;
            }
            return QStringLiteral("dark");
        }

        void useFusion(Mode mode)
        {
            QApplication::setStyle(QStringLiteral("Fusion"));
            QApplication::setPalette(mode == Mode::Light ? lightPalette() : darkPalette());
            runningMode = mode;
        }
    } // namespace

    Mode saved()
    {
        const QString name = QSettings().value(kAppearanceKey).toString();
        if (name == modeName(Mode::Light))
        {
            return Mode::Light;
        }
        if (name == modeName(Mode::Kvantum))
        {
            return Mode::Kvantum;
        }
        return Mode::Dark;
    }

    Mode running()
    {
        return runningMode;
    }

    bool kvantumInstalled()
    {
        return QStyleFactory::keys().contains(QStringLiteral("kvantum"), Qt::CaseInsensitive);
    }

    void beforeApplication()
    {
        // Kvantum is loaded the way the desktop does it, through
        // QT_STYLE_OVERRIDE. Otherwise the override is dropped before the
        // application exists, since a style loaded then leaves its palette
        // behind even after Fusion replaces it.
        if (saved() == Mode::Kvantum)
        {
            qputenv("QT_STYLE_OVERRIDE", "kvantum");
        }
        else
        {
            qunsetenv("QT_STYLE_OVERRIDE");
        }
    }

    void afterApplication()
    {
        const Mode mode = saved();
        if (mode == Mode::Kvantum &&
            QApplication::style()->name().compare(QStringLiteral("kvantum"), Qt::CaseInsensitive) == 0)
        {
            runningMode = Mode::Kvantum;
            return;
        }
        // Kvantum missing falls back to Dark.
        useFusion(mode == Mode::Light ? Mode::Light : Mode::Dark);
    }

    bool choose(Mode mode)
    {
        QSettings().setValue(kAppearanceKey, modeName(mode));
        if (mode == runningMode)
        {
            return false;
        }
        if (mode == Mode::Kvantum || runningMode == Mode::Kvantum)
        {
            return true;
        }
        useFusion(mode);
        return false;
    }
} // namespace appearance
