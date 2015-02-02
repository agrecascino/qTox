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

#include "privacyform.h"
#include "ui_privacysettings.h"
#include "src/widget/form/settingswidget.h"
#include "src/misc/settings.h"
#include "src/historykeeper.h"
#include "src/core.h"
#include "src/widget/widget.h"
#include "src/widget/form/setpassworddialog.h"
#include <QMessageBox>
#include <QFile>
#include <QDebug>

PrivacyForm::PrivacyForm() :
    GenericForm(tr("Privacy"), QPixmap(":/img/settings/privacy.png"))
{
    bodyUI = new Ui::PrivacySettings;
    bodyUI->setupUi(this);

    bodyUI->encryptToxHLayout->addStretch();
    bodyUI->encryptLogsHLayout->addStretch();

    connect(bodyUI->cbTypingNotification, SIGNAL(stateChanged(int)), this, SLOT(onTypingNotificationEnabledUpdated()));
    connect(bodyUI->cbKeepHistory, SIGNAL(stateChanged(int)), this, SLOT(onEnableLoggingUpdated()));
    connect(bodyUI->cbEncryptHistory, SIGNAL(clicked()), this, SLOT(onEncryptLogsUpdated()));
    connect(bodyUI->changeLogsPwButton, &QPushButton::clicked, this, &PrivacyForm::setChatLogsPassword);
    connect(bodyUI->cbEncryptTox, SIGNAL(clicked()), this, SLOT(onEncryptToxUpdated()));
    connect(bodyUI->changeToxPwButton, &QPushButton::clicked, this, &PrivacyForm::setToxPassword);
    connect(bodyUI->nospamLineEdit, SIGNAL(editingFinished()), this, SLOT(setNospam()));
    connect(bodyUI->randomNosapamButton, SIGNAL(clicked()), this, SLOT(generateRandomNospam()));
    connect(bodyUI->nospamLineEdit, SIGNAL(textChanged(QString)), this, SLOT(onNospamEdit()));
}

PrivacyForm::~PrivacyForm()
{
    delete bodyUI;
}

void PrivacyForm::onEnableLoggingUpdated()
{
    Settings::getInstance().setEnableLogging(bodyUI->cbKeepHistory->isChecked());
    bodyUI->cbEncryptHistory->setEnabled(bodyUI->cbKeepHistory->isChecked());
    HistoryKeeper::resetInstance();
    Widget::getInstance()->clearAllReceipts();
}

void PrivacyForm::onTypingNotificationEnabledUpdated()
{
    Settings::getInstance().setTypingNotification(bodyUI->cbTypingNotification->isChecked());
}

bool PrivacyForm::setChatLogsPassword()
{
    Core* core = Core::getInstance();
    SetPasswordDialog* dialog;
    QString body = tr("Please set your new chat log password.");
    if (core->isPasswordSet(Core::ptMain))
        dialog = new SetPasswordDialog(body, tr("Use data file password", "pushbutton text"), this);
    else
        dialog = new SetPasswordDialog(body, QString(), this);

    // check if an encrypted history exists because it was disabled earlier, and use it if possible
    QString path = HistoryKeeper::getHistoryPath(QString(), 1);
    QByteArray salt = core->getSaltFromFile(path);
    bool haveEncHist = salt.size() > 0;

    do {
        int r = dialog->exec();
        if (r == QDialog::Rejected)
            break;

        QList<HistoryKeeper::HistMessage> oldMessages = HistoryKeeper::exportMessagesDeleteFile();

        QString newpw = dialog->getPassword();

        if (r == 2)
            core->useOtherPassword(Core::ptHistory);
        else if (haveEncHist)
            core->setPassword(newpw, Core::ptHistory, reinterpret_cast<uint8_t*>(salt.data()));
        else
            core->setPassword(newpw, Core::ptHistory);

        if (!haveEncHist || HistoryKeeper::checkPassword(1))
        {
            Settings::getInstance().setEncryptLogs(true);
            HistoryKeeper::getInstance()->importMessages(oldMessages);
            Widget::getInstance()->reloadHistory();
            delete dialog;
            return true;
        }
        else
        {
            if (!Widget::getInstance()->askQuestion(tr("Old encrypted chat log", "popup title"), tr("There is currently an unused encrypted chat log, but the password you just entered doesn't match.\nWould you like to try again?")))
                haveEncHist = false; // logically this is really just a `break`, but conceptually this is more accurate
        }
    } while (haveEncHist);

    delete dialog;
    return false;
}

void PrivacyForm::onEncryptLogsUpdated()
{
    Core* core = Core::getInstance();

    if (bodyUI->cbEncryptHistory->isChecked())
    {
        if (!core->isPasswordSet(Core::ptHistory))
        {
            if (setChatLogsPassword())
            {
                bodyUI->cbEncryptHistory->setChecked(true);
                bodyUI->changeLogsPwButton->setEnabled(true);
                return;
            }
        }
    }
    else
    {
        QMessageBox::StandardButton button = QMessageBox::warning(
            Widget::getInstance(),
            tr("Old encrypted chat logs", "title"),
            tr("Would you like to decrypt your chat logs?\nOtherwise they will be deleted."),
            QMessageBox::Ok | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Cancel
        );

        if (button == QMessageBox::Ok)
        {
            QList<HistoryKeeper::HistMessage> oldMessages = HistoryKeeper::exportMessagesDeleteFile(true);
            core->clearPassword(Core::ptHistory);
            Settings::getInstance().setEncryptLogs(false);
            HistoryKeeper::getInstance()->importMessages(oldMessages);
        }
        else if (button == QMessageBox::No)
        {
            if (QMessageBox::critical(
                    Widget::getInstance(),
                    tr("Old encrypted chat logs", "title"),
                    tr("Are you sure you want to lose your entire chat history?"),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel
                    )
                == QMessageBox::Yes)
            {
                HistoryKeeper::removeHistory(true);
            }
            else
            {
                bodyUI->cbEncryptHistory->setChecked(true);
                return;
            }
        }
        else
        {
            bodyUI->cbEncryptHistory->setChecked(true);
            return;
        }
    }

    core->clearPassword(Core::ptHistory);
    Settings::getInstance().setEncryptLogs(false);
    bodyUI->cbEncryptHistory->setChecked(false);
    bodyUI->changeLogsPwButton->setEnabled(false);
    HistoryKeeper::resetInstance();
}

bool PrivacyForm::setToxPassword()
{
    Core* core = Core::getInstance();
    SetPasswordDialog* dialog;
    QString body = tr("Please set your new data file password.");
    if (core->isPasswordSet(Core::ptHistory))
        dialog = new SetPasswordDialog(body, tr("Use chat log password", "pushbutton text"), this);
    else
        dialog = new SetPasswordDialog(body, QString(), this);

    if (int r = dialog->exec())
    {
        QString newpw = dialog->getPassword();
        delete dialog;

        if (r == 2)
            core->useOtherPassword(Core::ptMain);
        else
            core->setPassword(newpw, Core::ptMain);

        Settings::getInstance().setEncryptTox(true);
        core->saveConfiguration();
        return true;
    }
    else
    {
        delete dialog;
        return false;
    }
}

void PrivacyForm::onEncryptToxUpdated()
{
    Core* core = Core::getInstance();

    if (bodyUI->cbEncryptTox->isChecked())
    {
        if (!core->isPasswordSet(Core::ptMain))
        {
            if (setToxPassword())
            {
                bodyUI->cbEncryptTox->setChecked(true);
                bodyUI->changeToxPwButton->setEnabled(true);
                return;
            }
        }
    }
    else
    {
        if (!Widget::getInstance()->askQuestion(tr("Decrypt your data file", "title"), tr("Would you like to decrypt your data file?")))
        {
            bodyUI->cbEncryptTox->setChecked(true);
            return;
        }
        // affirmative answer falls through to the catch all below
    }

    bodyUI->cbEncryptTox->setChecked(false);
    Settings::getInstance().setEncryptTox(false);
    bodyUI->changeToxPwButton->setEnabled(false);
    core->clearPassword(Core::ptMain);
}

void PrivacyForm::setNospam()
{
    QString newNospam = bodyUI->nospamLineEdit->text();

    bool ok;
    uint32_t nospam = newNospam.toLongLong(&ok, 16);
    if (ok)
        Core::getInstance()->setNospam(nospam);
}

void PrivacyForm::present()
{
    bodyUI->nospamLineEdit->setText(Core::getInstance()->getSelfId().noSpam);
    bodyUI->cbTypingNotification->setChecked(Settings::getInstance().isTypingNotificationEnabled());
    bodyUI->cbKeepHistory->setChecked(Settings::getInstance().getEnableLogging());
    bodyUI->cbEncryptHistory->setChecked(Settings::getInstance().getEncryptLogs());
    bodyUI->changeLogsPwButton->setEnabled(Settings::getInstance().getEncryptLogs());
    bodyUI->cbEncryptHistory->setEnabled(Settings::getInstance().getEnableLogging());
    bodyUI->cbEncryptTox->setChecked(Settings::getInstance().getEncryptTox());
    bodyUI->changeToxPwButton->setEnabled(Settings::getInstance().getEncryptTox());
}

void PrivacyForm::generateRandomNospam()
{
    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());

    uint32_t newNospam{0};
    for (int i = 0; i < 4; i++)
        newNospam = (newNospam<<8) + (qrand() % 256); // Generate byte by byte. For some reason.

    Core::getInstance()->setNospam(newNospam);
    bodyUI->nospamLineEdit->setText(Core::getInstance()->getSelfId().noSpam);
}

void PrivacyForm::onNospamEdit()
{
    QString str = bodyUI->nospamLineEdit->text();
    int curs = bodyUI->nospamLineEdit->cursorPosition();
    if (str.length() != 8)
    {
        str = QString("00000000").replace(0, str.length(), str);
        bodyUI->nospamLineEdit->setText(str);
        bodyUI->nospamLineEdit->setCursorPosition(curs);
    };
}
