#include <iostream>
#include <QFile>
#include <QByteArray>
#include <QDir>
#include <QCryptographicHash>
#include <sodium.h>
#include "serialize.h"

using namespace std;

/// Pass the target folder as first argument, no spaces allowed. We'll call that dir $TARGET
/// Update the content of $TARGET/files/ before calling this tool
/// We'll generate $TARGET/flist and exit
/// We need qtox-updater-skey in our working directory to sign the flist
///
/// The generated flist is very simple and just installs everything in the working directory ...

int main(int argc, char* argv[])
{
    cout << "qTox updater flist generator" << endl;

    /// First some basic error handling, prepare our handles, ...
    if (argc != 2)
    {
        cout << "ERROR: qtox-updater-genflist takes the target path in argument" << endl;
        return 1;
    }

    QFile skeyFile("qtox-updater-skey");
    if (!skeyFile.open(QIODevice::ReadOnly))
    {
        cout << "ERROR: qtox-updater-genflist can't open the secret (private) key file" << endl;
        return 1;
    }
    QByteArray skeyData = skeyFile.readAll();
    skeyData = QByteArray::fromHex(skeyData);
    skeyFile.close();

    QString target(argv[1]);

    QFile flistFile(target+"/flist");
    if (!flistFile.open(QIODevice::Truncate | QIODevice::WriteOnly))
    {
        cout << "ERROR: qtox-updater-genflist can't open the target flist" << endl;
        return 1;
    }

    QDir fdir(target+"/files/");
    if (!fdir.isReadable())
    {
        cout << "ERROR: qtox-updater-genflist can't open the target files directory" << endl;
        return 1;
    }

    QStringList filesListStr = fdir.entryList(QDir::Files);

    /// Serialize the flist data
    QByteArray flistData;
    for (QString fileStr : filesListStr)
    {
        cout << "Adding "<<fileStr.toStdString()<<"..."<<endl;

        QFile file(target+"/files/"+fileStr);
        if (!file.open(QIODevice::ReadOnly))
        {
            cout << "ERROR: qtox-updater-genflist couldn't open a target file to sign it" << endl;
            return 1;
        }

        QByteArray fileData = file.readAll();

        unsigned char sig[crypto_sign_BYTES];
        crypto_sign_detached(sig, nullptr, (unsigned char*)fileData.data(), fileData.size(), (unsigned char*)skeyData.data());

        flistData += QByteArray::fromRawData((char*)sig, crypto_sign_BYTES);
        flistData += stringToData(QCryptographicHash::hash(fileStr.toUtf8(), QCryptographicHash::Sha3_224).toHex());
        flistData += stringToData("./"+fileStr); ///< Always install in the working directory for now
        flistData += uint64ToData(fileData.size());

        file.close();
    }

    cout << "Signing and writing the flist..."<<endl;

    /// Sign our flist
    unsigned char sig[crypto_sign_BYTES];
    crypto_sign_detached(sig, nullptr, (unsigned char*)flistData.data(), flistData.size(), (unsigned char*)skeyData.data());

    /// Write the flist
    flistFile.write("1");
    flistFile.write((char*)sig, crypto_sign_BYTES);
    flistFile.write(flistData);

    flistFile.close();
    return 0;
}

