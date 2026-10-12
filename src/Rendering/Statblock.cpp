// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Rendering/Statblock.h"

#include "Rendering/ScribeParser.h"

#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QtMath>

namespace monster
{
    std::shared_ptr<scribe::PageLayout> layoutStatblock(const QString &scribeText,
                                                        scribe::Theme theme,
                                                        const std::atomic<bool> *cancel)
    {
        // A Scribe page's text width, with the padding a column has on the
        // page, so the statblock wraps as it does in a one-column Scribe
        // document.
        scribe::PageSpec spec;
        spec.width = scribe::PageSpec().contentWidth();
        spec.marginX = 0;
        spec.top = spec.columnPadding;
        // Tall enough that the statblock never breaks onto a second page;
        // cut to its height afterwards.
        spec.height = 100000;
        spec.contentBottomY = spec.height - spec.columnPadding;

        // On white, whatever the theme's paper (plan 5d).
        theme.pageBg = Qt::white;

        const scribe::Document doc = scribe::parse(scribeText);
        std::shared_ptr<scribe::PageLayout> layout =
            scribe::PageLayout::build(doc, spec, theme, QString(), cancel);
        if (layout && !layout->pages().empty())
        {
            layout->setPageHeight(layout->contentHeight(0) + spec.columnPadding);
        }
        return layout;
    }

    std::shared_ptr<scribe::PageLayout> layoutStatblockOnA4(const QString &scribeText,
                                                            scribe::Theme theme)
    {
        scribe::PageSpec spec; // Scribe's page: its width, margins and text area
        spec.height = 841.89;  // A4 (Scribe's own pages are 296.5 mm)
        theme.pageBg = Qt::white;
        return scribe::PageLayout::build(scribe::parse(scribeText), spec, theme);
    }

    bool writePdf(const scribe::PageLayout &layout, const QString &path, const QString &title)
    {
        QPdfWriter writer(path);
        writer.setResolution(72); // 1 painter unit == 1 pt, as in the layout
        writer.setPageSize(QPageSize(QSizeF(layout.spec().width, layout.spec().height), QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setTitle(title);
        writer.setCreator(QGuiApplication::applicationDisplayName());
        QPainter painter(&writer);
        if (!painter.isActive())
        {
            return false;
        }
        const int count = static_cast<int>(layout.pages().size());
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                writer.newPage();
            }
            layout.paintPage(painter, i);
        }
        return painter.end();
    }

    QImage renderPage(const scribe::PageLayout &layout, int page, qreal dpi)
    {
        const qreal scale = dpi / 72.0;
        QImage image(qCeil(layout.spec().width * scale), qCeil(layout.spec().height * scale),
                     QImage::Format_RGB32);
        image.setDotsPerMeterX(qRound(dpi / 0.0254));
        image.setDotsPerMeterY(qRound(dpi / 0.0254));
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(scale, scale);
        layout.paintPage(painter, page);
        return image;
    }
} // namespace monster
