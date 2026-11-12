// The Start menu entry, the desktop shortcut and the project file types on Windows (W4b); see WinShell.h.
#include "ui/WinShell.h"
#include "core/Launcher.h"
#include "core/Portable.h"
#include "core/TestSwitches.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace qb {
namespace winshell {

namespace {

QString say(const char *text) { return QCoreApplication::translate("WinShell", text); }

const char *const kProjectProgId = "Quillbinder.Project";
const char *const kScrivenerProgId = "Quillbinder.ScrivenerProject";

QString programFile() { return QDir::toNativeSeparators(QFileInfo(launcher::startProgram()).absoluteFilePath()); }
QString openCommand() { return QStringLiteral("\"%1\" \"%2\"").arg(programFile(), QStringLiteral("%1")); }

#ifdef Q_OS_WIN
// COM for this thread (Qt's Windows platform has it already: then only counted)
struct Com
{
    HRESULT hr;
    Com() : hr(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~Com()
    {
        if (SUCCEEDED(hr))
            ::CoUninitialize();
    }
};

QString lastError(HRESULT hr)
{
    wchar_t *buf = nullptr;
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                     DWORD(hr), 0, reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
    QString s = buf ? QString::fromWCharArray(buf).trimmed() : QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    if (buf)
        ::LocalFree(buf);
    return s;
}

// The program a shortcut starts ("" when the file is not a shortcut that can be read).
QString linkTarget(const QString &lnk)
{
    Com com;
    IShellLinkW *link = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link))))
        return {};
    QString target;
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file)))) {
        const std::wstring path = QDir::toNativeSeparators(lnk).toStdWString();
        if (SUCCEEDED(file->Load(path.c_str(), STGM_READ))) {
            wchar_t buf[MAX_PATH * 2] = {0};
            if (SUCCEEDED(link->GetPath(buf, MAX_PATH * 2, nullptr, SLGP_RAWPATH)))
                target = QString::fromWCharArray(buf);
        }
        file->Release();
    }
    link->Release();
    return target;
}

bool writeLink(const QString &lnk, QString *error)
{
    Com com;
    IShellLinkW *link = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (FAILED(hr)) {
        if (error)
            *error = lastError(hr);
        return false;
    }
    const std::wstring exe = programFile().toStdWString();
    const std::wstring dir = QDir::toNativeSeparators(portable::programFolder()).toStdWString();
    const std::wstring what = say(QT_TRANSLATE_NOOP("WinShell", "Quillbinder, a writing program")).toStdWString();
    link->SetPath(exe.c_str());
    link->SetWorkingDirectory(dir.c_str());
    link->SetDescription(what.c_str());
    link->SetIconLocation(exe.c_str(), 0);
    IPersistFile *file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));
    if (SUCCEEDED(hr)) {
        QDir().mkpath(QFileInfo(lnk).absolutePath());
        const std::wstring path = QDir::toNativeSeparators(lnk).toStdWString();
        hr = file->Save(path.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    if (FAILED(hr) && error)
        *error = lastError(hr);
    return SUCCEEDED(hr);
}

void tellShell() { ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
#endif

bool sameProgram(const QString &a, const QString &b)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)),
                                                                  Qt::CaseInsensitive)
           == 0;
}

} // namespace

bool available()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString shortcutFile(Item item)
{
    const QString forced = QB_TEST_ENV("QB_TEST_SHELL_DIR");
    if (item == Item::StartMenu)
        return (forced.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
                                 : forced + QStringLiteral("/Start Menu"))
               + QStringLiteral("/Quillbinder.lnk");
    if (item == Item::Desktop)
        return (forced.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                                 : forced + QStringLiteral("/Desktop"))
               + QStringLiteral("/Quillbinder.lnk");
    return {};
}

QString classesKey()
{
    const QString forced = QB_TEST_ENV("QB_TEST_SHELL_CLASSES");
    return forced.isEmpty() ? QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes") : forced;
}

State state(Item item)
{
#ifdef Q_OS_WIN
    if (item == Item::FileTypes) {
        QSettings reg(classesKey(), QSettings::NativeFormat);
        const QString cmd = reg.value(QStringLiteral("%1/shell/open/command/Default").arg(QLatin1String(kProjectProgId))).toString();
        if (cmd.isEmpty())
            return State::Absent;
        return cmd.compare(openCommand(), Qt::CaseInsensitive) == 0 ? State::Here : State::Elsewhere;
    }
    const QString lnk = shortcutFile(item);
    if (!QFileInfo::exists(lnk))
        return State::Absent;
    return sameProgram(linkTarget(lnk), programFile()) ? State::Here : State::Elsewhere;
#else
    Q_UNUSED(item)
    return State::Absent;
#endif
}

bool add(Item item, QString *error)
{
#ifdef Q_OS_WIN
    if (item == Item::FileTypes) {
        QSettings reg(classesKey(), QSettings::NativeFormat);
        const QString project = QLatin1String(kProjectProgId), scriv = QLatin1String(kScrivenerProgId);
        const QString icon = QStringLiteral("\"%1\",0").arg(programFile());
        reg.setValue(project + QStringLiteral("/Default"), say(QT_TRANSLATE_NOOP("WinShell", "Quillbinder project")));
        reg.setValue(project + QStringLiteral("/DefaultIcon/Default"), icon);
        reg.setValue(project + QStringLiteral("/shell/open/command/Default"), openCommand());
        reg.setValue(scriv + QStringLiteral("/Default"), say(QT_TRANSLATE_NOOP("WinShell", "Scrivener project")));
        reg.setValue(scriv + QStringLiteral("/DefaultIcon/Default"), icon);
        reg.setValue(scriv + QStringLiteral("/shell/open/command/Default"), openCommand());
        // .qbx files open with Quillbinder unless another program took them; .scrivx only in "Open with"
        const QString qbxDefault = reg.value(QStringLiteral(".qbx/Default")).toString();
        if (qbxDefault.isEmpty() || qbxDefault == project)
            reg.setValue(QStringLiteral(".qbx/Default"), project);
        reg.setValue(QStringLiteral(".qbx/OpenWithProgids/") + project, QString());
        reg.setValue(QStringLiteral(".scrivx/OpenWithProgids/") + scriv, QString());
        reg.sync();
        tellShell();
        if (reg.status() != QSettings::NoError || state(item) != State::Here) {
            if (error)
                *error = say(QT_TRANSLATE_NOOP("WinShell", "the registry entries could not be written"));
            return false;
        }
        return true;
    }
    QString why;
    if (!writeLink(shortcutFile(item), &why)) {
        if (error)
            *error = why;
        return false;
    }
    return true;
#else
    Q_UNUSED(item)
    if (error)
        *error = say(QT_TRANSLATE_NOOP("WinShell", "only on Windows"));
    return false;
#endif
}

bool remove(Item item, QString *error)
{
#ifdef Q_OS_WIN
    if (item == Item::FileTypes) {
        QSettings reg(classesKey(), QSettings::NativeFormat);
        const QString project = QLatin1String(kProjectProgId), scriv = QLatin1String(kScrivenerProgId);
        reg.remove(project);
        reg.remove(scriv);
        reg.remove(QStringLiteral(".qbx/OpenWithProgids/") + project);
        reg.remove(QStringLiteral(".scrivx/OpenWithProgids/") + scriv);
        if (reg.value(QStringLiteral(".qbx/Default")).toString() == project)
            reg.remove(QStringLiteral(".qbx/Default"));
        // keys left empty go too (another program's entries stay)
        for (const QString &ext : {QStringLiteral(".qbx"), QStringLiteral(".scrivx")}) {
            for (const QString &sub : {ext + QStringLiteral("/OpenWithProgids"), ext}) {
                reg.beginGroup(sub);
                const bool empty = reg.allKeys().isEmpty() && reg.childGroups().isEmpty();
                reg.endGroup();
                if (empty)
                    reg.remove(sub);
            }
        }
        reg.sync();
        tellShell();
        if (state(item) != State::Absent) {
            if (error)
                *error = say(QT_TRANSLATE_NOOP("WinShell", "the registry entries could not be removed"));
            return false;
        }
        return true;
    }
    const QString lnk = shortcutFile(item);
    if (QFileInfo::exists(lnk) && !QFile::remove(lnk)) {
        if (error)
            *error = say(QT_TRANSLATE_NOOP("WinShell", "%1 cannot be deleted")).arg(QDir::toNativeSeparators(lnk));
        return false;
    }
    return true;
#else
    Q_UNUSED(item)
    if (error)
        *error = say(QT_TRANSLATE_NOOP("WinShell", "only on Windows"));
    return false;
#endif
}

} // namespace winshell
} // namespace qb
