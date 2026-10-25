#include "core/ItemIcons.h"
#include "core/BinderItem.h"
#include "core/IconCatalog.h"
#include "core/Project.h"

#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace qb {
namespace icons {

static QHash<QString, QIcon> &cache()
{
    static QHash<QString, QIcon> c;
    return c;
}

void clearCache() { cache().clear(); }

static bool s_darkIcons = false;

void setDark(bool dark)
{
    if (dark == s_darkIcons)
        return;
    s_darkIcons = dark;
    clearCache();
}

bool isDark() { return s_darkIcons; }

static bool s_labelTint = true;

void setLabelTint(bool on)
{
    if (on == s_labelTint)
        return;
    s_labelTint = on;
    clearCache();
}

bool labelTint() { return s_labelTint; }

QImage tinted(const QImage &image, const QColor &color)
{
    QImage img = image.convertToFormat(QImage::Format_ARGB32);
    const double lr = color.redF(), lg = color.greenF(), lb = color.blueF();
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0)
                continue;
            const double r = qRed(px) / 255.0, g = qGreen(px) / 255.0, b = qBlue(px) / 255.0;
            // how light (and grey) the pixel is: white 1, black or a strong color 0
            const double light = qMin(r, qMin(g, b));
            double w = qBound(0.0, (light - 0.45) / 0.55, 1.0);
            w = w * w * (3 - 2 * w); // smooth
            const auto mix = [w](double c, double l) { return qBound(0, int(std::lround((c * (1 - w) + c * l * w) * 255)), 255); };
            line[x] = qRgba(mix(r, lr), mix(g, lg), mix(b, lb), a);
        }
    }
    return img;
}

static bool s_preferBundled = false;

void setPreferBundled(bool on)
{
    if (on == s_preferBundled)
        return;
    s_preferBundled = on;
    clearCache();
}

bool preferBundled() { return s_preferBundled; }

static QString bundledPath(const QString &name, int size)
{
    static const char *dirs[] = {"actions", "places", "mimetypes", "status", "apps", "devices", "emblems",
                                 "categories", "emotes"};
    for (const char *d : dirs) {
        const QString p = QStringLiteral(":/icons/%1/%2/%3/%4.svg")
                              .arg(s_darkIcons ? QLatin1String("qbicons-dark") : QLatin1String("qbicons"),
                                   QLatin1String(d))
                              .arg(size)
                              .arg(name);
        if (QFile::exists(p))
            return p;
    }
    return QString();
}

// The picture behind a name right now (cached until the variant changes).
static QIcon resolveBundled(const QString &name)
{
    const QString key = QStringLiteral("b:") + name;
    auto &c = cache();
    auto it = c.constFind(key);
    if (it != c.constEnd())
        return *it;
    QIcon icon;
    for (int size : {16, 22, 32, 48, 64}) {
        const QString p = bundledPath(name, size);
        if (!p.isEmpty())
            icon.addFile(p, QSize(size, size));
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(name);
    c.insert(key, icon);
    return icon;
}

static QIcon resolveAction(const QString &name)
{
    if (s_preferBundled)
        return resolveBundled(name);
    // looked up on every paint: remember the answer until the icons change
    const QString key = QStringLiteral("a:") + name;
    auto &c = cache();
    auto it = c.constFind(key);
    if (it != c.constEnd())
        return *it;
    const QIcon icon = QIcon::hasThemeIcon(name) ? QIcon::fromTheme(name) : resolveBundled(name);
    c.insert(key, icon);
    return icon;
}

namespace {
// An icon that is looked up when it is painted, so that buttons, actions
// and menus follow a change between light and dark themes without being
// given new icons.
class LiveEngine : public QIconEngine
{
public:
    LiveEngine(const QString &name, bool action) : m_name(name), m_action(action) {}
    QIcon icon() const { return m_action ? resolveAction(m_name) : resolveBundled(m_name); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        icon().paint(painter, rect, Qt::AlignCenter, mode, state);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return icon().pixmap(size, mode, state);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        return icon().pixmap(size, scale, mode, state);
    }
    QSize actualSize(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return icon().actualSize(size, mode, state);
    }
    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override
    {
        return icon().availableSizes(mode, state);
    }
    QString iconName() override { return m_name; }
    bool isNull() override { return icon().isNull(); }
    QIconEngine *clone() const override { return new LiveEngine(m_name, m_action); }

private:
    QString m_name;
    bool m_action;
};
} // namespace

QIcon get(const QString &name)
{
    return QIcon(new LiveEngine(name, false));
}

QIcon action(const QString &name)
{
    return QIcon(new LiveEngine(name, true));
}

QString defaultIconName(const Project *project, const BinderItem *item)
{
    if (!item)
        return QStringLiteral("text-x-generic");
    if (project && !project->templatesFolderId.isEmpty() && item->id == project->templatesFolderId)
        return QStringLiteral("folder-templates");
    switch (item->type) {
    case ItemType::Draft: return QStringLiteral("view-pim-journal");
    case ItemType::Research: return QStringLiteral("folder-documents");
    case ItemType::Trash: return item->children.isEmpty() ? QStringLiteral("user-trash") : QStringLiteral("user-trash-full");
    case ItemType::Folder: return QStringLiteral("folder");
    case ItemType::Text:
        if (!item->children.isEmpty())
            return QStringLiteral("document-multiple");
        return item->charCount > 0 ? QStringLiteral("text-x-generic") : QStringLiteral("application-x-zerosize");
    case ItemType::Image: return QStringLiteral("image-x-generic");
    case ItemType::Pdf: return QStringLiteral("application-pdf");
    case ItemType::Media:
        return isAudioSuffix(item->fileExt) ? QStringLiteral("audio-x-generic") : QStringLiteral("video-x-generic");
    case ItemType::WebPage: return QStringLiteral("text-html");
    case ItemType::File: return QStringLiteral("application-octet-stream");
    }
    return QStringLiteral("text-x-generic");
}

static QPixmap basePixmap(const QString &name, int size, const Project *project = nullptr)
{
    QIcon ic = isCustom(name) ? iconFor(project, name) : get(name);
    if (ic.isNull())
        ic = get(QStringLiteral("text-x-generic"));
    return ic.pixmap(QSize(size, size));
}

static void drawBadges(QPainter &p, int size, bool textBadge, bool snapshot, bool templ)
{
    p.setRenderHint(QPainter::Antialiasing);
    if (snapshot) {
        // dog-eared corner
        const qreal s = size * 0.34;
        QPainterPath path;
        path.moveTo(size - s, 0);
        path.lineTo(size, 0);
        path.lineTo(size, s);
        path.closeSubpath();
        p.fillPath(path, QColor(250, 250, 250));
        QPainterPath fold;
        fold.moveTo(size - s, 0);
        fold.lineTo(size - s, s);
        fold.lineTo(size, s);
        fold.closeSubpath();
        p.fillPath(fold, QColor(170, 170, 170));
    }
    if (textBadge) {
        const qreal w = size * 0.42, h = size * 0.5;
        const QRectF r(size - w, size - h, w - 0.5, h - 0.5);
        p.setPen(QPen(QColor(90, 90, 90), qMax(1.0, size / 22.0)));
        p.setBrush(Qt::white);
        p.drawRect(r);
        p.setPen(QPen(QColor(120, 120, 120), qMax(0.8, size / 30.0)));
        for (int i = 1; i <= 3; ++i) {
            const qreal y = r.top() + r.height() * i / 4.0;
            p.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));
        }
    }
    if (templ) {
        const qreal w = size * 0.5;
        const QRectF r(0, size - w, w, w);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 90, 170));
        p.drawRoundedRect(r, 2, 2);
        QFont f;
        f.setBold(true);
        f.setPixelSize(int(w * 0.8));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("T"));
    }
}

QIcon forItem(const Project *project, const BinderItem *item)
{
    if (!item)
        return QIcon();
    QString name = item->icon.isEmpty() ? defaultIconName(project, item) : item->icon;
    // a Scrivener icon without a picture here is shown as the nearest one (0.17)
    if (name.startsWith(QLatin1String("scriv:")))
        name = lookalike(name.mid(6));
    const bool textBadge = item->isFolder() && item->type != ItemType::Trash && item->charCount > 0;
    const bool snapshot = item->hasSnapshots;
    const bool templ = project && project->isTemplate(item);
    // scripts are yellow (documents with an icon of their own keep it)
    const bool script = item->scriptMode && item->icon.isEmpty() && item->type == ItemType::Text;
    QColor tint;
    if (s_labelTint && project && item->label >= 0)
        if (const Label *l = project->labelById(item->label))
            tint = l->color;
    const QString key = QStringLiteral("i:%1:%2%3%4%5:%6")
                            .arg(isCustom(name) && project ? project->folderPath() + QLatin1Char('|') + name : name)
                            .arg(textBadge)
                            .arg(snapshot)
                            .arg(templ)
                            .arg(script)
                            .arg(tint.isValid() ? tint.name() : QString());
    auto &c = cache();
    auto it = c.constFind(key);
    if (it != c.constEnd())
        return *it;
    QIcon icon;
    if (!textBadge && !snapshot && !templ && !script && !tint.isValid()) {
        icon = isCustom(name) ? iconFor(project, name) : get(name);
        if (icon.isNull())
            icon = get(QStringLiteral("text-x-generic"));
    } else {
        for (int size : {16, 22, 32, 48}) {
            QPixmap pm = basePixmap(name, size, project);
            if (tint.isValid())
                pm = QPixmap::fromImage(tinted(pm.toImage(), tint));
            QPainter p(&pm);
            if (script) {
                p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                p.fillRect(pm.rect(), QColor(255, 204, 0, 120));
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            drawBadges(p, size, textBadge, snapshot, templ);
            p.end();
            icon.addPixmap(pm);
        }
    }
    c.insert(key, icon);
    return icon;
}

QList<Choice> customChoices()
{
    // the icons offered before 0.17 (desktop icon theme); Documents > Change Icon
    // shows them under "More Icons" (the catalog is in IconCatalog.h)
    return {
        {QStringLiteral("folder"), QObject::tr("Folder (Blue)")},
        {QStringLiteral("folder-red"), QObject::tr("Folder (Red)")},
        {QStringLiteral("folder-orange"), QObject::tr("Folder (Orange)")},
        {QStringLiteral("folder-yellow"), QObject::tr("Folder (Yellow)")},
        {QStringLiteral("folder-green"), QObject::tr("Folder (Green)")},
        {QStringLiteral("folder-cyan"), QObject::tr("Folder (Cyan)")},
        {QStringLiteral("folder-violet"), QObject::tr("Folder (Violet)")},
        {QStringLiteral("folder-brown"), QObject::tr("Folder (Brown)")},
        {QStringLiteral("folder-grey"), QObject::tr("Folder (Grey)")},
        {QStringLiteral("view-pim-journal"), QObject::tr("Book")},
        {QStringLiteral("text-x-generic"), QObject::tr("Page")},
        {QStringLiteral("user-identity"), QObject::tr("Character")},
        {QStringLiteral("x-office-address-book"), QObject::tr("Characters")},
        {QStringLiteral("globe"), QObject::tr("Place")},
        {QStringLiteral("map-flat"), QObject::tr("Map")},
        {QStringLiteral("view-calendar"), QObject::tr("Timeline")},
        {QStringLiteral("help-hint"), QObject::tr("Idea")},
        {QStringLiteral("view-pim-notes"), QObject::tr("Notes")},
        {QStringLiteral("bookmarks"), QObject::tr("Bookmark")},
        {QStringLiteral("flag"), QObject::tr("Flag")},
        {QStringLiteral("star-shape"), QObject::tr("Star")},
        {QStringLiteral("tag"), QObject::tr("Tag")},
        {QStringLiteral("camera-photo"), QObject::tr("Photo")},
        {QStringLiteral("mail-attachment"), QObject::tr("Attachment")},
        {QStringLiteral("document-edit"), QObject::tr("Draft")},
        {QStringLiteral("chronometer"), QObject::tr("Clock")},
        {QStringLiteral("applications-education-language"), QObject::tr("Language")},
        {QStringLiteral("office-chart-bar"), QObject::tr("Chart")},
    };
}

} // namespace icons
} // namespace qb
