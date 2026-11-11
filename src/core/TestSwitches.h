#pragma once
// 0.81.0 (audit of 0.80.0, S-10): the environment variables that only the test suites use. In a build without
// QB_TEST_SWITCHES (every package: the AppImage, program updates, add-ons) they are not read at all, and their names
// are not in the program (qb_audit1_tests searches a release build for them, QB_RELEASE_BINARY). Before, 35 of the
// 42 QB_... variables were read by release builds too; QB_NO_DIALOGS, for one, kept warning windows from showing.
// The variables a release still reads are listed in the manual (appendix "Environment variables").
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#ifdef QB_TEST_SWITCHES
#define QB_TEST_ENV_SET(name) qEnvironmentVariableIsSet(name)
#define QB_TEST_ENV(name) qEnvironmentVariable(name)
#define QB_TEST_ENV_BYTES(name) qgetenv(name)
#define QB_TEST_ENV_INT(name, ok) qEnvironmentVariableIntValue(name, ok)
#else
#define QB_TEST_ENV_SET(name) false
#define QB_TEST_ENV(name) QString()
#define QB_TEST_ENV_BYTES(name) QByteArray()
#define QB_TEST_ENV_INT(name, ok) (((ok) ? (*(ok) = false) : false), 0)
#endif
