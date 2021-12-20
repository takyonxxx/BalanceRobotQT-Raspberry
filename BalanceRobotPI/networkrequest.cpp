#include "networkrequest.h"
#include "constants.h"

NetworkRequest *NetworkRequest::theInstance_= nullptr;

NetworkRequest* NetworkRequest::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new NetworkRequest();
    }
    return theInstance_;
}

NetworkRequest::NetworkRequest(QObject *parent) : QObject(parent)
{
    this->url.setUrl(baseDuckduckgo);
    connect(&networkAccessManager, &QNetworkAccessManager::finished, this, &NetworkRequest::responseReceived);
}

void NetworkRequest::sendRequest(QString request)
{
    QString _query =QString("q=%1&format=json&pretty=1&no_html=1&skip_disambig=1").arg(request);
    this->url.setQuery(_query);

    this->request.setUrl(this->url);
    this->request.setHeader(QNetworkRequest::ContentTypeHeader, "application/text");
    QNetworkReply *reply = networkAccessManager.get(this->request);
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),this, SLOT(slotError( QNetworkReply::NetworkError)));
}

void NetworkRequest::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    qDebug() << "Download progress:" << bytesReceived << bytesTotal;
}

void NetworkRequest::slotError(QNetworkReply::NetworkError) {
    qDebug() << "error";
}

void NetworkRequest::responseReceived(QNetworkReply *response)
{
    QString clearText{"no result"};
    QString strReply = (QString)response->readAll();

    auto jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
    if(jsonResponse.isObject())
    {
        QJsonObject obj = jsonResponse.object();
        QJsonObject::iterator itr = obj.find("RelatedTopics");
        if(itr == obj.end())
        {
            // object not found.
        }
        else
        {
            auto jsonValue =jsonResponse["RelatedTopics"][0];
            auto j_object = jsonValue.toObject();
            foreach(const QString& key, j_object.keys()) {
                QJsonValue value = j_object.value(key);
                if(key.contains("Text"))
                {
                    clearText = j_object.value(key).toString();
                }
            }
        }
    }

    emit sendResponse(clearText);
    response->deleteLater();
}

