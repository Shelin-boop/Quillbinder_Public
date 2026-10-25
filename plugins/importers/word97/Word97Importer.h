#pragma once
// Add-on "Word 97–2003 Import": .doc and .dot files become text documents.
// The built-in reader (WordReader.h) needs no other program; files it cannot
// read (Word 6.0 / 95, damaged files) are converted with LibreOffice if it is
// installed.

#include "import/ImporterApi.h"

#include <QObject>

class Word97Importer : public QObject, public qb::importers::ImporterInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_IMPORTER_IID)
    Q_INTERFACES(qb::importers::ImporterInterface)
public:
    QString id() const override { return QStringLiteral("import-word97"); }
    QString name() const override { return QStringLiteral("Word 97–2003 Import"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QStringList suffixes() const override { return {QStringLiteral("doc"), QStringLiteral("dot")}; }
    QString fileTypeName() const override { return tr("Word 97–2003 documents"); }
    bool readFile(const QString &path, const QString &workDir, qb::importers::Content *out, QString *error) override;
    bool importsWebPages() const override { return false; }
    bool importWebPage(QWidget *, const QString &, const QString &, bool, const QString &, const QString &,
                       qb::importers::WebPage *, QString *) override
    {
        return false;
    }

    // LibreOffice (soffice) if installed; QB_SOFFICE overrides (tests).
    static QString officeProgram();
    static bool convertWithOffice(const QString &path, const QString &workDir, QString *result, QString *error);
};
