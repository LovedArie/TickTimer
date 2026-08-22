// ---------------------------------------------------------------------------
// test_auth.cpp — the security-critical layer gets the most careful tests in
// the project, because a bug here doesn't crash, it silently lets the wrong
// person in. Every property that MUST hold for credential storage is pinned.
// ---------------------------------------------------------------------------

#include "JsonStore.h"
#include "AppData.h"
#include "AccountStore.h"
#include "PlannerStore.h"
#include "ShareStore.h"
#include "DeviceStore.h"
#include "SyncPlan.h"
#include "PasswordHash.h"

#include <QJsonObject>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <qscopeguard.h>
#include <QTemporaryDir>
#include <QFile>
#include <QSet>
#include <QtTest>

class TestAuth : public QObject
{
    Q_OBJECT

private slots:
    void hashIsNeverThePlaintext()
    {
        const QString stored = auth::hashPassword("hunter2");
        // The stored string must not contain the password anywhere — the
        // single most important property, checked first.
        QVERIFY(!stored.contains("hunter2"));
        QVERIFY(stored.startsWith("pbkdf2$")); // self-describing format
    }

    void samePasswordHashesDifferentlyEachTime()
    {
        // Distinct random salts → distinct stored strings for identical
        // passwords. Without this, a stolen file reveals which users share a
        // password, and rainbow tables crack them all at once.
        const QString a = auth::hashPassword("same-password");
        const QString b = auth::hashPassword("same-password");
        QVERIFY(a != b);
        // …yet BOTH must verify against the one password.
        QVERIFY(auth::verifyPassword("same-password", a));
        QVERIFY(auth::verifyPassword("same-password", b));
    }

    void verifyRejectsWrongPasswordAndGarbage()
    {
        const QString stored = auth::hashPassword("correct horse");
        QVERIFY(!auth::verifyPassword("wrong horse", stored));
        // Malformed stored strings are a failed verification, never a crash.
        QVERIFY(!auth::verifyPassword("x", "not-a-valid-hash"));
        QVERIFY(!auth::verifyPassword("x", ""));
        QVERIFY(!auth::verifyPassword("x", "pbkdf2$notanumber$aa$bb"));
    }

    void registerThenLogin()
    {
        QTemporaryDir dir;
        AccountStore store(dir.filePath("accounts.json"));

        QCOMPARE(store.registerAccount("alice", "s3cret"),
                 AccountStore::Result::Ok);
        QCOMPARE(store.login("alice", "s3cret"),
                 AccountStore::Result::Ok);
        QCOMPARE(store.login("alice", "wrong"),
                 AccountStore::Result::WrongPassword);
        QCOMPARE(store.login("bob", "whatever"),
                 AccountStore::Result::UsernameNotFound);
    }

    void duplicateNamesAreRefusedCaseInsensitively()
    {
        QTemporaryDir dir;
        AccountStore store(dir.filePath("accounts.json"));
        QCOMPARE(store.registerAccount("Alice", "pw1"),
                 AccountStore::Result::Ok);
        // "alice" is "Alice" — refused, and login works under either casing.
        QCOMPARE(store.registerAccount("alice", "pw2"),
                 AccountStore::Result::UsernameTaken);
        QCOMPARE(store.login("ALICE", "pw1"),
                 AccountStore::Result::Ok);
    }

    void emptyCredentialsRefused()
    {
        QTemporaryDir dir;
        AccountStore store(dir.filePath("accounts.json"));
        QCOMPARE(store.registerAccount("", "pw"),
                 AccountStore::Result::InvalidInput);
        QCOMPARE(store.registerAccount("name", ""),
                 AccountStore::Result::InvalidInput);
    }

    void accountsPersistAcrossReload()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("accounts.json");
        {
            AccountStore store(path);
            store.registerAccount("carol", "pw");
        }
        // A brand-new store reading the same file must find carol — and her
        // password must still verify (the HASH round-tripped, not the
        // plaintext, which was never written).
        AccountStore reloaded(path);
        QVERIFY(reloaded.hasUser("carol"));
        QCOMPARE(reloaded.login("carol", "pw"),
                 AccountStore::Result::Ok);
    }

    void usernamesMustBeFilenameSafe()
    {
        // Usernames become server-side FILENAMES (planners/<name>.json), so
        // the charset is enforced at registration. "../x" is a username
        // somebody will try; it must die at the door, not in the file system.
        QTemporaryDir dir;
        AccountStore store(dir.filePath("accounts.json"));
        QCOMPARE(store.registerAccount("../etc", "pw"),
                 AccountStore::Result::InvalidInput);
        QCOMPARE(store.registerAccount("we/ird", "pw"),
                 AccountStore::Result::InvalidInput);
        QCOMPARE(store.registerAccount("has space", "pw"),
                 AccountStore::Result::InvalidInput);
        QCOMPARE(store.registerAccount("fine_name-2", "pw"),
                 AccountStore::Result::Ok);
    }

    void plannerStoreVersionsDocuments()
    {
        QTemporaryDir dir;
        PlannerStore store(dir.path());

        // Nothing stored yet: revision 0 and an empty document — the state a
        // fresh account and a fresh device both agree on.
        QCOMPARE(store.revision("alice"), 0);
        QVERIFY(store.planner("alice").isEmpty());

        QJsonObject doc;
        doc[QStringLiteral("marker")] = 1;
        QCOMPARE(store.store("alice", doc), 1);       // first store → rev 1
        QCOMPARE(store.revision("alice"), 1);
        QCOMPARE(store.planner("alice")
                     .value(QStringLiteral("marker")).toInt(), 1);

        doc[QStringLiteral("marker")] = 2;
        QCOMPARE(store.store("alice", doc), 2);       // every store bumps

        // Case-insensitive identity: "Alice" is alice's shelf, not a new one.
        QCOMPARE(store.revision("Alice"), 2);

        // Per-user isolation: bob's shelf is untouched by alice's activity.
        QCOMPARE(store.revision("bob"), 0);
        QVERIFY(store.planner("bob").isEmpty());

        // Persistence: a fresh store over the same directory sees the same
        // shelf — revisions are on disk, not in memory.
        PlannerStore reloaded(dir.path());
        QCOMPARE(reloaded.revision("alice"), 2);
    }

    void syncDecisionTruthTable()
    {
        // The ENTIRE sync brain, all four rows (see SyncPlan.h). Pure
        // function: no network, no files — the test IS the table.
        using syncplan::Action;
        using syncplan::decide;
        QCOMPARE(decide(5, 5, false), Action::Nothing);  // nobody moved
        QCOMPARE(decide(5, 5, true),  Action::Push);     // only we did
        QCOMPARE(decide(7, 5, false), Action::Pull);     // only server did
        QCOMPARE(decide(7, 5, true),  Action::Conflict); // both — a human decides
    }

    void perAccountPathsAreDistinctAndCaseInsensitive()
    {
        // Each account maps to its own file; an empty user falls back to the
        // legacy global path (so tests/tools that pass no user are unchanged).
        const QString global = JsonStore::filePathForUser(QString());
        QVERIFY(global.endsWith("data.json"));

        const QString a = JsonStore::filePathForUser("phanperry");
        const QString b = JsonStore::filePathForUser("alice");
        QVERIFY(a != b);
        QVERIFY(a != global);
        QVERIFY(a.endsWith("data-phanperry.json"));

        // "Alice" and "alice" are one account → one file (server agrees).
        QCOMPARE(JsonStore::filePathForUser("Alice"),
                 JsonStore::filePathForUser("alice"));
    }

    void adoptionTransfersOldDataOnceThenLeavesOthersFresh()
    {
        // The migration the owner asked for, proven end to end. We drive the
        // REAL paths (JsonStore::defaultFilePath / filePathForUser) rather
        // than hand-built ones — QStandardPaths resolves at process start, so
        // second-guessing it in the test is how the first draft went wrong.
        // Unique usernames keep this hermetic without touching real accounts.
        const QString userA = "mig_first_" + uniq();
        const QString userB = "mig_second_" + uniq();
        const QString globalPath = JsonStore::defaultFilePath();
        const QString backupPath = globalPath + ".pre-accounts.bak";

        // Save/restore whatever real global file might exist, so the test
        // never eats a developer's actual data.json.
        const bool hadGlobal = QFile::exists(globalPath);
        const QString stash = globalPath + ".test-stash";
        if (hadGlobal) { QFile::remove(stash); QFile::rename(globalPath, stash); }
        auto cleanup = qScopeGuard([&] {
            QFile::remove(JsonStore::filePathForUser(userA));
            QFile::remove(JsonStore::filePathForUser(userB));
            QFile::remove(globalPath);
            QFile::remove(backupPath);
            if (hadGlobal) QFile::rename(stash, globalPath);
        });

        // Seed a recognisable "old" planner at the global path.
        {
            AppData old;
            const QString cat = old.addCategory("Old Work", QColor("#4C6FE0"));
            old.addActivity("Legacy Study", cat);
            QVERIFY(JsonStore(globalPath).save(old));
        }
        QVERIFY(QFile::exists(globalPath));

        // First user logs in → adopts the global file, data intact.
        QVERIFY(JsonStore::adoptGlobalDataForUser(userA));
        AppData adopted;
        QVERIFY(JsonStore(JsonStore::filePathForUser(userA)).load(adopted));
        QCOMPARE(adopted.categories().size(), 1);
        QCOMPARE(adopted.categories().first().name, QString("Old Work"));

        // The global file was retired to a backup — nothing destroyed.
        QVERIFY(!QFile::exists(globalPath));
        QVERIFY(QFile::exists(backupPath));

        // A SECOND user finds nothing to adopt → starts fresh (no file).
        QVERIFY(!JsonStore::adoptGlobalDataForUser(userB));
        QVERIFY(!QFile::exists(JsonStore::filePathForUser(userB)));

        // Re-running adoption for the first user is a no-op — their file
        // already exists, so their data is never clobbered by a re-run.
        QVERIFY(!JsonStore::adoptGlobalDataForUser(userA));
    }

    // ---- ShareStore — who may READ whose planner (share & compare) --------
    // A permission store is security-adjacent: a bug here doesn't crash, it
    // shows someone a planner they weren't given. Same care as the hashes.

    void shareGrantRevokeAndBothDirections()
    {
        QTemporaryDir dir;
        ShareStore shares(dir.filePath("shares.json"));

        // Nothing granted → nothing readable (except one's own, below).
        QVERIFY(!shares.canRead("bob", "alice"));

        QVERIFY(shares.grant("alice", "bob"));
        QVERIFY(shares.canRead("bob", "alice"));
        // DIRECTED: alice sharing with bob says nothing about bob's planner.
        QVERIFY(!shares.canRead("alice", "bob"));

        // Both query directions see the same single fact.
        QCOMPARE(shares.viewersOf("alice"), QStringList{"bob"});
        QCOMPARE(shares.ownersSharedWith("bob"), QStringList{"alice"});

        QVERIFY(shares.revoke("alice", "bob"));
        QVERIFY(!shares.canRead("bob", "alice"));
        QVERIFY(shares.viewersOf("alice").isEmpty());
    }

    void shareOwnersAlwaysReadTheirOwn()
    {
        QTemporaryDir dir;
        ShareStore shares(dir.filePath("shares.json"));
        // No grant needed, no grant possible: identity beats the table.
        QVERIFY(shares.canRead("alice", "alice"));
        QVERIFY(!shares.grant("alice", "alice")); // self-share refused
    }

    void shareIsCanonicalCaseInsensitive()
    {
        QTemporaryDir dir;
        ShareStore shares(dir.filePath("shares.json"));
        // The same identity rule as AccountStore/PlannerStore: "  Alice "
        // and "alice" are one person, so a grant under one spelling must be
        // visible under every spelling — or access depends on typing luck.
        QVERIFY(shares.grant("  Alice ", "BOB"));
        QVERIFY(shares.canRead("bob", "alice"));
        QVERIFY(shares.canRead("Bob", "ALICE"));
        QVERIFY(shares.revoke("ALICE", "bob"));
        QVERIFY(!shares.canRead("bob", "alice"));
    }

    void shareOperationsAreIdempotent()
    {
        QTemporaryDir dir;
        ShareStore shares(dir.filePath("shares.json"));
        // Grant twice: still one fact, both calls succeed (retry-friendly).
        QVERIFY(shares.grant("alice", "bob"));
        QVERIFY(shares.grant("alice", "bob"));
        QCOMPARE(shares.viewersOf("alice").size(), 1);
        // Revoke twice, and revoke what never existed: the requested end
        // state holds either way, so both report success.
        QVERIFY(shares.revoke("alice", "bob"));
        QVERIFY(shares.revoke("alice", "bob"));
        QVERIFY(shares.revoke("alice", "nobody"));
    }

    void sharesPersistAcrossReload()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("shares.json");
        {
            ShareStore shares(path);
            QVERIFY(shares.grant("alice", "bob"));
            QVERIFY(shares.grant("alice", "carol"));
            QVERIFY(shares.grant("dave", "alice"));
        } // store destroyed — only the file survives
        ShareStore reloaded(path);
        QCOMPARE(reloaded.viewersOf("alice"),
                 (QStringList{"bob", "carol"}));
        QVERIFY(reloaded.canRead("alice", "dave"));
        QVERIFY(!reloaded.canRead("carol", "dave"));
    }

    // ---- v30.2: remembered devices -----------------------------------------
    //
    // The credential that lets a phone stop asking for a password, without
    // making the persisted thing an open door. DeviceStore.h has the full
    // argument; these are the properties it rests on.

    // The raw token exists exactly once, in the reply to the client. What
    // lands on disk is a hash, so a stolen devices.json is a list of dead
    // strings rather than a stack of working credentials.
    void deviceFileNeverContainsTheTokenItIssued()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("devices.json");

        DeviceStore store(path);
        const QString raw = store.remember(QStringLiteral("alice"),
                                           QStringLiteral("Arie's phone"));
        QVERIFY(!raw.isEmpty());
        QCOMPARE(raw.size(), 32); // 128 bits, hex

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString onDisk = QString::fromUtf8(f.readAll());
        QVERIFY(!onDisk.contains(raw));                  // never the token
        QVERIFY(onDisk.contains(QStringLiteral("alice"))); // but we know whose
        QVERIFY(onDisk.contains(QStringLiteral("Arie's phone")));
    }

    // The one thing a device token buys: a name to mint a session for. It
    // survives a restart of the process, which is the entire point — session
    // tokens deliberately do not.
    void aRememberedDeviceResolvesAcrossRestarts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("devices.json");

        QString raw;
        {
            DeviceStore store(path);
            raw = store.remember(QStringLiteral("Alice"), QString());
        }

        DeviceStore reopened(path); // a fresh "server process"
        QCOMPARE(reopened.userFor(raw), QStringLiteral("alice")); // canonical
        QCOMPARE(reopened.count(), 1);
    }

    // Fail-safe, like every other lookup here: unknown, revoked, malformed
    // and empty are ONE answer — nobody. A credential that resolved to
    // somebody on a near miss is the bug this shape prevents.
    void unknownDeviceTokensResolveToNobody()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DeviceStore store(dir.filePath("devices.json"));

        const QString raw = store.remember(QStringLiteral("alice"), QString());

        QVERIFY(store.userFor(QString()).isEmpty());
        QVERIFY(store.userFor(QStringLiteral("   ")).isEmpty());
        QVERIFY(store.userFor(QStringLiteral("not-a-token")).isEmpty());
        QVERIFY(store.userFor(raw + QStringLiteral("00")).isEmpty());
        QVERIFY(store.userFor(raw.left(raw.size() - 1)).isEmpty());
        QVERIFY(!store.userFor(raw).isEmpty()); // the real one still works

        // An account with no name cannot own a device.
        QVERIFY(store.remember(QStringLiteral("  "), QString()).isEmpty());
        QCOMPARE(store.count(), 1);
    }

    // Revoking is what "log out" means once a device can be remembered, and
    // it is IDEMPOTENT: forgetting a token that was already gone is a success,
    // because the caller wanted it not to work and it does not.
    void revokingADeviceIsIdempotentAndTouchesNoOtherDevice()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DeviceStore store(dir.filePath("devices.json"));

        const QString phone  = store.remember(QStringLiteral("alice"),
                                              QStringLiteral("phone"));
        const QString laptop = store.remember(QStringLiteral("alice"),
                                              QStringLiteral("laptop"));
        const QString hers   = store.remember(QStringLiteral("bob"),
                                              QStringLiteral("phone"));
        QCOMPARE(store.count(), 3);

        QVERIFY(store.forget(phone));
        QVERIFY(!store.forget(phone));            // already gone, still fine
        QVERIFY(store.userFor(phone).isEmpty());  // and it stays dead
        QCOMPARE(store.userFor(laptop), QStringLiteral("alice"));
        QCOMPARE(store.userFor(hers), QStringLiteral("bob"));
        QCOMPARE(store.count(), 2);
    }

    // The "I lost my phone" door — and what a password change should call.
    // One account's devices, never anyone else's.
    void forgettingAnAccountsDevicesLeavesOtherAccountsAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DeviceStore store(dir.filePath("devices.json"));

        const QString a1 = store.remember(QStringLiteral("alice"), QString());
        const QString a2 = store.remember(QStringLiteral("ALICE"), QString());
        const QString b1 = store.remember(QStringLiteral("bob"), QString());

        QCOMPARE(store.forgetAllFor(QStringLiteral("Alice")), 2); // case-blind
        QVERIFY(store.userFor(a1).isEmpty());
        QVERIFY(store.userFor(a2).isEmpty());
        QCOMPARE(store.userFor(b1), QStringLiteral("bob"));
        QCOMPARE(store.forgetAllFor(QStringLiteral("nobody")), 0);
    }

    // Two devices never collide, and each resolves to its own owner. The
    // token is 128 bits from the system CSPRNG; this pins that we are not
    // accidentally handing out the same one twice.
    void everyDeviceTokenIsDistinct()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        DeviceStore store(dir.filePath("devices.json"));

        QSet<QString> seen;
        for (int i = 0; i < 50; ++i) {
            const QString raw =
                store.remember(QStringLiteral("alice"), QString());
            QVERIFY(!raw.isEmpty());
            QVERIFY(!seen.contains(raw));
            seen.insert(raw);
        }
        QCOMPARE(store.count(), 50);
    }

private:
    static QString uniq()
    {
        return QString::number(
            QDateTime::currentMSecsSinceEpoch() % 1000000);
    }
};

QTEST_MAIN(TestAuth)
#include "test_auth.moc"
