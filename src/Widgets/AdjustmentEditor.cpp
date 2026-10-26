// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/AdjustmentEditor.h"

#include "Model/Adjustments.h"
#include "Model/Homebrew.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    QLabel *hint(const QString &text)
    {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        label->setEnabled(false); // grey, as the form's notes
        return label;
    }

    QLabel *heading(const QString &text)
    {
        return new QLabel(QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()));
    }

    QPlainTextEdit *lines(const QString &placeholder, int rows = 3)
    {
        auto *edit = new QPlainTextEdit;
        edit->setPlaceholderText(placeholder);
        edit->setMaximumHeight(edit->fontMetrics().height() * rows + 10);
        return edit;
    }

    // "a, b, c" <-> ["a", "b", "c"].
    QJsonArray commaList(const QString &text)
    {
        QJsonArray list;
        for (const QString &item : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            if (const QString t = item.trimmed(); !t.isEmpty())
            {
                list.append(t);
            }
        }
        return list;
    }

    QString joinList(const QJsonValue &value)
    {
        QStringList items;
        for (const QJsonValue &v : value.toArray())
        {
            items << v.toString();
        }
        return items.join(QStringLiteral(", "));
    }

    QStringList nonEmptyLines(const QString &text)
    {
        QStringList out;
        for (const QString &line : text.split(QLatin1Char('\n')))
        {
            if (!line.trimmed().isEmpty())
            {
                out << line.trimmed();
            }
        }
        return out;
    }

    // A list of steps that is a single change for every level
    // ([{"change": 2}]), as a number; nullopt otherwise.
    std::optional<int> singleChange(const QJsonValue &steps)
    {
        const QJsonArray list = steps.toArray();
        if (list.size() != 1)
        {
            return std::nullopt;
        }
        const QJsonObject step = list.first().toObject();
        if (step.size() != 1 || !step.contains(QStringLiteral("change")))
        {
            return std::nullopt;
        }
        return step.value(QStringLiteral("change")).toInt();
    }

    QJsonArray stepsOf(int change)
    {
        return QJsonArray{QJsonObject{{QStringLiteral("change"), change}}};
    }

    // A typed entry ("fire" resistance, "fly" Speed) as one line: "fire 5",
    // "fire {=max(1, level / 2)}", "physical 5 (except silver)", or "strigoi
    // weaknesses" without an amount; nullopt if it can't be one line.
    std::optional<QString> typedLine(const QJsonValue &value)
    {
        if (value.isString())
        {
            return value.toString(); // an HP ability without an amount
        }
        const QJsonObject o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
        {
            if (!QStringList{"type", "steps", "expr", "suffix"}.contains(it.key()))
            {
                return std::nullopt;
            }
        }
        QString line = o.value(QStringLiteral("type")).toString();
        if (o.contains(QStringLiteral("expr")))
        {
            line += QStringLiteral(" {=%1}").arg(o.value(QStringLiteral("expr")).toString());
        }
        else if (o.contains(QStringLiteral("steps")))
        {
            const std::optional<int> change = singleChange(o.value(QStringLiteral("steps")));
            if (!change)
            {
                return std::nullopt;
            }
            line += QLatin1Char(' ') + QString::number(*change);
        }
        if (const QString suffix = o.value(QStringLiteral("suffix")).toString(); !suffix.isEmpty())
        {
            line += QLatin1Char(' ') + suffix;
        }
        return line;
    }

    // The other way: a line to a typed entry; `amountNeeded` (Speeds):
    // nullopt without one.
    std::optional<QJsonObject> typedFromLine(const QString &line, bool amountNeeded)
    {
        static const QRegularExpression parts(QStringLiteral(R"(^(.+?)\s+(\d+|\{=[^{}]*\})(?:\s+(.*))?$)"));
        const QRegularExpressionMatch match = parts.match(line.trimmed());
        if (!match.hasMatch())
        {
            if (amountNeeded)
            {
                return std::nullopt;
            }
            return QJsonObject{{QStringLiteral("type"), line.trimmed()}};
        }
        QJsonObject o{{QStringLiteral("type"), match.captured(1).trimmed()}};
        const QString amount = match.captured(2);
        if (amount.startsWith(QLatin1Char('{')))
        {
            o.insert(QStringLiteral("expr"), amount.mid(2, amount.size() - 3).trimmed());
        }
        else
        {
            o.insert(QStringLiteral("steps"), stepsOf(amount.toInt()));
        }
        if (!match.captured(3).trimmed().isEmpty())
        {
            o.insert(QStringLiteral("suffix"), match.captured(3).trimmed());
        }
        return o;
    }

    bool linesShow(const QJsonValue &value)
    {
        for (const QJsonValue &v : value.toArray())
        {
            if (!typedLine(v))
            {
                return false;
            }
        }
        return true;
    }

    QString linesOf(const QJsonValue &value)
    {
        QStringList out;
        for (const QJsonValue &v : value.toArray())
        {
            out << typedLine(v).value_or(QString());
        }
        return out.join(QLatin1Char('\n'));
    }

    // "Storm Lord" -> "storm-lord".
    QString slug(const QString &text)
    {
        QString s = text.trimmed().toLower();
        s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
        return s;
    }

    QComboBox *combo(const QList<std::pair<QString, QString>> &items)
    {
        auto *box = new QComboBox;
        for (const auto &[data, label] : items)
        {
            box->addItem(label, data);
        }
        return box;
    }

    const QStringList &skillKeys()
    {
        static const QStringList keys{"acrobatics", "arcana",     "athletics", "crafting", "deception", "diplomacy",
                                      "intimidation", "medicine", "nature",    "occultism", "performance",
                                      "religion",   "society",    "stealth",   "survival", "thievery"};
        return keys;
    }

    // The keys of "changes" the form shows.
    const QSet<QString> &formChangeKeys()
    {
        static const QSet<QString> keys{"level",     "bonus",        "bonusSkip",      "strikeDamage", "hp",
                                        "spellBonus", "traits",      "removeTraits",   "replaceOrAdd", "rarity",
                                        "senses",    "languages",    "languagesAlways", "languageExtras", "landSpeed", "speeds",
                                        "size",      "immunities",   "resistances",    "weaknesses",   "hpAbilities",
                                        "saveNote",  "items",        "skillBenchmarks", "lores"};
        return keys;
    }

    const QSet<QString> &formTopKeys()
    {
        static const QSet<QString> keys{"format", "kind",   "id",       "name",     "group",   "prefix",
                                        "rules",  "optional", "appliesTo", "source", "dialog", "changes",
                                        "abilities", "strikes", "choices"};
        return keys;
    }

    const QSet<QString> &strikeFormKeys()
    {
        static const QSet<QString> keys{"key",    "name",   "type",       "from",  "attack", "damage",
                                        "traits", "die",    "damageType", "extra", "mergeExtra", "when"};
        return keys;
    }
} // namespace

// PairListEditor.

PairListEditor::PairListEditor(const QString &fromPlaceholder, const QString &toPlaceholder,
                               const QString &addLabel, QWidget *parent)
    : QWidget(parent), fromPlaceholder_(fromPlaceholder), toPlaceholder_(toPlaceholder)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    rows_ = new QVBoxLayout;
    rows_->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(rows_);
    auto *add = new QPushButton(addLabel);
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(add);
    addRow->addStretch();
    layout->addLayout(addRow);
    connect(add, &QPushButton::clicked, this, [this] {
        this->addRow(QString(), QString());
        edits_.last().first->setFocus();
    });
}

void PairListEditor::addRow(const QString &from, const QString &to)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *fromEdit = new QLineEdit(from);
    fromEdit->setPlaceholderText(fromPlaceholder_);
    if (!fromSuggestions.isEmpty())
    {
        auto *completer = new QCompleter(fromSuggestions, fromEdit);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        fromEdit->setCompleter(completer);
    }
    auto *toEdit = new QLineEdit(to);
    toEdit->setPlaceholderText(toPlaceholder_);
    auto *remove = new QPushButton(QObject::tr("Remove"));
    layout->addWidget(fromEdit, 1);
    layout->addWidget(new QLabel(QStringLiteral("\u2192")));
    layout->addWidget(toEdit, 1);
    layout->addWidget(remove);
    rows_->addWidget(row);
    edits_.push_back({fromEdit, toEdit});
    for (QLineEdit *edit : {fromEdit, toEdit})
    {
        connect(edit, &QLineEdit::textEdited, this, [this] {
            if (onEdited)
            {
                onEdited();
            }
        });
    }
    connect(remove, &QPushButton::clicked, this, [this, row, fromEdit] {
        edits_.removeIf([fromEdit](const auto &pair) { return pair.first == fromEdit; });
        row->deleteLater();
        if (onEdited)
        {
            onEdited();
        }
    });
}

void PairListEditor::setPairs(const QJsonObject &pairs)
{
    for (const auto &[from, to] : edits_)
    {
        from->parentWidget()->deleteLater();
    }
    edits_.clear();
    for (auto it = pairs.begin(); it != pairs.end(); ++it)
    {
        addRow(it.key(), it.value().toString());
    }
}

QJsonObject PairListEditor::pairs() const
{
    QJsonObject out;
    for (const auto &[from, to] : edits_)
    {
        if (!from->text().trimmed().isEmpty() && !to->text().trimmed().isEmpty())
        {
            out.insert(from->text().trimmed(), to->text().trimmed());
        }
    }
    return out;
}

int PairListEditor::incompleteRows() const
{
    int count = 0;
    for (const auto &[from, to] : edits_)
    {
        count += from->text().trimmed().isEmpty() != to->text().trimmed().isEmpty() ? 1 : 0;
    }
    return count;
}

// ObjectListEditor.

ObjectListEditor::ObjectListEditor(const QString &noun, std::function<QString(const QJsonObject &)> label,
                                   QWidget *parent)
    : QWidget(parent), noun_(noun), label_(std::move(label))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *left = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setMaximumWidth(fontMetrics().averageCharWidth() * 28);
    list_->setMaximumHeight(fontMetrics().height() * 9);
    left->addWidget(list_);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QObject::tr("Add"));
    auto *remove = new QPushButton(QObject::tr("Remove"));
    add->setToolTip(QObject::tr("Add a new %1").arg(noun));
    remove->setToolTip(QObject::tr("Remove the %1 picked").arg(noun));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    left->addLayout(buttons);
    left->addStretch();
    layout->addLayout(left);
    detail_ = new QWidget;
    fields_ = new QFormLayout(detail_);
    fields_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(detail_, 1);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { showCurrent(); });
    connect(add, &QPushButton::clicked, this, [this] {
        items_.append(newItem ? newItem() : QJsonObject());
        refreshLabels();
        list_->setCurrentRow(static_cast<int>(items_.size()) - 1);
        if (onEdited)
        {
            onEdited();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= items_.size())
        {
            return;
        }
        items_.removeAt(row);
        refreshLabels();
        list_->setCurrentRow(std::min(row, static_cast<int>(items_.size()) - 1));
        showCurrent();
        if (onEdited)
        {
            onEdited();
        }
    });
    detail_->setEnabled(false);
}

void ObjectListEditor::bind(const QString &key, QLineEdit *edit)
{
    bindCustom(
        edit, [edit, key](const QJsonObject &o) { edit->setText(o.value(key).toString()); },
        [edit, key](QJsonObject &o) {
            if (edit->text().trimmed().isEmpty())
            {
                o.remove(key);
            }
            else
            {
                o.insert(key, edit->text().trimmed());
            }
        });
    connect(edit, &QLineEdit::textEdited, this, [this] { fieldEdited(); });
}

void ObjectListEditor::bind(const QString &key, QPlainTextEdit *edit)
{
    bindCustom(
        edit, [edit, key](const QJsonObject &o) { edit->setPlainText(o.value(key).toString()); },
        [edit, key](QJsonObject &o) {
            if (edit->toPlainText().trimmed().isEmpty())
            {
                o.remove(key);
            }
            else
            {
                o.insert(key, edit->toPlainText().trimmed());
            }
        });
    connect(edit, &QPlainTextEdit::textChanged, this, [this] { fieldEdited(); });
}

void ObjectListEditor::bind(const QString &key, QComboBox *box)
{
    bindCustom(
        box,
        [box, key](const QJsonObject &o) {
            const int at = box->findData(o.value(key).toString());
            if (at >= 0)
            {
                box->setCurrentIndex(at);
            }
            else if (box->isEditable())
            {
                box->setEditText(o.value(key).toString());
            }
            else
            {
                box->setCurrentIndex(0);
            }
        },
        [box, key](QJsonObject &o) {
            const QString value = box->isEditable() && box->currentIndex() < 0 ? box->currentText().trimmed()
                                                                                : box->currentData().toString();
            if (value.isEmpty())
            {
                o.remove(key);
            }
            else
            {
                o.insert(key, value);
            }
        });
    connect(box, &QComboBox::activated, this, [this] { fieldEdited(); });
}

void ObjectListEditor::bindCustom(QWidget *field, std::function<void(const QJsonObject &)> fill,
                                  std::function<void(QJsonObject &)> read)
{
    bindings_.push_back({QString(), field, std::move(fill), std::move(read)});
}

void ObjectListEditor::setItems(const QJsonArray &items)
{
    items_ = items;
    refreshLabels();
    list_->setCurrentRow(items_.isEmpty() ? -1 : 0);
    showCurrent();
}

void ObjectListEditor::showCurrent()
{
    const int row = list_->currentRow();
    detail_->setEnabled(row >= 0 && row < items_.size());
    filling_ = true;
    const QJsonObject current = row >= 0 && row < items_.size() ? items_[row].toObject() : QJsonObject();
    for (const Binding &b : bindings_)
    {
        const QSignalBlocker block(b.field);
        b.fill(current);
    }
    filling_ = false;
}

void ObjectListEditor::fieldEdited()
{
    const int row = list_->currentRow();
    if (filling_ || row < 0 || row >= items_.size())
    {
        return;
    }
    QJsonObject current = items_[row].toObject();
    for (const Binding &b : bindings_)
    {
        b.read(current);
    }
    items_[row] = current;
    list_->item(row)->setText(label_(current));
    if (onEdited)
    {
        onEdited();
    }
}

void ObjectListEditor::refreshLabels()
{
    const QSignalBlocker block(list_);
    const int row = list_->currentRow();
    list_->clear();
    for (const QJsonValue &v : items_)
    {
        list_->addItem(label_(v.toObject()));
    }
    list_->setCurrentRow(std::min(row, static_cast<int>(items_.size()) - 1));
}

// AdjustmentEditor.

AdjustmentEditor::AdjustmentEditor(QWidget *parent) : HomebrewEditor(parent)
{
    setUp(tr("Homebrew Adjustments and Templates"));
}

QSet<QString> AdjustmentEditor::formKeys() const
{
    return formTopKeys();
}

void AdjustmentEditor::addRow(QFormLayout *form, const QString &label, QWidget *field, const QString &key,
                              bool inChanges, const QString &tip)
{
    form->addRow(label, field);
    if (!tip.isEmpty())
    {
        field->setToolTip(tip);
        field->setProperty("plainTip", tip);
    }
    fieldOf_.insert(inChanges ? key : QStringLiteral("/") + key, field);
}

void AdjustmentEditor::buildForm(QFormLayout *form)
{
    auto edited = [this] { formEdited(); };
    auto watch = [&](QWidget *w) {
        if (auto *e = qobject_cast<QLineEdit *>(w))
        {
            connect(e, &QLineEdit::textEdited, this, edited);
        }
        else if (auto *p = qobject_cast<QPlainTextEdit *>(w))
        {
            connect(p, &QPlainTextEdit::textChanged, this, edited);
        }
        else if (auto *c = qobject_cast<QComboBox *>(w))
        {
            connect(c, &QComboBox::activated, this, edited);
            if (c->isEditable())
            {
                connect(c, &QComboBox::editTextChanged, this, edited);
            }
        }
        else if (auto *s = qobject_cast<QSpinBox *>(w))
        {
            connect(s, &QSpinBox::valueChanged, this, edited);
        }
        else if (auto *b = qobject_cast<QCheckBox *>(w))
        {
            connect(b, &QCheckBox::toggled, this, edited);
        }
    };
    auto spin = [](int min, int max, const QString &none = QString()) {
        auto *box = new QSpinBox;
        box->setRange(min, max);
        if (!none.isEmpty())
        {
            box->setSpecialValueText(none);
        }
        box->setMaximumWidth(box->fontMetrics().averageCharWidth() * 14);
        return box;
    };

    form->addRow(hint(tr("An adjustment changes a creature (Elite, an ancestry); a template remakes it (a "
                         "vampire). The statblock shows the open monster with this applied while you edit. "
                         "Empty fields change nothing.")));

    // General.
    form->addRow(heading(tr("General")));
    kindCombo_ = combo({{"adjustment", tr("Adjustment (Creature or Hazard > Adjustments)")},
                        {"template", tr("Template (Creature or Hazard > Templates)")}});
    addRow(form, tr("Kind"), kindCombo_, QStringLiteral("kind"), false);
    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("Stonekin"));
    addRow(form, tr("Name"), name_, QStringLiteral("name"), false, tr("What the menu shows"));
    group_ = new QComboBox;
    group_->setEditable(true);
    addRow(form, tr("Submenu"), group_, QStringLiteral("group"), false,
           tr("The submenu it goes in (\"Ancestry\", \"Undead\"; \"Undead/Vampire\" for one inside another); "
              "empty: the menu itself"));
    prefix_ = new QLineEdit;
    prefix_->setPlaceholderText(tr("optional, e.g. Stonekin (put before the monster's name)"));
    addRow(form, tr("Name prefix"), prefix_, QStringLiteral("prefix"), false);
    rules_ = lines(tr("what it does, in a line or two (shown in the status bar)"));
    addRow(form, tr("Description"), rules_, QStringLiteral("rules"), false);
    optional_ = lines(tr("optional: what the GM does by hand (a grey note under it in the form)"));
    addRow(form, tr("Grey note"), optional_, QStringLiteral("optional"), false);
    minLevel_ = spin(-2, 25, tr("any"));
    maxLevel_ = spin(-2, 25, tr("any"));
    auto *levels = new QWidget;
    auto *levelsLayout = new QHBoxLayout(levels);
    levelsLayout->setContentsMargins(0, 0, 0, 0);
    levelsLayout->addWidget(new QLabel(tr("from level")));
    levelsLayout->addWidget(minLevel_);
    levelsLayout->addWidget(new QLabel(tr("to level")));
    levelsLayout->addWidget(maxLevel_);
    levelsLayout->addStretch();
    addRow(form, tr("For creatures"), levels, QStringLiteral("appliesTo"), false,
           tr("Outside these levels it changes nothing (and the form says so)"));
    id_ = new QLabel;
    id_->setEnabled(false);
    form->addRow(tr("Id"), id_);
    source_ = new QLineEdit;
    source_->setPlaceholderText(tr("for you: where it comes from"));
    addRow(form, tr("Source"), source_, QStringLiteral("source"), false);

    // Numbers.
    form->addRow(heading(tr("Numbers")));
    level_ = spin(-5, 10);
    addRow(form, tr("Level change"), level_, QStringLiteral("level"));
    bonus_ = spin(-10, 10);
    skipPerception_ = new QCheckBox(tr("not Perception"));
    skipDcs_ = new QCheckBox(tr("not DCs"));
    auto *bonusRow = new QWidget;
    auto *bonusLayout = new QHBoxLayout(bonusRow);
    bonusLayout->setContentsMargins(0, 0, 0, 0);
    bonusLayout->addWidget(bonus_);
    bonusLayout->addWidget(skipPerception_);
    bonusLayout->addWidget(skipDcs_);
    bonusLayout->addStretch();
    addRow(form, tr("Bonus"), bonusRow, QStringLiteral("bonus"), true,
           tr("Added to AC, attacks, DCs, saves, Perception and skills"));
    fieldOf_.insert(QStringLiteral("bonusSkip"), bonusRow);
    strikeDamage_ = spin(-10, 20);
    addRow(form, tr("Strike damage"), strikeDamage_, QStringLiteral("strikeDamage"));
    hp_ = spin(-500, 500);
    addRow(form, tr("HP change"), hp_, QStringLiteral("hp"));
    spellBonus_ = spin(-10, 10);
    addRow(form, tr("Spell DC and attack"), spellBonus_, QStringLiteral("spellBonus"));

    // Traits.
    form->addRow(heading(tr("Traits")));
    traits_ = new QLineEdit;
    traits_->setPlaceholderText(tr("stonekin, earth (commas between)"));
    addRow(form, tr("Add traits"), traits_, QStringLiteral("traits"));
    removeTraits_ = new QLineEdit;
    removeTraits_->setPlaceholderText(tr("traits it loses"));
    addRow(form, tr("Remove traits"), removeTraits_, QStringLiteral("removeTraits"));
    replaceOrAdd_ = new PairListEditor(tr("the trait it has (human), or any ancestry"),
                                       tr("the trait it becomes, e.g. stonekin"), tr("Add a trait to replace"));
    replaceOrAdd_->fromSuggestions = {monster::kAnyAncestry};
    replaceOrAdd_->onEdited = edited;
    addRow(form, tr("Replace traits"), replaceOrAdd_, QStringLiteral("replaceOrAdd"), true,
           tr("A trait the creature has becomes another; if it doesn't have it, the other is added. "
              "\"any ancestry\" replaces every ancestry trait it has (human, elf, your own ancestries)"));
    rarity_ = combo({{"", tr("unchanged")}, {"uncommon", tr("at least uncommon")}, {"rare", tr("at least rare")},
                     {"unique", tr("unique")}, {"raise", tr("one step rarer")}});
    addRow(form, tr("Rarity"), rarity_, QStringLiteral("rarity"));

    // Senses and languages.
    form->addRow(heading(tr("Senses and Languages")));
    senses_ = new QLineEdit;
    senses_->setPlaceholderText(tr("darkvision, tremorsense (imprecise) 30 feet"));
    addRow(form, tr("Senses"), senses_, QStringLiteral("senses"));
    languages_ = new QLineEdit;
    languages_->setPlaceholderText(tr("Petran"));
    languagesAlways_ = new QCheckBox(tr("also to a creature with no languages"));
    auto *languageRow = new QWidget;
    auto *languageLayout = new QHBoxLayout(languageRow);
    languageLayout->setContentsMargins(0, 0, 0, 0);
    languageLayout->addWidget(languages_, 1);
    languageLayout->addWidget(languagesAlways_);
    addRow(form, tr("Languages"), languageRow, QStringLiteral("languages"));
    fieldOf_.insert(QStringLiteral("languagesAlways"), languagesAlways_);
    languageExtras_ = new QLineEdit;
    languageExtras_->setPlaceholderText(tr("telepathy 100 feet"));
    addRow(form, tr("After the languages"), languageExtras_, QStringLiteral("languageExtras"), true,
           tr("After the semicolon in its Languages line (\"Aklo; telepathy 100 feet\"), even if it has no "
              "languages; one it has with a smaller amount is raised"));

    // Speeds and size.
    form->addRow(heading(tr("Speeds and Size")));
    landMode_ = combo({{"", tr("unchanged")}, {"set", tr("set to")}, {"add", tr("raised by")},
                       {"max", tr("at most")}, {"min", tr("at least")}});
    landFeet_ = spin(0, 200);
    landFeet_->setSingleStep(5);
    landFeet_->setSuffix(tr(" feet"));
    auto *landRow = new QWidget;
    auto *landLayout = new QHBoxLayout(landRow);
    landLayout->setContentsMargins(0, 0, 0, 0);
    landLayout->addWidget(landMode_);
    landLayout->addWidget(landFeet_);
    landLayout->addStretch();
    addRow(form, tr("Land Speed"), landRow, QStringLiteral("landSpeed"));
    speeds_ = lines(tr("fly 30 (one per line: a Speed it gains, or raises to this)"), 2);
    addRow(form, tr("Other Speeds"), speeds_, QStringLiteral("speeds"));
    size_ = combo({{"", tr("unchanged")}, {"tiny", tr("Tiny")}, {"small", tr("Small")}, {"medium", tr("Medium")},
                   {"large", tr("Large")}, {"huge", tr("Huge")}, {"gargantuan", tr("Gargantuan")}});
    addRow(form, tr("Size"), size_, QStringLiteral("size"));

    // Defenses.
    form->addRow(heading(tr("Defenses")));
    immunities_ = new QLineEdit;
    immunities_->setPlaceholderText(tr("fire, poison"));
    addRow(form, tr("Immunities"), immunities_, QStringLiteral("immunities"));
    resistances_ = lines(tr("fire 5 / physical 5 (except silver) / fire {=max(1, level / 2)}: one per line"), 3);
    addRow(form, tr("Resistances"), resistances_, QStringLiteral("resistances"));
    weaknesses_ = lines(tr("cold 5: one per line"), 2);
    addRow(form, tr("Weaknesses"), weaknesses_, QStringLiteral("weaknesses"));
    hpAbilities_ = lines(tr("fast healing 5 / void healing: one per line, after its HP"), 2);
    addRow(form, tr("HP abilities"), hpAbilities_, QStringLiteral("hpAbilities"));
    saveNote_ = new QLineEdit;
    saveNote_->setPlaceholderText(tr("+1 status to all saves vs. magic"));
    addRow(form, tr("Saves note"), saveNote_, QStringLiteral("saveNote"));

    // Items and skills.
    form->addRow(heading(tr("Items and Skills")));
    items_ = new QLineEdit;
    items_->setPlaceholderText(tr("clan dagger"));
    addRow(form, tr("Items"), items_, QStringLiteral("items"));
    skills_ = lines(tr("stealth high (one per line: a skill raised to that benchmark)"), 2);
    addRow(form, tr("Skills"), skills_, QStringLiteral("skillBenchmarks"));
    lores_ = lines(tr("Stonekin Lore high (one per line)"), 2);
    addRow(form, tr("Lore skills"), lores_, QStringLiteral("lores"));

    // Abilities.
    form->addRow(heading(tr("Abilities")));
    abilities_ = new ObjectListEditor(tr("ability"), [](const QJsonObject &o) {
        const QString name = o.value(QStringLiteral("name")).toString();
        return name.isEmpty() ? QObject::tr("(no name)") : name;
    });
    abilities_->newItem = [] { return QJsonObject{{"type", "offense"}, {"actions", "none"}}; };
    {
        auto *name = new QLineEdit;
        auto *type = combo({{"general", tr("General (top of the statblock)")},
                            {"defense", tr("Defense")},
                            {"offense", tr("Offense")}});
        auto *actions = combo({{"none", tr("none")}, {"one", tr("1 action")}, {"two", tr("2 actions")},
                               {"three", tr("3 actions")}, {"one to two", tr("1 to 2 actions")},
                               {"one to three", tr("1 to 3 actions")}, {"two to three", tr("2 to 3 actions")},
                               {"free", tr("free action")},
                               {"reaction", tr("reaction")}});
        auto *traits = new QLineEdit;
        traits->setPlaceholderText(tr("aura, fire"));
        auto *description = lines(tr("{Name} breathes fire in a 30-foot cone, dealing {=level / 2}d6 fire "
                                     "damage (DC {spelldc:high} basic Reflex save)."),
                                  5);
        auto *when = new QLineEdit;
        when->setPlaceholderText(tr("empty: always; abilities:rock-hide: only when that choice is picked"));
        QFormLayout *f = abilities_->fields();
        f->addRow(tr("Name"), name);
        f->addRow(tr("Where"), type);
        f->addRow(tr("Actions"), actions);
        f->addRow(tr("Traits"), traits);
        f->addRow(tr("Text"), description);
        f->addRow(hint(tr("{Name} / {name}: the creature; {level}; {=level / 2}: a number from its level; "
                          "{spelldc:high}, {dc:standard}, {skill:high}: from the tables at its level.")));
        f->addRow(tr("Only when"), when);
        abilities_->bind(QStringLiteral("name"), name);
        abilities_->bind(QStringLiteral("type"), type);
        abilities_->bind(QStringLiteral("actions"), actions);
        abilities_->bind(QStringLiteral("traits"), traits);
        abilities_->bind(QStringLiteral("description"), description);
        abilities_->bind(QStringLiteral("when"), when);
    }
    abilities_->onEdited = edited;
    form->addRow(abilities_);
    fieldOf_.insert(QStringLiteral("/abilities"), abilities_);

    // Strikes.
    form->addRow(heading(tr("Strikes")));
    strikes_ = new ObjectListEditor(tr("Strike"), [](const QJsonObject &o) {
        const QString name = o.value(QStringLiteral("name")).toString();
        return name.isEmpty() ? QObject::tr("(no name)") : name;
    });
    strikes_->newItem = [] { return QJsonObject{{"type", "Melee"}, {"from", "best"}}; };
    {
        auto *name = new QLineEdit;
        name->setPlaceholderText(tr("claw"));
        auto *type = combo({{"Melee", tr("Melee")}, {"Ranged", tr("Ranged")}});
        auto *from = combo({{"best", tr("copy its best Strike of that type")},
                            {"lowest", tr("copy its weakest Strike of that type")},
                            {"agile", tr("its agile Strike's damage (else three-quarters)")},
                            {"nonagile", tr("its non-agile Strike's damage")},
                            {"none", tr("the benchmarks below")}});
        auto *attack = combo({{"", tr("moderate")}, {"best", tr("its best melee attack")}, {"extreme", tr("extreme")},
                              {"high", tr("high")}, {"low", tr("low")}});
        auto *damage = combo({{"", tr("low")}, {"extreme", tr("extreme")}, {"high", tr("high")},
                              {"moderate", tr("moderate")}});
        auto *traits = new QLineEdit;
        traits->setPlaceholderText(tr("agile, finesse"));
        auto *die = combo({{"", tr("as copied")}, {"d4", tr("d4")}, {"d6", tr("d6")}, {"d8", tr("d8")},
                           {"d10", tr("d10")}, {"d12", tr("d12")}});
        auto *damageType = new QLineEdit;
        damageType->setPlaceholderText(tr("slashing"));
        auto *extra = new QLineEdit;
        extra->setPlaceholderText(tr("plus Grab"));
        auto *mergeExtra = new QCheckBox(tr("if it has a Strike of this name, add the traits and "
                                            "\"After the damage\" to it"));
        auto *when = new QLineEdit;
        when->setPlaceholderText(tr("empty: always; body:hands: only when that choice is picked"));
        QFormLayout *f = strikes_->fields();
        f->addRow(tr("Name"), name);
        f->addRow(tr("Type"), type);
        f->addRow(tr("Its numbers"), from);
        f->addRow(tr("Attack (benchmarks)"), attack);
        f->addRow(tr("Damage (benchmarks)"), damage);
        f->addRow(tr("Traits"), traits);
        f->addRow(tr("Damage die"), die);
        f->addRow(tr("Damage type"), damageType);
        f->addRow(tr("After the damage"), extra);
        f->addRow(tr("Already has it"), mergeExtra);
        f->addRow(tr("Only when"), when);
        strikes_->bind(QStringLiteral("name"), name);
        strikes_->bind(QStringLiteral("type"), type);
        strikes_->bind(QStringLiteral("from"), from);
        strikes_->bind(QStringLiteral("attack"), attack);
        strikes_->bind(QStringLiteral("damage"), damage);
        strikes_->bind(QStringLiteral("traits"), traits);
        strikes_->bind(QStringLiteral("damageType"), damageType);
        strikes_->bind(QStringLiteral("extra"), extra);
        strikes_->bind(QStringLiteral("die"), die);
        strikes_->bindCustom(
            mergeExtra, [mergeExtra](const QJsonObject &o) { mergeExtra->setChecked(o.value(QStringLiteral("mergeExtra")).toBool()); },
            [mergeExtra](QJsonObject &o) {
                if (mergeExtra->isChecked())
                {
                    o.insert(QStringLiteral("mergeExtra"), true);
                }
                else
                {
                    o.remove(QStringLiteral("mergeExtra"));
                }
            });
        connect(mergeExtra, &QCheckBox::toggled, strikes_, [this] { strikes_->fieldEdited(); });
        strikes_->bind(QStringLiteral("when"), when);
    }
    strikes_->onEdited = edited;
    form->addRow(strikes_);
    fieldOf_.insert(QStringLiteral("/strikes"), strikes_);

    // Choices.
    form->addRow(heading(tr("Choices")));
    form->addRow(hint(tr("With choices, applying it opens a dialog. An ability or Strike whose \"Only when\" "
                         "names a choice (\"abilities:rock-hide\": the item rock-hide of the choice "
                         "abilities; \"armored\": a checkbox choice) is made only when it's picked.")));
    choices_ = new ObjectListEditor(tr("choice"), [](const QJsonObject &o) {
        const QString label = o.value(QStringLiteral("label")).toString();
        return label.isEmpty() ? o.value(QStringLiteral("key")).toString() : label;
    });
    choices_->newItem = [] { return QJsonObject{{"type", "pick"}}; };
    {
        auto *key = new QLineEdit;
        key->setPlaceholderText(tr("abilities (what \"Only when\" names)"));
        auto *type = combo({{"pick", tr("several of (checkboxes)")}, {"choice", tr("one of (a list)")},
                            {"check", tr("a checkbox")}, {"text", tr("typed text")}});
        auto *label = new QLineEdit;
        label->setPlaceholderText(tr("Stonekin abilities"));
        auto *note = new QLineEdit;
        note->setPlaceholderText(tr("optional: a line under it"));
        auto *items = lines(tr("*rock-hide: Rock Hide\nroll: Roll (one per line; * ticked at first)"), 4);
        auto *def = new QLineEdit;
        def->setPlaceholderText(tr("a checkbox: true; typed text: its default"));
        QFormLayout *f = choices_->fields();
        f->addRow(tr("Key"), key);
        f->addRow(tr("Kind"), type);
        f->addRow(tr("Label"), label);
        f->addRow(tr("Note"), note);
        f->addRow(tr("Items"), items);
        f->addRow(tr("Default"), def);
        choices_->bind(QStringLiteral("key"), key);
        choices_->bind(QStringLiteral("type"), type);
        choices_->bind(QStringLiteral("label"), label);
        choices_->bind(QStringLiteral("note"), note);
        choices_->bindCustom(
            items,
            [items](const QJsonObject &o) {
                QStringList out;
                for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray())
                {
                    const QJsonObject item = v.toObject();
                    out << (item.value(QStringLiteral("on")).toBool() ? QStringLiteral("*") : QString()) +
                               item.value(QStringLiteral("key")).toString() + QStringLiteral(": ") +
                               item.value(QStringLiteral("label")).toString();
                }
                items->setPlainText(out.join(QLatin1Char('\n')));
            },
            [items](QJsonObject &o) {
                QJsonArray list;
                for (QString line : nonEmptyLines(items->toPlainText()))
                {
                    QJsonObject item;
                    if (line.startsWith(QLatin1Char('*')))
                    {
                        item.insert(QStringLiteral("on"), true);
                        line = line.mid(1).trimmed();
                    }
                    const qsizetype colon = line.indexOf(QLatin1Char(':'));
                    const QString label = colon < 0 ? line : line.mid(colon + 1).trimmed();
                    item.insert(QStringLiteral("key"), colon < 0 ? slug(line) : line.left(colon).trimmed());
                    item.insert(QStringLiteral("label"), label);
                    list.append(item);
                }
                if (list.isEmpty())
                {
                    o.remove(QStringLiteral("items"));
                }
                else
                {
                    o.insert(QStringLiteral("items"), list);
                }
            });
        choices_->bindCustom(
            def,
            [def](const QJsonObject &o) {
                const QJsonValue v = o.value(QStringLiteral("default"));
                def->setText(v.isBool() ? (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"))
                                        : v.toString());
            },
            [def](QJsonObject &o) {
                const QString text = def->text().trimmed();
                if (text.isEmpty())
                {
                    o.remove(QStringLiteral("default"));
                }
                else if (text == QLatin1String("true") || text == QLatin1String("false"))
                {
                    o.insert(QStringLiteral("default"), text == QLatin1String("true"));
                }
                else
                {
                    o.insert(QStringLiteral("default"), text);
                }
            });
        // The custom fields store through the list editor on each edit.
        connect(items, &QPlainTextEdit::textChanged, choices_, [this] { choices_->fieldEdited(); });
        connect(def, &QLineEdit::textEdited, choices_, [this] { choices_->fieldEdited(); });
    }
    choices_->onEdited = edited;
    form->addRow(choices_);
    fieldOf_.insert(QStringLiteral("/choices"), choices_);

    for (QWidget *w : {static_cast<QWidget *>(kindCombo_), static_cast<QWidget *>(name_),
                       static_cast<QWidget *>(group_), static_cast<QWidget *>(prefix_),
                       static_cast<QWidget *>(rules_), static_cast<QWidget *>(optional_),
                       static_cast<QWidget *>(minLevel_), static_cast<QWidget *>(maxLevel_),
                       static_cast<QWidget *>(source_), static_cast<QWidget *>(level_),
                       static_cast<QWidget *>(bonus_), static_cast<QWidget *>(skipPerception_),
                       static_cast<QWidget *>(skipDcs_), static_cast<QWidget *>(strikeDamage_),
                       static_cast<QWidget *>(hp_), static_cast<QWidget *>(spellBonus_),
                       static_cast<QWidget *>(traits_), static_cast<QWidget *>(removeTraits_),
                       static_cast<QWidget *>(rarity_),
                       static_cast<QWidget *>(senses_), static_cast<QWidget *>(languages_),
                       static_cast<QWidget *>(languagesAlways_), static_cast<QWidget *>(languageExtras_),
                       static_cast<QWidget *>(landMode_),
                       static_cast<QWidget *>(landFeet_), static_cast<QWidget *>(speeds_),
                       static_cast<QWidget *>(size_), static_cast<QWidget *>(immunities_),
                       static_cast<QWidget *>(resistances_), static_cast<QWidget *>(weaknesses_),
                       static_cast<QWidget *>(hpAbilities_), static_cast<QWidget *>(saveNote_),
                       static_cast<QWidget *>(items_), static_cast<QWidget *>(skills_),
                       static_cast<QWidget *>(lores_)})
    {
        watch(w);
    }
    connect(name_, &QLineEdit::textEdited, this, [this] {
        if (savedId_.isEmpty())
        {
            id_->setText(idFor().isEmpty() ? tr("made from the name when saved") : idFor());
        }
    });
    connect(kindCombo_, &QComboBox::activated, this, [this] {
        // The submenus of that kind, and the ones they are in ("Undead" for
        // "Undead/Vampire").
        const QString typed = group_->currentText();
        const QSignalBlocker block(group_);
        group_->clear();
        group_->addItem(QString());
        QStringList groups;
        for (const monster::Modifier &m : monster::modifiers())
        {
            if (m.kind != kindCombo_->currentData().toString())
            {
                continue;
            }
            for (QString path = m.group; !path.isEmpty(); path = path.section(QLatin1Char('/'), 0, -2))
            {
                if (!groups.contains(path))
                {
                    groups << path;
                }
            }
        }
        groups.sort(Qt::CaseInsensitive);
        group_->addItems(groups);
        group_->setEditText(typed);
    });
}

bool AdjustmentEditor::formShows(const QString &key, const QJsonValue &value, bool inChanges)
{
    if (!inChanges)
    {
        if (!formTopKeys().contains(key))
        {
            return false;
        }
        if (key == QLatin1String("dialog"))
        {
            return value.toString() == QLatin1String("options");
        }
        if (key == QLatin1String("strikes"))
        {
            for (const QJsonValue &v : value.toArray())
            {
                const QJsonObject o = v.toObject();
                for (auto it = o.begin(); it != o.end(); ++it)
                {
                    if (!strikeFormKeys().contains(it.key()))
                    {
                        return false;
                    }
                }
            }
        }
        if (key == QLatin1String("choices"))
        {
            static const QSet<QString> choiceKeys{"key", "type", "label", "note", "default", "items"};
            static const QSet<QString> itemKeys{"key", "label", "on"};
            for (const QJsonValue &v : value.toArray())
            {
                const QJsonObject o = v.toObject();
                for (auto it = o.begin(); it != o.end(); ++it)
                {
                    if (!choiceKeys.contains(it.key()))
                    {
                        return false;
                    }
                }
                for (const QJsonValue &item : o.value(QStringLiteral("items")).toArray())
                {
                    const QJsonObject i = item.toObject();
                    for (auto it = i.begin(); it != i.end(); ++it)
                    {
                        if (!itemKeys.contains(it.key()))
                        {
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }
    if (!formChangeKeys().contains(key))
    {
        return false;
    }
    if (key == QLatin1String("level") || key == QLatin1String("hp"))
    {
        return value.toArray().isEmpty() || singleChange(value).has_value();
    }
    if (key == QLatin1String("bonusSkip"))
    {
        for (const QJsonValue &v : value.toArray())
        {
            if (v.toString() != QLatin1String("perception") && v.toString() != QLatin1String("dcs"))
            {
                return false;
            }
        }
        return true;
    }
    if (key == QLatin1String("landSpeed"))
    {
        return value.toObject().size() <= 1;
    }
    if (key == QLatin1String("speeds") || key == QLatin1String("resistances") || key == QLatin1String("weaknesses") ||
        key == QLatin1String("hpAbilities"))
    {
        return linesShow(value);
    }
    if (key == QLatin1String("lores"))
    {
        for (const QJsonValue &v : value.toArray())
        {
            const QJsonObject o = v.toObject();
            for (auto it = o.begin(); it != o.end(); ++it)
            {
                if (it.key() != QLatin1String("name") && it.key() != QLatin1String("benchmark"))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

QJsonObject AdjustmentEditor::advancedPart(const QJsonObject &entry) const
{
    QJsonObject other;
    for (auto it = entry.begin(); it != entry.end(); ++it)
    {
        if (it.key() == QLatin1String("changes"))
        {
            continue;
        }
        if (!formShows(it.key(), it.value(), false))
        {
            other.insert(it.key(), it.value());
        }
    }
    QJsonObject changes;
    const QJsonObject all = entry.value(QStringLiteral("changes")).toObject();
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (!formShows(it.key(), it.value(), true))
        {
            changes.insert(it.key(), it.value());
        }
    }
    if (!changes.isEmpty())
    {
        other.insert(QStringLiteral("changes"), changes);
    }
    return other;
}

QJsonObject AdjustmentEditor::mergeAdvanced(QJsonObject entry, const QJsonObject &advanced) const
{
    for (auto it = advanced.begin(); it != advanced.end(); ++it)
    {
        if (it.key() == QLatin1String("changes"))
        {
            QJsonObject changes = entry.value(QStringLiteral("changes")).toObject();
            const QJsonObject more = it.value().toObject();
            for (auto c = more.begin(); c != more.end(); ++c)
            {
                if (!changes.contains(c.key()))
                {
                    changes.insert(c.key(), c.value());
                }
            }
            entry.insert(QStringLiteral("changes"), changes);
        }
        else if (!entry.contains(it.key()))
        {
            entry.insert(it.key(), it.value());
        }
    }
    return entry;
}

QStringList AdjustmentEditor::advancedProblems(const QJsonObject &advanced) const
{
    QJsonObject top = advanced;
    top.remove(QStringLiteral("changes"));
    QStringList problems = monster::unknownKeyMessages(top, monster::modifierTopKeys());
    for (const QString &p : monster::unknownKeyMessages(advanced.value(QStringLiteral("changes")).toObject(),
                                                        monster::modifierChangeKeys()))
    {
        problems << QStringLiteral("In \"changes\": ") + p;
    }
    return problems;
}

QString AdjustmentEditor::idFor() const
{
    return savedId_.isEmpty() ? slug(name_->text()) : savedId_;
}

void AdjustmentEditor::fillForm(const QString &name, const QJsonObject &entry)
{
    savedId_ = name;
    const QJsonObject changes = entry.value(QStringLiteral("changes")).toObject();
    // Which keys the form can't show (they're in the Advanced box).
    inAdvanced_.clear();
    for (auto it = entry.begin(); it != entry.end(); ++it)
    {
        if (it.key() != QLatin1String("changes") && !formShows(it.key(), it.value(), false))
        {
            inAdvanced_ << QStringLiteral("/") + it.key();
        }
    }
    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        if (!formShows(it.key(), it.value(), true))
        {
            inAdvanced_ << it.key();
        }
    }
    auto showsHere = [this](const QString &key) { return !inAdvanced_.contains(key); };

    kindCombo_->setCurrentIndex(entry.value(QStringLiteral("kind")).toString() == QLatin1String("template") ? 1 : 0);
    emit kindCombo_->activated(kindCombo_->currentIndex()); // the submenus of that kind
    name_->setText(entry.value(QStringLiteral("name")).toString());
    group_->setEditText(entry.value(QStringLiteral("group")).toString());
    prefix_->setText(entry.value(QStringLiteral("prefix")).toString());
    rules_->setPlainText(entry.value(QStringLiteral("rules")).toString());
    optional_->setPlainText(entry.value(QStringLiteral("optional")).toString());
    const QJsonObject applies = entry.value(QStringLiteral("appliesTo")).toObject();
    minLevel_->setValue(applies.contains(QStringLiteral("min")) ? applies.value(QStringLiteral("min")).toInt() : -2);
    maxLevel_->setValue(applies.contains(QStringLiteral("max")) ? applies.value(QStringLiteral("max")).toInt() : -2);
    source_->setText(entry.value(QStringLiteral("source")).toString());
    id_->setText(savedId_.isEmpty() ? tr("made from the name when saved") : savedId_);

    level_->setValue(showsHere("level") ? singleChange(changes.value(QStringLiteral("level"))).value_or(0) : 0);
    bonus_->setValue(changes.value(QStringLiteral("bonus")).toInt());
    const QJsonArray skip = changes.value(QStringLiteral("bonusSkip")).toArray();
    skipPerception_->setChecked(skip.contains(QStringLiteral("perception")));
    skipDcs_->setChecked(skip.contains(QStringLiteral("dcs")));
    strikeDamage_->setValue(changes.value(QStringLiteral("strikeDamage")).toInt());
    hp_->setValue(showsHere("hp") ? singleChange(changes.value(QStringLiteral("hp"))).value_or(0) : 0);
    spellBonus_->setValue(changes.value(QStringLiteral("spellBonus")).toInt());

    traits_->setText(joinList(changes.value(QStringLiteral("traits"))));
    removeTraits_->setText(joinList(changes.value(QStringLiteral("removeTraits"))));
    replaceOrAdd_->setPairs(changes.value(QStringLiteral("replaceOrAdd")).toObject());
    rarity_->setCurrentIndex(std::max(0, rarity_->findData(changes.value(QStringLiteral("rarity")).toString())));

    senses_->setText(joinList(changes.value(QStringLiteral("senses"))));
    languages_->setText(joinList(changes.value(QStringLiteral("languages"))));
    languagesAlways_->setChecked(changes.value(QStringLiteral("languagesAlways")).toBool());
    languageExtras_->setText(joinList(changes.value(QStringLiteral("languageExtras"))));
    const QJsonObject land = showsHere("landSpeed") ? changes.value(QStringLiteral("landSpeed")).toObject() : QJsonObject();
    landMode_->setCurrentIndex(land.isEmpty() ? 0 : std::max(0, landMode_->findData(land.begin().key())));
    landFeet_->setValue(land.isEmpty() ? 0 : land.begin().value().toInt());
    speeds_->setPlainText(showsHere("speeds") ? linesOf(changes.value(QStringLiteral("speeds"))) : QString());
    size_->setCurrentIndex(std::max(0, size_->findData(changes.value(QStringLiteral("size")).toString())));

    immunities_->setText(joinList(changes.value(QStringLiteral("immunities"))));
    resistances_->setPlainText(showsHere("resistances") ? linesOf(changes.value(QStringLiteral("resistances"))) : QString());
    weaknesses_->setPlainText(showsHere("weaknesses") ? linesOf(changes.value(QStringLiteral("weaknesses"))) : QString());
    hpAbilities_->setPlainText(showsHere("hpAbilities") ? linesOf(changes.value(QStringLiteral("hpAbilities"))) : QString());
    saveNote_->setText(changes.value(QStringLiteral("saveNote")).toString());

    items_->setText(joinList(changes.value(QStringLiteral("items"))));
    QStringList skillLines;
    const QJsonObject skills = changes.value(QStringLiteral("skillBenchmarks")).toObject();
    for (auto it = skills.begin(); it != skills.end(); ++it)
    {
        skillLines << QStringLiteral("%1 %2").arg(it.key(), it.value().toString());
    }
    skills_->setPlainText(skillLines.join(QLatin1Char('\n')));
    QStringList loreLines;
    if (showsHere("lores"))
    {
        for (const QJsonValue &v : changes.value(QStringLiteral("lores")).toArray())
        {
            const QJsonObject o = v.toObject();
            loreLines << QStringLiteral("%1 %2").arg(o.value(QStringLiteral("name")).toString(),
                                                     o.value(QStringLiteral("benchmark")).toString(QStringLiteral("high")));
        }
    }
    lores_->setPlainText(loreLines.join(QLatin1Char('\n')));

    abilities_->setItems(showsHere("/abilities") ? entry.value(QStringLiteral("abilities")).toArray() : QJsonArray());
    strikes_->setItems(showsHere("/strikes") ? entry.value(QStringLiteral("strikes")).toArray() : QJsonArray());
    choices_->setItems(showsHere("/choices") ? entry.value(QStringLiteral("choices")).toArray() : QJsonArray());

    // Fields whose key is in the Advanced box: disabled, saying so.
    for (auto it = fieldOf_.begin(); it != fieldOf_.end(); ++it)
    {
        const bool advanced = inAdvanced_.contains(it.key());
        it.value()->setEnabled(!advanced);
        if (advanced)
        {
            it.value()->setToolTip(tr("This file sets it in a way the form can't show; it's in the Advanced "
                                      "box below, kept as written."));
        }
        else
        {
            it.value()->setToolTip(it.value()->property("plainTip").toString());
        }
    }
}

QJsonObject AdjustmentEditor::readForm(QString *name) const
{
    *name = idFor();
    QJsonObject entry{{QStringLiteral("kind"), kindCombo_->currentData().toString()},
                      {QStringLiteral("id"), *name},
                      {QStringLiteral("name"), name_->text().trimmed()}};
    auto text = [&](const char *key, const QString &value) {
        if (!value.trimmed().isEmpty())
        {
            entry.insert(QLatin1String(key), value.trimmed());
        }
    };
    text("group", monster::groupPath(group_->currentText()));
    text("prefix", prefix_->text());
    text("rules", rules_->toPlainText());
    text("optional", optional_->toPlainText());
    text("source", source_->text());
    QJsonObject applies;
    if (minLevel_->value() > minLevel_->minimum())
    {
        applies.insert(QStringLiteral("min"), minLevel_->value());
    }
    if (maxLevel_->value() > maxLevel_->minimum())
    {
        applies.insert(QStringLiteral("max"), maxLevel_->value());
    }
    if (!applies.isEmpty())
    {
        entry.insert(QStringLiteral("appliesTo"), applies);
    }

    QJsonObject changes;
    auto shows = [this](const QString &key) { return !inAdvanced_.contains(key); };
    auto number = [&](const char *key, int value) {
        if (value != 0 && shows(QLatin1String(key)))
        {
            changes.insert(QLatin1String(key), value);
        }
    };
    auto list = [&](const char *key, const QString &value) {
        const QJsonArray items = commaList(value);
        if (!items.isEmpty() && shows(QLatin1String(key)))
        {
            changes.insert(QLatin1String(key), items);
        }
    };
    if (level_->value() != 0 && shows(QStringLiteral("level")))
    {
        changes.insert(QStringLiteral("level"), stepsOf(level_->value()));
    }
    number("bonus", bonus_->value());
    QJsonArray skip;
    if (skipPerception_->isChecked())
    {
        skip.append(QStringLiteral("perception"));
    }
    if (skipDcs_->isChecked())
    {
        skip.append(QStringLiteral("dcs"));
    }
    if (!skip.isEmpty() && shows(QStringLiteral("bonusSkip")))
    {
        changes.insert(QStringLiteral("bonusSkip"), skip);
    }
    number("strikeDamage", strikeDamage_->value());
    if (hp_->value() != 0 && shows(QStringLiteral("hp")))
    {
        changes.insert(QStringLiteral("hp"), stepsOf(hp_->value()));
    }
    number("spellBonus", spellBonus_->value());

    list("traits", traits_->text());
    list("removeTraits", removeTraits_->text());
    const QJsonObject replace = replaceOrAdd_->pairs();
    if (!replace.isEmpty() && shows(QStringLiteral("replaceOrAdd")))
    {
        changes.insert(QStringLiteral("replaceOrAdd"), replace);
    }
    if (!rarity_->currentData().toString().isEmpty() && shows(QStringLiteral("rarity")))
    {
        changes.insert(QStringLiteral("rarity"), rarity_->currentData().toString());
    }

    list("senses", senses_->text());
    list("languages", languages_->text());
    if (languagesAlways_->isChecked() && shows(QStringLiteral("languagesAlways")))
    {
        changes.insert(QStringLiteral("languagesAlways"), true);
    }
    list("languageExtras", languageExtras_->text());
    if (!landMode_->currentData().toString().isEmpty() && shows(QStringLiteral("landSpeed")))
    {
        changes.insert(QStringLiteral("landSpeed"),
                       QJsonObject{{landMode_->currentData().toString(), landFeet_->value()}});
    }
    auto typedList = [&](const char *key, QPlainTextEdit *edit, bool amountNeeded) {
        QJsonArray items;
        for (const QString &line : nonEmptyLines(edit->toPlainText()))
        {
            if (const std::optional<QJsonObject> o = typedFromLine(line, amountNeeded))
            {
                // An HP ability without an amount is plain text.
                if (QLatin1String(key) == QLatin1String("hpAbilities") && o->size() == 1)
                {
                    items.append(o->value(QStringLiteral("type")));
                }
                else
                {
                    items.append(*o);
                }
            }
        }
        if (!items.isEmpty() && shows(QLatin1String(key)))
        {
            changes.insert(QLatin1String(key), items);
        }
    };
    typedList("speeds", speeds_, true);
    if (!size_->currentData().toString().isEmpty() && shows(QStringLiteral("size")))
    {
        changes.insert(QStringLiteral("size"), size_->currentData().toString());
    }

    list("immunities", immunities_->text());
    typedList("resistances", resistances_, false);
    typedList("weaknesses", weaknesses_, false);
    typedList("hpAbilities", hpAbilities_, false);
    if (!saveNote_->text().trimmed().isEmpty() && shows(QStringLiteral("saveNote")))
    {
        changes.insert(QStringLiteral("saveNote"), saveNote_->text().trimmed());
    }

    list("items", items_->text());
    QJsonObject skills;
    for (const QString &line : nonEmptyLines(skills_->toPlainText()))
    {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() == 2)
        {
            skills.insert(parts[0].toLower(), parts[1].toLower());
        }
    }
    if (!skills.isEmpty() && shows(QStringLiteral("skillBenchmarks")))
    {
        changes.insert(QStringLiteral("skillBenchmarks"), skills);
    }
    QJsonArray lores;
    for (const QString &line : nonEmptyLines(lores_->toPlainText()))
    {
        const qsizetype space = line.lastIndexOf(QLatin1Char(' '));
        if (space > 0)
        {
            lores.append(QJsonObject{{QStringLiteral("name"), line.left(space).trimmed()},
                                     {QStringLiteral("benchmark"), line.mid(space + 1).trimmed().toLower()}});
        }
    }
    if (!lores.isEmpty() && shows(QStringLiteral("lores")))
    {
        changes.insert(QStringLiteral("lores"), lores);
    }
    if (!changes.isEmpty())
    {
        entry.insert(QStringLiteral("changes"), changes);
    }

    // Abilities and Strikes get a key from their name if they have none.
    auto withKeys = [](QJsonArray items) {
        QStringList used;
        for (qsizetype i = 0; i < items.size(); ++i)
        {
            QJsonObject o = items[i].toObject();
            if (o.value(QStringLiteral("key")).toString().isEmpty())
            {
                QString key = slug(o.value(QStringLiteral("name")).toString());
                if (key.isEmpty())
                {
                    key = QStringLiteral("entry");
                }
                QString unique = key;
                for (int n = 2; used.contains(unique); ++n)
                {
                    unique = QStringLiteral("%1-%2").arg(key).arg(n);
                }
                o.insert(QStringLiteral("key"), unique);
                items[i] = o;
            }
            used << o.value(QStringLiteral("key")).toString();
        }
        return items;
    };
    if (shows(QStringLiteral("/abilities")) && !abilities_->items().isEmpty())
    {
        entry.insert(QStringLiteral("abilities"), withKeys(abilities_->items()));
    }
    if (shows(QStringLiteral("/strikes")) && !strikes_->items().isEmpty())
    {
        entry.insert(QStringLiteral("strikes"), withKeys(strikes_->items()));
    }
    if (shows(QStringLiteral("/choices")) && !choices_->items().isEmpty())
    {
        entry.insert(QStringLiteral("choices"), choices_->items());
    }
    // With choices (in the form or the Advanced box), the options dialog.
    if (entry.contains(QStringLiteral("choices")) || !shows(QStringLiteral("/choices")))
    {
        entry.insert(QStringLiteral("dialog"), QStringLiteral("options"));
    }
    return entry;
}

QList<HomebrewEditor::FieldProblem> AdjustmentEditor::checkForm() const
{
    QList<FieldProblem> problems;
    if (name_->text().trimmed().isEmpty())
    {
        problems.push_back({name_, tr("It needs a name (what the menu shows).")});
    }
    // Lines that can't be read.
    auto badLines = [&](QPlainTextEdit *edit, bool amountNeeded, const QString &example) {
        QStringList bad;
        for (const QString &line : nonEmptyLines(edit->toPlainText()))
        {
            if (!typedFromLine(line, amountNeeded))
            {
                bad << line;
            }
        }
        if (!bad.isEmpty())
        {
            problems.push_back({edit, tr("These lines need a type and an amount, like \"%1\": %2")
                                          .arg(example, bad.join(QStringLiteral("; ")))});
        }
    };
    if (speeds_->isEnabled())
    {
        badLines(speeds_, true, QStringLiteral("fly 30"));
    }
    if (const int incomplete = replaceOrAdd_->incompleteRows(); incomplete > 0)
    {
        problems.push_back({replaceOrAdd_, tr("A trait to replace needs both boxes: the trait it has and the "
                                              "one it becomes (or remove the row).")});
    }
    QStringList badSkills;
    const QStringList skillBenchmarks{"extreme", "high", "moderate", "low", "terrible", "primary", "secondary"};
    for (const QString &line : nonEmptyLines(skills_->toPlainText()))
    {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 2 || !skillKeys().contains(parts[0].toLower()) ||
            !skillBenchmarks.contains(parts[1].toLower()))
        {
            badSkills << line;
        }
    }
    if (!badSkills.isEmpty())
    {
        problems.push_back({skills_, tr("Each line should be a skill and a benchmark, like \"stealth high\" "
                                        "(extreme, high, moderate, low, terrible): %1")
                                         .arg(badSkills.join(QStringLiteral("; ")))});
    }
    QStringList badLores;
    for (const QString &line : nonEmptyLines(lores_->toPlainText()))
    {
        if (!skillBenchmarks.contains(line.section(QLatin1Char(' '), -1).toLower()) ||
            !line.contains(QLatin1Char(' ')))
        {
            badLores << line;
        }
    }
    if (!badLores.isEmpty())
    {
        problems.push_back({lores_, tr("Each line should be a Lore and a benchmark, like \"Stonekin Lore high\": %1")
                                        .arg(badLores.join(QStringLiteral("; ")))});
    }
    // The lists' entries.
    QStringList choiceKeys;
    for (const QJsonValue &v : choices_->items())
    {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QStringLiteral("key")).toString();
        if (key.isEmpty())
        {
            problems.push_back({choices_, tr("A choice needs a key (what \"Only when\" names).")});
        }
        choiceKeys << key;
    }
    // Choices the form can't show are in the Advanced box.
    for (const QJsonValue &v : advanced().value(QStringLiteral("choices")).toArray())
    {
        choiceKeys << v.toObject().value(QStringLiteral("key")).toString();
    }
    auto checkList = [&](ObjectListEditor *editor, const QString &what) {
        for (const QJsonValue &v : editor->items())
        {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            {
                problems.push_back({editor, tr("Each %1 needs a name.").arg(what)});
            }
            const QString when = o.value(QStringLiteral("when")).toString();
            for (QString part : when.split(QLatin1Char('&'), Qt::SkipEmptyParts))
            {
                part = part.trimmed();
                if (part.startsWith(QLatin1Char('!')))
                {
                    part = part.mid(1);
                }
                const QString key = part.section(QLatin1Char(':'), 0, 0);
                if (!key.isEmpty() && !choiceKeys.contains(key))
                {
                    problems.push_back({editor,
                                        tr("\"%1\" (%2's Only when) names no choice of this one; it would "
                                           "never be made.")
                                            .arg(when, o.value(QStringLiteral("name")).toString()),
                                        false});
                }
            }
        }
    };
    if (abilities_->isEnabled())
    {
        checkList(abilities_, tr("ability"));
    }
    if (strikes_->isEnabled())
    {
        checkList(strikes_, tr("Strike"));
    }
    for (const monster::Modifier &m : monster::modifiers())
    {
        if (m.file.isEmpty() && m.name.compare(name_->text().trimmed(), Qt::CaseInsensitive) == 0 &&
            m.kind == kindCombo_->currentData().toString())
        {
            problems.push_back({name_, tr("A built-in one is called %1 too; both will be in the menu.").arg(m.name),
                                false});
        }
    }
    return problems;
}

QString AdjustmentEditor::entryLabel(const QString &name, const QJsonObject &entry) const
{
    const QString title = entry.value(QStringLiteral("name")).toString(name);
    return entry.value(QStringLiteral("kind")).toString() == QLatin1String("template") ? title + tr(" (template)")
                                                                                       : title;
}

QStringList AdjustmentEditor::builtInNames() const
{
    QStringList names;
    for (const monster::Modifier &m : monster::modifiers())
    {
        // Mythic has its own dialog, which the form can't edit.
        if (m.file.isEmpty() && m.dialog != QLatin1String("mythic"))
        {
            names << (m.kind == QLatin1String("template") ? m.name + tr(" (template)") : m.name);
        }
    }
    return names;
}

void AdjustmentEditor::fillFromBuiltIn(const QString &name)
{
    for (const monster::Modifier &m : monster::modifiers())
    {
        const QString shown = m.kind == QLatin1String("template") ? m.name + tr(" (template)") : m.name;
        if (!m.file.isEmpty() || shown != name)
        {
            continue;
        }
        // Its file, as written.
        QFile file(QStringLiteral(":/data/adjustments/%1.json").arg(m.id));
        if (!file.open(QIODevice::ReadOnly))
        {
            return;
        }
        QJsonObject entry = QJsonDocument::fromJson(file.readAll()).object();
        entry.remove(QStringLiteral("id"));
        entry.remove(QStringLiteral("order"));
        entry.remove(QStringLiteral("license"));
        entry.insert(QStringLiteral("name"), m.name + tr(" (homebrew)"));
        entry.insert(QStringLiteral("source"), tr("Copied from the built-in %1").arg(m.name));
        fillForm(QString(), entry);
        setAdvanced(advancedPart(entry));
        return;
    }
}

void AdjustmentEditor::entryChanged()
{
    QString name;
    const std::optional<QJsonObject> entry = currentEntry(&name);
    emit previewChanged(entry && !name_->text().trimmed().isEmpty() ? *entry : QJsonObject());
}
