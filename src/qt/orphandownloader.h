#ifndef ORPHANDOWNLOADER_H
#define ORPHANDOWNLOADER_H

#include <QDialog>
#include <QObject>
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QMenu>
#include <QTableWidget>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QThread>
#include <QString>
#include <QFile>


class OrphanDownloader : public QDialog
{
	Q_OBJECT

public:
    explicit OrphanDownloader(QString xURL, QString xDest, int iTimeout);
    ~OrphanDownloader();
	void Print();
	void Get();
	bool bDownloadFinished;

private:
	QString sURL;
	QString sDestName;
	int iTimeout;
	QNetworkAccessManager *manager;
    QNetworkReply *reply;
    QFile *file;

public Q_SLOTS:
	void onDownloadProgress(qint64,qint64);
    void onFinished(QNetworkReply*);
    void onReadyRead();
    void onReplyFinished();
	void BusyWait();
	void DownloadFailure();


};

#endif // ORPHANDOWNLOADER_H
