#include "businessobjectlist.h"
#include "bitcoinunits.h"
#include "ui_businessobjectlist.h"
#include "secdialog.h"
#include "ui_secdialog.h"
#include "writeorphan.h"
#include "walletmodel.h"
#include "guiutil.h"
#include "rpcpog.h"
#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>
#include <QTimer>
#include <univalue.h>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

bool bSlotsCreated = false;

QStringList BusinessObjectList::GetHeaders(std::string sFields)
{
	QStringList pHeaders;
	UniValue aBO;
	if (ObjectType != "pog_leaderboard")
	{
		aBO	= GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
		sFields = "id," + aBO["fields"].getValStr();	
	}
	
	sHeaderFields = sFields;

	std::vector<std::string> vFields = Split(sFields.c_str(),",");
	for (int i = 0; i < (int)vFields.size(); i++)
	{
		std::string sFieldName = vFields[i];
		pHeaders << GUIUtil::TOQS(sFieldName);
	}
	return pHeaders;
}

BusinessObjectList::BusinessObjectList(const PlatformStyle *platformStyle, QWidget *parent) : ui(new Ui::BusinessObjectList)
{
    ui->setupUi(this);
}


BusinessObjectList::~BusinessObjectList()
{
    delete ui;
}

void BusinessObjectList::setModel(WalletModel *model)
{
    this->model = model;
}

void BusinessObjectList::RefreshPogLeaderboard()
{
	if (ObjectType == "pog_leaderboard") 
		UpdateObject("pog_leaderboard");
}

void BusinessObjectList::UpdateObject(std::string objType)
{
	ObjectType = objType;
	std::string sFields;
    QString pString;

	if (objType == "pog_leaderboard")
	{
		sFields = "id,nickname,address,height,amount,weight";
		pString = GUIUtil::TOQS(GetPOGBusinessObjectList(ObjectType, sFields));
		// Update once per minute
		QTimer::singleShot(60000, this, SLOT(RefreshPogLeaderboard()));
	}
	else
	{
		UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
		sFields = aBO["fields"].getValStr();
		pString	= GUIUtil::TOQS(GetBusinessObjectList(ObjectType, sFields));
	}
	// Show the difficulty, my_tithes, my_nickname, grand total (amount) as grand total rows
    QStringList pHeaders = GetHeaders(sFields);
    this->createUI(pHeaders, pString);
}

void BusinessObjectList::addFooterRow(int& rows, int& iFooterRow, std::string sCaption, std::string sValue)
{
	rows++;
    ui->tableWidget->setItem(rows, 0, new QTableWidgetItem(GUIUtil::TOQS(sCaption)));
	ui->tableWidget->setItem(rows, 1, new QTableWidgetItem(GUIUtil::TOQS(sValue)));
}

void BusinessObjectList::createUI(const QStringList &headers, const QString &pStr)
{
    ui->tableWidget->setShowGrid(true);
	ui->tableWidget->setRowCount(0);

    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);

    QVector<QVector<QString> > pMatrix;
	if (pStr == "") return;

    pMatrix = SplitData(pStr);
	int rows = pMatrix.size();
	int iFooterRow = 0;
	int iAmountCol = 0;
	int iNameCol = 0;
	if (ObjectType == "pog_leaderboard") 
	{
		iFooterRow += 6;
		iAmountCol = -1;
		iNameCol = GetUrlColumn("nickname");
	}
	else
	{
		iAmountCol = GetUrlColumn("Amount");
		iFooterRow = (iAmountCol > -1) ? 1 : 0;
	}

    ui->tableWidget->setRowCount(rows + iFooterRow);
    int cols = pMatrix[0].size() - 1;
    ui->tableWidget->setColumnCount(cols);
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    QString s;
	double dGrandTotal = 0;
	
    for(int i = 0; i < rows; i++)
	{
		bool bHighlighted = (iNameCol > 0 && pMatrix[i][iNameCol] == GUIUtil::TOQS(msNickName));
	
        for(int j = 0; j < cols; j++)
		{
			QTableWidgetItem* q = new QTableWidgetItem(pMatrix[i][j]);
			ui->tableWidget->setItem(i, j, q);
		}
		if (bHighlighted)
		{
			ui->tableWidget->selectRow(i);
			ui->tableWidget->item(i, iNameCol)->setBackground(Qt::yellow);
		}

		if (iAmountCol > -1)
		{
			dGrandTotal += cdbl(GUIUtil::FROMQS(pMatrix[i][iAmountCol]), 2);
		}
	}
	
	if (ObjectType == "pog_leaderboard")
	{
		std::string sXML = GUIUtil::FROMQS(pStr);
		addFooterRow(rows, iFooterRow, "Difficulty:", ExtractXML(sXML, "<difficulty>","</difficulty>"));
		addFooterRow(rows, iFooterRow, "My Tithes:", ExtractXML(sXML, "<my_tithes>","</my_tithes>"));
		addFooterRow(rows, iFooterRow, "My Nick Name:", ExtractXML(sXML, "<my_nickname>","</my_nickname>"));
		addFooterRow(rows, iFooterRow, "Total Pool Tithes:", ExtractXML(sXML, "<total>","</total>"));
		addFooterRow(rows, iFooterRow, "Total Pool Participants:", ExtractXML(sXML, "<participants>","</participants>"));
	}
	else if (iFooterRow > 0)
	{
		ui->tableWidget->setItem(rows, 0, new QTableWidgetItem("Grand Total:"));
		ui->tableWidget->setItem(rows, iAmountCol, new QTableWidgetItem(GUIUtil::TOQS(RoundToString(dGrandTotal, 2))));
	}

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->resizeRowsToContents();
    ui->tableWidget->resizeColumnsToContents();
    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);

	// Column widths should be set 
	for (int j=0; j < cols; j++)
	{
		ui->tableWidget->setColumnWidth(j, 128);
	}

	if (!bSlotsCreated)
	{
		connect(ui->tableWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotCustomMenuRequested(QPoint)));
		bSlotsCreated = true;
	}
}

void BusinessObjectList::slotCustomMenuRequested(QPoint pos)
{
    /* Create an object context menu */
    QMenu * menu = new QMenu(this);
    //  Create, Connect and Set the actions to the menu
    menu->addAction(tr("Navigate To"), this, SLOT(slotNavigateTo()));
	menu->addAction(tr("List"), this, SLOT(slotList()));
	menu->addAction(tr("View"), this, SLOT(slotView()));
	if (ObjectType=="orphan")
	{
		if (Params().NetworkIDString() != "main") menu->addAction(tr("Write Orphan"), this, SLOT(slotWriteOrphan()));
	}
	if (ObjectType == "letter")
	{
		if (Params().NetworkIDString() != "main") menu->addAction(tr("Review Letter"), this, SLOT(slotReviewLetter()));
	}

    menu->popup(ui->tableWidget->viewport()->mapToGlobal(pos));
}

std::string BusinessObjectList::GetHtmlForm(std::string PK)
{
	UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
	std::string sFields = aBO["fields"].getValStr();	
	std::vector<std::string> vFields = Split(sFields.c_str(), ",");
	std::vector<std::string> vPK = Split(PK.c_str(), "-");
	std::string sError = "";
	if (vPK.size() > 2)
	{
		aBO = GetBusinessObject(vPK[0], vPK[1], sError);
		std::string sHTML = "<table>";
		for (int i = 0; i < (int)vFields.size(); i++)
		{
			std::string sFieldName = vFields[i];
			std::string sFV = aBO[sFieldName].getValStr();
			std::string sRow = "<tr><td>" + sFieldName + ":</td><td>" + sFV + "</td></tr>";
			sHTML += sRow;
		}
		sHTML += "</table>";
		return sHTML;
	}
	return "Business object not found";
}

int BusinessObjectList::GetUrlColumn(std::string sTarget)
{
	boost::to_upper(sTarget);
	// UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
	// std::string sFields = "id," + aBO["fields"].getValStr();	
	std::vector<std::string> vFields = Split(sHeaderFields.c_str(), ",");
	for (int i = 0; i < (int)vFields.size(); i++)
	{
		std::string sFieldName = vFields[i];
		boost::to_upper(sFieldName);
		if (sFieldName == sTarget) return i;
	}
	return -1;
}


void BusinessObjectList::slotList()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
		// Navigate to the List of the Object
        std::string sID = GUIUtil::FROMQS(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash of business object
		int iCol = GetUrlColumn("object_name");
		if (iCol > -1)
		{
			std::string sTarget = GUIUtil::FROMQS(ui->tableWidget->item(row, iCol)->text());
			// Close existing menu
			UpdateObject(sTarget);
		}
    }
}

void BusinessObjectList::slotReviewLetter()
{
	int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        std::string id = GUIUtil::FROMQS(ui->tableWidget->item(row, 0)->text());
		WriteOrphan *dlg = new WriteOrphan(this, "REVIEW", "", id);
		dlg->show();
	}
}


void BusinessObjectList::slotWriteOrphan()
{
	int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        std::string id = GUIUtil::FROMQS(ui->tableWidget->item(row, 0)->text());
		int iURLCol = GetUrlColumn("ORPHANID");
		std::string orphanId = GUIUtil::FROMQS(ui->tableWidget->item(row, iURLCol)->text());
		// The last argument is the letter ID, this is required on an Edit but not on an Add
		WriteOrphan *dlg = new WriteOrphan(this, "ADD", orphanId, "");
		dlg->show();
    }
}

void BusinessObjectList::slotView()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        std::string id = GUIUtil::FROMQS(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash of business object
		msgBox.setWindowTitle(GUIUtil::TOQS(id));
	    msgBox.setText(GUIUtil::TOQS(GetHtmlForm(id)));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
		msgBox.exec();
    }
}


void BusinessObjectList::slotNavigateTo()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
		// Open the URL
        QMessageBox msgBox;
		int iURLCol = GetUrlColumn("URL");
		if (iURLCol > -1)
		{
			QString Url = ui->tableWidget->item(row, iURLCol)->text();
			QUrl pUrl(Url);
			QDesktopServices::openUrl(pUrl);
		}
    }
}

QVector<QVector<QString> > BusinessObjectList::SplitData(const QString &pStr)
{
	QStringList proposals = pStr.split(QRegExp("<object>"),QString::SkipEmptyParts);
    int nProposals = proposals.size();
    QVector<QVector<QString> > proposalMatrix;
    for (int i=0; i < nProposals; i++)
    {
        QStringList proposalDetail = proposals[i].split(QRegExp("<col>"));
        int detailSize = proposalDetail.size();
		if (detailSize > 1)
		{
			proposalMatrix.append(QVector<QString>());
			for (int j = 0; j < detailSize; j++)
			{
				QString sData = proposalDetail[j];
				/*  Reserved for BitcoinUnits
					sData = BitcoinUnits::format(2, cdbl(GUIUtil::FROMQS(sData), 2) * 100, false, BitcoinUnits::separatorAlways);		
				*/
				proposalMatrix[i].append(sData);
			}
		}
    }
	return proposalMatrix;
}
