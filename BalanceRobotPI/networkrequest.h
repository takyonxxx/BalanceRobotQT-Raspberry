#ifndef NETWORKREQUEST_H
#define NETWORKREQUEST_H

#include <QObject>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>

class NetworkRequest : public QObject
{
    Q_OBJECT
public:
    explicit NetworkRequest(QObject *parent = nullptr);
    static NetworkRequest* getInstance();

private:
    QNetworkAccessManager networkAccessManager;
    QNetworkRequest request;
    QUrl url{};

    static NetworkRequest *theInstance_;

public:
    void sendRequest(QString request);

private slots:
    void responseReceived(QNetworkReply *response);
signals:
    void sendResponse(QString response);

signals:

};

#endif // NETWORKREQUEST_H
