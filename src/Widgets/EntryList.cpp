// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vorthas

#include "Widgets/EntryList.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
    const QString kMime = QStringLiteral("application/x-pf2monster-entry");

    // The handle an entry is dragged by.
    class Grip : public QLabel
    {
    public:
        Grip(int index, QWidget *parent = nullptr) : QLabel(QStringLiteral("☰"), parent), index_(index)
        {
            setAlignment(Qt::AlignCenter);
            setCursor(Qt::OpenHandCursor);
            setToolTip(QObject::tr("Drag to move"));
        }

    protected:
        void mousePressEvent(QMouseEvent *event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                pressed_ = event->position().toPoint();
            }
        }

        void mouseMoveEvent(QMouseEvent *event) override
        {
            if (!(event->buttons() & Qt::LeftButton) ||
                (event->position().toPoint() - pressed_).manhattanLength() <
                    QApplication::startDragDistance())
            {
                return;
            }
            auto *mime = new QMimeData;
            mime->setData(kMime, QByteArray::number(index_));
            auto *drag = new QDrag(this);
            drag->setMimeData(mime);
            // The entry itself, faded, under the pointer.
            if (QWidget *row = parentWidget())
            {
                QPixmap picture = row->grab();
                QPixmap faded(picture.size());
                faded.fill(Qt::transparent);
                {
                    QPainter painter(&faded);
                    painter.setOpacity(0.6);
                    painter.drawPixmap(0, 0, picture);
                }
                drag->setPixmap(faded);
                drag->setHotSpot(mapTo(row, event->position().toPoint()));
            }
            drag->exec(Qt::MoveAction);
        }

    private:
        int index_;
        QPoint pressed_;
    };

    QToolButton *controlButton(const QString &text, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        return button;
    }
} // namespace

EntryList::EntryList(QWidget *parent) : QWidget(parent)
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    setAcceptDrops(true);

    dropLine_ = new QFrame(this);
    dropLine_->setFrameShape(QFrame::HLine);
    dropLine_->setLineWidth(2);
    dropLine_->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    dropLine_->hide();
}

void EntryList::rebuild(int count)
{
    for (QWidget *widget : rows_ + separators_)
    {
        widget->deleteLater();
    }
    rows_.clear();
    separators_.clear();
    while (QLayoutItem *item = layout_->takeAt(0))
    {
        delete item;
    }

    for (int i = 0; i < count; ++i)
    {
        auto *row = new QFrame;
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 4, 0, 4);
        line->addWidget(factory_ ? factory_(i) : new QWidget, 1);

        // Grip and buttons in a column on the right.
        auto *controls = new QVBoxLayout;
        controls->setSpacing(0);
        QToolButton *remove = controlButton(QStringLiteral("✕"), deleteToolTip_);
        if (removable_ && !removable_(i))
        {
            // Keeps its room so the column lines up with the other rows.
            QSizePolicy keep = remove->sizePolicy();
            keep.setRetainSizeWhenHidden(true);
            remove->setSizePolicy(keep);
            remove->hide();
        }
        QToolButton *up = controlButton(QStringLiteral("▲"), tr("Move up"));
        QToolButton *down = controlButton(QStringLiteral("▼"), tr("Move down"));
        up->setEnabled(i > 0);
        down->setEnabled(i + 1 < count);
        controls->addWidget(remove);
        controls->addWidget(new Grip(i, row));
        controls->addWidget(up);
        controls->addWidget(down);
        controls->addStretch();
        line->addLayout(controls);

        connect(remove, &QToolButton::clicked, this, [this, i] { later([this, i] { emit removeRequested(i); }); });
        connect(up, &QToolButton::clicked, this, [this, i] { later([this, i] { emit moveRequested(i, i - 1); }); });
        connect(down, &QToolButton::clicked, this, [this, i] { later([this, i] { emit moveRequested(i, i + 1); }); });

        if (i > 0)
        {
            auto *separator = new QFrame;
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Sunken);
            layout_->addWidget(separator);
            separators_ << separator;
        }
        layout_->addWidget(row);
        rows_ << row;
    }
}

void EntryList::later(std::function<void()> request)
{
    QTimer::singleShot(0, this, std::move(request));
}

int EntryList::dropSlot(int y) const
{
    int slot = 0;
    for (QWidget *row : rows_)
    {
        if (y > row->geometry().center().y())
        {
            ++slot;
        }
    }
    return slot;
}

void EntryList::showDropLine(int slot)
{
    // Between entries: above entry `slot`, or below the last one.
    const QList<QWidget *> &entries = rows_;
    if (entries.isEmpty())
    {
        dropLine_->hide();
        return;
    }
    const int y = slot < entries.size() ? entries[slot]->geometry().top()
                                        : entries.last()->geometry().bottom();
    dropLine_->setGeometry(0, y - 1, width(), 3);
    dropLine_->raise();
    dropLine_->show();
}

void EntryList::dragEnterEvent(QDragEnterEvent *event)
{
    // Only entries of this list.
    if (event->mimeData()->hasFormat(kMime) && event->source() && isAncestorOf(qobject_cast<QWidget *>(event->source())))
    {
        event->acceptProposedAction();
    }
}

void EntryList::dragMoveEvent(QDragMoveEvent *event)
{
    showDropLine(dropSlot(event->position().toPoint().y()));
    event->acceptProposedAction();
}

void EntryList::dragLeaveEvent(QDragLeaveEvent *)
{
    dropLine_->hide();
}

void EntryList::dropEvent(QDropEvent *event)
{
    dropLine_->hide();
    const int from = event->mimeData()->data(kMime).toInt();
    const int slot = dropSlot(event->position().toPoint().y());
    // Taking the entry out first moves the places after it up by one.
    const int to = slot > from ? slot - 1 : slot;
    event->acceptProposedAction();
    if (to != from)
    {
        later([this, from, to] { emit moveRequested(from, to); });
    }
}
