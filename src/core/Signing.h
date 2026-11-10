#pragma once
// Signatures of packages (0.65.0, external audit of 2026-11-10, F12).
//
// The release workflow signs the description of every package it makes (manifest.json, which lists the
// SHA-256 of every file of the package) with the project's release key, an Ed25519 key that only the
// workflow holds; the signature is the package's file manifest.sig:
//   {"algorithm": "ed25519", "key": "<key id>", "signature": "<64 bytes, Base64>", "signs": "manifest.json"}
// The program carries the public half of that key and checks the signature before anything else. A
// bundle is as trustworthy as its parts: each part carries a signature of its own.
#include <QByteArray>
#include <QList>
#include <QString>

namespace qb {
namespace signing {

enum class State {
    Unsigned,   // no manifest.sig
    Valid,      // signed with a key this program trusts
    Invalid,    // a signature that does not match (changed after signing, or damaged)
    UnknownKey, // signed with a key this program does not know (counts as not signed)
};

struct Check {
    State state = State::Unsigned;
    QString key; // the key id the signature names
};

// Ed25519 (RFC 8032): whether signature (64 bytes) is publicKey's (32 bytes) signature of message
bool verify(const QByteArray &message, const QByteArray &signature, const QByteArray &publicKey);
// the first 16 hex digits of the SHA-256 of a public key
QString keyId(const QByteArray &publicKey);
// the keys this program trusts: the project's release key (in builds with the test switches also the key in
// QB_TEST_SIGNING_KEY, 64 hex digits)
QList<QByteArray> trustedKeys();
// manifest: the bytes of manifest.json as in the package; signature: the bytes of manifest.sig ("" when none)
Check checkManifest(const QByteArray &manifest, const QByteArray &signature);

} // namespace signing
} // namespace qb
