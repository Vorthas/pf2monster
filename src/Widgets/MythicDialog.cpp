// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/MythicDialog.h"

#include "Model/Mythic.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
    QString ordinal(int n)
    {
        const char *suffix = n % 10 == 1 && n != 11   ? "st"
                             : n % 10 == 2 && n != 12 ? "nd"
                             : n % 10 == 3 && n != 13 ? "rd"
                                                      : "th";
        return QString::number(n) + QLatin1String(suffix);
    }

    QString capitalized(QString text)
    {
        if (!text.isEmpty())
        {
            text[0] = text[0].toUpper();
        }
        return text;
    }

    // Removes and deletes everything in `layout`.
    void clear(QLayout *layout)
    {
        while (QLayoutItem *item = layout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
    }
} // namespace

MythicDialog::MythicDialog(const monster::Monster &monster, int level, const QJsonObject &options,
                           const QString &name, QWidget *parent)
    : QDialog(parent), monster_(monster), level_(level), options_(options), name_(name)
{
    setWindowTitle(tr("Mythic"));
    // As wide as about 80 characters, whatever the template's texts.
    setFixedWidth(fontMetrics().averageCharWidth() * 80);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("The mythic trait, Mythic Power and the basic mythic abilities "
                                "up to the creature's level (%1), and optionally a template "
                                "(War of Immortals).")
                                 .arg(level));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    layout->addLayout(form);
    template_ = new QComboBox;
    template_->addItem(tr("None (basic mythic abilities only)"), QString());
    for (const monster::MythicTemplate &t : monster::mythicTemplates())
    {
        template_->addItem(tr("Mythic %1").arg(t.name), t.id);
    }
    template_->setCurrentIndex(
        std::max(0, template_->findData(options_.value(QStringLiteral("template")).toString())));
    form->addRow(tr("Template"), template_);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    form->addRow(QString(), summary_);
    tradition_ = new QComboBox;
    for (const char *t : {"arcane", "divine", "occult", "primal"})
    {
        tradition_->addItem(capitalized(QString::fromLatin1(t)), QString::fromLatin1(t));
    }
    tradition_->setCurrentIndex(
        std::max(0, tradition_->findData(monster::mythicTradition(monster_, options_))));
    tradition_->setToolTip(tr("Mythic Knowledge makes this tradition's skill extreme: Arcana, "
                              "Nature, Occultism or Religion"));
    traditionLabel_ = new QLabel(tr("Tradition"));
    form->addRow(traditionLabel_, tradition_);

    auto *slotsBox = new QGroupBox(tr("Basic Mythic Abilities"));
    auto *slotsLayout = new QVBoxLayout(slotsBox);
    slots_ = new QGridLayout;
    slotsLayout->addLayout(slots_);
    // Below 4th level a creature has no Mythic Points (the rules' table);
    // this gives it the pool anyway.
    earlyPoints_ = new QCheckBox(tr("Give it Mythic Points anyway (3)"));
    earlyPoints_->setToolTip(tr("Mythic Power comes at 4th level. Ticking this, or a template "
                                "ability that costs Mythic Points, gives it the pool earlier."));
    earlyPoints_->setChecked(options_.value(QStringLiteral("earlyPoints")).toBool());
    earlyPoints_->setVisible(level_ < 4);
    slotsLayout->addWidget(earlyPoints_);
    layout->addWidget(slotsBox);

    auto *abilitiesBox = new QGroupBox(tr("Template Abilities"));
    abilities_ = new QVBoxLayout(abilitiesBox);
    layout->addWidget(abilitiesBox);

    // Paizo's short entries (as in War of Immortals), or the rules' text in
    // full, naming the creature as set here.
    auto *textBox = new QGroupBox(tr("Statblock Text"));
    auto *textLayout = new QFormLayout(textBox);
    fullText_ = new QCheckBox(tr("Write out the abilities' rules text"));
    fullText_->setToolTip(tr("Off: short entries as in War of Immortals (\"Mythic Ferocity "
                             "Cost 1 Mythic Point, 65 HP\"), the rest in the rules. On: each "
                             "ability with its full text."));
    fullText_->setChecked(options_.value(QStringLiteral("fullText")).toBool());
    textLayout->addRow(fullText_);
    prefixName_ = new QCheckBox(tr("Put \"Mythic\" before the name"));
    prefixName_->setToolTip(tr("\"Mythic ogre boss\" in the statblock's title; off by default "
                               "for a proper name such as \"Sziskazha\""));
    prefixName_->setChecked(monster::mythicPrefixName(name_, options_));
    textLayout->addRow(prefixName_);
    callIt_ = new QLineEdit(monster::mythicCallIt(name_, options_));
    callIt_->setToolTip(tr("How the abilities' text names the creature: \"the mythic ogre boss\", "
                           "or a proper name such as \"Sziskazha\""));
    textLayout->addRow(tr("Called"), callIt_);
    layout->addWidget(textBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(template_, &QComboBox::activated, this, [this] {
        options_.insert(QStringLiteral("template"), template_->currentData().toString());
        rebuildLater();
    });
    connect(tradition_, &QComboBox::activated, this, [this] {
        options_.insert(QStringLiteral("tradition"), tradition_->currentData().toString());
        rebuildLater();
    });
    rebuild();
}

void MythicDialog::rebuildLater()
{
    if (!rebuildPending_)
    {
        rebuildPending_ = true;
        QTimer::singleShot(0, this, [this] {
            rebuildPending_ = false;
            rebuild();
        });
    }
}

void MythicDialog::rebuild()
{
    const monster::MythicTemplate *t =
        monster::findMythicTemplate(options_.value(QStringLiteral("template")).toString());
    summary_->setText(t ? t->summary + QStringLiteral("\n") + t->weakness : QString());
    summary_->setVisible(t != nullptr);
    bool usesTradition = false;
    if (t)
    {
        for (const QString &key : t->abilities)
        {
            const monster::MythicAbility *a = monster::findMythicAbility(key);
            usesTradition = usesTradition || (a && a->skill == QLatin1String("tradition"));
        }
    }
    tradition_->setVisible(usesTradition);
    traditionLabel_->setVisible(usesTradition);

    // A row per level-table slot: a choice, or what it gives.
    clear(slots_);
    const std::vector<monster::MythicSlot> rows = monster::mythicSlots(monster_, level_, options_);
    int row = 0;
    if (rows.empty())
    {
        slots_->addWidget(new QLabel(tr("None below 1st level.")), 0, 0);
    }
    for (const monster::MythicSlot &slot : rows)
    {
        slots_->addWidget(new QLabel(ordinal(slot.level)), row, 0);
        if (slot.choices.size() > 1)
        {
            auto *combo = new QComboBox;
            for (const QString &choice : slot.choices)
            {
                combo->addItem(monster::mythicChoiceLabel(choice), choice);
            }
            combo->setCurrentIndex(std::max(0, combo->findData(slot.choice)));
            slots_->addWidget(combo, row, 1);
            connect(combo, &QComboBox::activated, this, [this, combo, at = slot.level] {
                QJsonObject stored = options_.value(QStringLiteral("slots")).toObject();
                stored.insert(QString::number(at), combo->currentData().toString());
                options_.insert(QStringLiteral("slots"), stored);
                rebuildLater();
            });
        }
        else
        {
            slots_->addWidget(new QLabel(slot.choices.isEmpty()
                                             ? tr("Nothing left to gain here")
                                             : monster::mythicChoiceLabel(slot.choice)),
                              row, 1);
        }
        ++row;
        // Mythic Skill's skills: one, a second from 12th level.
        if (slot.choice == QLatin1String("skill"))
        {
            const QStringList skills = monster::mythicSkills(monster_, level_, options_);
            for (int i = 0; i < skills.size(); ++i)
            {
                auto *combo = new QComboBox;
                for (const char *key : monster::kSkills)
                {
                    combo->addItem(capitalized(QString::fromLatin1(key)), QString::fromLatin1(key));
                }
                combo->setCurrentIndex(std::max(0, combo->findData(skills[i])));
                slots_->addWidget(new QLabel(i == 0 ? tr("Skill") : tr("Second skill")), row, 0);
                slots_->addWidget(combo, row++, 1);
                connect(combo, &QComboBox::activated, this, [this, combo, i] {
                    QStringList chosen = monster::mythicSkills(monster_, level_, options_);
                    chosen[i] = combo->currentData().toString();
                    options_.insert(QStringLiteral("skills"), QJsonArray::fromStringList(chosen));
                    rebuildLater();
                });
            }
        }
    }
    slots_->setColumnStretch(1, 1);

    // The template's abilities, each to include or not.
    clear(abilities_);
    if (t)
    {
        for (const QString &key : t->abilities)
        {
            const monster::MythicAbility *a = monster::findMythicAbility(key);
            if (!a)
            {
                continue;
            }
            auto *box = new QCheckBox(a->name);
            box->setChecked(monster::mythicAbilityOn(options_, key, level_));
            abilities_->addWidget(box);
            // What it does, under it: the change to the numbers, or the
            // ability's text (added as a special ability).
            QString text = a->summary.isEmpty() ? a->description : a->summary;
            text.remove(QStringLiteral("**"))
                .replace(QStringLiteral("\\%"), QStringLiteral("%"))
                .replace(QStringLiteral("{Name}"), QStringLiteral("The creature"))
                .replace(QStringLiteral("{name}"), QStringLiteral("the creature"));
            if (a->costsPoints && level_ < 4)
            {
                text += tr(" (Costs a Mythic Point: off by default below 4th level, where it has none.)");
            }
            auto *about = new QLabel(text);
            about->setTextFormat(Qt::PlainText);
            about->setWordWrap(true);
            about->setContentsMargins(box->style()->pixelMetric(QStyle::PM_IndicatorWidth) + 6, 0, 0, 4);
            about->setEnabled(box->isChecked());
            abilities_->addWidget(about);
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
                QJsonObject chosen = options_.value(QStringLiteral("abilities")).toObject();
                chosen.insert(key, on);
                options_.insert(QStringLiteral("abilities"), chosen);
                rebuildLater(); // a skill ability can change Mythic Skill's default
            });
        }
    }
    else
    {
        abilities_->addWidget(new QLabel(tr("Choose a template for its abilities.")));
    }
    fitHeight();
    // The new rows are shown (and fully styled) on the next pass of the
    // event loop: measure again then.
    QTimer::singleShot(0, this, &MythicDialog::fitHeight);
}

void MythicDialog::fitHeight()
{
    // Measured once shown, with the style's sizes (Kvantum's are taller
    // than Fusion's): rows made since must take the style first.
    if (!isVisible())
    {
        return;
    }
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>())
    {
        child->ensurePolished();
    }
    // Tall enough for the wrapped texts at this width (adjustSize()
    // measures those before they know their width), and never below what
    // the rows need.
    layout()->activate();
    int height = layout()->totalMinimumSize().height();
    if (layout()->hasHeightForWidth())
    {
        height = std::max(height, layout()->totalHeightForWidth(width()));
    }
    resize(width(), height);
}

void MythicDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitHeight();
}

QJsonObject MythicDialog::options() const
{
    QJsonObject result = options_;
    result.insert(QStringLiteral("fullText"), fullText_->isChecked());
    result.insert(QStringLiteral("prefixName"), prefixName_->isChecked());
    result.insert(QStringLiteral("earlyPoints"), level_ < 4 && earlyPoints_->isChecked());
    // Kept only when it differs from the default, so a renamed monster
    // follows.
    if (callIt_->text().trimmed() == monster::mythicCallIt(name_, QJsonObject()))
    {
        result.remove(QStringLiteral("callIt"));
    }
    else
    {
        result.insert(QStringLiteral("callIt"), callIt_->text().trimmed());
    }
    QJsonObject stored;
    for (const monster::MythicSlot &slot : monster::mythicSlots(monster_, level_, options_))
    {
        stored.insert(QString::number(slot.level), slot.choice);
    }
    result.insert(QStringLiteral("slots"), stored);
    result.insert(QStringLiteral("skills"),
                  QJsonArray::fromStringList(monster::mythicSkills(monster_, level_, options_)));
    if (!tradition_->isHidden() || !result.value(QStringLiteral("tradition")).toString().isEmpty())
    {
        result.insert(QStringLiteral("tradition"), monster::mythicTradition(monster_, options_));
    }
    return result;
}
