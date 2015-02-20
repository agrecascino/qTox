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


#include "src/ipc.h"
#include "src/misc/settings.h"
#include <QDebug>
#include <QCoreApplication>
#include <unistd.h>


IPC::IPC()
    : globalMemory{"qtox-" IPC_PROTOCOL_VERSION}
{
    qRegisterMetaType<IPCEventHandler>("IPCEventHandler");

    timer.setInterval(EVENT_TIMER_MS);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &IPC::processEvents);

    // The first started instance gets to manage the shared memory by taking ownership
    // Every time it processes events it updates the global shared timestamp "lastProcessed"
    // If the timestamp isn't updated, that's a timeout and someone else can take ownership
    // This is a safety measure, in case one of the clients crashes
    // If the owner exits normally, it can set the timestamp to 0 first to immediately give ownership

    qsrand(time(0));
    globalId = ((uint64_t)qrand()) * ((uint64_t)qrand()) * ((uint64_t)qrand());
    qDebug() << "IPC: Our global ID is " << globalId;
    if (globalMemory.create(sizeof(IPCMemory)))
    {
        qDebug() << "IPC: Creating the global shared memory and taking ownership";
        if (globalMemory.lock())
        {
            IPCMemory* mem = global();
            memset(mem, 0, sizeof(IPCMemory));
            mem->globalId = globalId;
            mem->lastProcessed = time(0);
            globalMemory.unlock();
        }
        else
        {
            qWarning() << "IPC: Couldn't lock to take ownership";
        }
    }
    else if (globalMemory.attach())
    {
        qDebug() << "IPC: Attaching to the global shared memory";
    }
    else
    {
        qDebug() << "IPC: Failed to attach to the global shared memory, giving up";
        return; // We won't be able to do any IPC without being attached, let's get outta here
    }

    timer.start();
}

IPC::~IPC()
{
    if (isCurrentOwner())
    {
        if (globalMemory.lock())
        {
            global()->globalId = 0;
            globalMemory.unlock();
        }
    }
}

time_t IPC::postEvent(const QString &name, const QByteArray& data/*=QByteArray()*/, uint32_t dest/*=0*/)
{
    QByteArray binName = name.toUtf8();
    if (binName.length() > (int32_t)sizeof(IPCEvent::name))
        return 0;

    if (data.length() > (int32_t)sizeof(IPCEvent::data))
        return 0;

    if (globalMemory.lock())
    {
        IPCEvent* evt = 0;
        IPCMemory* mem = global();
        time_t result = 0;

        for (uint32_t i = 0; !evt && i < EVENT_QUEUE_SIZE; i++)
        {
            if (mem->events[i].posted == 0)
                evt = &mem->events[i];
        }

        if (evt)
        {
            memset(evt, 0, sizeof(IPCEvent));
            memcpy(evt->name, binName.constData(), binName.length());
            memcpy(evt->data, data.constData(), data.length());
            mem->lastEvent = evt->posted = result = qMax(mem->lastEvent + 1, time(0));
            evt->dest = dest;
            evt->sender = getpid();
            qDebug() << "IPC: postEvent " << name << "to" << dest;
        }
        globalMemory.unlock();
        return result;
    }
    else
        qDebug() << "IPC: Failed to lock in postEvent()";

    return 0;
}

bool IPC::isCurrentOwner()
{
    if (globalMemory.lock())
    {
        bool isOwner = ((*(uint64_t*)globalMemory.data()) == globalId);
        globalMemory.unlock();
        return isOwner;
    }
    else
    {
        qWarning() << "IPC: isCurrentOwner failed to lock, returning false";
        return false;
    }
}

void IPC::registerEventHandler(const QString &name, IPCEventHandler handler)
{
    eventHandlers[name] = handler;
}

bool IPC::isEventProcessed(time_t time)
{
    bool result = false;
    if (globalMemory.lock())
    {
        if (difftime(global()->lastProcessed, time) > 0)
        {
            IPCMemory* mem = global();
            for (uint32_t i = 0; i < EVENT_QUEUE_SIZE; i++)
            {
                if (mem->events[i].posted == time && mem->events[i].processed)
                {
                    result = true;
                    break;
                }
            }
        }
        globalMemory.unlock();
    }
    else
    {
        qWarning() << "IPC: isEventProcessed failed to lock, returning false";
    }
    return result;
}

bool IPC::isEventAccepted(time_t time)
{
    bool result = false;
    if (globalMemory.lock())
    {
        // if (difftime(global()->lastProcessed, time) > 0)
        {
            IPCMemory* mem = global();
            for (uint32_t i = 0; i < EVENT_QUEUE_SIZE; i++)
            {
                if (mem->events[i].posted == time)
                {
                    result = mem->events[i].accepted;
                    break;
                }
            }
        }
        globalMemory.unlock();
    }
    else
    {
        qWarning() << "IPC: isEventAccepted failed to lock, returning false";
    }
    return result;
}

bool IPC::waitUntilProcessed(time_t postTime, int32_t timeout/*=-1*/)
{
    bool result = false;
    time_t start = time(0);
    while (!(result = isEventProcessed(postTime)))
    {
        qApp->processEvents();
        if (timeout > 0 && difftime(time(0), start) >= timeout)
            break;
    }
    return result;
}

IPC::IPCEvent *IPC::fetchEvent()
{
    IPCMemory* mem = global();
    for (uint32_t i = 0; i < EVENT_QUEUE_SIZE; i++)
    {
        IPCEvent* evt = &mem->events[i];

        // Garbage-collect events that were not processed in EVENT_GC_TIMEOUT
        // and events that were processed and EVENT_GC_TIMEOUT passed after
        // so sending instance has time to react to those events.
        if ((evt->processed && difftime(time(0), evt->processed) > EVENT_GC_TIMEOUT) ||
            (!evt->processed && difftime(time(0), evt->posted) > EVENT_GC_TIMEOUT))
            memset(evt, 0, sizeof(IPCEvent));

        if (evt->posted && !evt->processed && evt->sender != getpid())
        {
            if (evt->dest == Settings::getInstance().getCurrentProfileId() || (evt->dest == 0 && isCurrentOwner()))
            {
                evt->processed = time(0);
                return evt;
            }
        }
    }
    return 0;
}

bool IPC::runEventHandler(IPCEventHandler handler, const QByteArray& arg)
{
    bool result = false;
    if (QThread::currentThread() != qApp->thread())
    {
        QMetaObject::invokeMethod(this, "runEventHandler",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, result),
                                  Q_ARG(IPCEventHandler, handler),
                                  Q_ARG(const QByteArray&, arg));
        return result;
    }
    else
    {
        result = handler(arg);
        return result;
    }
}

void IPC::processEvents()
{
    if (globalMemory.lock())
    {
        IPCMemory* mem = global();

        if (mem->globalId == globalId)
        {
            // We're the owner, let's process those events
            mem->lastProcessed = time(0);
        }
        else
        {
            // Only the owner processes events. But if the previous owner's dead, we can take ownership now
            if (difftime(time(0), mem->lastProcessed) >= OWNERSHIP_TIMEOUT_S)
            {
                qDebug() << "IPC: Previous owner timed out, taking ownership" << mem->globalId << "->" << globalId;
                // Ignore events that were not meant for this instance
                memset(mem, 0, sizeof(IPCMemory));
                mem->globalId = globalId;
                mem->lastProcessed = time(0);
            }
            // Non-main instance is limited to events destined for specific profile it runs
        }

        while (IPCEvent* evt = fetchEvent())
        {
            QString name = QString::fromUtf8(evt->name);
            auto it = eventHandlers.find(name);
            if (it != eventHandlers.end())
            {
                evt->accepted = runEventHandler(it.value(), evt->data);
                qDebug() << "IPC: Processing event: " << name << ":" << evt->posted << "=" << evt->accepted;
            }
        }

        globalMemory.unlock();
    }
    timer.start();
}

IPC::IPCMemory *IPC::global()
{
    return (IPCMemory*)globalMemory.data();
}
