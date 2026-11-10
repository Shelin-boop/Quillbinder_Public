#include "core/Signing.h"

#include "core/third_party/monocypher/monocypher-ed25519.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace qb {
namespace signing {

namespace {
// The project's release key (public half), made by the writer on 2026-11-10 with OpenSSL (Ed25519); the private
// half is the secret QB_SIGNING_KEY of the public repository and never leaves the release workflow.
constexpr char kReleaseKey[] = "bc3c5800083f66e66044ee544a5fd107a4c956c21bf2db6f73d2c41f15c908f8";
} // namespace

bool verify(const QByteArray &message, const QByteArray &signature, const QByteArray &publicKey)
{
    if (signature.size() != 64 || publicKey.size() != 32)
        return false;
    return crypto_ed25519_check(reinterpret_cast<const uint8_t *>(signature.constData()),
                                reinterpret_cast<const uint8_t *>(publicKey.constData()),
                                reinterpret_cast<const uint8_t *>(message.constData()), size_t(message.size()))
           == 0;
}

QString keyId(const QByteArray &publicKey)
{
    return QString::fromLatin1(QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex().left(16));
}

QList<QByteArray> trustedKeys()
{
    QList<QByteArray> keys{QByteArray::fromHex(kReleaseKey)};
#ifdef QB_TEST_SWITCHES
    const QByteArray test = QByteArray::fromHex(qgetenv("QB_TEST_SIGNING_KEY"));
    if (test.size() == 32)
        keys << test;
#endif
    return keys;
}

Check checkManifest(const QByteArray &manifest, const QByteArray &signature)
{
    Check c;
    if (signature.trimmed().isEmpty())
        return c;
    const QJsonObject o = QJsonDocument::fromJson(signature).object();
    c.key = o.value(QStringLiteral("key")).toString();
    const QByteArray sig = QByteArray::fromBase64(o.value(QStringLiteral("signature")).toString().toLatin1());
    if (o.value(QStringLiteral("algorithm")).toString() != QLatin1String("ed25519") || sig.size() != 64
        || o.value(QStringLiteral("signs")).toString() != QLatin1String("manifest.json")) {
        c.state = State::Invalid;
        return c;
    }
    for (const QByteArray &k : trustedKeys()) {
        if (keyId(k) != c.key)
            continue;
        c.state = verify(manifest, sig, k) ? State::Valid : State::Invalid;
        return c;
    }
    c.state = State::UnknownKey;
    return c;
}

} // namespace signing
} // namespace qb
