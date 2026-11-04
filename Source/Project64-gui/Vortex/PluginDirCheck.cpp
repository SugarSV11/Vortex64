#include "PluginDirCheck.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Vortex
{
static QByteArray Digest(const QString & Path)
{
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    QCryptographicHash Hash(QCryptographicHash::Sha256);
    Hash.addData(&File);
    return Hash.result();
}

QStringList PluginsDifferentFromOwn(const QString & SelectedDirectory, const QString & OwnDirectory)
{
    QStringList Different;
    const QDir Selected(SelectedDirectory), Own(OwnDirectory);
    if (SelectedDirectory.isEmpty() || OwnDirectory.isEmpty() || QFileInfo(Selected.absolutePath()).canonicalFilePath() == QFileInfo(Own.absolutePath()).canonicalFilePath())
    {
        return Different;
    }
    static const char * const Names[] = {"libProject64-video.so", "libProject64-audio.so", "libProject64-input.so", "libProject64-rsp.so", "libProject64-video-qtconfig.so", "libProject64-audio-qtconfig.so", "libProject64-input-qtconfig.so", "libGLideN64.so"};
    for (const char * Name : Names)
    {
        const QString A = Selected.filePath(QString::fromLatin1(Name)), B = Own.filePath(QString::fromLatin1(Name));
        if (!QFileInfo::exists(A) || !QFileInfo::exists(B))
        {
            continue;
        }
        if (QFileInfo(A).size() != QFileInfo(B).size() || Digest(A) != Digest(B))
        {
            Different << QString::fromLatin1(Name);
        }
    }
    return Different;
}
} // namespace Vortex
