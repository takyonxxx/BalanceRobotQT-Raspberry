#include <QCoreApplication>
#include <balancerobot.h>
#include <csignal>

// BLE GATT (telefon) ile A2DP ses (JBL) aynı hci0 çipini paylaşınca Qt'nin
// BLE katmanı ses paketlerini görüp "HCI ACL packet data size ..." uyarısı
// basar. Zararsızdır ama müzik/TTS çalarken logu doldurur - SADECE bu mesajı
// filtrele, diğer tüm uyarılar aynen akmaya devam etsin.
static QtMessageHandler g_defaultHandler = nullptr;
static void filteredMessageHandler(QtMsgType type,
                                   const QMessageLogContext &ctx,
                                   const QString &msg)
{
    if (msg.contains(QLatin1String("HCI ACL packet data size")))
        return;
    if (g_defaultHandler)
        g_defaultHandler(type, ctx, msg);
}

// SIGTERM / SIGINT (Ctrl+C) handler — politely ask Qt to exit so destructors
// run, motors stop, and settings.ini gets flushed to disk.
static void signalHandler(int /*sig*/)
{
    QCoreApplication::quit();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    g_defaultHandler = qInstallMessageHandler(filteredMessageHandler);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    BalanceRobot robot;
    return app.exec();
}
