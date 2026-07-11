#include "AuthServer.h"

#include <QCoreApplication>
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
    if (argc > 1)
        dataDir = QString::fromLocal8Bit(argv[1]);

    quint16 port = 8080;
    if (argc > 2)
        port = static_cast<quint16>(QString::fromLocal8Bit(argv[2]).toUShort());

    AuthServer server(dataDir);
    if (!server.start(port))
        return 1; // couldn't bind the port — message already printed

    // A service should ANNOUNCE where its state lives. This line exists
    // because it once didn't: configuring version.json meant spelunking
    // through AppData guessing at Qt's default paths. Never again —
    // accounts.json, shares.json, and version.json are all in this folder.
    QTextStream(stdout) << "Data folder (accounts, shares, version.json):\n"
                        << "  " << QDir(dataDir).absolutePath() << "\n\n";

    return app.exec(); // block in the event loop, serving requests
}
