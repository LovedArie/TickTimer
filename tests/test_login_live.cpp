#include "LoginDialog.h"
#include "AuthClient.h"
#include "AppData.h"
#include "JsonStore.h"
#include "SyncClient.h"
#include "SyncService.h"
#include "ShareClient.h"
#include "UpdateClient.h"
#include "Version.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QtTest>
#include <QProcess>
#include <functional>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QTemporaryDir>

class TestLoginLive : public QObject {
  Q_OBJECT
  QProcess server;
  QTemporaryDir dir;
private slots:
  void initTestCase() {
    // The server binary is built into the SAME folder as this test binary,
    // so ask Qt where we are instead of hardcoding "./build-linux/..." —
    // which only worked when ctest happened to run from the repo root, and
    // broke the moment the build folder had a different name (as it does in
    // Qt Creator on Windows). applicationDirPath() is the one path that is
    // true by construction. (Windows resolves the missing ".exe" itself.)
    server.start(QCoreApplication::applicationDirPath()
                     + QStringLiteral("/ticktimer-server"),
                 {dir.path(), "8091"});
    QVERIFY(server.waitForStarted(3000));
    QTest::qWait(800); // let it bind
  }
  void cleanupTestCase() { server.kill(); server.waitForFinished(2000); }

  // Fire one request and BLOCK until its single result arrives — turning the
  // async client into a synchronous call FOR THE TEST only. A QSignalSpy +
  // local event loop is the standard Qt idiom for testing async code without
  // the race a shared variable invites (the first version reused one `got`
  // and the next request could overwrite it before the assertion ran).
  AuthClient::Outcome await(AuthClient& client,
                            std::function<void()> fire) {
    AuthClient::Outcome result{};
    bool done = false;
    QEventLoop loop;
    // QTRY_* macros expand to `return;`, so they only compile inside a void
    // test slot — not a helper that returns a value. A local QEventLoop is the
    // portable idiom: connect, fire, spin until the result arrives (or a
    // timeout guard quits us), then return it.
    QMetaObject::Connection c = connect(
        &client, &AuthClient::resultReady,
        [&](AuthClient::Outcome o, const QString&) {
            result = o; done = true; loop.quit();
        });
    fire();
    QTimer::singleShot(5000, &loop, &QEventLoop::quit); // safety timeout
    loop.exec();
    disconnect(c);
    Q_ASSERT(done);
    return result;
  }

  void registerThenLoginThroughTheDialog() {
    AuthClient client("http://localhost:8091");

    QCOMPARE(await(client, [&]{ client.registerUser("dave", "pw123"); }),
             AuthClient::Outcome::Success);
    QCOMPARE(await(client, [&]{ client.registerUser("dave", "other"); }),
             AuthClient::Outcome::UsernameTaken);
    QCOMPARE(await(client, [&]{ client.login("dave", "wrong"); }),
             AuthClient::Outcome::BadCredentials);
    QCOMPARE(await(client, [&]{ client.login("dave", "pw123"); }),
             AuthClient::Outcome::Success);
  }

  void unreachableServerReportsNetworkError() {
    AuthClient client("http://localhost:1"); // nothing there
    AuthClient::Outcome got = AuthClient::Outcome::Success;
    connect(&client, &AuthClient::resultReady,
            [&](AuthClient::Outcome o, const QString&){ got = o; });
    client.login("x", "y");
    QTRY_COMPARE_WITH_TIMEOUT(got, AuthClient::Outcome::NetworkError, 3000);
  }

private:
  // ---- sync helpers: same one-shot QEventLoop idiom as await() ----------
  // In a plain `private:` section, NOT `private slots:` — moc tolerates
  // functions in a slots section but chokes on the struct below with
  // "Not a signal or slot declaration" (the QB L4 lesson, second sighting).

  QString loginForToken(const QString& user, const QString& pass) {
    AuthClient client("http://localhost:8091");
    QString token;
    QEventLoop loop;
    connect(&client, &AuthClient::resultReady,
            [&](AuthClient::Outcome, const QString&, const QString& t) {
                token = t; loop.quit();
            });
    client.registerUser(user, pass); // registering IS logging in — token minted
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return token;
  }

  struct PullResult { SyncClient::Outcome outcome; int revision; QJsonObject data; };
  PullResult awaitPull(SyncClient& c) {
    PullResult r{SyncClient::Outcome::NetworkError, 0, {}};
    QEventLoop loop;
    auto conn = connect(&c, &SyncClient::pullFinished,
        [&](SyncClient::Outcome o, int rev, const QJsonObject& d) {
            r = {o, rev, d}; loop.quit();
        });
    c.pull();
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return r;
  }

  QPair<SyncClient::Outcome, int> awaitPush(SyncClient& c,
                                            const QJsonObject& data,
                                            int base, bool force) {
    QPair<SyncClient::Outcome, int> r{SyncClient::Outcome::NetworkError, 0};
    QEventLoop loop;
    auto conn = connect(&c, &SyncClient::pushFinished,
        [&](SyncClient::Outcome o, int rev) { r = {o, rev}; loop.quit(); });
    c.push(data, base, force);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return r;
  }

  // Await one SyncService outcome. fire() is deferred with singleShot(0) —
  // resolveUseServer() emits finished() SYNCHRONOUSLY, and firing before
  // loop.exec() would mean quit() runs before the loop starts and the test
  // hangs forever. Deferring puts the emission inside the running loop.
  bool awaitService(SyncService& s, std::function<void()> fire,
                    bool& conflictOut) {
    bool ok = false;
    conflictOut = false;
    QEventLoop loop;
    auto c1 = connect(&s, &SyncService::finished,
                      [&](bool o, const QString&) { ok = o; loop.quit(); });
    auto c2 = connect(&s, &SyncService::conflictDetected,
                      [&](int) { conflictOut = true; loop.quit(); });
    QTimer::singleShot(0, fire);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(c1);
    disconnect(c2);
    return ok;
  }

  // ---- share helpers: the same one-shot QEventLoop idiom, third client ----
  ShareClient::Outcome awaitShareUpdate(ShareClient& c,
                                        std::function<void()> fire) {
    ShareClient::Outcome r = ShareClient::Outcome::NetworkError;
    QEventLoop loop;
    auto conn = connect(&c, &ShareClient::shareUpdated,
        [&](ShareClient::Outcome o) { r = o; loop.quit(); });
    fire();
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return r;
  }

  struct SharesResult {
    ShareClient::Outcome outcome; QStringList iShareWith, sharedWithMe;
  };
  SharesResult awaitShares(ShareClient& c) {
    SharesResult r{ShareClient::Outcome::NetworkError, {}, {}};
    QEventLoop loop;
    auto conn = connect(&c, &ShareClient::sharesReady,
        [&](ShareClient::Outcome o, const QStringList& a,
            const QStringList& b) { r = {o, a, b}; loop.quit(); });
    c.fetchShares();
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return r;
  }

  struct PeerResult { ShareClient::Outcome outcome; QJsonObject data; };
  PeerResult awaitPeer(ShareClient& c, const QString& user) {
    PeerResult r{ShareClient::Outcome::NetworkError, {}};
    QEventLoop loop;
    auto conn = connect(&c, &ShareClient::peerPlannerReady,
        [&](ShareClient::Outcome o, const QString&, const QJsonObject& d) {
            r = {o, d}; loop.quit();
        });
    c.fetchPeerPlanner(user);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return r;
  }

private slots:
  void syncPushPullConflictAndForce() {
    const QString token = loginForToken("erin", "pw");
    QVERIFY(!token.isEmpty()); // login now mints a session token

    SyncClient sc("http://localhost:8091", token);

    // Fresh account: revision 0, empty shelf.
    PullResult p = awaitPull(sc);
    QCOMPARE(p.outcome, SyncClient::Outcome::Success);
    QCOMPARE(p.revision, 0);

    // First push, based on 0 → revision 1.
    QJsonObject doc; doc["marker"] = 1;
    auto push1 = awaitPush(sc, doc, 0, false);
    QCOMPARE(push1.first, SyncClient::Outcome::Success);
    QCOMPARE(push1.second, 1);

    // Pull sees exactly what was pushed — the round trip, over a real wire.
    p = awaitPull(sc);
    QCOMPARE(p.revision, 1);
    QCOMPARE(p.data.value("marker").toInt(), 1);

    // A STALE push (still claiming base 0) is refused — optimistic
    // concurrency doing its one job — and the refusal names the current
    // revision so the client knows where the server stands.
    doc["marker"] = 2;
    auto stale = awaitPush(sc, doc, 0, false);
    QCOMPARE(stale.first, SyncClient::Outcome::Conflict);
    QCOMPARE(stale.second, 1);

    // force=true is the human's explicit override: same base, accepted.
    auto forced = awaitPush(sc, doc, 0, true);
    QCOMPARE(forced.first, SyncClient::Outcome::Success);
    QCOMPARE(forced.second, 2);
  }

  void syncRequiresAValidTokenAndIsolatesAccounts() {
    // A made-up token gets 401 — the shelf answers to sessions, not to hope.
    SyncClient intruder("http://localhost:8091", "deadbeef");
    QCOMPARE(awaitPull(intruder).outcome, SyncClient::Outcome::AuthError);

    // A second account sees ITS OWN empty shelf, not erin's revision 2 —
    // the token IS the identity; there's no username in the request to lie
    // about.
    const QString token = loginForToken("frank", "pw");
    QVERIFY(!token.isEmpty());
    SyncClient sc("http://localhost:8091", token);
    PullResult p = awaitPull(sc);
    QCOMPARE(p.outcome, SyncClient::Outcome::Success);
    QCOMPARE(p.revision, 0);
    QVERIFY(p.data.isEmpty());
  }

  void syncServiceRunsTheWholePlaybook() {
    // Device A = a real SyncService over a real AppData; device B = a raw
    // SyncClient pushing behind A's back. One process, two "devices",
    // every row of the truth table exercised against the live server.
    QSettings settings;
    settings.remove("sync/lastRevision");
    settings.remove("sync/dirty");

    const QString token = loginForToken("gina", "pw");
    QVERIFY(!token.isEmpty());

    SyncClient clientA("http://localhost:8091", token);
    AppData data;
    SyncService service(&data, &clientA);
    bool conflict = false;

    // Row 2 — PUSH: fresh device defaults dirty, local work exists.
    data.addCategory("Deep work", QColor("#4C6FE0"));
    QVERIFY(awaitService(service, [&]{ service.syncNow(); }, conflict));
    QVERIFY(!conflict);
    QCOMPARE(service.lastRevision(), 1);
    QVERIFY(!service.dirty()); // push cleared the flag

    // Row 3 — PULL: device B publishes revision 2, A hasn't touched a thing.
    AppData other;
    other.addCategory("From elsewhere", QColor("#1D9E75"));
    SyncClient clientB("http://localhost:8091", token);
    QCOMPARE(awaitPush(clientB, JsonStore::toJsonObject(other), 1, false)
                 .first, SyncClient::Outcome::Success);

    QVERIFY(awaitService(service, [&]{ service.syncNow(); }, conflict));
    QVERIFY(!conflict);
    QCOMPARE(service.lastRevision(), 2);
    // The pull REPLACED local data with the server's version…
    QCOMPARE(data.categories().size(), 1);
    QCOMPARE(data.categories().first().name, QString("From elsewhere"));
    // …and the reentrancy guard kept our own apply from re-flagging dirty —
    // without it, every pull would immediately claim new work to push.
    QVERIFY(!service.dirty());

    // Row 4 — CONFLICT, resolved "use server": both sides moved.
    data.addCategory("Local edit", QColor("#D85A30")); // A is dirty now
    other.addCategory("Elsewhere again", QColor("#7F77DD"));
    QCOMPARE(awaitPush(clientB, JsonStore::toJsonObject(other), 2, false)
                 .first, SyncClient::Outcome::Success);        // server rev 3

    awaitService(service, [&]{ service.syncNow(); }, conflict);
    QVERIFY(conflict); // a human must choose — nothing was overwritten yet

    QVERIFY(awaitService(service, [&]{ service.resolveUseServer(); },
                         conflict));
    QCOMPARE(service.lastRevision(), 3);
    QCOMPARE(data.categories().size(), 2); // the server's two, ours gone
    QVERIFY(!service.dirty());

    // Row 4 again — CONFLICT, resolved "keep mine": the force-push path.
    data.addCategory("Mine wins", QColor("#D4537E"));
    QCOMPARE(awaitPush(clientB, JsonStore::toJsonObject(other), 3, false)
                 .first, SyncClient::Outcome::Success);        // server rev 4
    awaitService(service, [&]{ service.syncNow(); }, conflict);
    QVERIFY(conflict);
    QVERIFY(awaitService(service, [&]{ service.resolveKeepMine(); },
                         conflict));
    QCOMPARE(service.lastRevision(), 5); // force-push minted a new revision

    // The server now holds A's version — B's next pull proves it.
    PullResult final = awaitPull(clientB);
    QCOMPARE(final.revision, 5);
    QCOMPARE(final.data.value("categories").toArray().size(), 3);
  }

  void sharingGatesReadAccessToAPeersPlanner() {
    // Two real accounts, one planner, and the full permission lifecycle
    // over a live socket: forbidden → granted → readable → revoked →
    // forbidden again. The 403s in the middle are the feature — the happy
    // path proves sharing works, the refusals prove privacy does.
    const QString henryToken = loginForToken("henry", "pw");
    const QString irisToken  = loginForToken("iris", "pw");
    QVERIFY(!henryToken.isEmpty());
    QVERIFY(!irisToken.isEmpty());

    // Henry publishes a recognisable planner.
    SyncClient henrySync("http://localhost:8091", henryToken);
    QJsonObject doc; doc["marker"] = 7;
    QCOMPARE(awaitPush(henrySync, doc, 0, false).first,
             SyncClient::Outcome::Success);

    ShareClient henry("http://localhost:8091", henryToken);
    ShareClient iris("http://localhost:8091", irisToken);

    // Before any grant: iris is KNOWN (valid token) but not PERMITTED —
    // 403 Forbidden, not 401, and the client keeps them distinct.
    QCOMPARE(awaitPeer(iris, "henry").outcome,
             ShareClient::Outcome::Forbidden);

    // Sharing with a typo'd name fails loudly at the door (404), instead
    // of "working" and confusing both humans later.
    QCOMPARE(awaitShareUpdate(henry, [&]{ henry.share("nobodyy"); }),
             ShareClient::Outcome::NotFound);

    // The real grant. Registration canonicalises names, so sharing with
    // "IRIS" must reach the same account as "iris".
    QCOMPARE(awaitShareUpdate(henry, [&]{ henry.share("IRIS"); }),
             ShareClient::Outcome::Success);

    // Both sides see the grant, each from their own direction.
    SharesResult henrysView = awaitShares(henry);
    QCOMPARE(henrysView.outcome, ShareClient::Outcome::Success);
    QVERIFY(henrysView.iShareWith.contains("iris"));
    SharesResult irisView = awaitShares(iris);
    QVERIFY(irisView.sharedWithMe.contains("henry"));

    // Iris now reads henry's actual blob — the compare feature's data path.
    PeerResult peer = awaitPeer(iris, "henry");
    QCOMPARE(peer.outcome, ShareClient::Outcome::Success);
    QCOMPARE(peer.data.value("marker").toInt(), 7);

    // Read-only in the strictest sense: the grant is an arrow, and it
    // points ONE way. Henry still can't read iris.
    QCOMPARE(awaitPeer(henry, "iris").outcome,
             ShareClient::Outcome::Forbidden);

    // Revoke closes the door again, immediately.
    QCOMPARE(awaitShareUpdate(henry, [&]{ henry.unshare("iris"); }),
             ShareClient::Outcome::Success);
    QCOMPARE(awaitPeer(iris, "henry").outcome,
             ShareClient::Outcome::Forbidden);

    // And with no token at all, the answer is 401 — "who are you?",
    // upstream of any permission question.
    ShareClient intruder("http://localhost:8091", "deadbeef");
    QCOMPARE(awaitPeer(intruder, "henry").outcome,
             ShareClient::Outcome::AuthError);
  }

  void versionRouteRepeatsTheFileAndNeedsNoRestart() {
    UpdateClient updates("http://localhost:8091");
    const auto check = [&]() {
      struct { UpdateClient::Outcome o; QString latest, url, notes; } r{
          UpdateClient::Outcome::NetworkError, {}, {}, {}};
      QEventLoop loop;
      auto conn = connect(&updates, &UpdateClient::checkFinished,
          [&](UpdateClient::Outcome o, const QString& l, const QString& u,
              const QString& n) { r = {o, l, u, n}; loop.quit(); });
      updates.checkForUpdate();
      QTimer::singleShot(5000, &loop, &QEventLoop::quit);
      loop.exec();
      disconnect(conn);
      return r;
    };

    // The server has been running since initTestCase, and its data dir has
    // no version.json: the feature is unconfigured, and the answer must be
    // Unavailable — the outcome the client maps to total silence.
    QCOMPARE(check().o, UpdateClient::Outcome::Unavailable);

    // Now write the file INTO THE RUNNING SERVER'S data dir. No restart —
    // the route re-reads per request, which is the whole point of choosing
    // a file over a hardcoded constant: announcing a release is an edit.
    {
      QFile f(dir.filePath("version.json"));
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QJsonDocument(QJsonObject{
                  {"latest", "99.1.0"},
                  {"url", "https://example.com/releases"},
                  {"notes", "big news"}})
                  .toJson());
    }
    auto r = check();
    QCOMPARE(r.o, UpdateClient::Outcome::Success);
    QCOMPARE(r.latest, QStringLiteral("99.1.0"));
    QCOMPARE(r.url, QStringLiteral("https://example.com/releases"));
    QCOMPARE(r.notes, QStringLiteral("big news"));

    // And the pure rule agrees this one would speak:
    QCOMPARE(int(version::decideBanner(version::current(), r.latest, "")),
             int(version::Banner::Show));

    // Delete the file → unconfigured again, immediately.
    QVERIFY(QFile::remove(dir.filePath("version.json")));
    QCOMPARE(check().o, UpdateClient::Outcome::Unavailable);
  }
};
QTEST_MAIN(TestLoginLive)
#include "test_login_live.moc"
