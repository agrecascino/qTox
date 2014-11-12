#ifndef CHATLINECONTENT_H
#define CHATLINECONTENT_H

#include <QGraphicsItem>

class ChatLine;

class ChatLineContent : public QGraphicsItem
{
public:
    enum GraphicsItemType
    {
        ChatLineContentType = QGraphicsItem::UserType + 1,
    };

    ChatLine* getChatLine() const;

    int getColumn() const;
    int getRow() const;

    virtual void setWidth(qreal width) = 0;
    virtual int type() const final;

    virtual void selectionMouseMove(QPointF scenePos);
    virtual void selectionStarted(QPointF scenePos);
    virtual void selectionCleared();
    virtual void selectAll();
    virtual bool isOverSelection(QPointF scenePos) const;
    virtual QString getSelectedText() const;

    virtual qreal firstLineVOffset();

    virtual QRectF boundingSceneRect() const = 0;
    virtual QRectF boundingRect() const = 0;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) = 0;

    virtual void visibilityChanged(bool visible);

private:
    friend class ChatLine;

    void setIndex(int row, int col);
    void setChatLine(ChatLine* chatline);

private:
    ChatLine* line = nullptr;
    int row = -1;
    int col = -1;
};

#endif // CHATLINECONTENT_H
