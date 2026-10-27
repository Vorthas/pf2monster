// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#pragma once

#include <QString>

namespace monster
{
    struct Monster;

    // The monster as a Scribe "item( ... )" block: what the preview renders,
    // and what Export / Copy Scribe Markdown hand out for pasting into a
    // Scribe document. Lines in the order of Paizo's statblocks (and the
    // website's): identity and senses, defense, offense. Empty statistics
    // are left out. The description follows the block as ordinary text.
    // `withoutLevel`: GM Core's Proficiency without Level variant, the
    // monster's level taken off Perception, skills, AC, saves, Strike and
    // spell attacks, spell DCs, the DCs typed in its abilities (not flat
    // checks) and Recall Knowledge; nothing below level 1.
    QString toScribe(const Monster &monster, bool withoutLevel = false);

    // The website's references in `text`: "[ac]", "[athletics]", "[dex]",
    // "[dc]" ... become what the statblock shows for that statistic (its
    // hint when the value is empty), "%" the monster's name. "\%" is a
    // literal %; an unknown reference stays as typed. toScribe() applies it
    // to the fields the website does: the description, ability
    // descriptions, Strike names, spells, items and languages.
    QString expandReferences(const Monster &monster, const QString &text);

    // "+5" for 5, "-1" for -1; text that isn't a number is kept as is.
    QString withSign(const QString &value);
} // namespace monster
