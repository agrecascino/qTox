#include "aboutuser.h"
#include "ui_aboutuser.h"
#include "src/persistence/settings.h"

#include <QDir>
#include <QFileDialog>

AboutUser::AboutUser(ToxId &toxId, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutUser)
{
    ui->setupUi(this);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &AboutUser::onAcceptedClicked);
    connect(ui->autoaccept, &QCheckBox::clicked, this, &AboutUser::onAutoAcceptClicked);
    connect(ui->selectSaveDir, &QPushButton::clicked, this,  &AboutUser::onSelectDirClicked);

    this->toxId = toxId;
    QString dir = Settings::getInstance().getAutoAcceptDir(this->toxId);
    ui->autoaccept->setChecked(!dir.isEmpty());
    ui->selectSaveDir->setEnabled(ui->autoaccept->isChecked());

    if(ui->autoaccept->isChecked())
        ui->selectSaveDir->setText(Settings::getInstance().getAutoAcceptDir(this->toxId));
}

void AboutUser::setFriend(Friend *f)
{
    this->setWindowTitle(f->getDisplayedName());
    ui->userName->setText(f->getDisplayedName());
    ui->publicKey->setText(QString(f->getToxId().toString()));
    ui->publicKey->setCursorPosition(0); //scroll textline to left
    ui->note->setPlainText(Settings::getInstance().getContactNote(f->getToxId()));

    QPixmap avatar = Settings::getInstance().getSavedAvatar(f->getToxId().toString());
    ui->statusMessage->setText(f->getStatusMessage());
    if(!avatar.isNull()) {
        ui->avatar->setPixmap(avatar);
    } else {
        ui->avatar->setPixmap(QPixmap(":/img/contact_dark.svg"));
    }

}

void AboutUser::onAutoAcceptClicked()
{
    QString dir;
    if (!ui->autoaccept->isChecked())
    {
        dir = QDir::homePath();
        ui->autoaccept->setChecked(false);
        Settings::getInstance().setAutoAcceptDir(this->toxId, "");
        ui->selectSaveDir->setText("Auto accept for this contact is disabled");
    }
    else if (ui->autoaccept->isChecked())
    {
        dir = QFileDialog::getExistingDirectory(this, tr("Choose an auto accept directory",
                                                         "popup title"), dir);
        ui->autoaccept->setChecked(true);
        Settings::getInstance().setAutoAcceptDir(this->toxId, dir);
        ui->selectSaveDir->setText(Settings::getInstance().getAutoAcceptDir(this->toxId));
    }
    Settings::getInstance().saveGlobal();
    ui->selectSaveDir->setEnabled(ui->autoaccept->isChecked());
}

void AboutUser::onSelectDirClicked()
{
    QString dir;
    dir = QFileDialog::getExistingDirectory(this, tr("Choose an auto accept directory",
                                                     "popup title"), dir);
    ui->autoaccept->setChecked(true);
    Settings::getInstance().setAutoAcceptDir(this->toxId, dir);
    Settings::getInstance().saveGlobal();
}

/**
 * @brief AboutUser::onAcceptedClicked When users clicks the bottom OK button,
 *          save all settings
 */
void AboutUser::onAcceptedClicked()
{
    Settings::getInstance().setContactNote(ui->publicKey->text(), ui->note->toPlainText());
    Settings::getInstance().saveGlobal();
}

AboutUser::~AboutUser()
{
    delete ui;
}
