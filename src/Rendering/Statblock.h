// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include "Rendering/PageLayout.h"

#include <QImage>

#include <atomic>
#include <memory>

namespace monster
{
    // Lays out Scribe text (a monster's item( ... ) block) alone on one
    // white page, as wide as a Scribe page's text and as tall as the
    // statblock. May run on any thread (see textEngineMutex()); nullptr if
    // `cancel` is set first.
    std::shared_ptr<scribe::PageLayout> layoutStatblock(const QString &scribeText,
                                                        scribe::Theme theme,
                                                        const std::atomic<bool> *cancel = nullptr);
    // The same on A4 pages with a Scribe page's margins, the statblock at
    // the top of the first (and continuing on the next if it is that long).
    std::shared_ptr<scribe::PageLayout> layoutStatblockOnA4(const QString &scribeText,
                                                            scribe::Theme theme);
    // Writes every page of `layout` to a PDF at `path`, text as text (one
    // unit is one point). Call on the thread that built the layout: the PDF
    // writer reads the fonts once painting is over (see textEngineMutex()).
    bool writePdf(const scribe::PageLayout &layout, const QString &path, const QString &title);

    // Page `page` of `layout` as an image at `dpi`, on white.
    QImage renderPage(const scribe::PageLayout &layout, int page, qreal dpi);
} // namespace monster
