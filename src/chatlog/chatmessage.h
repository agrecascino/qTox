#ifndef CHATMESSAGE_H
#define CHATMESSAGE_H

#include "chatline.h"

class QGraphicsScene;

class ChatMessage : public ChatLine
{
public:
     ChatMessage(QGraphicsScene* scene, const QString& rawMessage);

    void markAsSent(const QDateTime& time);
    QString toString() const;

private:
    ChatLineContent* midColumn = nullptr;
    QString rawString;
};

#endif // CHATMESSAGE_H
