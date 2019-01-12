#include "orphandownloader.h"
#include <univalue.h>
#include "rpcipfs.h"

#include "guiutil.h"
#include "rpcpog.h"
#include "timedata.h"
#include <QUrl>
#include <boost/algorithm/string/case_conv.hpp> 
#include <QDir>
#include <QTimer>
#include <QString>


OrphanDownloader::OrphanDownloader(QString xURL, QString xDestName, int xTimeout) : sURL(xURL), sDestName(xDestName), iTimeout(xTimeout)
{

}


void OrphanDownloader::Get()
{
	if (sURL == "" || sDestName == "") return;
	int64_t nFileSize = GetFileSize(GUIUtil::FROMQS(sDestName));
	bDownloadFinished = false;
	if (nFileSize > 0) 
	{
		return;
	}

	// We must call out using an HTTPS request here - to pull down the Orphan's picture locally (otherwise QT won't display the image)
	manager = new QNetworkAccessManager;
    QNetworkRequest request;
    request.setUrl(QUrl(sURL));
	reply = manager->get(request);
	
    file = new QFile;
    file->setFileName(sDestName);
    file->open(QIODevice::WriteOnly);
    
	connect(reply,SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(onDownloadProgress(qint64,qint64)));
    connect(reply,SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(reply,SIGNAL(finished()), this, SLOT(onReplyFinished()));
	connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));
	QTimer::singleShot(iTimeout, this, SLOT(DownloadFailure()));
}

void OrphanDownloader::BusyWait()
{
	int iBroken = 0;
	while (1==1)
	{
		int64_t nFileSize = GetFileSize(GUIUtil::FROMQS(sDestName));
		if (nFileSize > 0 || bDownloadFinished) break;
		MilliSleep(250);
		iBroken++;
		if (iBroken > (iTimeout/500)) break;
	}
}

void OrphanDownloader::DownloadFailure()
{
	LogPrintf("Download failed\n");
	bDownloadFinished = true;
}

void OrphanDownloader::onDownloadProgress(qint64 bytesRead,qint64 bytesTotal)
{
	printf("OnDownloadProgress %f ", (double)bytesRead);
}

void OrphanDownloader::onFinished(QNetworkReply * reply)
{ 
    switch(reply->error()) 
    {
        case QNetworkReply::NoError:
        {
			printf(" \n Downloaded successfully. ");
        }
		break;
        default:
		{
            printf(" BioDownloadError %s ", GUIUtil::FROMQS(reply->errorString()).c_str());
        };
    }
    if(file->isOpen())
    {
        file->close();
        file->deleteLater();
    }
	bDownloadFinished = true;
}

void OrphanDownloader::onReadyRead()
{
    file->write(reply->readAll());
}

void OrphanDownloader::onReplyFinished()
{
    if(file->isOpen())
    {
        file->close();
        file->deleteLater();
    }
}

OrphanDownloader::~OrphanDownloader()
{
	// Note - there is no UI to delete
}

