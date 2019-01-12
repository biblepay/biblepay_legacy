#ifndef WRITEORPHAN_H
#define WRITEORPHAN_H

#include <QDialog>
#include <QObject>
#include <QWidget>
#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <QVector>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QTableWidget>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QStringList>

class OptionsModel;
class PlatformStyle;
class WalletModel;


namespace Ui {
	class WriteOrphan;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class WriteOrphan :  public QDialog
{
    Q_OBJECT

public:
    explicit WriteOrphan(QWidget *parent = 0, std::string xMode = "", std::string xOrphanID = "", std::string xLetterID = "");
    ~WriteOrphan();
	void setModel(WalletModel *model);
	void UpdateObject(std::string objType);
	void GetImage(std::string sURL, std::string sDest, int iTimeout);

private:
    Ui::WriteOrphan *ui;
	WalletModel *model;
    void createUI(const QStringList &headers, const QString &pStr);
    QVector<QVector<QString> > SplitData(const QString &pStr);
	void PopulateAttachedPics();
	QString ReadAllText(std::string sPath);
	void DepersistOrphanName();
	std::string Save();
	int nTries;
	std::string sOrphanID;
	std::string sObjectID;
	std::string sMode;
	std::string sDir;
	std::string sBIOURL;
	std::string sName;
	std::string sBioImage;
	bool fDownloadFinished;
	static const int MAX_RETRIES_NETWORK_FAILURE = 50;
	static const int MAX_ATTACHMENT_COUNT = 16;

public Q_SLOTS:
	void AttachImage();
	void PopulateBIO();
	void SaveRecord();
	void Load();


};

#endif // WRITEORPHAN_H
