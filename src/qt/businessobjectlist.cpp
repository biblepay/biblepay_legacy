#include "businessobjectlist.h"
#include "bitcoinunits.h"
#include "ui_businessobjectlist.h"
#include "masternode-sync.h"
#include "secdialog.h"
#include "ui_secdialog.h"
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
std::string sMode;

QStringList BusinessObjectList::GetHeaders(std::string sFields)
{
	QStringList pHeaders;

	sFields = "campaign,nickname,cpk,points,owed,prominence";
	sHeaderFields = sFields;

	std::vector<std::string> vFields = Split(sFields.c_str(), ",");
	for (int i = 0; i < (int)vFields.size(); i++)
	{
		std::string sFieldName = vFields[i];
		pHeaders << GUIUtil::TOQS(sFieldName);
	}
	return pHeaders;
}

BusinessObjectList::BusinessObjectList(const PlatformStyle *platformStyle, QWidget *parent) : ui(new Ui::BusinessObjectList)
{
	sMode = "pog";
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
	UpdateObject("leaderboard");
}

void BusinessObjectList::UpdateObject(std::string objType)
{
	ObjectType = objType;
	std::string sFields;
    QString pString;
	static bool bRefreshed = false;
	if (masternodeSync.IsBlockchainSynced())
	{
		sFields = "campaign,nickname,cpk,points,owed,prominence";
		pString = GUIUtil::TOQS(GetPOGBusinessObjectList(sMode, sFields));
        // Update once per seven minutes
		if (!bRefreshed)
		{
			bRefreshed = true;
			QTimer::singleShot(700000, this, SLOT(RefreshPogLeaderboard()));
		}
	}
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
	ui->tableWidget->setSortingEnabled(false);
	
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);

    QVector<QVector<QString> > pMatrix;
	if (pStr == "") return;

    pMatrix = SplitData(pStr);
	int rows = pMatrix.size();
	int iFooterRow = 0;
	int iAmountCol = 3;
	int iNameCol = 1;
	iFooterRow += 6;
	std::string sXML = GUIUtil::FROMQS(pStr);
	std::string msNickName = ExtractXML(sXML, "<my_nickname>","</my_nickname>");
    ui->tableWidget->setRowCount(rows + iFooterRow);
    int cols = pMatrix[0].size();
    ui->tableWidget->setColumnCount(cols);
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    QString s;
	double dGrandTotal = 0;
	int iHighlighted = 0;
	//	Leaderboard fields = "nickname,cpk,points,owed,prominence";
	ui->tableWidget->setSortingEnabled(false);

    for (int i = 0; i < rows; i++)
	{
		bool bHighlighted = (pMatrix[i][iNameCol] == GUIUtil::TOQS(msNickName));
		// sFields = "campaign,nickname,cpk,points,owed,prominence";

        for(int j = 0; j < cols; j++)
		{
            QTableWidgetItem* q;
			bool bNumeric = (j == 3 || j == 4 || j == 5);
            if (bNumeric) 
			{
                q = new NumericTableWidgetItem(pMatrix[i][j]);
            }
            else 
			{
                q = new QTableWidgetItem(pMatrix[i][j]);
            }
			ui->tableWidget->setItem(i, j, q);
		}
		if (bHighlighted)
		{
			ui->tableWidget->selectRow(i);
			ui->tableWidget->item(i, iNameCol)->setBackground(Qt::yellow);
			iHighlighted = i;
		}

		if (iAmountCol > -1)
		{
			dGrandTotal += cdbl(GUIUtil::FROMQS(pMatrix[i][iAmountCol]), 2);
		}
	}
	
	bool fLeaderboard = true;

	if (fLeaderboard)
	{
        // Sort by ShareWeight descending (unless there is already a different one)
        int default_sort_column = 5;
        int iSortColumn = ui->tableWidget->horizontalHeader()->sortIndicatorSection();
        Qt::SortOrder soDefaultOrder = Qt::DescendingOrder;
        Qt::SortOrder soCurrentOrder = ui->tableWidget->horizontalHeader()->sortIndicatorOrder();
        if (soDefaultOrder == soCurrentOrder && iSortColumn == default_sort_column && iSortColumn > 1)   
		{
            ui->tableWidget->sortByColumn(default_sort_column, soDefaultOrder);
        }
		ui->tableWidget->setSortingEnabled(false);

		addFooterRow(rows, iFooterRow, "Difficulty:", ExtractXML(sXML, "<difficulty>","</difficulty>"));
		addFooterRow(rows, iFooterRow, "My Points:", ExtractXML(sXML, "<my_points>","</my_points>"));
		addFooterRow(rows, iFooterRow, "My Nick Name:", ExtractXML(sXML, "<my_nickname>","</my_nickname>"));
		addFooterRow(rows, iFooterRow, "Total Points:", ExtractXML(sXML, "<total_points>","</total_points>"));
		addFooterRow(rows, iFooterRow, "Total Participants:", ExtractXML(sXML, "<participants>","</participants>"));

		if (iHighlighted > 0) 
		{
			ui->tableWidget->selectRow(iHighlighted);
		}
		std::string sLowBlock = ExtractXML(sXML, "<lowblock>", "</lowblock>");
		std::string sHighBlock = ExtractXML(sXML, "<highblock>", "</highblock>");
		std::string sHeading = "Leaderboard v1.0 - Range " + sLowBlock + " to " + sHighBlock;
		// Label Heading
		ui->lblLeaderboard->setText(GUIUtil::TOQS(sHeading));
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
	    connect(ui->btnSummary, SIGNAL(clicked()), this, SLOT(showSummary()));
		connect(ui->btnDetails, SIGNAL(clicked()), this, SLOT(showDetails()));
		connect(ui->tableWidget->horizontalHeader(), SIGNAL(sectionPressed(int)),this, SLOT(HandleIndicatorChanged(int)));
		bSlotsCreated = true;
	}
}

void BusinessObjectList::HandleIndicatorChanged(int logicalIndex)
{
	if (logicalIndex != 0 && logicalIndex != 1)
	{
		ui->tableWidget->horizontalHeader()->setSortIndicatorShown(true);
		Qt::SortOrder soCurrentOrder = ui->tableWidget->horizontalHeader()->sortIndicatorOrder();
		ui->tableWidget->sortByColumn(logicalIndex, soCurrentOrder);
	}
}

void BusinessObjectList::slotCustomMenuRequested(QPoint pos)
{
    return;

    /* Create an object context menu 
    QMenu * menu = new QMenu(this);
    //  Create, Connect and Set the actions to the menu
    menu->addAction(tr("Navigate To"), this, SLOT(slotNavigateTo()));
	menu->addAction(tr("List"), this, SLOT(slotList()));
	menu->popup(ui->tableWidget->viewport()->mapToGlobal(pos));
	*/
}

int BusinessObjectList::GetUrlColumn(std::string sTarget)
{
	boost::to_upper(sTarget);
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
        std::string sID = GUIUtil::FROMQS(ui->tableWidget->item(row, 0)->text()); // PK-2PK-IPFS Hash
		int iCol = GetUrlColumn("object_name");
		if (iCol > -1)
		{
			std::string sTarget = GUIUtil::FROMQS(ui->tableWidget->item(row, iCol)->text());
			// Close existing menu
			UpdateObject(sTarget);
		}
    }
}

void BusinessObjectList::showSummary()
{
	sMode = "pog";
	UpdateObject("leaderboard");
}

void BusinessObjectList::showDetails()
{
	sMode = "pogdetails";
	UpdateObject("leaderboard");
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
