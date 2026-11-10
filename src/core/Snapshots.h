#pragma once
#include <QDateTime>
#include <QList>
#include <QString>

class QTextDocument;

namespace qb {

class Project;

struct Snapshot {
    QString id;       // owning document
    QString file;     // absolute path
    QDateTime date;
    QString title;
};

namespace snapshots {
QList<Snapshot> list(Project *p, const QString &docId);   // newest first
// from: the text to store if it is not the document's own (a copy read from its file)
bool take(Project *p, const QString &docId, const QString &title, QString *error = nullptr, QTextDocument *from = nullptr);
void load(Project *p, const Snapshot &s, QTextDocument *into);
// 0.65.0 (audit F02): false with a reason when the snapshot's file cannot be read whole
bool load(Project *p, const Snapshot &s, QTextDocument *into, QString *error);
bool remove(Project *p, const Snapshot &s);
bool setTitle(Project *p, const Snapshot &s, const QString &title);
// The text replaced with the snapshot; false with a reason (nothing replaced) when it cannot be read whole,
// or when it holds no text while its file is not empty (0.65.0, audit F02)
bool rollBack(Project *p, const Snapshot &s, QString *error = nullptr, bool allowEmpty = false);

// All snapshots of a project (also of documents in the Trash and of documents no longer there).
struct Totals {
    int snapshots = 0;
    int documents = 0;   // documents with snapshots
    qint64 bytes = 0;    // the project's snapshot folder
};
Totals totals(Project *p);
// Project > Delete Old Snapshots (0.61.1): every snapshot of the project; the texts stay as they
// are. A Scrivener-format project loses them in the .scriv with its next full save.
// Returns the snapshots deleted.
int removeAll(Project *p, QString *error = nullptr);
} // namespace snapshots

// ---------------------------------------------------------------- diff
struct DiffOp {
    enum Type { Equal, Insert, Delete } type;
    QString text;
};

enum class DiffGranularity { Paragraph, Sentence, Word, Character };

QList<DiffOp> diffTexts(const QString &oldText, const QString &newText, DiffGranularity g = DiffGranularity::Word);

} // namespace qb
