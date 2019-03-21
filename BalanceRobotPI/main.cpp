#include <QCoreApplication>
#include <balancerobot.h>

int main(int argc, char *argv[])
{
    //QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = true"));

    QCoreApplication app(argc, argv);

    BalanceRobot robot;

    return app.exec();
}
