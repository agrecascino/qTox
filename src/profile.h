#ifndef PROFILE_H
#define PROFILE_H

#include <QVector>
#include <QString>
#include <QByteArray>

class Core;
class QThread;

/// Manages user profiles
class Profile
{
public:
    /// Locks and loads an existing profile and create the associate Core* instance
    /// Returns a nullptr on error, for example if the profile is already in use
    static Profile* loadProfile(QString name, QString password);
    /// Creates a new profile and the associated Core* instance
    /// If password is not empty, the profile will be encrypted
    /// Returns a nullptr on error, for example if the profile already exists
    static Profile* createProfile(QString name, QString password);
    ~Profile();

    Core* getCore();
    QString getName();

    void startCore(); ///< Starts the Core thread
    bool isNewProfile();
    QByteArray loadToxSave(); ///< Loads the profile's .tox save from file, unencrypted
    void saveToxSave(); ///< Saves the profile's .tox save, encrypted if needed
    void saveToxSave(QByteArray data); ///< Saves the profile's .tox save with this data, encrypted if needed

    /// Scan for profile, automatically importing them if needed
    /// NOT thread-safe
    static void scanProfiles();
    static QVector<QString> getProfiles();

    static bool profileExists(QString name);
    static bool isProfileEncrypted(QString name); ///< Returns false on error.

private:
    Profile(QString name, QString password, bool newProfile);
    /// Lists all the files in the config dir with a given extension
    /// Pass the raw extension, e.g. "jpeg" not ".jpeg".
    static QVector<QString> getFilesByExt(QString extension);
    /// Creates a .ini file for the given .tox profile
    /// Only pass the basename, without extension
    static void importProfile(QString name);

private:
    Core* core;
    QThread* coreThread;
    QString name, password;
    static QVector<QString> profiles;
    bool newProfile; ///< True if this is a newly created profile, with no .tox save file yet.
    /// How much data we need to read to check if the file is encrypted
    /// Must be >= TOX_ENC_SAVE_MAGIC_LENGTH (8), which isn't publicly defined
    static constexpr int encryptHeaderSize = 8;
};

#endif // PROFILE_H
