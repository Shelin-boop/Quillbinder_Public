#pragma once
#include <QString>
#include <memory>

namespace qb {

class Project;
class BinderItem;

namespace templates {

struct Info {
    QString id;
    QString category;
    QString name;
    QString description;
    QString icon;
    QString zipPath;   // user templates only
};

QList<Info> all();
QStringList categories();
QString userTemplatesDir();
// File > Options > General > Shared Templates (0.21): a further folder of project
// templates (for example one synchronized between computers); empty = none.
void setSharedTemplatesDir(const QString &dir);
QString sharedTemplatesDir();

std::unique_ptr<Project> createProject(const Info &info, const QString &parentDir, const QString &name,
                                       QString *error);
bool saveAsTemplate(Project *p, const QString &name, const QString &category, const QString &description,
                    QString *error);

// Helper used by templates, import and tests.
void setMarkdown(Project *p, BinderItem *item, const QString &markdown);

} // namespace templates
} // namespace qb
