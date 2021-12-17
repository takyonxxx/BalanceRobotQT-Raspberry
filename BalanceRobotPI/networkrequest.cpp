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
    this->url.setUrl(baseWikiApi);
    connect(&networkAccessManager, &QNetworkAccessManager::finished, this, &NetworkRequest::responseReceived);
}

void NetworkRequest::sendRequest(QString request)
{
    this->url.setQuery("format=json&action=query&prop=extracts&srlimit=1&origin=*&exintro&explaintext&redirects=1&titles=" + request.split(" ")[0]);

    this->request.setUrl(this->url);
    this->request.setHeader(QNetworkRequest::ContentTypeHeader, "application/text");
    QNetworkReply *reply = networkAccessManager.get(this->request);
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),this, SLOT(slotError( QNetworkReply::NetworkError)));
    //connect(reply, SIGNAL(downloadProgress(qint64,qint64)), SLOT(onDownloadProgress(qint64,qint64)));
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
    QString clearText{};
    QString strReply = (QString)response->readAll();

    auto jsonResponse = QJsonDocument::fromJson(strReply.toUtf8());
    auto jsonObject = jsonResponse.object();
    QString strFromObj = QJsonDocument(jsonObject).toJson(QJsonDocument::Compact).toStdString().c_str();

    auto jsonValue =jsonResponse["query"]["pages"];

    auto j1_object = jsonValue.toObject();
    foreach(const QString& key, j1_object.keys()) {
        QJsonValue value = j1_object.value(key);
        auto j2_object = value.toObject();
        foreach(const QString& key, j2_object.keys())
        {
            if(key.contains("extract"))
            {
                clearText = j2_object.value(key).toString();
            }
        }
    }

    if(clearText.isEmpty())
    {
        foreach(const QString& key, j1_object.keys()) {
            QJsonValue value = j1_object.value(key);
            auto j2_object = value.toObject();
            foreach(const QString& key, j2_object.keys())
            {
                if(key.contains("title"))
                {
                    clearText = j2_object.value(key).toString();
                }
            }
        }
    }

    if(!clearText.isEmpty())
    {
        clearText = clearText.split(".")[0];

        clearText.replace( QRegExp( "[" + QRegExp::escape( "\\/:*?\"<>|(){}" ) + "]" ), QString( "" ) );
        clearText = clearText.normalized(QString::NormalizationForm_KD);

        /*QString result{};
        copy_if(clearText.begin(), clearText.end(), back_inserter(result), [](QChar& c) {
            return c.toLatin1() != 0;
        });*/
    }

    emit sendResponse(clearText);
    response->deleteLater();
}
