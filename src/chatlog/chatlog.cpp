/*
    Copyright (C) 2014 by Project Tox <https://tox.im>

    This file is part of qTox, a Qt-based graphical interface for Tox.

    This program is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the COPYING file for more details.
*/

#include "chatlog.h"
#include "chatline.h"
#include "chatmessage.h"
#include "chatlinecontent.h"

#include <QDebug>
#include <QScrollBar>
#include <QApplication>
#include <QMenu>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>

template<class T>
T clamp(T x, T min, T max)
{
    if(x > max)
        return max;
    if(x < min)
        return min;
    return x;
}

ChatLog::ChatLog(QWidget* parent)
    : QGraphicsView(parent)
{
    scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(scene);

    setInteractive(true);
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    setViewportUpdateMode(SmartViewportUpdate);
    //setRenderHint(QPainter::TextAntialiasing);
    setAcceptDrops(false);

    const QColor selGraphColor = QColor(166,225,255);
    selGraphItem = scene->addRect(0,0,0,0,selGraphColor.darker(120),selGraphColor);
    selGraphItem->setZValue(-10.0); //behind all items

    // copy action
    copyAction = new QAction(this);
    copyAction->setShortcut(QKeySequence::Copy);
    addAction(copyAction);
    connect(copyAction, &QAction::triggered, this, [ = ](bool)
    {
        copySelectedText();
    });
}

ChatLog::~ChatLog()
{
    for(ChatLine* line : lines)
        delete line;
}

ChatMessage* ChatLog::addChatMessage(const QString& sender, const QString &msg, bool self, bool alert)
{
    ChatMessage* line = ChatMessage::createChatMessage(scene, sender, msg, false, alert, self);
    insertChatline(line);

    return line;
}

ChatMessage* ChatLog::addChatMessage(const QString& sender, const QString& msg, const QDateTime& timestamp, bool self, bool alert)
{
    ChatMessage* line = ChatMessage::createChatMessage(scene, sender, msg, false, alert, self, timestamp);
    insertChatline(line);

    return line;
}

ChatMessage *ChatLog::addChatAction(const QString &sender, const QString &msg, const QDateTime &timestamp)
{
    ChatMessage* line = ChatMessage::createChatMessage(scene, sender, msg, true, false, false, timestamp);
    insertChatline(line);

    return line;
}

ChatMessage *ChatLog::addChatAction(const QString &sender, const QString &msg)
{
    ChatMessage* line = ChatMessage::createChatMessage(scene, sender, msg, true, false, false);
    insertChatline(line);

    return line;
}

ChatMessage *ChatLog::addSystemMessage(const QString &msg, const QDateTime& timestamp)
{
    ChatMessage* line = ChatMessage::createChatInfoMessage(scene, msg, "", timestamp);
    insertChatline(line);

    return line;
}

ChatMessage *ChatLog::addFileTransferMessage(const QString &sender, const ToxFile &file,  const QDateTime& timestamp, bool self)
{
    ChatMessage* line = ChatMessage::createFileTransferMessage(scene, sender, "", file, self, timestamp);
    insertChatline(line);

    return line;
}

void ChatLog::clearSelection()
{
    for(int i=selFirstRow; i<=selLastRow && i<lines.size() && i >= 0; ++i)
        lines[i]->selectionCleared();

    selFirstRow = -1;
    selLastRow = -1;
    selClickedCol = -1;
    selClickedRow = -1;

    selectionMode = None;

    updateMultiSelectionRect();
}

QRect ChatLog::getVisibleRect() const
{
    return mapToScene(viewport()->rect()).boundingRect().toRect();
}

void ChatLog::updateSceneRect()
{
    setSceneRect(QRectF(-margins.left(), -margins.top(), useableWidth(), (lines.empty() ? 0.0 : lines.last()->boundingSceneRect().bottom()) + margins.bottom() + margins.top()));
}

bool ChatLog::layout(int start, int end, qreal width)
{
    //qDebug() << "layout " << start << end;
    if(lines.empty())
        return false;

    start = clamp<int>(start, 0, lines.size() - 1);
    end = clamp<int>(end + 1, 0, lines.size());

    qreal h = lines[start]->boundingSceneRect().top();

    bool needsReposition = false;
    for(int i = start; i < end; ++i)
    {
        ChatLine* l = lines[i];

        qreal oldHeight = l->boundingSceneRect().height();
        l->layout(width, QPointF(0.0, h));

        if(oldHeight != l->boundingSceneRect().height())
            needsReposition = true;

        h += l->boundingSceneRect().height() + lineSpacing;
    }

    // move up
    if(needsReposition)
        reposition(end-1, end+10);

    return needsReposition;
}

void ChatLog::partialUpdate()
{
    checkVisibility();

    if(visibleLines.empty())
        return;

    auto oldUpdateMode = viewportUpdateMode();
    setViewportUpdateMode(NoViewportUpdate);

    bool repos;
    do
    {
        repos = false;
        if(!visibleLines.empty())
            repos = layout(visibleLines.first()->getRowIndex(), visibleLines.last()->getRowIndex(), useableWidth());

        checkVisibility();
    }
    while(repos);

    if(!visibleLines.empty())
        reposition(visibleLines.last()->getRowIndex(), lines.size());

    checkVisibility();

    setViewportUpdateMode(oldUpdateMode);
    updateSceneRect();
}

void ChatLog::fullUpdate()
{
    layout(0, lines.size(), useableWidth());
    checkVisibility();
    updateSceneRect();
}

void ChatLog::mousePressEvent(QMouseEvent* ev)
{
    QGraphicsView::mousePressEvent(ev);

    QPointF scenePos = mapToScene(ev->pos());

    if(ev->button() == Qt::LeftButton)
    {
        clickPos = ev->pos();
        clearSelection();
    }

    if(ev->button() == Qt::RightButton)
    {
        if(!isOverSelection(scenePos))
            clearSelection();

        showContextMenu(ev->globalPos(), scenePos);
    }
}

void ChatLog::mouseReleaseEvent(QMouseEvent* ev)
{
    QGraphicsView::mouseReleaseEvent(ev);
}

void ChatLog::mouseMoveEvent(QMouseEvent* ev)
{
    QGraphicsView::mouseMoveEvent(ev);

    QPointF scenePos = mapToScene(ev->pos());

    if(ev->buttons() & Qt::LeftButton)
    {
        if(selectionMode == None && (clickPos - ev->pos()).manhattanLength() > QApplication::startDragDistance())
        {
            QPointF sceneClickPos = mapToScene(clickPos.toPoint());

            ChatLineContent* content = getContentFromPos(sceneClickPos);
            if(content)
            {
                selClickedRow = content->getRow();
                selClickedCol = content->getColumn();
                selFirstRow = content->getRow();
                selLastRow = content->getRow();

                content->selectionStarted(sceneClickPos);

                selectionMode = Precise;

                // ungrab mouse grabber
                if(scene->mouseGrabberItem())
                    scene->mouseGrabberItem()->ungrabMouse();
            }
        }

        if(selectionMode != None && ev->pos() != lastPos)
        {
            lastPos = ev->pos();

            ChatLineContent* content = getContentFromPos(scenePos);

            if(content)
            {
                int row = content->getRow();
                int col = content->getColumn();

                if(row >= selClickedRow)
                    selLastRow = row;

                if(row <= selClickedRow)
                    selFirstRow = row;

                if(row == selClickedRow && col == selClickedCol)
                {
                    selectionMode = Precise;

                    content->selectionMouseMove(scenePos);
                    selGraphItem->hide();
                }
                else
                {
                    selectionMode = Multi;

                    lines[selClickedRow]->selectionCleared();

                    updateMultiSelectionRect();
                }
            }
        }
    }
}

ChatLineContent* ChatLog::getContentFromPos(QPointF scenePos) const
{
    QGraphicsItem* item = scene->itemAt(scenePos, QTransform());

    if(item && item->type() == ChatLineContent::ChatLineContentType)
        return static_cast<ChatLineContent*>(item);

    return nullptr;
}

bool ChatLog::isOverSelection(QPointF scenePos)
{
    if(selectionMode == Precise)
    {
        ChatLineContent* content = getContentFromPos(scenePos);

        if(content)
            return content->isOverSelection(scenePos);
    }
    else if(selectionMode == Multi)
    {
        if(selGraphItem->rect().contains(scenePos))
            return true;
    }

    return false;
}

qreal ChatLog::useableWidth()
{
    return width() - verticalScrollBar()->sizeHint().width() - margins.right() - margins.left();
}

void ChatLog::reposition(int start, int end)
{
    if(lines.isEmpty())
        return;

    start = clamp<int>(start, 0, lines.size() - 1);
    end = clamp<int>(end + 1, 0, lines.size());

    qreal h = lines[start]->boundingSceneRect().bottom() + lineSpacing;

    for(int i = start + 1; i < end; ++i)
    {
        ChatLine* l = lines[i];
        l->layout(QPointF(0, h));
        h += l->boundingSceneRect().height() + lineSpacing;
    }
}

void ChatLog::repositionDownTo(int start, qreal end)
{
    if(lines.isEmpty())
        return;

    start = clamp<int>(start, 0, lines.size() - 1);

    qreal h = lines[start]->boundingSceneRect().bottom() + lineSpacing;

    for(int i = start + 1; i < lines.size(); ++i)
    {
        ChatLine* l = lines[i];
        l->layout(QPointF(0, h));
        h += l->boundingSceneRect().height() + lineSpacing;

        if(h > end)
            break;
    }
}

void ChatLog::insertChatline(ChatLine* l)
{
    stickToBtm = stickToBottom();

    l->setRowIndex(lines.size());
    lines.append(l);

    layout(lines.last()->getRowIndex() - 1, lines.size(), useableWidth());
    updateSceneRect();

    if(stickToBtm)
        scrollToBottom();

    checkVisibility();
}

bool ChatLog::stickToBottom()
{
    return verticalScrollBar()->value() == verticalScrollBar()->maximum();
}

void ChatLog::scrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    updateGeometry();
    checkVisibility();
}

QString ChatLog::getSelectedText() const
{
    if(selectionMode == Precise)
    {
        return lines[selClickedRow]->content[selClickedCol]->getSelectedText();
    }
    else if(selectionMode == Multi)
    {
        // build a nicely formatted message
        QString out;

        QString lastSender;
        for(int i=selFirstRow; i<=selLastRow && i>=0 && i<lines.size(); ++i)
        {
            if(lastSender != lines[i]->content[0]->getText() && !lines[i]->content[0]->getText().isEmpty())
            {
                //author changed
                out += lines[i]->content[0]->getText() + ":\n";
                lastSender = lines[i]->content[0]->getText();
            }

            out += lines[i]->content[1]->getText();
            out += "\n\n";
        }

        return out;
    }

    return QString();
}

QString ChatLog::toPlainText() const
{
    QString out;
    QString lastSender;

    for(ChatLine* l : lines)
    {
        if(lastSender != l->content[0]->getText() && !l->content[0]->getText().isEmpty())
        {
            //author changed
            out += l->content[0]->getText() + ":\n";
            lastSender = l->content[0]->getText();
        }

        out += l->content[1]->getText();
        out += "\n\n";
    }

    return out;
}

bool ChatLog::isEmpty() const
{
    return lines.isEmpty();
}

void ChatLog::showContextMenu(const QPoint& globalPos, const QPointF& scenePos)
{
    QMenu menu;

    // populate
    QAction* copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), tr("Copy"));
    menu.addSeparator();
    QAction* clearAction = menu.addAction(QIcon::fromTheme("edit-clear") ,tr("Clear chat log"));
    QAction* saveAction = menu.addAction(QIcon::fromTheme("document-save") ,tr("Save chat log"));

    if(!isOverSelection(scenePos))
        copyAction->setDisabled(true);

    // show
    QAction* action = menu.exec(globalPos);

    if(action == copyAction)
        copySelectedText();

    if(action == clearAction)
        clear();

    if(action == saveAction)
    {
        QString path = QFileDialog::getSaveFileName(0, tr("Save chat log"));
        if (path.isEmpty())
            return;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return;

        file.write(toPlainText().toUtf8());
        file.close();
    }
}

void ChatLog::clear()
{
    visibleLines.clear();
    clearSelection();

    for(ChatLine* line : lines)
        delete line;

    lines.clear();
    updateSceneRect();
}

void ChatLog::copySelectedText() const
{
    QString text = getSelectedText();

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(text);
}

void ChatLog::checkVisibility()
{
    // find first visible row
    QList<ChatLine*>::const_iterator upperBound;
    upperBound = std::upper_bound(lines.cbegin(), lines.cend(), getVisibleRect().top(), [](const qreal lhs, const ChatLine* rhs)
    {
        return lhs < rhs->boundingSceneRect().bottom();
    });

    if(upperBound == lines.end())
    {
        //no lines visible
        for(ChatLine* line : visibleLines)
            line->visibilityChanged(false);

        visibleLines.clear();
        return;
    }

    // find last visible row
    QList<ChatLine*>::const_iterator lowerBound;
    lowerBound = std::lower_bound(lines.cbegin(), lines.cend(), getVisibleRect().bottom(), [](const ChatLine* lhs, const qreal rhs)
    {
        return lhs->boundingSceneRect().bottom() < rhs;
    });

    // set visibilty
    QList<ChatLine*> newVisibleLines;
    for(auto itr = upperBound; itr <= lowerBound && itr != lines.cend(); ++itr)
    {
        newVisibleLines.append(*itr);

        if(!visibleLines.contains(*itr))
            (*itr)->visibilityChanged(true);

        visibleLines.removeOne(*itr);
    }

    for(ChatLine* line : visibleLines)
        line->visibilityChanged(false);

    visibleLines = newVisibleLines;

    // assure order
    std::sort(visibleLines.begin(), visibleLines.end(), [](const ChatLine* lhs, const ChatLine* rhs)
    {
        return lhs->getRowIndex() < rhs->getRowIndex();
    });

    //if(!visibleLines.empty())
    //    qDebug() << "visible from " << visibleLines.first()->getRowIndex() << "to " << visibleLines.last()->getRowIndex() << " total " << visibleLines.size();
}

void ChatLog::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    partialUpdate();
}

void ChatLog::resizeEvent(QResizeEvent* ev)
{
    bool stb = stickToBottom();

    QGraphicsView::resizeEvent(ev);

    if(lines.count() > 300)
        partialUpdate();
    else
        fullUpdate();

    if(stb)
        scrollToBottom();

    updateMultiSelectionRect();
}

void ChatLog::updateMultiSelectionRect()
{
    if(selectionMode == Multi && selFirstRow >= 0 && selLastRow >= 0)
    {
        QRectF selBBox;
        selBBox = selBBox.united(lines[selFirstRow]->boundingSceneRect());
        selBBox = selBBox.united(lines[selLastRow]->boundingSceneRect());

        selGraphItem->setRect(selBBox);
        selGraphItem->show();
    }
    else
    {
        selGraphItem->hide();
    }
}
