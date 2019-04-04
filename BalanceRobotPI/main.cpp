#include <QCoreApplication>
#include <balancerobot.h>

int main(int argc, char *argv[])
{   
    QCoreApplication app(argc, argv);
    BalanceRobot robot;
    return app.exec();
}
