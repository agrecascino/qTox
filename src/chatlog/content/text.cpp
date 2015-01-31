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

#include "text.h"

#include "../customtextdocument.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QDebug>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QDesktopServices>
#include <QTextFragment>

Text::Text(const QString& txt, QFont font, bool enableElide, const QString &rwText)
    : rawText(rwText)
    , elide(enableElide)
    , defFont(font)
{
    setText(txt);
    setAcceptedMouseButtons(Qt::LeftButton);
}

Text::~Text()
{
    delete doc;
}

void Text::setText(const QString& txt)
{
    text = txt;
    dirty = true;

    regenerate();
}

void Text::setWidth(qreal w)
{
    if(w == width)
        return;

    width = w;
    dirty = true;

    if(elide)
    {
        QFontMetrics metrics = QFontMetrics(defFont);
        elidedText = metrics.elidedText(text, Qt::ElideRight, width);
    }

    regenerate();
}

void Text::selectionMouseMove(QPointF scenePos)
{
    if(!doc)
        return;

    int cur = cursorFromPos(scenePos);
    if(cur >= 0)
    {
        selectionEnd = cur;
        selectedText = extractSanitizedText(getSelectionStart(), getSelectionEnd());
    }

    update();
}

void Text::selectionStarted(QPointF scenePos)
{
    int cur = cursorFromPos(scenePos);
    if(cur >= 0)
    {
        selectionEnd = cur;
        selectionAnchor = cur;
    }
}

void Text::selectionCleared()
{
    selectedText.clear();
    selectedText.squeeze();

    // Do not reset selectionAnchor!
    selectionEnd = -1;

    update();
}

void Text::selectionDoubleClick(QPointF scenePos)
{
    if(!doc)
        return;

    int cur = cursorFromPos(scenePos);

    if(cur >= 0)
    {
        QTextCursor cursor(doc);
        cursor.setPosition(cur);
        cursor.select(QTextCursor::WordUnderCursor);

        selectionAnchor = cursor.selectionStart();
        selectionEnd = cursor.selectionEnd();

        selectedText = extractSanitizedText(getSelectionStart(), getSelectionEnd());
    }

    update();
}

bool Text::isOverSelection(QPointF scenePos) const
{
    int cur = cursorFromPos(scenePos);
    if(getSelectionStart() < cur && getSelectionEnd() >= cur)
        return true;

    return false;
}

QString Text::getSelectedText() const
{
    return selectedText;
}

QRectF Text::boundingRect() const
{
    return QRectF(QPointF(0, 0), size);
}

void Text::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    if(doc)
    {
        // draw selection
        QAbstractTextDocumentLayout::PaintContext ctx;
        QAbstractTextDocumentLayout::Selection sel;

        if(hasSelection())
        {
            sel.cursor = QTextCursor(doc);
            sel.cursor.setPosition(getSelectionStart());
            sel.cursor.setPosition(getSelectionEnd(), QTextCursor::KeepAnchor);
        }

        sel.format.setBackground(QApplication::palette().color(QPalette::Highlight));
        sel.format.setForeground(QApplication::palette().color(QPalette::HighlightedText));
        ctx.selections.append(sel);

        // draw text
        doc->documentLayout()->draw(painter, ctx);
    }

    Q_UNUSED(option)
    Q_UNUSED(widget)
}

void Text::visibilityChanged(bool visible)
{
    keepInMemory = visible;

    regenerate();
    update();
}

qreal Text::getAscent() const
{
    return ascent;
}

void Text::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
        event->accept(); // grabber
}

void Text::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if(!doc)
        return;

    QString anchor = doc->documentLayout()->anchorAt(event->pos());

    // open anchor in browser
    if(!anchor.isEmpty())
        QDesktopServices::openUrl(anchor);
}

QString Text::getText() const
{
    return rawText;
}

void Text::regenerate()
{
    if(!doc)
    {
        doc = new CustomTextDocument();
        doc->setDefaultFont(defFont);
        dirty = true;
    }

    if(dirty)
    {
        if(!elide)
        {
            doc->setHtml(text);
        }
        else
        {
            QTextOption opt;
            opt.setWrapMode(QTextOption::NoWrap);
            doc->setDefaultTextOption(opt);
            doc->setPlainText(elidedText);
        }

        dirty = false;
    }

    // width & layout
    doc->setTextWidth(width);
    doc->documentLayout()->update();

    // update ascent
    if(doc->firstBlock().layout()->lineCount() > 0)
        ascent = doc->firstBlock().layout()->lineAt(0).ascent();

    // let the scene know about our change in size
    if(size != idealSize())
        prepareGeometryChange();

    // get the new width and height
    size = idealSize();

    // if we are not visible -> free mem
    if(!keepInMemory)
        freeResources();
}

void Text::freeResources()
{
    delete doc;
    doc = nullptr;
}

QSizeF Text::idealSize()
{
    if(doc)
        return QSizeF(qMin(doc->idealWidth(), width), doc->size().height());

    return size;
}

int Text::cursorFromPos(QPointF scenePos) const
{
    if(doc)
        return doc->documentLayout()->hitTest(mapFromScene(scenePos), Qt::FuzzyHit);

    return -1;
}

int Text::getSelectionEnd() const
{
    return qMax(selectionAnchor, selectionEnd);
}

int Text::getSelectionStart() const
{
    return qMin(selectionAnchor, selectionEnd);
}

bool Text::hasSelection() const
{
    return selectionEnd >= 0;
}

QString Text::extractSanitizedText(int from, int to) const
{
    if(!doc)
        return "";

    QString txt;
    QTextBlock block = doc->firstBlock();

    for(QTextBlock::Iterator itr = block.begin(); itr!=block.end(); ++itr)
    {
        int pos = itr.fragment().position(); //fragment position -> position of the first character in the fragment

        if(itr.fragment().charFormat().isImageFormat())
        {
            QTextImageFormat imgFmt = itr.fragment().charFormat().toImageFormat();
            QString key = imgFmt.name(); //img key (eg. key::D for :D)
            QString rune = key.mid(4);

            if(pos >= from && pos < to)
            {
                txt += rune;
                pos++;
            }
        }
        else
        {
            for(QChar c : itr.fragment().text())
            {
                if(pos >= from && pos < to)
                    txt += c;

                pos++;
            }
        }
    }

    return txt;
}
