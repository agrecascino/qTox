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

#include "filesform.h"

FilesForm::FilesForm()
    : QObject()
{
    head = new QWidget();
    QFont bold;
    bold.setBold(true);
    headLabel.setText(tr("Transfered Files","\"Headline\" of the window"));
    headLabel.setFont(bold);
    head->setLayout(&headLayout);
    headLayout.addWidget(&headLabel);
    
    main.addTab(&recvd, tr("Downloads"));
    main.addTab(&sent, tr("Uploads"));

    //these need to go in widget.cpp (I think, not really sure atm)
    //connect(something, SIGNAL(DOWNLOAD_DONE), this, SLOT(onFileDownloadComplete()));
    //connect(something, SIGNAL(DOWNLOAD_DONE), this, SLOT(onFileUploadComplete()));
    
    connect(&sent, SIGNAL(itemActivated(QListWidgetItem*)), this, SLOT(onFileActivated(QListWidgetItem*)));

}

FilesForm::~FilesForm()
{
    //delete head;
    // having this line caused a SIGABRT because free() received an invalid pointer
    // but since this is only called on program shutdown anyways, 
    // I'm not too bummed about removing it
}

void FilesForm::show(Ui::Widget& ui)
{
    ui.mainContent->layout()->addWidget(&main);
    ui.mainHead->layout()->addWidget(head);
    main.show();
    head->show();
}

void FilesForm::onFileDownloadComplete(const QString& path)
{
    QListWidgetItem* tmp = new QListWidgetItem(/*QIcon("checkmark.png"),*/path);
    recvd.addItem(tmp);
}

void FilesForm::onFileUploadComplete(const QString& path)
{
    QListWidgetItem* tmp = new QListWidgetItem(/*QIcon("checkmark.png"),*/path);
    sent.addItem(tmp);
}

void FilesForm::onFileActivated(QListWidgetItem* item)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->text()));
}
