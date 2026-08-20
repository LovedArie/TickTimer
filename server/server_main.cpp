#include "AuthServer.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QStringList>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

// The server is a QCoreApplication, not a QApplication: no GUI, no widgets,
// just an event loop pumping network I/O. That's the right base class for a
// headless service and it's why the server links only Qt6::Network, not
// Qt6::Widgets — smaller, and honest about what it is.
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // The server keeps its data next to the app's, but in a "server"
    // subfolder — one machine can host the server and also run the client,
    // and their files must not collide. Overridable by a command-line arg so
    // you can run several instances or point at a chosen folder.
    QString dataDir = QStandardPaths::writableLocation(
                          QStandardPaths::AppDataLocation)
                      + QStringLiteral("/server");
    quint16     port = 8080;
    QHostAddress bind = QHostAddress::LocalHost;
    QString     invite;

    // v30.2.1 — flags, with the two old POSITIONAL arguments still honoured.
    // Every existing script and every line in SERVER.md keeps working:
    // anything not starting with "--" fills dataDir then port, in order,
    // exactly as before.
    QStringList positional;
    const QStringList args = QCoreApplication::arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args.at(i);
        const QString next = (i + 1 < args.size()) ? args.at(i + 1) : QString();
        if (a == QLatin1String("--bind") && !next.isEmpty()) {
            // "any" is the word for "reachable from other devices", spelled
            // out rather than left as an IP nobody remembers. This is the
            // flag a laptop serving a phone on the same Wi-Fi needs.
            bind = (next.compare(QLatin1String("any"), Qt::CaseInsensitive) == 0)
                       ? QHostAddress(QHostAddress::Any)
                       : QHostAddress(next);
            ++i;
        } else if (a == QLatin1String("--port") && !next.isEmpty()) {
            port = static_cast<quint16>(next.toUShort());
            ++i;
        } else if (a == QLatin1String("--data") && !next.isEmpty()) {
            dataDir = next;
            ++i;
        } else if (a == QLatin1String("--invite") && !next.isEmpty()) {
            invite = next;
            ++i;
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            QTextStream(stdout)
                << "ticktimer-server [--data DIR] [--port N] "
                   "[--bind any|ADDRESS] [--invite CODE]\n\n"
                   "  --bind    default 127.0.0.1 (this machine only).\n"
                   "            Pass 'any' to let other devices reach it —\n"
                   "            correct on your own LAN, wrong on a public\n"
                   "            box, where a reverse proxy should be the only\n"
                   "            thing that talks to this.\n"
                   "  --invite  require this code to create an account.\n"
                   "            Leave unset only on a network you control.\n\n"
                   "  DIR and PORT may also be given positionally, as before.\n";
            return 0;
        } else if (!a.startsWith(QLatin1String("--"))) {
            positional << a;
        }
    }
    if (positional.size() > 0)
        dataDir = positional.at(0);
    if (positional.size() > 1)
        port = static_cast<quint16>(positional.at(1).toUShort());

    if (bind.isNull()) {
        QTextStream(stderr) << "TickTimer server: --bind was not an address "
                               "this machine understands.\n";
        return 1;
    }

    AuthServer server(dataDir);
    server.setInviteCode(invite); // empty = open, and start() says so loudly
    if (!server.start(port, bind))
        return 1; // couldn't bind the port — message already printed

    // A service should ANNOUNCE where its state lives. This line exists
    // because it once didn't: configuring version.json meant spelunking
    // through AppData guessing at Qt's default paths. Never again —
    // accounts.json, shares.json, and version.json are all in this folder.
    QTextStream(stdout) << "Data folder (accounts, shares, version.json):\n"
                        << "  " << QDir(dataDir).absolutePath() << "\n\n";

    return app.exec(); // block in the event loop, serving requests
}
