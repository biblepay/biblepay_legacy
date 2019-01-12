#include "writeorphan.h"
#include "bitcoinunits.h"
#include "ui_writeorphan.h"
#include "secdialog.h"
#include "ui_secdialog.h"
#include "guiutil.h"
#include "walletmodel.h"
#include <univalue.h>
#include "rpcipfs.h"
#include "rpcpog.h"
#include "timedata.h"
#include "orphandownloader.h"

#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <QDir>
#include <QTimer>


WriteOrphan::WriteOrphan(QWidget *parent, std::string xMode, std::string xOrphanID, std::string xLetterID) : QDialog(parent),ui(new Ui::WriteOrphan)
{
    ui->setupUi(this);
	sMode = xMode;
	sOrphanID = xOrphanID;
	nTries = 0;
	if (sMode == "ADD")
	{
		// The letter ID is a new Letter ID
		sObjectID = RoundToString(GetAdjustedTime(), 0);
		std::string sText = "Please type your letter here.";
	    QString qsp = QString::fromStdString(sText);
	    ui->txtOrphan->setText(qsp);
		DepersistOrphanName();
		std::string sTitle = "Write Orphan - " + sName + "- " + sMode;
	    setWindowTitle(QString::fromStdString(sTitle));
	}
	else
	{
		// The letter ID is the IPFS letter ID
		sObjectID = xLetterID;
	}
	// The Attachment directory is the hash of the letterID
	sDir = GetSANDirectory2() + RetrieveMd5(sObjectID) + "/";
	EnsureDirectoryExists(sDir);
	if (sMode == "REVIEW")
	{
		Load();
		ui->btnSave->setEnabled(false);
		ui->btnAttachImage->setEnabled(false);
	}
	PopulateAttachedPics();
	connect(ui->btnAttachImage, SIGNAL(clicked()), this, SLOT(AttachImage()));
	connect(ui->btnSave, SIGNAL(clicked()), this, SLOT(SaveRecord()));
	PopulateBIO();
}

void WriteOrphan::SaveRecord()
{
	std::string sTXID = Save();
	std::string sNarr = (sTXID.empty()) ? "Unable to save record.  (Ensure wallet is unlocked and retry, next check IPFS configuration.)" : "Successfully saved letter " + sTXID + ".  Thank you for writing to BiblePay Orphans!";
	QMessageBox::warning(this, tr("Save Letter"), GUIUtil::TOQS(sNarr));
}

void WriteOrphan::DepersistOrphanName()
{
	// Retrieve the Orphans Name
	UniValue aBO = GetBusinessObjectByFieldValue("orphan", "orphanid", sOrphanID);
	sName = aBO["name"].getValStr();
	sBIOURL = aBO["url"].getValStr();
	LogPrintf(" orphanid %s name %s bio %s ",sOrphanID.c_str(), sName.c_str(), sBIOURL.c_str());
}

void WriteOrphan::PopulateAttachedPics()
{
	// Attached PICS
	ui->listPics->setViewMode(QListWidget::IconMode);
	ui->listPics->setIconSize(QSize(210, 180));
	ui->listPics->setResizeMode(QListWidget::Adjust);
	ui->listPics->clear();
	QDir directory(GUIUtil::TOQS(sDir)); 
	directory.setNameFilters(QStringList() << "*.jpg" << "*.jpeg");
	QStringList fileList = directory.entryList();
    for (int i = 0; i < fileList.count(); i++)
    {
		QListWidgetItem *item = new QListWidgetItem(QIcon(fileList[i]), fileList[i]);
		QString sPath = GUIUtil::TOQS(sDir) + fileList[i];
		QImage image(sPath);
		if (sPath != GUIUtil::TOQS(sBioImage))
		{
			item->setData(Qt::DecorationRole, QPixmap::fromImage(image));
			item->setIcon(QIcon(sPath));
			item->setSizeHint(QSize(210,210));
			ui->listPics->addItem(item);
		}
	}
}

void WriteOrphan::AttachImage()
{
	// Add a picture to the outbound letter
	QString filename = GUIUtil::getOpenFileName(this, tr("Select a picture to attach to your outbound letter"), "", tr("Pictures (*.jpg *.jpeg)"), NULL);
    if(filename.isEmpty()) return;
    QUrl fileUri = QUrl::fromLocalFile(filename);
	std::string sFN = GUIUtil::FROMQS(fileUri.toString());
	bool bFromWindows = Contains(sFN, "file:///C:") || Contains(sFN, "file:///D:") || Contains(sFN, "file:///E:");
	if (!bFromWindows)
	{
		sFN = strReplace(sFN, "file://", "");  // This leaves the full unix path
	}
	else
	{
		sFN = strReplace(sFN, "file:///", "");  // This leaves the windows drive letter
	}
	// Copy the file into the outbound IPFS folder
	int64_t nSize = GetFileSize(sFN);
	if (nSize < 1) return;
	std::string sExt = boost::filesystem::extension(sFN);
	std::string sDestHash = RetrieveMd5(sFN);
	std::string sDest = sDir + sDestHash + sExt;
	CopyFile(sFN, sDest);
	PopulateAttachedPics();
}

void WriteOrphan::GetImage(std::string sURL, std::string sDest, int iTimeout)
{
	OrphanDownloader* thread1 = new OrphanDownloader(GUIUtil::TOQS(sURL), GUIUtil::TOQS(sDest), iTimeout);
	thread1->Get();
}

QString WriteOrphan::ReadAllText(std::string sPath)
{
	QFile myBiofile(GUIUtil::TOQS(sPath));
	myBiofile.open(QFile::ReadOnly | QFile::Text);
	QTextStream stream(&myBiofile);
	QString mytext = stream.readAll();
	myBiofile.close();
	return mytext;
}

void WriteOrphan::Load()
{
	//PK=LETTER, SK=LETTER+TIMESTAMP
	nTries++;
	if (nTries > MAX_RETRIES_NETWORK_FAILURE) return;  // In case of network failure, this occurs if the page links cannot be downloaded
	std::string sError;
	UniValue aBO(UniValue::VOBJ);
	std::vector<std::string> vFields = Split(sObjectID.c_str(), "-");
	
	aBO = GetBusinessObject("LETTER", vFields[1], sError);
	sOrphanID = aBO["orphanid"].getValStr();
	std::string sBody = aBO["body"].getValStr();
	std::string sAdded = aBO["added"].getValStr();
	std::string sFrom = aBO["from"].getValStr();
    for (int i = 0; i < MAX_ATTACHMENT_COUNT; i++)
	{
		std::string sAttachment = aBO["attachment" + RoundToString(i, 0)].getValStr();
		std::string sImage = sDir + sAttachment + ".jpg";
		int64_t nSize = GetFileSize(sImage);
		if (!sAttachment.empty())
		{
			if (nSize < 1)
			{
				std::string sURL = "http://ipfs.biblepay.org:8080/ipfs/" + sAttachment;
				GetImage(sURL, sImage, 7000);
				QTimer::singleShot(1000, this, SLOT(Load()));
			}
			
		}
	}
	DepersistOrphanName();
	ui->txtOrphan->setText(QString::fromStdString(sBody));
	std::string sTitle = "Review Orphan Letter - " + sName + "- " + sMode;
	setWindowTitle(QString::fromStdString(sTitle));
}

std::string WriteOrphan::Save()
{
	// Create a 'Letter' Business Object
	UniValue oBO(UniValue::VOBJ);
	oBO.push_back(Pair("objecttype", "letter"));
	oBO.push_back(Pair("primarykey", "letter"));
	oBO.push_back(Pair("secondarykey", sObjectID));
	oBO.push_back(Pair("added", RoundToString(GetAdjustedTime(), 0)));
	oBO.push_back(Pair("deleted", "0"));
	std::string sNickName = GetArg("-nickname", "");
	oBO.push_back(Pair("nickname", sNickName));
	std::string sReceivingAddress = DefaultRecAddress(BUSINESS_OBJECTS);
	oBO.push_back(Pair("receiving_address", sReceivingAddress));
	std::string sBody = GUIUtil::FROMQS(ui->txtOrphan->toPlainText());
	oBO.push_back(Pair("body", sBody));
	oBO.push_back(Pair("orphanid", sOrphanID));
	// Loop through the attachments
	QDir directory(GUIUtil::TOQS(sDir)); 
	directory.setNameFilters(QStringList() << "*.jpg" << "*.jpeg");
	QStringList fileList = directory.entryList();
	int j = 0;
	std::string sError;
	
    for (int i = 0; i < fileList.count(); i++)
    {
		QString sPath = GUIUtil::TOQS(sDir) + fileList[i];
		if (sPath != GUIUtil::TOQS(sBioImage))
		{
			std::string sIPFSHash = SubmitToIPFS(GUIUtil::FROMQS(sPath), sError);
			if (!sError.empty())
			{
				std::string sFromPath = GUIUtil::FROMQS(sPath);
				LogPrintf("Error while adding attachment %s to IPFS %s ", sError.c_str(), sFromPath.c_str());
			}
			if (!sIPFSHash.empty())
			{
				j++;
				oBO.push_back(Pair("attachment" + RoundToString(j, 0), sIPFSHash));
			}
		}					
	}
	sError = "";
	std::string txid = StoreBusinessObjectWithPK(oBO, sError);
	return txid;
}

void WriteOrphan::PopulateBIO()
{
	nTries++;
	if (nTries > MAX_RETRIES_NETWORK_FAILURE) return;

	std::string sPath = sDir + "bio.htm";
	int64_t nBioSize = GetFileSize(sPath);
	if (nBioSize <= 1)
	{
		GetImage(sBIOURL, sPath, 7000);
		QTimer::singleShot(1000, this, SLOT(PopulateBIO()));
    }
	nBioSize = GetFileSize(sPath);
	if (nBioSize <= 1) return;

	// Convert the unsecure links to local file system, otherwise QT will not display these images:
	QString sMyText = ReadAllText(sPath);
	std::string s1 = GUIUtil::FROMQS(sMyText);
	std::string sImage = "https://" + ExtractXML(s1, "src=\"https://", ".jpg") + ".jpg";
	// Calculate hash of Orphan Bio Image
	std::string sImageHash = RetrieveMd5(sImage);
	sBioImage = sDir + sImageHash + ".jpg";

	if (fDebugMaster) LogPrintf(" %s downloading %s to file %s \n",sBIOURL.c_str(), sImage.c_str(), sBioImage.c_str());
	int64_t nFileSize = GetFileSize(sBioImage);
	if (nFileSize <= 1)
	{	
		GetImage(sImage, sBioImage, 7000);
		QTimer::singleShot(1000, this, SLOT(PopulateBIO()));
    }
	s1 = strReplace(s1, sImage, sBioImage);
	ui->txtBio->setHtml(GUIUtil::TOQS(s1));
	PopulateAttachedPics();
}


WriteOrphan::~WriteOrphan()
{
    delete ui;
}

void WriteOrphan::setModel(WalletModel *model)
{
    this->model = model;
}

