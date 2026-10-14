// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

// The program's own look (View > Appearance): Fusion with dark or light
// colours, or the desktop's Kvantum style. Dark and Light switch at once;
// Kvantum is only loaded or dropped at start, since a style loaded once
// leaves its palette and widget changes behind.
namespace appearance
{
    enum class Mode
    {
        Dark,
        Light,
        Kvantum,
    };

    // Before the QApplication exists (it needs the organization and
    // application names): picks the style the application will load.
    void beforeApplication();
    // Right after the QApplication exists: Fusion and its colours, unless
    // this run uses Kvantum.
    void afterApplication();

    // The choice saved in the settings, and the one this run looks like.
    Mode saved();
    Mode running();
    bool kvantumInstalled();

    // Saves the choice and applies it if it can be done now. True when it
    // only takes effect at the next start.
    bool choose(Mode mode);
} // namespace appearance
