#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace qb {

class Project;

namespace backup {
QString defaultDirectory();
// Zips the whole project folder into dir. Keeps the newest `keep` backups of
// this project (0 = keep all).
bool backupProject(Project *p, const QString &dir, int keep, QString *outPath, QString *error);
QStringList backupsFor(const QString &projectTitle, const QString &dir);
} // namespace backup

} // namespace qb
