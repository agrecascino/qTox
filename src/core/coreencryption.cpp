/*
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

/* This file is part of the Core class, but was separated for my sanity */
/* The load function delegates to loadEncrypted here, and the save function */
/* was permanently moved here to handle encryption */

#include "core.h"
#include "src/widget/gui.h"
#include "src/misc/settings.h"
#include "src/misc/cstring.h"
#include "src/historykeeper.h"
#include <tox/tox.h>
#include <tox/toxencryptsave.h>
#include <QApplication>
#include <QDebug>
#include <QSaveFile>
#include <QFile>
#include <QThread>
#include <algorithm>
#include <cassert>

void Core::setPassword(const QString& password, PasswordType passtype, uint8_t* salt)
{
    clearPassword(passtype);
    if (password.isEmpty())
        return;

    pwsaltedkeys[passtype] = new TOX_PASS_KEY;

    CString str(password);
    if (salt)
        tox_derive_key_with_salt(str.data(), str.size(), salt, pwsaltedkeys[passtype], nullptr);
    else
        tox_derive_key_from_pass(str.data(), str.size(), pwsaltedkeys[passtype], nullptr);
}

void Core::useOtherPassword(PasswordType type)
{
    clearPassword(type);
    pwsaltedkeys[type] = new TOX_PASS_KEY;

    PasswordType other = (type == ptMain) ? ptHistory : ptMain;

    std::copy(pwsaltedkeys[other], pwsaltedkeys[other]+1, pwsaltedkeys[type]);
}

void Core::clearPassword(PasswordType passtype)
{
    delete pwsaltedkeys[passtype];
    pwsaltedkeys[passtype] = nullptr;
}

QByteArray Core::encryptData(const QByteArray& data, PasswordType passtype)
{
    if (!pwsaltedkeys[passtype])
        return QByteArray();

    uint8_t encrypted[data.size() + TOX_PASS_ENCRYPTION_EXTRA_LENGTH];
    if (!tox_pass_key_encrypt(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                            pwsaltedkeys[passtype], encrypted, nullptr))
    {
        qWarning() << "Encryption failed";
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<char*>(encrypted), data.size() + TOX_PASS_ENCRYPTION_EXTRA_LENGTH);
}

QByteArray Core::decryptData(const QByteArray& data, PasswordType passtype)
{
    if (!pwsaltedkeys[passtype])
        return QByteArray();

    int sz = data.size() - TOX_PASS_ENCRYPTION_EXTRA_LENGTH;
    uint8_t decrypted[sz];
    if (!tox_pass_key_decrypt(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                              pwsaltedkeys[passtype], decrypted, nullptr))
    {
        qWarning() << "Decryption failed";
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<char*>(decrypted), sz);
}

bool Core::isPasswordSet(PasswordType passtype)
{
    if (pwsaltedkeys[passtype])
        return true;

    return false;
}

QByteArray Core::getSaltFromFile(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "file" << filename << "doesn't exist";
        return QByteArray();
    }
    QByteArray data = file.read(TOX_PASS_ENCRYPTION_EXTRA_LENGTH);
    file.close();

    uint8_t *salt = new uint8_t[TOX_PASS_SALT_LENGTH];
    if (!tox_get_salt(reinterpret_cast<uint8_t *>(data.data()), salt))
    {
        qWarning() << "can't get salt from" << filename << "header";
        return QByteArray();
    }

    QByteArray res(reinterpret_cast<const char*>(salt), TOX_PASS_SALT_LENGTH);
    delete[] salt;
    return res;
}

void Core::checkEncryptedHistory()
{
    QString path = HistoryKeeper::getHistoryPath();
    bool exists = QFile::exists(path);

    QByteArray salt = getSaltFromFile(path);
    if (exists && salt.size() == 0)
    {   // maybe we should handle this better
        GUI::showWarning(tr("Encrypted chat history"), tr("No encrypted chat history file found, or it was corrupted.\nHistory will be disabled!"));
        Settings::getInstance().setEncryptLogs(false);
        Settings::getInstance().setEnableLogging(false);
        HistoryKeeper::resetInstance();
        return;
    }

    QString a(tr("Please enter the password for the chat history for the %1 profile.", "used in load() when no hist pw set").arg(Settings::getInstance().getCurrentProfile()));
    QString b(tr("The previous password is incorrect; please try again:", "used on retries in load()"));
    QString c(tr("\nDisabling chat history now will leave the encrypted history intact (but not usable); if you later remember the password, you may re-enable encryption from the Privacy tab with the correct password to use the history.", "part of history password dialog"));
    QString dialogtxt;

    if (pwsaltedkeys[ptHistory])
    {
        if (!exists || HistoryKeeper::checkPassword())
            return;

        dialogtxt = tr("The chat history password failed. Please try another?", "used only when pw set before load() doesn't work");
    }
    else
    {
        dialogtxt = a;
    }

    dialogtxt += "\n" + c;

    if (pwsaltedkeys[ptMain])
    {
        useOtherPassword(ptHistory);
        if (!exists || HistoryKeeper::checkPassword())
        {
            qDebug() << "using main password for chat history";
            return;
        }
        clearPassword(ptHistory);
    }

    bool error = true;
    do
    {
        QString pw = GUI::passwordDialog(tr("Disable chat history"), dialogtxt);

        if (pw.isEmpty())
        {
            clearPassword(ptHistory);
            Settings::getInstance().setEncryptLogs(false);
            Settings::getInstance().setEnableLogging(false);
            HistoryKeeper::resetInstance();
            return;
        }
        else
        {
            setPassword(pw, ptHistory, reinterpret_cast<uint8_t*>(salt.data()));
        }

        error = exists && !HistoryKeeper::checkPassword();
        dialogtxt = a + "\n" + c + "\n" + b;
    } while (error);
}
