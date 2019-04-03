#include <QCoreApplication>
#include <balancerobot.h>

int main(int argc, char *argv[])
{
    int result = 0;

    do
    {
        QCoreApplication app(argc, argv);
        BalanceRobot robot;
        result = app.exec();

    } while( result == 1111 );

    return result;
}
