#include "profile.h"
#include "profilelocker.h"
#include "src/misc/settings.h"
#include "src/core/core.h"
#include "src/historykeeper.h"
#include <cassert>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>
#include <QObject>
#include <QDebug>
QVector<QString> Profile::profiles;

Profile::Profile(QString name, QString password, bool isNewProfile)
    : name{name}, password{password},
      newProfile{isNewProfile}, isRemoved{false}
{
    Settings::getInstance().setCurrentProfile(name);

    coreThread = new QThread();
    coreThread->setObjectName("qTox Core");
    core = new Core(coreThread, *this);
    core->moveToThread(coreThread);
    QObject::connect(coreThread, &QThread::started, core, &Core::start);
}

Profile* Profile::loadProfile(QString name, QString password)
{
    if (ProfileLocker::hasLock())
    {
        qCritical() << "Tried to load profile "<<name<<", but another profile is already locked!";
        return nullptr;
    }

    if (!ProfileLocker::lock(name))
    {
        qWarning() << "Failed to lock profile "<<name;
        return nullptr;
    }

    return new Profile(name, password, false);
}

Profile* Profile::createProfile(QString name, QString password)
{
    if (ProfileLocker::hasLock())
    {
        qCritical() << "Tried to create profile "<<name<<", but another profile is already locked!";
        return nullptr;
    }

    if (profileExists(name))
    {
        qCritical() << "Tried to create profile "<<name<<", but it already exists!";
        return nullptr;
    }

    if (!ProfileLocker::lock(name))
    {
        qWarning() << "Failed to lock profile "<<name;
        return nullptr;
    }

    Settings::getInstance().createPersonal(name);
    return new Profile(name, password, true);
}

Profile::~Profile()
{
    if (!isRemoved && core->isReady())
        saveToxSave();
    delete core;
    delete coreThread;
    ProfileLocker::assertLock();
    assert(ProfileLocker::getCurLockName() == name);
    ProfileLocker::unlock();
}

QVector<QString> Profile::getFilesByExt(QString extension)
{
    QDir dir(Settings::getInstance().getSettingsDirPath());
    QVector<QString> out;
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setNameFilters(QStringList("*."+extension));
    QFileInfoList list = dir.entryInfoList();
    out.reserve(list.size());
    for (QFileInfo file : list)
        out += file.completeBaseName();
    return out;
}

void Profile::scanProfiles()
{
    profiles.clear();
    QVector<QString> toxfiles = getFilesByExt("tox"), inifiles = getFilesByExt("ini");
    for (QString toxfile : toxfiles)
    {
        if (!inifiles.contains(toxfile))
            importProfile(toxfile);
        profiles.append(toxfile);
    }
}

void Profile::importProfile(QString name)
{
    assert(!profileExists(name));
    Settings::getInstance().createPersonal(name);
}

QVector<QString> Profile::getProfiles()
{
    return profiles;
}

Core* Profile::getCore()
{
    return core;
}

QString Profile::getName()
{
    return name;
}

void Profile::startCore()
{
    coreThread->start();
}

bool Profile::isNewProfile()
{
    return newProfile;
}

QByteArray Profile::loadToxSave()
{
    assert(!isRemoved);

    /// TODO: Cache the data, invalidate it only when we save
    QByteArray data;

    QString path = Settings::getSettingsDirPath() + QDir::separator() + name + ".tox";
    QFile saveFile(path);
    qint64 fileSize;
    qDebug() << "Loading tox save "<<path;

    if (!saveFile.exists())
    {
        qWarning() << "The tox save file "<<path<<" was not found";
        goto fail;
    }

    if (!saveFile.open(QIODevice::ReadOnly))
    {
        qCritical() << "The tox save file " << path << " couldn't' be opened";
        goto fail;
    }

    fileSize = saveFile.size();
    if (fileSize <= 0)
    {
        qWarning() << "The tox save file"<<path<<" is empty!";
        goto fail;
    }

    data = saveFile.readAll();
    if (tox_is_data_encrypted((uint8_t*)data.data()))
    {
        if (password.isEmpty())
        {
            qCritical() << "The tox save file is encrypted, but we don't have a password!";
            data.clear();
            goto fail;
        }

        uint8_t salt[TOX_PASS_SALT_LENGTH];
        tox_get_salt(reinterpret_cast<uint8_t *>(data.data()), salt);
        core->setPassword(password, Core::ptMain, salt);

        data = core->decryptData(data, Core::ptMain);
        if (data.isEmpty())
            qCritical() << "Failed to decrypt the tox save file";
    }
    else
    {
        if (!password.isEmpty())
            qWarning() << "We have a password, but the tox save file is not encrypted";
    }

fail:
    saveFile.close();
    return data;
}

void Profile::saveToxSave()
{
    assert(core->isReady());
    QByteArray data = core->getToxSaveData();
    assert(data.size());
    saveToxSave(data);
}

void Profile::saveToxSave(QByteArray data)
{
    assert(!isRemoved);
    ProfileLocker::assertLock();
    assert(ProfileLocker::getCurLockName() == name);

    QString path = Settings::getSettingsDirPath() + QDir::separator() + name + ".tox";
    qDebug() << "Saving tox save to "<<path;
    QSaveFile saveFile(path);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        qCritical() << "Tox save file " << path << " couldn't be opened";
        return;
    }

    if (!password.isEmpty())
    {
        core->setPassword(password, Core::ptMain);
        data = core->encryptData(data, Core::ptMain);
        if (data.isEmpty())
        {
            qCritical() << "Failed to encrypt, can't save!";
            saveFile.cancelWriting();
            return;
        }
    }

    saveFile.write(data);
    saveFile.commit();
    newProfile = false;
}

bool Profile::profileExists(QString name)
{
    QString path = Settings::getSettingsDirPath() + QDir::separator() + name;
    return QFile::exists(path+".tox") && QFile::exists(path+".ini");
}

bool Profile::isProfileEncrypted(QString name)
{
    uint8_t data[encryptHeaderSize] = {0};
    QString path = Settings::getSettingsDirPath() + QDir::separator() + name + ".tox";
    QFile saveFile(path);
    if (!saveFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "Couldn't open tox save "<<path;
        return false;
    }

    saveFile.read((char*)data, encryptHeaderSize);
    saveFile.close();

    return tox_is_data_encrypted(data);
}

void Profile::remove()
{
    if (isRemoved)
    {
        qWarning() << "Profile "<<name<<" is already removed!";
        return;
    }
    isRemoved = true;

    qDebug() << "Removing profile"<<name;
    profiles.removeAll(name);
    QString path = Settings::getSettingsDirPath() + QDir::separator() + name;
    QFile::remove(path+".tox");
    QFile::remove(path+".ini");

    QFile::remove(HistoryKeeper::getHistoryPath(name, 0));
    QFile::remove(HistoryKeeper::getHistoryPath(name, 1));
}

bool Profile::rename(QString newName)
{
    QString path = Settings::getSettingsDirPath() + QDir::separator() + name,
            newPath = Settings::getSettingsDirPath() + QDir::separator() + newName;

    if (!ProfileLocker::lock(newName))
        return false;

    QFile::rename(path+".tox", newPath+".tox");
    QFile::rename(path+".ini", newPath+".ini");
    HistoryKeeper::renameHistory(name, newName);
    bool resetAutorun = Settings::getInstance().getAutorun();
    Settings::getInstance().setAutorun(false);
    Settings::getInstance().setCurrentProfile(newName);
    if (resetAutorun)
        Settings::getInstance().setAutorun(true); // fixes -p flag in autostart command line

    name = newName;
    return true;
}

bool Profile::checkPassword()
{
    if (isRemoved)
        return false;

    return !loadToxSave().isEmpty();
}
