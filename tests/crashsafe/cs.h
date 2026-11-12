#pragma once
// The system test (crash safety and usability), stage 1: shared parts.
// Specification: the project document claude/quillbinder-test-procedure-crashsafe.md (revision of
// 2026-11-08 for 0.63.0). Each case runs in a child process of its own (qb_crashsafe_tests --case
// <id> <dir>); tools/run-crashsafe.sh starts them, then a fresh process checks what the case left
// (--verify <dir>: G1, G2, G3, G4).
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/Tables.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <functional>
#include <memory>
#include <vector>

class QTextDocument;

namespace qb {
class MainWindow;
class TextEditor;
class EditorPane;
class BinderPanel;
}

namespace cs {

using namespace qb;

// ---------------------------------------------------------------- the case and its results
struct Ctx {
    QString id;        // "S-T01"
    QString dir;       // the case's result folder
    QString work;      // its working folder (fixture copies)
    QString fixtures;  // the golden fixtures of the run
    QString buildDir;  // where the program and the tests were built
    QString sourceDir; // the repository
    bool quick = true; // QB_CS_PROFILE=quick (standard) or full
    bool smoke = false; // QB_CS_PROFILE=smoke: quick, with the shortest typing runs and loops (a shakedown)
    quint32 seed = 1;  // QB_CS_SEED or derived from the case id
    int failures = 0;
    int passes = 0;
    QJsonObject facts; // recorded observations (written to facts.json)
};
Ctx &ctx();

void check(bool ok, const QString &what, const QString &detail = QString());
void note(const QString &s);
void fact(const QString &key, const QJsonValue &v);   // an observation for the report
void step(const QString &s);                          // crash::note + a line in the log
void settle(int ms = 150);
bool waitFor(const std::function<bool()> &cond, int ms);
void shot(QWidget *w, const QString &name);
QString readText(const QString &file);
bool writeText(const QString &file, const QByteArray &data);
// 0.64.2: the project file in a folder (unpacked backup, copy): never settings/spare-copy.qbx
QString projectFileIn(const QString &dir);
bool copyTree(const QString &from, const QString &to);
QString runTool(const QString &program, const QStringList &args, int *rc = nullptr, int ms = 120000);

// alive: when given, the answer waits only while *alive is true (see BoxText)
template <typename T>
void withModal(int delayMs, std::function<void(T *)> fn, int tries = 100,   // about 15 s (shakedown 2026-11-09: 6 s was too short)
               std::shared_ptr<bool> alive = nullptr)
{
    QTimer::singleShot(delayMs, qApp, [fn, tries, alive] {
        if (alive && !*alive)
            return;   // the step that waited for the window is over
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<T *>(tl))
                if (d->isVisible()) {
                    fn(d);
                    return;
                }
        if (tries > 0) {
            withModal<T>(150, fn, tries - 1, alive);
            return;
        }
        note(QStringLiteral("(the expected window did not open)"));
    });
}

// The text of a box answered by answerBoxLater: the answer waits only while this variable lives. (2026-11-12, S-I10:
// a box that never came left its answer waiting 15 s; it wrote into a variable gone out of scope, which corrupted the
// stack and later the heap, and it would have answered a box of another step.)
class BoxText : public QString
{
public:
    BoxText() : alive(std::make_shared<bool>(true)) {}
    ~BoxText() { *alive = false; }
    BoxText(const BoxText &) = delete;
    BoxText &operator=(const BoxText &) = delete;
    using QString::operator=;
    std::shared_ptr<bool> alive;
};

// ---------------------------------------------------------------- G5, G6
void installMessageCounter();          // G6: qFatal / qCritical and the listed warnings fail
void finishMessages();                 // writes the counts, fails the case on G6
void checkCleanWindows(QWidget *main); // G5

// ---------------------------------------------------------------- the writer's text (AT) and typing (TM)
QStringList atParagraphs(quint32 seed, int words, bool german = false);
// text for the outside sender: only characters a German keyboard gives without AltGr or dead keys
QString gateText(quint32 seed, int chars);
// TM: types `text` at the end of the editor's document like a writer (chunks, corrections,
// overtyping, undo and redo); returns the text the document should end with (raw, with U+2029)
QString typeLikeWriter(TextEditor *e, const QString &text, quint32 seed);
void keysInto(QWidget *w, const QString &text);   // plain key events, '\n' = Return

// ---------------------------------------------------------------- G7 typing gates (outside sender)
struct GateResult {
    QString label, profile, result;   // result: "Pass fast", "Pass slow", "Fail"
    int sent = 0, handled = 0;
    bool textOk = false;
    double median = 0, p95 = 0, p99 = 0, largest = 0, throughput = 0;
    int backlog = 0;
    QString line;
};
// Sends keys from outside (tests/crashsafe/sender.py through XTest) into the focused editor for
// `seconds` with the profile ("slow", "fast"), measures key-to-screen, compares the text.
GateResult typingGate(TextEditor *e, const QString &profile, int seconds, const QString &label);
// the app: notes when key presses are handled and when the editor's viewport is painted
class CsApp : public QApplication
{
public:
    CsApp(int &argc, char **argv) : QApplication(argc, argv) {}
    bool notify(QObject *o, QEvent *e) override;
};
qint64 nowNs();

// ---------------------------------------------------------------- the program in this process
void setUpProgram();   // style, icons, settings for the tests, plugins, crash handlers
// a main window (shown, active) with the project at `path` (.qbx, .scriv or .scrivx)
MainWindow *openWindow(const QString &path, int w = 1400, int h = 950);
TextEditor *showDoc(MainWindow *w, BinderItem *it);   // in the active pane, as a single document
EditorPane *visiblePane(MainWindow *w);
QList<EditorPane *> visiblePanes(MainWindow *w);
QAction *action(MainWindow *w, const QString &name);
// a menu entry chosen the way the writer does: the menu bar, then each entry by mouse
bool chooseMenu(MainWindow *w, const QStringList &path);
// the same for the menu bar entry of an action (found by its object name)
bool chooseByMenu(MainWindow *w, const QString &actionName);
QStringList menuPathOf(MainWindow *w, const QString &actionName);
// the text's right-click menu at a position; the entry chosen by mouse. "" = only open and close
bool rightClickChoose(TextEditor *e, int position, const QString &entry, QStringList *entries = nullptr);
BinderItem *itemByTitle(Project *p, const QString &title);
QList<BinderItem *> textItems(Project *p, BinderItem *under = nullptr);
QString rawText(QTextDocument *d);
QString firstDifference(const QString &a, const QString &b, int context = 80);   // "" when equal

// ---------------------------------------------------------------- fixtures (section 4.3)
// FX-1 "Novel" (stage 1: without mind map, objects, collections, templates), FX-2 "Big"
bool buildFixtures(const QString &dir);
// a fresh copy of a fixture in the case's working folder; returns the path to open
QString fixtureCopy(const QString &name);   // "fx1-qb", "fx1-scriv", "fx2-scriv", "fx2-qb"
QImage testPicture(int w, int h, QRgb color = 0xff4477aa);
int insertPicture(Project *p, BinderItem *it, QTextDocument *d, int position, const QImage &img);
QString tableShape(QTextDocument *d);   // tables and figures as text (G3 model)
// stage 2: projects made by code
std::unique_ptr<Project> projectFromTemplate(const QString &dir, const QString &name, const QString &templateId);
void fillText(Project *p, BinderItem *it, const QStringList &paras);   // paragraphs at the end of its text
void fillNovelInto(Project *p);                                        // FX-1's content
void fillBigInto(Project *p, int chapters, int scenes, int sceneWords); // FX-2's generator
QObject *speechToolObject();   // the speech add-on's tool (cs_sync.cpp), nullptr when not loaded
QString runsOf(QTextDocument *d);                                      // the format runs as G3 sees them

// ---------------------------------------------------------------- stage 2: system cases (cs_sys_common.cpp)
bool menuPath(MainWindow *w, const QString &path);          // "Documents > Convert > Convert to Folder"
void clickLater(int ms, const QString &buttonText, int tries = 40);   // a button of the next window, by its text
void answerBoxLater(int ms, QMessageBox::StandardButton button, BoxText *text = nullptr);
void fileDialogLater(int ms, const QString &path, bool *asked = nullptr);
void closeDialogsLater(int ms);                             // rejects every open dialog
BinderPanel *binderOf(MainWindow *w);
void selectItems(MainWindow *w, const QList<BinderItem *> &items);
QString projectPathOf(Project *p);                          // the .qbx, or the .scriv
MainWindow *reopenWindow(MainWindow *w);                    // save, close, open again (deletes w)
QString docText(Project *p, BinderItem *it);
QString pdfToText(const QString &pdf, bool layout = false);
QByteArray zipFile(const QString &zip, const QString &name);
QString variant();                                          // QB_CS_VARIANT of a sub-run
// runs this case again in a fresh home (QB_CS_VARIANT=name; env: more variables, a null value removes one);
// returns its failures (-1: it did not end), the FAIL lines in *summary
int subCase(const QString &variantName, const QMap<QString, QString> &env = {}, QString *summary = nullptr);
bool realDrag(QWidget *from, const QPoint &a, QWidget *to, const QPoint &b);
// the start dialog (File > New Project): a template, a name, a folder, the format; Create (cs_sys_projects.cpp)
void startDialogLater(int ms, const templates::Info &t, const QString &name, const QString &folder, bool scrivener, bool *done);   // the mouse through xdotool
QPoint binderPoint(MainWindow *w, BinderItem *it);          // the item's row in the binder (shown)
void typeInto(QWidget *w, const QString &text);

// ---------------------------------------------------------------- fault injection (cs_faults.cpp, libqbfault.so)
QString faultLibrary();                               // <build>/tests/libqbfault.so
bool faultsLoaded();                                  // this process runs with the library (LD_PRELOAD)
QMap<QString, QString> faultEnv(const QString &dir);  // LD_PRELOAD, QB_FAULT_CTL, QB_FAULT_LOG for a sub-run
void setFaults(const QStringList &rules);             // "<op> <path part> <errno> [<after> [<times>]]"
void clearFaults();
QStringList firedFaults(bool reset = true);           // the faults that hit since the last reset
bool runUnderFaults();                                // true: the case ran in a sub-run with the library
QString modelDifference(const QJsonObject &a, const QJsonObject &b);   // "" when equal (silent compareModels)
QString scrivUuidOf(const QString &id);               // the UUID an item has in the .scriv (as the program writes it)

// ---------------------------------------------------------------- G3 model, G4, the verify step
// Saves, writes model.json (every item: title, text, format runs, notes, synopsis, label, status,
// keywords, tables, figures; the project's own settings), closes the project.
bool finishProject(MainWindow *w);
bool writeModel(Project *p);
QJsonObject modelOf(Project *p, bool reduced);   // reduced: what a .scriv read afresh must give back
int compareModels(const QJsonObject &want, const QJsonObject &got, const QString &what);
int verifyCase(const QString &caseDir);   // a fresh process: G1, G2, G3, G4

// ---------------------------------------------------------------- the cases
struct Case {
    QString id;
    QString title;
    int estQuickSec;
    int estFullSec;
    QString needs;   // "", "S" (whisper + add-on), "L" (LibreOffice), "A" (add-ons)
    std::function<void()> run;
    bool verify;     // a project is left for the verify step
};
// 0.90.0 (cs_new090.cpp): what came after 0.64.2
void caseC43();
void caseSN01();
void caseSN02();
void caseSN03();
void caseSN04();
void caseSN05();
const std::vector<Case> &cases();   // every case, one list (crashsafe/cs_list.cpp)
// a count for a loop: n in quick and full, at most `smokeMax` (at least 1) in a smoke run
int sized(int n, int smokeMax = 3);

// case groups (cs_tables.cpp, cs_cases.cpp, cs_process.cpp)
void caseC28();
void caseST01();
void caseST02();
void caseST03();
void caseST04();
void caseST05();
void caseC27();
void caseSW15();
void caseSW18();
void caseSI11();
void caseSL06();
void caseSR01();
void caseSD01();
void caseSY01();
void caseSY02();
void caseSY03();
void caseSR05();
void caseX02();
void caseX03();

// stage 2 (2026-11-09): cs_component.cpp ...
void caseC01();
void caseC02();
void caseC03();
void caseC04();
void caseC05();
void caseC06();
void caseC07();
void caseC08();
void caseC09();
void caseC10();
void caseC11();
void caseC12();
void caseC13();
void caseC14();
void caseC15();
void caseC16();
void caseC17();
void caseC18();
void caseC19();
void caseC20();
void caseC21();
void caseC22();
void caseC23();
void caseC24();
void caseC25();
void caseC26();
void caseC29();
void caseC30();
void caseC31();
void caseC32();
void caseC33();
void caseSP01();
void caseSP02();
void caseSP03();
void caseSP04();
void caseSP05();
void caseSP06();
void caseSP07();
void caseSB01();
void caseSB02();
void caseSB03();
void caseSB04();
void caseSB05();
void caseSB06();
void caseSB07();
void caseSB08();
void caseSB09();
void caseSB10();
void caseSB11();
void caseSW01();
void caseSW02();
void caseSW03();
void caseSW04();
void caseSW05();
void caseSW06();
void caseSW07();
void caseSW08();
void caseSW09();
void caseSW10();
void caseSW11();
void caseSW12();
void caseSW13();
void caseSW14();
void caseSW16();
void caseSW17();
void caseSW19();
void caseSK01();
void caseSK02();
void caseSK03();
void caseSK04();
void caseSK05();
void caseST06();
void caseSV01();
void caseSV02();
void caseSV03();
void caseSV04();
void caseSV05();
void caseSV06();
void caseSV07();
void caseSV08();
void caseSV09();
void caseSV10();
void caseSV11();
void caseSI01();
void caseSI02();
void caseSI03();
void caseSI04();
void caseSI05();
void caseSI06();
void caseSI07();
void caseSI08();
void caseSI09();
void caseSI10();
void caseSO01();
void caseSO02();
void caseSO03();
void caseSO04();
void caseSO05();
void caseSO06();
void caseSC01();
void caseSC02();
void caseSC03();
void caseSC04();
void caseSC05();
void caseSC06();
void caseSM01();
void caseSM02();
void caseSM03();
void caseSM04();
void caseSM05();
void caseSM06();
void caseSL01();
void caseSL02();
void caseSL03();
void caseSL04();
void caseSL05();
void caseSL07();
void caseSR02();
void caseSR03();
void caseSR04();
void caseSA01();
void caseSA02();
void caseSA03();
void caseSA04();
void caseSA05();
void caseSA06();
void caseSD02();
void caseSD03();
void caseSD04();
void caseSD05();
void caseSY04();
void caseSE01();
void caseSE02();
void caseSE03();
void caseSE04();
void caseU01();
void caseU02();
void caseU03();
void caseU04();
void caseU05();
void caseU06();
void caseU07();
void caseU08();
void caseU09();
void caseU10();
void caseU11();
void caseU12();
void caseU13();
void caseU14();
void caseU15();
void caseU16();
void caseU17();
void caseU18();
void caseU19();
void caseX01();
void caseX04();
void caseX05();
void caseX06();
void caseX07();
void caseX08();
void caseX09();
// the four areas added on 2026-11-09 (cs_faults.cpp, cs_roundtrip.cpp, cs_process.cpp)
void caseC34();
void caseC35();
void caseC36();
void caseC37();
void caseC38();
void caseC39();
void caseSD06();
void caseSP08();
void caseSE05();
void caseX10();
void caseX11();
void caseX12();
// 0.64.1 (2026-11-09): the external audit's fixes (cs_audit.cpp)
void caseC40();
void caseC41();
void caseC42();
void caseSP09();
void caseSI12();
void caseSL08();
void caseSA07();
void caseU20();

} // namespace cs
