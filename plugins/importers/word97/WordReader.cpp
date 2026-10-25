#include "WordReader.h"

#include "Cfb.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <functional>

using qb::importers::Content;
using qb::importers::Image;
using qb::importers::Note;
using qb::importers::Paragraph;
using qb::importers::Run;

namespace qbdoc {

namespace {

// ------------------------------------------------------------------ bytes

quint8 u8(const QByteArray &d, qint64 at) { return (at >= 0 && at < d.size()) ? quint8(d.at(int(at))) : 0; }
quint16 u16(const QByteArray &d, qint64 at)
{
    return (at >= 0 && at + 2 <= d.size()) ? qFromLittleEndian<quint16>(d.constData() + at) : 0;
}
qint16 i16(const QByteArray &d, qint64 at) { return qint16(u16(d, at)); }
quint32 u32(const QByteArray &d, qint64 at)
{
    return (at >= 0 && at + 4 <= d.size()) ? qFromLittleEndian<quint32>(d.constData() + at) : 0;
}
qint32 i32(const QByteArray &d, qint64 at) { return qint32(u32(d, at)); }
QByteArray slice(const QByteArray &d, qint64 at, qint64 len)
{
    if (at < 0 || len <= 0 || at >= d.size())
        return QByteArray();
    return d.mid(int(at), int(qMin<qint64>(len, d.size() - at)));
}

// Characters 0x80-0x9F of compressed (8-bit) text: Windows-1252.
QChar cp1252(quint8 c)
{
    static const ushort hi[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                  0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                  0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    if (c >= 0x80 && c < 0xA0)
        return QChar(hi[c - 0x80]);
    return QChar(c);
}

// Colors of the old color index (ico).
QColor icoColor(int ico)
{
    static const QRgb table[17] = {0,        0x000000, 0x0000FF, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0xFFFF00, 0xFFFFFF,
                                   0x000080, 0x008080, 0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0};
    if (ico <= 0 || ico > 16)
        return QColor();
    return QColor(table[ico]);
}

// ------------------------------------------------------------------ properties

struct Chp {
    bool bold = false, italic = false, strike = false, dstrike = false, smallCaps = false, caps = false;
    bool vanish = false, underline = false;
    bool fSpec = false, fData = false, fOle2 = false, rmarkDel = false;
    int hps = 20;          // half points
    int iss = 0;           // 1 super, 2 sub
    int ftc = -1;          // font index
    QColor color;          // invalid: automatic
    int highlight = 0;     // ico
    qint32 picLocation = -1;
    int istd = -1;         // character style
    quint16 symbolChar = 0;
};

struct Pap {
    int istd = 0;
    int jc = 0;
    int dxaLeft = 0, dxaRight = 0, dxaLeft1 = 0;
    bool pageBreakBefore = false;
    int ilvl = 0, ilfo = 0;
    bool inTable = false, ttp = false;
    int itap = 0;
    int outLvl = 9;
};

bool toggle(quint8 v, bool styleValue)
{
    switch (v) {
    case 0: return false;
    case 1: return true;
    case 0x80: return styleValue;
    case 0x81: return !styleValue;
    default: return v != 0;
    }
}

// Size of a sprm's operand; `op` points at the operand.
int operandSize(quint16 sprm, const QByteArray &g, int op)
{
    switch (sprm >> 13) {
    case 0:
    case 1: return 1;
    case 2:
    case 4:
    case 5: return 2;
    case 3: return 4;
    case 7: return 3;
    default:
        break;
    }
    if (sprm == 0xD608 || sprm == 0xD606) // sprmTDefTable: 2-byte size, one too large
        return 2 + qMax(0, int(u16(g, op)) - 1);
    if (sprm == 0xC615 && u8(g, op) == 255) { // sprmPChgTabs with its own layout
        int n = 1;
        const int del = u8(g, op + n);
        n += 1 + del * 4;
        const int add = u8(g, op + n);
        n += 1 + add * 3;
        return n;
    }
    return 1 + u8(g, op);
}

template<typename F>
void forEachSprm(const QByteArray &g, F f)
{
    int at = 0;
    while (at + 2 <= g.size()) {
        const quint16 sprm = u16(g, at);
        const int op = at + 2;
        const int len = operandSize(sprm, g, op);
        if (op + len > g.size() + 1)
            break;
        f(sprm, op, len);
        at = op + len;
    }
}

void applyChp(Chp &c, const Chp &style, const QByteArray &g, quint16 sprm, int op)
{
    switch (sprm) {
    case 0x0835: c.bold = toggle(u8(g, op), style.bold); break;
    case 0x0836: c.italic = toggle(u8(g, op), style.italic); break;
    case 0x0837: c.strike = toggle(u8(g, op), style.strike); break;
    case 0x2A53: c.dstrike = toggle(u8(g, op), style.dstrike); break;
    case 0x083A: c.smallCaps = toggle(u8(g, op), style.smallCaps); break;
    case 0x083B: c.caps = toggle(u8(g, op), style.caps); break;
    case 0x083C: c.vanish = toggle(u8(g, op), style.vanish); break;
    case 0x2A3E: c.underline = u8(g, op) != 0; break;
    case 0x2A42: c.color = icoColor(u8(g, op)); break;
    case 0x6870: {
        const quint32 cv = u32(g, op);
        if ((cv & 0xFF000000) == 0xFF000000)
            c.color = QColor();
        else
            c.color = QColor(int(cv & 0xFF), int((cv >> 8) & 0xFF), int((cv >> 16) & 0xFF));
        break;
    }
    case 0x4A43: c.hps = qMax(2, int(u16(g, op))); break;
    case 0x2A48: c.iss = u8(g, op); break;
    case 0x4A4F: c.ftc = u16(g, op); break;
    case 0x2A0C: c.highlight = u8(g, op); break;
    case 0x0855: c.fSpec = u8(g, op) != 0; break;
    case 0x0806: c.fData = u8(g, op) != 0; break;
    case 0x080A: c.fOle2 = u8(g, op) != 0; break;
    case 0x0800: c.rmarkDel = u8(g, op) != 0; break;
    case 0x6A03: c.picLocation = i32(g, op); break;
    case 0x6A09: c.symbolChar = u16(g, op + 2); break;
    case 0x2A33: { // sprmCPlain: back to the style
        const qint32 pic = c.picLocation;
        const bool spec = c.fSpec;
        c = style;
        c.picLocation = pic;
        c.fSpec = spec;
        break;
    }
    default: break;
    }
}

void applyPap(Pap &p, const QByteArray &g, quint16 sprm, int op)
{
    switch (sprm) {
    case 0x4600: p.istd = u16(g, op); break;
    case 0x2403:
    case 0x2461: p.jc = u8(g, op); break;
    case 0x840F:
    case 0x845E: p.dxaLeft = i16(g, op); break;
    case 0x840E:
    case 0x845D: p.dxaRight = i16(g, op); break;
    case 0x8411:
    case 0x8460: p.dxaLeft1 = i16(g, op); break;
    case 0x2407: p.pageBreakBefore = u8(g, op) != 0; break;
    case 0x260A: p.ilvl = qBound(0, int(u8(g, op)), 8); break;
    case 0x460B: p.ilfo = i16(g, op); break;
    case 0x2416: p.inTable = u8(g, op) != 0; break;
    case 0x2417: p.ttp = u8(g, op) != 0; break;
    case 0x6649: p.itap = i32(g, op); break;
    case 0x244C: p.ttp = p.ttp || u8(g, op) != 0; break;
    case 0x2640: p.outLvl = u8(g, op); break;
    default: break;
    }
}

struct StyleDef {
    QString name;
    int sti = 0x0FFE;
    int stk = 0;
    int base = 0x0FFF;
    QByteArray papx, chpx;
    // resolved
    bool done = false, busy = false;
    Chp chp;
    Pap pap;
};

struct Piece {
    quint32 cpStart = 0, cpEnd = 0;
    quint32 fc = 0;          // stream offset of the first character
    bool compressed = false;
    QByteArray prmGrpprl;    // properties of the whole piece (fast save)
};

struct FcRun {
    quint32 start = 0, end = 0;
    QByteArray grpprl;
    int istd = 0;            // paragraphs
};

struct Plc {
    QVector<quint32> cps;
    QByteArray data;         // n entries of cbData bytes
    int cbData = 0;
    int count() const { return qMax(0, int(cps.size()) - 1); }
    QByteArray entry(int i) const { return data.mid(i * cbData, cbData); }
};

Plc readPlc(const QByteArray &t, quint32 fc, quint32 lcb, int cbData)
{
    Plc p;
    if (lcb < 4 || qint64(fc) + lcb > t.size())
        return p;
    const int n = int((lcb - 4) / (4 + cbData));
    for (int i = 0; i <= n; ++i)
        p.cps << u32(t, fc + qint64(i) * 4);
    p.cbData = cbData;
    p.data = slice(t, fc + qint64(n + 1) * 4, qint64(n) * cbData);
    return p;
}

QString readXst(const QByteArray &d, qint64 at, qint64 *after = nullptr)
{
    const int cch = u16(d, at);
    QString s;
    for (int i = 0; i < cch && at + 2 + i * 2 + 2 <= d.size(); ++i)
        s.append(QChar(u16(d, at + 2 + i * 2)));
    if (after)
        *after = at + 2 + qint64(cch) * 2;
    return s;
}

// The address of a HYPERLINK field ("HYPERLINK "http://…" \\o "tip""); empty for
// links inside the document (\\l "bookmark") and other fields.
QString hyperlinkTarget(const QString &instr)
{
    QStringList tokens;
    static const QRegularExpression tok(QStringLiteral("\"([^\"]*)\"|(\\S+)"));
    for (auto it = tok.globalMatch(instr); it.hasNext();) {
        const auto m = it.next();
        tokens << (m.capturedStart(1) >= 0 ? m.captured(1) : m.captured(2));
    }
    if (tokens.isEmpty() || tokens.first().compare(QLatin1String("HYPERLINK"), Qt::CaseInsensitive) != 0)
        return QString();
    for (int i = 1; i < tokens.size(); ++i) {
        const QString &t = tokens.at(i);
        if (t.startsWith(QLatin1Char('\\'))) {
            if (t == QLatin1String("\\l"))
                return QString(); // a place in this document
            ++i;                  // the switch's argument
            continue;
        }
        return t;
    }
    return QString();
}

// ------------------------------------------------------------------ the reader

class Reader
{
public:
    Failure run(const QByteArray &file, Content *out, QString *error);

private:
    // FIB
    QPair<quint32, quint32> fcLcb(int index) const
    {
        if (index >= m_cbRgFcLcb)
            return {0, 0};
        return {u32(m_wd, m_fcLcbStart + index * 8), u32(m_wd, m_fcLcbStart + index * 8 + 4)};
    }
    void readPieces();
    void readFkps(int index, bool para);
    void readStyles();
    const StyleDef *resolveStyle(int istd);
    void readLists();
    void readFonts();
    void readNotes();
    QChar charAt(quint32 cp, quint32 *fc, const Piece **piece) const;
    const Piece *pieceFor(quint32 cp) const;
    const FcRun *runFor(const QVector<FcRun> &runs, quint32 fc) const;
    Chp chpAt(quint32 cp, const Pap &pap) const;
    Pap papForMark(quint32 cp) const;
    void emitRange(quint32 from, quint32 to, bool main);
    QString plainText(quint32 from, quint32 to);
    int pictureAt(const Chp &chp);
    void flushParagraph(Paragraph &p, const Pap &pap, bool tableRow);

    QByteArray m_wd, m_table, m_data;
    qint64 m_fcLcbStart = 0;
    int m_cbRgFcLcb = 0;
    quint32 m_ccpText = 0, m_ccpFtn = 0, m_ccpHdd = 0, m_ccpMcr = 0, m_ccpAtn = 0, m_ccpEdn = 0, m_ccpTxbx = 0;
    QVector<Piece> m_pieces;
    QVector<QByteArray> m_prc;
    QVector<FcRun> m_chpRuns, m_papRuns;
    QVector<StyleDef> m_styles;
    int m_defaultFtc = 0;
    QStringList m_fonts;
    QHash<int, QVector<bool>> m_lfoNumbered;  // ilfo (1-based) -> numbered per level
    // notes in the main text: cp -> note index
    QHash<quint32, int> m_footnoteAt, m_commentAt;
    QVector<int> m_commentOfCp;   // main text cp -> comment note (anchor ranges)
    Content *m_out = nullptr;
    QHash<qint32, int> m_pictures; // picture location -> image index
    int m_skippedPictures = 0;
    bool m_carryBreak = false;
    bool m_atBreak = false;
    mutable QHash<quint64, Chp> m_chpCache;
    int m_objects = 0;
};

QChar Reader::charAt(quint32 cp, quint32 *fc, const Piece **piece) const
{
    const Piece *p = pieceFor(cp);
    if (piece)
        *piece = p;
    if (!p) {
        if (fc)
            *fc = 0;
        return QChar();
    }
    const quint32 i = cp - p->cpStart;
    if (p->compressed) {
        const quint32 at = p->fc + i;
        if (fc)
            *fc = at;
        return cp1252(u8(m_wd, at));
    }
    const quint32 at = p->fc + i * 2;
    if (fc)
        *fc = at;
    return QChar(u16(m_wd, at));
}

const Piece *Reader::pieceFor(quint32 cp) const
{
    auto it = std::upper_bound(m_pieces.begin(), m_pieces.end(), cp,
                               [](quint32 v, const Piece &p) { return v < p.cpEnd; });
    if (it == m_pieces.end() || cp < it->cpStart)
        return nullptr;
    return &*it;
}

const FcRun *Reader::runFor(const QVector<FcRun> &runs, quint32 fc) const
{
    auto it = std::upper_bound(runs.begin(), runs.end(), fc, [](quint32 v, const FcRun &r) { return v < r.end; });
    if (it == runs.end() || fc < it->start)
        return nullptr;
    return &*it;
}

void Reader::readPieces()
{
    const auto [fc, lcb] = fcLcb(33);
    const QByteArray clx = slice(m_table, fc, lcb);
    int at = 0;
    while (at < clx.size() && u8(clx, at) == 0x01) {
        const int cb = i16(clx, at + 1);
        m_prc << clx.mid(at + 3, cb);
        at += 3 + qMax(0, cb);
    }
    if (u8(clx, at) != 0x02)
        return;
    const quint32 len = u32(clx, at + 1);
    const QByteArray plc = clx.mid(at + 5, int(len));
    const int n = int((plc.size() - 4) / 12);
    for (int i = 0; i < n; ++i) {
        Piece p;
        p.cpStart = u32(plc, i * 4);
        p.cpEnd = u32(plc, (i + 1) * 4);
        const qint64 pcd = qint64(n + 1) * 4 + qint64(i) * 8;
        const quint32 raw = u32(plc, pcd + 2);
        p.compressed = raw & 0x40000000;
        p.fc = p.compressed ? (raw & 0x3FFFFFFF) / 2 : (raw & 0x3FFFFFFF);
        const quint16 prm = u16(plc, pcd + 6);
        if (prm & 1) {
            const int idx = prm >> 1;
            if (idx < m_prc.size())
                p.prmGrpprl = m_prc.at(idx);
        }
        if (p.cpEnd > p.cpStart)
            m_pieces << p;
    }
    std::sort(m_pieces.begin(), m_pieces.end(), [](const Piece &a, const Piece &b) { return a.cpStart < b.cpStart; });
}

void Reader::readFkps(int index, bool para)
{
    const auto [fc, lcb] = fcLcb(index);
    const Plc bte = readPlc(m_table, fc, lcb, 4);
    QVector<FcRun> &runs = para ? m_papRuns : m_chpRuns;
    for (int i = 0; i < bte.count(); ++i) {
        const quint32 pn = u32(bte.data, i * 4) & 0x3FFFFF;
        const QByteArray page = slice(m_wd, qint64(pn) * 512, 512);
        if (page.size() < 512)
            continue;
        const int n = u8(page, 511);
        for (int k = 0; k < n; ++k) {
            FcRun r;
            r.start = u32(page, k * 4);
            r.end = u32(page, (k + 1) * 4);
            if (para) {
                const int bOff = u8(page, (n + 1) * 4 + k * 13) * 2;
                if (bOff > 0 && bOff < 511) {
                    int cb = u8(page, bOff);
                    int start = bOff + 1;
                    int size;
                    if (cb == 0) {
                        cb = u8(page, bOff + 1);
                        start = bOff + 2;
                        size = cb * 2;
                    } else {
                        size = cb * 2 - 1;
                    }
                    const QByteArray gp = page.mid(start, qMin(size, 511 - start));
                    r.istd = u16(gp, 0);
                    r.grpprl = gp.mid(2);
                }
            } else {
                const int off = u8(page, (n + 1) * 4 + k) * 2;
                if (off > 0 && off < 511) {
                    const int cb = u8(page, off);
                    r.grpprl = page.mid(off + 1, qMin(cb, 510 - off));
                }
            }
            if (r.end > r.start)
                runs << r;
        }
    }
    std::sort(runs.begin(), runs.end(), [](const FcRun &a, const FcRun &b) { return a.start < b.start; });
}

void Reader::readStyles()
{
    const auto [fc, lcb] = fcLcb(1);
    const QByteArray st = slice(m_table, fc, lcb);
    const int cbStshi = u16(st, 0);
    const int cstd = u16(st, 2);
    const int cbBase = u16(st, 4) ? u16(st, 4) : 10;
    if (cbStshi >= 18)
        m_defaultFtc = u16(st, 2 + 12);
    qint64 at = 2 + cbStshi;
    for (int i = 0; i < cstd && at + 2 <= st.size(); ++i) {
        const int cbStd = u16(st, at);
        StyleDef s;
        if (cbStd > 0) {
            const QByteArray std = slice(st, at + 2, cbStd);
            s.sti = u16(std, 0) & 0x0FFF;
            s.stk = u16(std, 2) & 0x000F;
            s.base = u16(std, 2) >> 4;
            const int cupx = u16(std, 4) & 0x000F;
            qint64 p = cbBase;
            s.name = readXst(std, p, &p);
            p += 2; // terminating zero
            for (int u = 0; u < cupx && p + 2 <= std.size(); ++u) {
                if (p & 1)
                    ++p;
                const int cb = u16(std, p);
                const QByteArray upx = slice(std, p + 2, cb);
                p += 2 + cb;
                if (s.stk == 1) {
                    if (u == 0)
                        s.papx = upx.mid(2); // after the istd
                    else if (u == 1)
                        s.chpx = upx;
                } else if (s.stk == 2 && u == 0) {
                    s.chpx = upx;
                }
            }
        } else {
            s.stk = 0; // empty slot
        }
        m_styles << s;
        at += 2 + cbStd;
    }
}

const StyleDef *Reader::resolveStyle(int istd)
{
    if (istd < 0 || istd >= m_styles.size())
        return nullptr;
    StyleDef &s = m_styles[istd];
    if (s.done || s.busy)
        return &s;
    s.busy = true;
    Chp chp;
    chp.ftc = m_defaultFtc;
    Pap pap;
    if (s.base != 0x0FFF && s.base != istd)
        if (const StyleDef *b = resolveStyle(s.base)) {
            chp = b->chp;
            pap = b->pap;
        }
    const Chp baseChp = chp;
    forEachSprm(s.chpx, [&](quint16 sprm, int op, int) { applyChp(chp, baseChp, s.chpx, sprm, op); });
    forEachSprm(s.papx, [&](quint16 sprm, int op, int) { applyPap(pap, s.papx, sprm, op); });
    chp.istd = -1;
    chp.fSpec = false;
    chp.picLocation = -1;
    s.chp = chp;
    s.pap = pap;
    s.pap.istd = istd;
    s.done = true;
    s.busy = false;
    return &s;
}

void Reader::readLists()
{
    // LSTF records with their levels (only the kind of numbering is needed)
    auto [fc, lcb] = fcLcb(73);
    QHash<quint32, QVector<bool>> byLsid;
    if (lcb > 2) {
        const QByteArray &t = m_table;
        const int n = i16(t, fc);
        qint64 at = fc + 2;
        QVector<QPair<quint32, bool>> lsts; // lsid, simple
        for (int i = 0; i < n && at + 28 <= t.size(); ++i) {
            lsts << qMakePair(u32(t, at), bool(u8(t, at + 26) & 1));
            at += 28;
        }
        // the LVLs follow all LSTFs
        for (const auto &l : lsts) {
            QVector<bool> numbered;
            const int levels = l.second ? 1 : 9;
            for (int k = 0; k < levels && at + 28 <= t.size(); ++k) {
                const int nfc = u8(t, at + 4);
                const int cbChpx = u8(t, at + 24);
                const int cbPapx = u8(t, at + 25);
                numbered << (nfc != 23 && nfc != 255);
                qint64 p = at + 28 + cbPapx + cbChpx;
                qint64 after = p;
                readXst(t, p, &after);
                at = after;
            }
            byLsid.insert(l.first, numbered);
        }
    }
    auto [fc2, lcb2] = fcLcb(74);
    if (lcb2 > 4) {
        const quint32 n = u32(m_table, fc2);
        for (quint32 i = 0; i < n && i < 10000; ++i) {
            const quint32 lsid = u32(m_table, fc2 + 4 + qint64(i) * 16);
            m_lfoNumbered.insert(int(i) + 1, byLsid.value(lsid));
        }
    }
}

void Reader::readFonts()
{
    const auto [fc, lcb] = fcLcb(15);
    const QByteArray t = slice(m_table, fc, lcb);
    if (t.size() < 4)
        return;
    int n = u16(t, 0);
    qint64 at = 4;
    if (n == 0xFFFF) { // an extended table header
        n = u16(t, 2);
        at = 6;
    }
    for (int i = 0; i < n && at < t.size(); ++i) {
        const int len = u8(t, at) + 1;
        QString name;
        for (qint64 p = at + 40; p + 2 <= at + len && u16(t, p) != 0; p += 2)
            name.append(QChar(u16(t, p)));
        m_fonts << name;
        at += len;
    }
}

void Reader::readNotes()
{
    // footnotes and endnotes: a reference in the main text, the text in a subdocument
    auto addNotes = [this](int refIndex, int txtIndex, quint32 base, Note::Kind kind) {
        const auto [rf, rl] = fcLcb(refIndex);
        const auto [tf, tl] = fcLcb(txtIndex);
        const Plc ref = readPlc(m_table, rf, rl, 2);
        const Plc txt = readPlc(m_table, tf, tl, 0);
        for (int i = 0; i < ref.count(); ++i) {
            if (i + 1 >= txt.cps.size())
                break;
            Note n;
            n.kind = kind;
            n.text = plainText(base + txt.cps.at(i), base + txt.cps.at(i + 1));
            m_out->notes << n;
            m_footnoteAt.insert(ref.cps.at(i), int(m_out->notes.size()) - 1);
        }
    };
    addNotes(2, 3, m_ccpText, Note::Footnote);
    addNotes(46, 47, m_ccpText + m_ccpFtn + m_ccpHdd + m_ccpMcr + m_ccpAtn, Note::Endnote);

    // comments
    const auto [af, al] = fcLcb(4);
    const auto [tf, tl] = fcLcb(5);
    const Plc ref = readPlc(m_table, af, al, 30);
    const Plc txt = readPlc(m_table, tf, tl, 0);
    if (ref.count() == 0)
        return;
    QStringList owners;
    {
        const auto [of, ol] = fcLcb(36);
        const QByteArray o = slice(m_table, of, ol);
        qint64 at = 0;
        while (at + 2 <= o.size()) {
            qint64 after = at;
            owners << readXst(o, at, &after);
            if (after <= at)
                break;
            at = after;
        }
    }
    // the commented ranges: bookmarks whose tag the comment names
    QHash<qint32, QPair<quint32, quint32>> ranges;
    {
        const auto [sf, sl] = fcLcb(37);
        const QByteArray s = slice(m_table, sf, sl);
        QVector<qint32> tags;
        if (u16(s, 0) == 0xFFFF) {
            const int n = u16(s, 2);
            const int extra = u16(s, 4);
            qint64 at = 6;
            for (int i = 0; i < n && at < s.size(); ++i) {
                const int cch = u16(s, at);
                at += 2 + cch * 2;
                tags << i32(s, at + 2);
                at += extra;
            }
        }
        const auto [bf, bl] = fcLcb(42);
        const auto [ef, el] = fcLcb(43);
        const Plc bkf = readPlc(m_table, bf, bl, 4);
        const Plc bkl = readPlc(m_table, ef, el, 0);
        for (int k = 0; k < bkf.count() && k < tags.size(); ++k) {
            const int ibkl = u16(bkf.data, k * 4);
            if (ibkl < bkl.cps.size())
                ranges.insert(tags.at(k), {bkf.cps.at(k), bkl.cps.at(ibkl)});
        }
    }
    m_commentOfCp = QVector<int>(int(m_ccpText) + 1, -1);
    const quint32 base = m_ccpText + m_ccpFtn + m_ccpHdd + m_ccpMcr;
    for (int i = 0; i < ref.count(); ++i) {
        const QByteArray atrd = ref.entry(i);
        Note n;
        n.kind = Note::Comment;
        const int ibst = i16(atrd, 20);
        if (ibst >= 0 && ibst < owners.size())
            n.author = owners.at(ibst);
        if (n.author.isEmpty())
            n.author = readXst(atrd, 0).left(9);
        if (i + 1 < txt.cps.size())
            n.text = plainText(base + txt.cps.at(i), base + txt.cps.at(i + 1));
        m_out->notes << n;
        const int idx = int(m_out->notes.size()) - 1;
        const quint32 refCp = ref.cps.at(i);
        m_commentAt.insert(refCp, idx);
        quint32 from = refCp, to = refCp;
        const qint32 tag = i32(atrd, 26);
        if (tag != -1 && ranges.contains(tag)) {
            from = ranges.value(tag).first;
            to = qMax(ranges.value(tag).second, from);
        }
        if (from == to) {
            // no range: the word before the mark
            quint32 c = refCp;
            while (c > 0 && charAt(c - 1, nullptr, nullptr).isSpace())
                --c;
            while (c > 0 && !charAt(c - 1, nullptr, nullptr).isSpace() && charAt(c - 1, nullptr, nullptr) >= QChar(0x20))
                --c;
            from = c;
            to = refCp;
        }
        for (quint32 c = from; c < to && c < m_ccpText; ++c)
            m_commentOfCp[int(c)] = idx;
    }
}

Pap Reader::papForMark(quint32 cp) const
{
    Pap pap;
    quint32 fc = 0;
    const Piece *piece = nullptr;
    charAt(cp, &fc, &piece);
    const FcRun *r = runFor(m_papRuns, fc);
    const int istd = r ? r->istd : 0;
    if (istd >= 0 && istd < m_styles.size() && m_styles.at(istd).done)
        pap = m_styles.at(istd).pap;
    pap.istd = istd;
    if (r)
        forEachSprm(r->grpprl, [&](quint16 sprm, int op, int) { applyPap(pap, r->grpprl, sprm, op); });
    if (piece && !piece->prmGrpprl.isEmpty())
        forEachSprm(piece->prmGrpprl, [&](quint16 sprm, int op, int) { applyPap(pap, piece->prmGrpprl, sprm, op); });
    return pap;
}

Chp Reader::chpAt(quint32 cp, const Pap &pap) const
{
    Chp style;
    style.ftc = m_defaultFtc;
    if (pap.istd >= 0 && pap.istd < m_styles.size() && m_styles.at(pap.istd).done)
        style = m_styles.at(pap.istd).chp;
    quint32 fc = 0;
    const Piece *piece = nullptr;
    charAt(cp, &fc, &piece);
    const FcRun *r = runFor(m_chpRuns, fc);
    // the same run of the same piece in the same style looks the same
    const quint64 key = (quint64(r ? quint32(r - m_chpRuns.constData()) + 1 : 0) << 40)
                        ^ (quint64(piece ? quint32(piece - m_pieces.constData()) + 1 : 0) << 16) ^ quint64(pap.istd & 0xFFFF);
    const auto hit = m_chpCache.constFind(key);
    if (hit != m_chpCache.constEnd())
        return hit.value();
    QByteArray g = r ? r->grpprl : QByteArray();
    if (piece && !piece->prmGrpprl.isEmpty())
        g += piece->prmGrpprl;
    // a character style first
    forEachSprm(g, [&](quint16 sprm, int op, int) {
        if (sprm == 0x4A30) {
            const int istd = u16(g, op);
            if (istd >= 0 && istd < m_styles.size() && m_styles.at(istd).stk == 2) {
                const StyleDef &cs = m_styles.at(istd);
                const Chp base = style;
                forEachSprm(cs.chpx, [&](quint16 s2, int op2, int) { applyChp(style, base, cs.chpx, s2, op2); });
                style.istd = istd;
            }
        }
    });
    Chp chp = style;
    forEachSprm(g, [&](quint16 sprm, int op, int) { applyChp(chp, style, g, sprm, op); });
    m_chpCache.insert(key, chp);
    return chp;
}

QString Reader::plainText(quint32 from, quint32 to)
{
    QString out;
    int depth = 0;
    QVector<bool> inResult;
    for (quint32 cp = from; cp < to; ++cp) {
        const QChar ch = charAt(cp, nullptr, nullptr);
        const ushort u = ch.unicode();
        if (u == 0x13) {
            ++depth;
            inResult << false;
            continue;
        }
        if (u == 0x14) {
            if (!inResult.isEmpty())
                inResult.last() = true;
            continue;
        }
        if (u == 0x15) {
            if (!inResult.isEmpty())
                inResult.removeLast();
            depth = qMax(0, depth - 1);
            continue;
        }
        bool visible = true;
        for (bool r : inResult)
            visible = visible && r;
        if (!visible)
            continue;
        if (u == 0x0D || u == 0x07 || u == 0x0B || u == 0x0C)
            out += QLatin1Char('\n');
        else if (u == 0x09)
            out += QLatin1Char('\t');
        else if (u >= 0x20 && u != 0x1F)
            out += u == 0x1E ? QChar(0x2011) : ch;
    }
    return out.trimmed();
}

int Reader::pictureAt(const Chp &chp)
{
    if (chp.picLocation < 0 || m_data.isEmpty())
        return -1;
    if (m_pictures.contains(chp.picLocation))
        return m_pictures.value(chp.picLocation);
    const qint64 off = chp.picLocation;
    const qint64 lcb = i32(m_data, off);
    const int cbHeader = u16(m_data, off + 4);
    if (lcb <= cbHeader || cbHeader < 0x44 || off + lcb > m_data.size()) {
        ++m_skippedPictures;
        return -1;
    }
    const int mm = u16(m_data, off + 6);
    const int dxaGoal = i16(m_data, off + 0x1C);
    const int dyaGoal = i16(m_data, off + 0x1E);
    const int mx = u16(m_data, off + 0x20);
    const int my = u16(m_data, off + 0x22);
    qint64 pos = off + cbHeader;
    if (mm == 0x0066)
        pos += 1 + u8(m_data, pos);
    const qint64 end = off + lcb;
    QByteArray bytes;
    bool metafile = false;
    // records: the shape container, then the picture (inside an FBSE or alone)
    std::function<void(qint64, qint64)> walk = [&](qint64 p, qint64 stop) {
        while (bytes.isEmpty() && p + 8 <= stop) {
            const quint16 verInst = u16(m_data, p);
            const quint16 type = u16(m_data, p + 2);
            const quint32 len = u32(m_data, p + 4);
            const qint64 body = p + 8;
            const qint64 next = body + len;
            if (next > stop + 1 || len > quint32(m_data.size()))
                return;
            const int inst = verInst >> 4;
            if (type == 0xF007) { // FBSE: 36 bytes, a name, then the picture record
                walk(body + 36 + u8(m_data, body + 33), next);
            } else if (type == 0xF01D || type == 0xF01E || type == 0xF029 || type == 0xF02A || type == 0xF01F) {
                const bool twoUids = (inst & 1) != 0;
                const qint64 data = body + 16 + (twoUids ? 16 : 0) + 1;
                QByteArray b = slice(m_data, data, next - data);
                if (type == 0xF01F && b.size() > 40) {
                    // a device-independent bitmap: give it the header of a BMP file
                    const quint32 hdr = u32(b, 0);
                    const int bits = u16(b, 14);
                    const quint32 comp = u32(b, 16);
                    quint32 colors = u32(b, 32);
                    if (colors == 0 && bits <= 8)
                        colors = 1u << bits;
                    const quint32 offset = 14 + hdr + colors * 4 + (comp == 3 && hdr == 40 ? 12 : 0);
                    QByteArray bmp("BM");
                    QByteArray le(12, 0);
                    qToLittleEndian<quint32>(quint32(14 + b.size()), le.data());
                    qToLittleEndian<quint32>(offset, le.data() + 8);
                    bmp += le + b;
                    b = bmp;
                }
                bytes = b;
            } else if (type == 0xF01A || type == 0xF01B || type == 0xF01C) {
                metafile = true;
            } else if (type == 0xF004 || type == 0xF003 || type == 0xF001) {
                // containers we do not need (shape properties)
            }
            p = next;
        }
    };
    walk(pos, end);
    if (bytes.isEmpty() && !metafile) {
        // no records found: look for the picture itself
        const QByteArray area = slice(m_data, pos, end - pos);
        int i = area.indexOf(QByteArray("\x89PNG\r\n\x1a\n", 8));
        if (i < 0)
            i = area.indexOf(QByteArray("\xFF\xD8\xFF", 3));
        if (i >= 0)
            bytes = area.mid(i);
    }
    if (bytes.isEmpty()) {
        ++m_skippedPictures;
        m_pictures.insert(chp.picLocation, -1);
        return -1;
    }
    Image img;
    img.data = bytes;
    if (dxaGoal > 0)
        img.width = dxaGoal / 20.0 * (mx > 0 ? mx : 1000) / 1000.0;
    if (dyaGoal > 0)
        img.height = dyaGoal / 20.0 * (my > 0 ? my : 1000) / 1000.0;
    m_out->images << img;
    const int idx = int(m_out->images.size()) - 1;
    m_pictures.insert(chp.picLocation, idx);
    return idx;
}

void Reader::flushParagraph(Paragraph &p, const Pap &pap, bool tableRow)
{
    if (m_carryBreak) {
        p.pageBreakBefore = true;
        m_carryBreak = false;
    }
    if (p.runs.isEmpty() && (p.pageBreakBefore || m_atBreak)) {
        // an empty paragraph that only holds a page break: the break goes to the next one
        m_carryBreak = p.pageBreakBefore || pap.pageBreakBefore || m_atBreak;
        m_atBreak = false;
        p = Paragraph();
        return;
    }
    m_atBreak = false;
    // style: by name, by the built-in style number, by outline level
    const StyleDef *s = (pap.istd >= 0 && pap.istd < m_styles.size()) ? &m_styles.at(pap.istd) : nullptr;
    if (s) {
        if (s->sti >= 1 && s->sti <= 9)
            p.headingLevel = s->sti;
        else if (s->sti == 62)
            p.style = QStringLiteral("Title");
        else if (s->sti == 74)
            p.style = QStringLiteral("Subtitle");
        else if (s->sti == 34)
            p.style = QStringLiteral("Caption");
        else if (s->sti != 0)
            p.style = s->name;
    }
    if (p.headingLevel == 0 && p.style.isEmpty() && pap.outLvl >= 0 && pap.outLvl < 9 && !tableRow)
        p.headingLevel = pap.outLvl + 1;
    switch (pap.jc) {
    case 1: p.align = 1; break;
    case 2: p.align = 2; break;
    case 3:
    case 4:
    case 5: p.align = 3; break;
    default: p.align = 0; break;
    }
    p.leftIndent = pap.dxaLeft / 20.0;
    p.rightIndent = pap.dxaRight / 20.0;
    p.firstIndent = pap.dxaLeft1 / 20.0;
    // headings often carry the outline numbering of the document: not a list
    const bool heading = p.headingLevel > 0 || p.style.startsWith(QLatin1String("heading"), Qt::CaseInsensitive)
                         || p.style == QLatin1String("Title") || p.style == QLatin1String("Subtitle");
    if (pap.ilfo > 0 && !tableRow && !heading) {
        p.listLevel = pap.ilvl;
        p.listId = pap.ilfo;
        const QVector<bool> kinds = m_lfoNumbered.value(pap.ilfo);
        p.numbered = pap.ilvl < kinds.size() ? kinds.at(pap.ilvl) : false;
        p.leftIndent = p.firstIndent = 0;
    }
    p.tableRow = tableRow;
    if (pap.pageBreakBefore)
        p.pageBreakBefore = true;
    // merge neighboring runs with the same look
    QList<Run> merged;
    for (const Run &r : p.runs) {
        if (!merged.isEmpty() && r.image < 0 && r.note < 0 && merged.last().image < 0 && merged.last().note < 0) {
            Run &l = merged.last();
            if (l.bold == r.bold && l.italic == r.italic && l.underline == r.underline && l.strike == r.strike
                && l.smallCaps == r.smallCaps && l.allCaps == r.allCaps && l.vertical == r.vertical && l.link == r.link
                && l.font == r.font && l.size == r.size && l.color == r.color && l.highlight == r.highlight
                && l.comment == r.comment && l.code == r.code) {
                l.text += r.text;
                continue;
            }
        }
        merged << r;
    }
    p.runs = merged;
    m_out->paragraphs << p;
    p = Paragraph();
}

void Reader::emitRange(quint32 from, quint32 to, bool main)
{
    struct Field {
        bool result = false;
        QString instr;
        QString link;
    };
    QVector<Field> fields;
    Paragraph para;
    bool pendingBreak = false;
    // the paragraph format is known at the paragraph's end: find marks first
    quint32 cp = from;
    while (cp < to) {
        // end of this paragraph
        quint32 end = cp;
        bool cellRow = false;
        while (end < to) {
            const ushort u = charAt(end, nullptr, nullptr).unicode();
            if (u == 0x0D || u == 0x07 || u == 0x0C)
                break;
            ++end;
        }
        const quint32 mark = qMin(end, to - 1);
        const ushort markChar = end < to ? charAt(end, nullptr, nullptr).unicode() : 0x0D;
        // (a page break inside a paragraph belongs to the paragraph's format run as well)
        Pap pap = papForMark(mark);
        bool inTable = pap.inTable || pap.itap > 0;
        // a table row: cells end with 0x07, the row with 0x07 of a TTP paragraph
        if (inTable && markChar != 0x0C) {
            // collect the whole row
            quint32 rowEnd = end;
            while (rowEnd < to) {
                const ushort u = charAt(rowEnd, nullptr, nullptr).unicode();
                if (u == 0x07 && papForMark(rowEnd).ttp)
                    break;
                ++rowEnd;
            }
            end = qMin(rowEnd, to);
            cellRow = true;
        }
        if (pendingBreak) {
            para.pageBreakBefore = true;
            pendingBreak = false;
        }
        for (quint32 c = cp; c < end; ++c) {
            const QChar ch = charAt(c, nullptr, nullptr);
            const ushort u = ch.unicode();
            if (u == 0x13) {
                fields << Field();
                continue;
            }
            if (u == 0x14) {
                if (!fields.isEmpty()) {
                    Field &f = fields.last();
                    f.result = true;
                    f.link = hyperlinkTarget(f.instr);
                }
                continue;
            }
            if (u == 0x15) {
                if (!fields.isEmpty())
                    fields.removeLast();
                continue;
            }
            if (!fields.isEmpty() && !fields.last().result) {
                fields.last().instr += ch;
                continue;
            }
            bool hiddenByField = false;
            for (int i = 0; i + 1 < fields.size(); ++i)
                hiddenByField = hiddenByField || !fields.at(i).result;
            if (hiddenByField)
                continue;
            const Chp chp = chpAt(c, pap);
            if (chp.vanish || chp.rmarkDel)
                continue;
            Run r;
            if (main && chp.fSpec) {
                if (u == 0x02 && m_footnoteAt.contains(c)) {
                    r.note = m_footnoteAt.value(c);
                    para.runs << r;
                    continue;
                }
                if (u == 0x05)
                    continue; // the comment mark itself
                if (u == 0x01 && !chp.fOle2) {
                    const int img = pictureAt(chp);
                    if (img >= 0) {
                        r.image = img;
                        para.runs << r;
                    }
                    continue;
                }
                if (u == 0x01 || u == 0x08) {
                    if (chp.fOle2 || u == 0x08)
                        ++m_objects;
                    continue;
                }
                if (u == 0x28 && chp.symbolChar) {
                    r.text = QChar(chp.symbolChar >= 0xF000 ? chp.symbolChar - 0xF000 : chp.symbolChar);
                } else if (u < 0x20 && u != 0x09) {
                    continue;
                }
            }
            if (r.text.isEmpty()) {
                if (u == 0x0B)
                    r.text = QChar(QChar::LineSeparator);
                else if (u == 0x0D && cellRow)
                    r.text = QStringLiteral(" "); // a paragraph inside a table cell: the row stays one line
                else if (u == 0x07 && cellRow)
                    r.text = (c + 1 < end) ? QStringLiteral("\t") : QString();
                else if (u == 0x1E)
                    r.text = QChar(0x2011);
                else if (u == 0x1F || u == 0x0E)
                    continue;
                else if (u == 0x09)
                    r.text = QStringLiteral("\t");
                else if (u < 0x20)
                    continue;
                else
                    r.text = ch;
            }
            if (r.text.isEmpty())
                continue;
            r.bold = chp.bold;
            r.italic = chp.italic;
            r.underline = chp.underline;
            r.strike = chp.strike || chp.dstrike;
            r.smallCaps = chp.smallCaps;
            r.allCaps = chp.caps;
            r.vertical = chp.iss == 1 ? 1 : chp.iss == 2 ? -1 : 0;
            {
                // fonts and sizes set by hand (not those of the paragraph's style)
                const StyleDef *s = (pap.istd >= 0 && pap.istd < m_styles.size()) ? &m_styles.at(pap.istd) : nullptr;
                if (chp.ftc >= 0 && chp.ftc < m_fonts.size() && (!s || s->chp.ftc != chp.ftc))
                    r.font = m_fonts.at(chp.ftc);
                if (!s || s->chp.hps != chp.hps)
                    r.size = chp.hps / 2.0;
            }
            r.color = chp.color;
            if (chp.highlight > 0)
                r.highlight = icoColor(chp.highlight);
            if (chp.istd >= 0 && chp.istd < m_styles.size()) {
                const QString n = m_styles.at(chp.istd).name.toLower();
                if (n.contains(QLatin1String("code")) || n.contains(QLatin1String("html")))
                    r.code = true;
            }
            for (const Field &f : fields)
                if (!f.link.isEmpty())
                    r.link = f.link;
            if (main && int(c) < m_commentOfCp.size())
                r.comment = m_commentOfCp.at(int(c));
            para.runs << r;
        }
        const bool breakHere = end < to && charAt(end, nullptr, nullptr).unicode() == 0x0C;
        m_atBreak = breakHere && para.runs.isEmpty();
        flushParagraph(para, pap, cellRow);
        if (breakHere)
            pendingBreak = true;
        cp = end + 1;
    }
}

Failure Reader::run(const QByteArray &file, Content *out, QString *error)
{
    m_out = out;
    CompoundFile cf;
    if (!cf.open(file)) {
        if (error)
            *error = QObject::tr("The file is not a Word document (%1).").arg(cf.error());
        return Failure::NotWord;
    }
    m_wd = cf.stream(QStringLiteral("WordDocument"));
    if (m_wd.size() < 0x200) {
        if (error)
            *error = QObject::tr("The file holds no Word document.");
        return Failure::NotWord;
    }
    const quint16 ident = u16(m_wd, 0);
    const quint16 nFib = u16(m_wd, 2);
    if (ident != 0xA5EC || nFib < 0x00C1) {
        if (error)
            *error = QObject::tr("This is a document of an older Word (6.0 or 95), which is not read here.");
        return Failure::OldWord;
    }
    const quint16 flags = u16(m_wd, 0x0A);
    if (flags & 0x0100) {
        if (error)
            *error = QObject::tr("The document is protected with a password. Remove the password in a word processor first.");
        return Failure::Encrypted;
    }
    m_table = cf.stream((flags & 0x0200) ? QStringLiteral("1Table") : QStringLiteral("0Table"));
    m_data = cf.stream(QStringLiteral("Data"));
    qint64 pos = 0x20;
    const int csw = u16(m_wd, pos);
    pos += 2 + csw * 2;
    const int cslw = u16(m_wd, pos);
    pos += 2;
    const qint64 lw = pos;
    pos += qint64(cslw) * 4;
    m_cbRgFcLcb = u16(m_wd, pos);
    m_fcLcbStart = pos + 2;
    m_ccpText = u32(m_wd, lw + 3 * 4);
    m_ccpFtn = u32(m_wd, lw + 4 * 4);
    m_ccpHdd = u32(m_wd, lw + 5 * 4);
    m_ccpMcr = u32(m_wd, lw + 6 * 4);
    m_ccpAtn = u32(m_wd, lw + 7 * 4);
    m_ccpEdn = u32(m_wd, lw + 8 * 4);
    m_ccpTxbx = u32(m_wd, lw + 9 * 4);
    if (m_table.isEmpty() || m_cbRgFcLcb < 34) {
        if (error)
            *error = QObject::tr("The document is damaged (no table stream).");
        return Failure::Damaged;
    }
    readPieces();
    if (m_pieces.isEmpty()) {
        if (error)
            *error = QObject::tr("The document is damaged (no text).");
        return Failure::Damaged;
    }
    readFkps(12, false);
    readFkps(13, true);
    readStyles();
    for (int i = 0; i < m_styles.size(); ++i)
        resolveStyle(i);
    readLists();
    readFonts();
    readNotes();
    emitRange(0, m_ccpText, true);
    // text boxes: their text at the end
    if (m_ccpTxbx > 1) {
        const quint32 base = m_ccpText + m_ccpFtn + m_ccpHdd + m_ccpMcr + m_ccpAtn + m_ccpEdn;
        const int before = int(out->paragraphs.size());
        emitRange(base, base + m_ccpTxbx - 1, false);
        // drop empty paragraphs of the text box story
        for (int i = int(out->paragraphs.size()) - 1; i >= before; --i) {
            QString t;
            for (const Run &r : out->paragraphs.at(i).runs)
                t += r.text;
            if (t.trimmed().isEmpty() && out->paragraphs.at(i).runs.isEmpty())
                out->paragraphs.removeAt(i);
        }
        if (int(out->paragraphs.size()) > before)
            out->warnings << QObject::tr("the text of text boxes was added at the end of the document.");
    }
    // drop the empty last paragraph Word keeps at the end
    while (!out->paragraphs.isEmpty()) {
        const Paragraph &l = out->paragraphs.last();
        bool empty = true;
        for (const Run &r : l.runs)
            empty = empty && r.image < 0 && r.note < 0 && r.text.trimmed().isEmpty();
        if (!empty || out->paragraphs.size() == 1)
            break;
        out->paragraphs.removeLast();
    }
    if (m_skippedPictures > 0)
        out->warnings << (m_skippedPictures == 1
                              ? QObject::tr("1 picture in a format the program does not show (WMF, EMF, PICT) was left out.")
                              : QObject::tr("%1 pictures in a format the program does not show (WMF, EMF, PICT) were left out.").arg(m_skippedPictures));
    if (m_objects > 0)
        out->warnings << (m_objects == 1 ? QObject::tr("1 drawing or embedded object was left out.")
                                         : QObject::tr("%1 drawings or embedded objects were left out.").arg(m_objects));
    if (m_ccpHdd > 0)
        out->warnings << QObject::tr("headers and footers were left out.");
    return Failure::None;
}

} // namespace

Failure readWord97(const QByteArray &file, Content *out, QString *error)
{
    Reader r;
    return r.run(file, out, error);
}

} // namespace qbdoc
