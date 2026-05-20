#include <QCoreApplication>
#include <balancerobot.h>
#include <csignal>

// SIGTERM / SIGINT (Ctrl+C) handler — politely ask Qt to exit so destructors
// run, motors stop, and settings.ini gets flushed to disk.
static void signalHandler(int /*sig*/)
{
    QCoreApplication::quit();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    BalanceRobot robot;
    return app.exec();
}
