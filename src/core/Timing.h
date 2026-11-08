#pragma once
// Times of the steps of a save, written to the error output when the program runs with
// QB_TIMING=1 (0.60.5). Costs nothing otherwise.
//
//   timing::Laps laps;
//   ... laps.lap("texts"); ... laps.lap("project file");
//   laps.print("save");   // "timing: save 41 ms: texts 3, project file 12, ..."

#include <QElapsedTimer>
#include <QStringList>
#include <cstdio>

namespace qb {
namespace timing {

inline bool enabled()
{
    static const bool on = qEnvironmentVariableIsSet("QB_TIMING");
    return on;
}

class Laps
{
public:
    Laps()
    {
        if (enabled()) {
            m_all.start();
            m_step.start();
        }
    }
    void lap(const char *step)
    {
        if (enabled())
            m_parts << QStringLiteral("%1 %2").arg(QLatin1String(step)).arg(m_step.restart());
    }
    void print(const char *what) const
    {
        if (enabled())
            std::fprintf(stderr, "timing: %s %lld ms: %s\n", what, static_cast<long long>(m_all.elapsed()),
                         qPrintable(m_parts.join(QStringLiteral(", "))));
    }

private:
    QElapsedTimer m_all, m_step;
    QStringList m_parts;
};

} // namespace timing
} // namespace qb
