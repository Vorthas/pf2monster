// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/OptionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    QLabel *noteLabel(const QString &text)
    {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setEnabled(false); // greyed, as the form's notes
        return label;
    }

    // Whether `options` has the choice `when` names made (see
    // Modifier::Ability::when).
    bool isChosen(const QJsonObject &options, const QString &when)
    {
        if (when.isEmpty())
        {
            return true;
        }
        if (when.contains(QLatin1Char('&')))
        {
            const QStringList parts = when.split(QLatin1Char('&'));
            return std::all_of(parts.begin(), parts.end(),
                               [&](const QString &part) { return isChosen(options, part.trimmed()); });
        }
        if (when.startsWith(QLatin1Char('!')))
        {
            return !isChosen(options, when.mid(1));
        }
        const qsizetype colon = when.indexOf(QLatin1Char(':'));
        const QJsonValue value = options.value(colon < 0 ? when : when.left(colon));
        if (colon < 0)
        {
            return value.toBool();
        }
        const QString item = when.mid(colon + 1);
        return value.isArray() ? value.toArray().contains(item) : value.toString() == item;
    }
} // namespace

OptionsDialog::OptionsDialog(const monster::Modifier &modifier, int level, const QJsonObject &options,
                             QWidget *parent)
    : QDialog(parent), modifier_(modifier), options_(monster::optionsWithDefaults(modifier, options))
{
    setWindowTitle(modifier.name);
    setFixedWidth(fontMetrics().averageCharWidth() * 70);
    auto *outer = new QVBoxLayout(this);

    auto *intro = new QLabel(modifier.rules);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    // The choices scroll when the dialog would be taller than the screen;
    // the rules above and the buttons below always show.
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_ = new QWidget;
    auto *layout = new QVBoxLayout(content_);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll_->setWidget(content_);
    outer->addWidget(scroll_, 1);

    // The level after this adjustment, for the items offered from a level.
    int after = level;
    for (const monster::Modifier::Step &step : modifier.level)
    {
        if ((!step.min || level >= *step.min) && (!step.max || level <= *step.max))
        {
            after += step.change;
            break;
        }
    }

    for (const monster::Modifier::Choice &choice : modifier.choices)
    {
        const QString key = choice.key;
        QWidget *made = nullptr;
        if (choice.type == QLatin1String("pick"))
        {
            auto *box = new QGroupBox(choice.label);
            auto *boxLayout = new QVBoxLayout(box);
            if (!choice.note.isEmpty())
            {
                boxLayout->addWidget(noteLabel(choice.note));
            }
            const QJsonArray picked = options_.value(key).toArray();
            std::vector<std::pair<QString, QCheckBox *>> checks;
            // A long list (Skill Savvy's feats) in two columns.
            auto *grid = new QGridLayout;
            boxLayout->addLayout(grid);
            const int perColumn = choice.items.size() > 6 ? static_cast<int>(choice.items.size() + 1) / 2
                                                          : static_cast<int>(choice.items.size());
            int index = 0;
            for (const monster::Modifier::Choice::Item &item : choice.items)
            {
                auto *check = new QCheckBox(item.label);
                check->setToolTip(item.tip);
                check->setChecked(picked.contains(item.key));
                // Offered from a level: hidden below it (kept if picked).
                if (item.minLevel && after < *item.minLevel)
                {
                    check->setVisible(false);
                }
                grid->addWidget(check, index % perColumn, index / perColumn);
                ++index;
                checks.emplace_back(item.key, check);
                connect(check, &QCheckBox::toggled, this, [this] { updateEnabled(); });
            }
            readers_.push_back([key, checks](QJsonObject &out) {
                QJsonArray list;
                for (const auto &[itemKey, check] : checks)
                {
                    if (check->isChecked())
                    {
                        list.append(itemKey);
                    }
                }
                out.insert(key, list);
            });
            made = box;
        }
        else if (choice.type == QLatin1String("choice"))
        {
            auto *box = new QGroupBox(choice.label);
            auto *boxLayout = new QVBoxLayout(box);
            if (!choice.note.isEmpty())
            {
                boxLayout->addWidget(noteLabel(choice.note));
            }
            auto *combo = new QComboBox;
            for (const monster::Modifier::Choice::Item &item : choice.items)
            {
                combo->addItem(item.label, item.key);
                combo->setItemData(combo->count() - 1, item.tip, Qt::ToolTipRole);
            }
            combo->setCurrentIndex(std::max(0, combo->findData(options_.value(key).toString())));
            boxLayout->addWidget(combo);
            connect(combo, &QComboBox::activated, this, [this] { updateEnabled(); });
            readers_.push_back([key, combo](QJsonObject &out) { out.insert(key, combo->currentData().toString()); });
            made = box;
        }
        else if (choice.type == QLatin1String("check"))
        {
            auto *holder = new QWidget;
            auto *holderLayout = new QVBoxLayout(holder);
            holderLayout->setContentsMargins(0, 0, 0, 0);
            auto *check = new QCheckBox(choice.label);
            check->setChecked(options_.value(key).toBool());
            holderLayout->addWidget(check);
            if (!choice.note.isEmpty())
            {
                holderLayout->addWidget(noteLabel(choice.note));
            }
            // Offered from a level: hidden below it (kept if ticked).
            if (choice.minLevel && after < *choice.minLevel)
            {
                holder->setVisible(false);
            }
            connect(check, &QCheckBox::toggled, this, [this] { updateEnabled(); });
            readers_.push_back([key, check](QJsonObject &out) { out.insert(key, check->isChecked()); });
            made = holder;
        }
        else // text
        {
            auto *box = new QGroupBox(choice.label);
            auto *boxLayout = new QVBoxLayout(box);
            if (!choice.note.isEmpty())
            {
                boxLayout->addWidget(noteLabel(choice.note));
            }
            auto *edit = new QLineEdit(options_.value(key).toString());
            edit->setPlaceholderText(choice.placeholder);
            if (const QString fallback = choice.defaultValue.toString(); !fallback.isEmpty())
            {
                edit->setToolTip(tr("Empty: \"%1\"").arg(fallback));
            }
            boxLayout->addWidget(edit);
            readers_.push_back([key, edit](QJsonObject &out) { out.insert(key, edit->text().trimmed()); });
            made = box;
        }
        layout->addWidget(made);
        if (!choice.when.isEmpty())
        {
            conditional_.emplace_back(choice.when, made);
        }
    }

    if (!modifier.optional.isEmpty())
    {
        layout->addWidget(noteLabel(modifier.optional));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
    updateEnabled();
}

QJsonObject OptionsDialog::options() const
{
    QJsonObject out = options_;
    for (const auto &read : readers_)
    {
        read(out);
    }
    return out;
}

void OptionsDialog::updateEnabled()
{
    const QJsonObject now = options();
    for (const auto &[when, widget] : conditional_)
    {
        widget->setEnabled(isChosen(now, when));
    }
}

void OptionsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitHeight();
}

void OptionsDialog::fitHeight()
{
    // As MythicDialog: measured once shown, with the style's sizes, and
    // tall enough for the wrapped texts at this width; but no taller than
    // the screen, where the choices scroll.
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>())
    {
        child->ensurePolished();
    }
    // The choices' full height at the scroll area's width, held as its
    // minimum while the dialog is measured.
    QLayout *inner = content_->layout();
    inner->activate();
    const int innerWidth = width() - layout()->contentsMargins().left() - layout()->contentsMargins().right();
    int innerHeight = inner->totalMinimumSize().height();
    if (inner->hasHeightForWidth())
    {
        innerHeight = std::max(innerHeight, inner->totalHeightForWidth(innerWidth));
    }
    scroll_->setMinimumHeight(innerHeight);
    layout()->activate();
    int height = layout()->totalMinimumSize().height();
    if (layout()->hasHeightForWidth())
    {
        height = std::max(height, layout()->totalHeightForWidth(width()));
    }
    scroll_->setMinimumHeight(fontMetrics().height() * 4);
    // The screen less the window's title bar and frame.
    if (const QScreen *s = screen())
    {
        const int frame = std::max(frameGeometry().height() - geometry().height(), fontMetrics().height() * 2);
        height = std::min(height, s->availableGeometry().height() - frame);
    }
    resize(width(), height);
}
