#include "businessobjectlist.h"
#include "bitcoinunits.h"
#include "ui_businessobjectlist.h"
#include "secdialog.h"
#include "ui_secdialog.h"
#include "writeorphan.h"
#include "walletmodel.h"
#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>
#include <univalue.h>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()

QString ToQstring(std::string s);
std::string FromQStringW(QString qs);
std::string RoundToString(double d, int place);
std::string GetBusinessObjectList(std::string sType, std::string sFields);
UniValue GetBusinessObjectByFieldValue(std::string sType, std::string sFieldName, std::string sSearchValue);
UniValue GetBusinessObject(std::string sType, std::string sPrimaryKey, std::string& sError);
double cdbl(std::string s, int place);
std::string ObjectType = "";
extern int GetUrlColumn(std::string sTarget);
bool bSlotsCreated = false;

QStringList BusinessObjectList::GetHeaders()
{
	QStringList pHeaders;
	UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
	std::string sFields = "id," + aBO["fields"].getValStr();	
	std::vector<std::string> vFields = Split(sFields.c_str(),",");
	for (int i = 0; i < (int)vFields.size(); i++)
	{
		std::string sFieldName = vFields[i];
		pHeaders << ToQstring(sFieldName);
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

void BusinessObjectList::UpdateObject(std::string objType)
{
	ObjectType = objType;
	UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
	std::string sFields = aBO["fields"].getValStr();	
    QString pString = ToQstring(GetBusinessObjectList(ObjectType, sFields));
    QStringList pHeaders = GetHeaders();
    this->createUI(pHeaders, pString);
}


void BusinessObjectList::createUI(const QStringList &headers, const QString &pStr)
{
    ui->tableWidget->setShowGrid(true);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);

    QVector<QVector<QString> > pMatrix;
	if (pStr == "") return;

    pMatrix = SplitData(pStr);
	int iAmountCol = GetUrlColumn("Amount");
	int iFooterRow = (iAmountCol > -1) ? 1 : 0;
    int rows = pMatrix.size();
    ui->tableWidget->setRowCount(rows + iFooterRow);
    int cols = pMatrix[0].size() - 1;
    ui->tableWidget->setColumnCount(cols);
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    QString s;
	double dGrandTotal = 0;
    for(int i=0; i < rows; i++)
	{
        for(int j=0; j < cols; j++)
		{
            ui->tableWidget->setItem(i,j, new QTableWidgetItem(pMatrix[i][j]));
		}
		if (iAmountCol > -1)
		{
			dGrandTotal += cdbl(FromQStringW(pMatrix[i][iAmountCol]), 2);
		}
	}
	if (iFooterRow > 0)
	{
		ui->tableWidget->setItem(rows, 0, new QTableWidgetItem("Grand Total:"));
		ui->tableWidget->setItem(rows, iAmountCol, new QTableWidgetItem(ToQstring(RoundToString(dGrandTotal, 2))));
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

    menu->popup(ui->tableWidget->viewport()->mapToGlobal(pos));
}

std::string GetHtmlForm(std::string PK)
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

int GetUrlColumn(std::string sTarget)
{
	boost::to_upper(sTarget);
	UniValue aBO = GetBusinessObjectByFieldValue("object", "object_name", ObjectType);
	std::string sFields = "id," + aBO["fields"].getValStr();	
	std::vector<std::string> vFields = Split(sFields.c_str(), ",");
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
        std::string sID = FromQStringW(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash of business object
		int iCol = GetUrlColumn("object_name");
		if (iCol > -1)
		{
			std::string sTarget = FromQStringW(ui->tableWidget->item(row, iCol)->text());
			// Close existing menu

			UpdateObject(sTarget);
		}
    }
}

void BusinessObjectList::slotWriteOrphan()
{
	int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        std::string id = FromQStringW(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash of business object
		WriteOrphan dlg(this);
		dlg.exec();
    }
}
void BusinessObjectList::slotView()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        std::string id = FromQStringW(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash of business object
		msgBox.setWindowTitle(ToQstring(id));
	    msgBox.setText(ToQstring(GetHtmlForm(id)));
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
        proposalMatrix.append(QVector<QString>());
        QStringList proposalDetail = proposals[i].split(QRegExp("<col>"));
        int detailSize = proposalDetail.size();
        for (int j = 0; j < detailSize; j++)
		{
			QString sData = proposalDetail[j];
			/*  Reserved for BitcoinUnits
				sData = BitcoinUnits::format(2, cdbl(FromQStringW(sData), 2) * 100, false, BitcoinUnits::separatorAlways);		
			*/
			proposalMatrix[i].append(sData);
		}
    }
	return proposalMatrix;
}
