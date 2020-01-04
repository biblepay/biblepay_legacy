#include "proposals.h"
#include "bitcoinunits.h"
#include "ui_proposals.h"
#include "secdialog.h"
#include "ui_secdialog.h"
#include "walletmodel.h"
#include "guiutil.h"
#include "rpcpog.h"
#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>


QStringList Proposals::GetHeaders()
{
	QStringList pHeaders;
	pHeaders << tr("Proposal ID")
                       << tr("Proposal Name")
                       << tr("Amount")
                       << tr("Expense Type")
                       << tr("Created")
                       << tr("Yes Ct")
                       << tr("No Ct")
                       << tr("Abstain Ct")
                       << tr("Url");
	return pHeaders;
}

Proposals::Proposals(const PlatformStyle *platformStyle, QWidget *parent) :
    ui(new Ui::Proposals)
{
    ui->setupUi(this);
    
	/* Reserved - Use when we add buttons to this page
	QString theme = GUIUtil::getThemeName();
    
    if (!platformStyle->getImagesOnButtons()) 
	{
        ui->btnSubmit->setIcon(QIcon());
    } else {
        ui->btnSubmit->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
    }
	*/
	UpdateDisplay();
}

/* Input String Format
	<proposal>1,proposal name1,amount1,expensetype1,createtime1,yescount1,nocount1,abstaincount1,url1
*/

Proposals::~Proposals()
{
    delete ui;
}


void Proposals::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateDisplay();
    }
}


void Proposals::UpdateDisplay()
{
    QString pString = GUIUtil::TOQS(GetActiveProposals());
    QStringList pHeaders = GetHeaders();
    this->createUI(pHeaders, pString);
}


void Proposals::createUI(const QStringList &headers, const QString &pStr)
{

    ui->tableWidget->setShowGrid(true);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);

    QVector<QVector<QString> > pMatrix;
	if (pStr == "") return;

    pMatrix = SplitData(pStr);
    int rows = pMatrix.size();
    ui->tableWidget->setRowCount(rows);
    int cols = pMatrix[0].size();
	if (cols > 9) cols = 9; //Limit to the URL column for now
    ui->tableWidget->setColumnCount(cols);
    ui->tableWidget->setHorizontalHeaderLabels(headers);

    QString s;
    for(int i=0; i<rows; i++)
        for(int j=0; j<cols; j++)
            ui->tableWidget->setItem(i,j, new QTableWidgetItem(pMatrix[i][j]));

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->resizeRowsToContents();
    ui->tableWidget->resizeColumnsToContents();
    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	// Column 0 width should be slimmer
	ui->tableWidget->setColumnWidth(0, 115);
	ui->tableWidget->setColumnWidth(1, 150);
	ui->tableWidget->setColumnWidth(2, 80);  //Amount
	ui->tableWidget->setColumnWidth(3, 80);  //Expense Type
	ui->tableWidget->setColumnWidth(4, 100); //Created Date
	ui->tableWidget->setColumnWidth(5, 50);

	ui->tableWidget->setColumnWidth(6, 50);
	ui->tableWidget->setColumnWidth(7, 65);
	ui->tableWidget->setColumnWidth(8, 120);

	ui->tableWidget->sortByColumn(4, Qt::AscendingOrder);

    // Connect SLOT to context menu
    connect(ui->tableWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotCustomMenuRequested(QPoint)));
}

void Proposals::slotCustomMenuRequested(QPoint pos)
{
    /* Create an object context menu */
    QMenu * menu = new QMenu(this);
    //  Create, Connect and Set the actions to the menu
    menu->addAction(tr("Vote For"), this, SLOT(slotVoteFor()));
    menu->addAction(tr("Vote Against"), this, SLOT(slotVoteAgainst()));
    menu->addAction(tr("Vote Abstain"), this, SLOT(slotAbstainCount()));
    menu->addAction(tr("Chart Proposal"), this, SLOT(slotChartProposal()));
    menu->addAction(tr("View Proposal"), this, SLOT(slotViewProposal()));
    menu->popup(ui->tableWidget->viewport()->mapToGlobal(pos));
}


void Proposals::ProcessVote(std::string gobject, std::string signal, std::string outcome)
{
		std::string sError = "";
		int nSuccessful = 0;
		int nFailed = 0;
		VoteManyForGobject(gobject, signal, outcome, 0, nSuccessful, nFailed, sError);
		std::string sVoteNarr = "";
		if (nSuccessful > 0)
		{
			sVoteNarr = "Voting was successful.  Voted " + RoundToString(nSuccessful, 0) + " times, failed to vote " + RoundToString(nFailed, 0) + " time(s).  ";
			if (!sError.empty()) sVoteNarr += " [" + sError + "]";
		}
		else
		{
			sVoteNarr = "Voting Failed!  Failed to vote " + RoundToString(nFailed, 0) + " time(s).  ";
			if (sError.empty()) sVoteNarr += " (Note: You must wait 3 minutes in-between re-votes due to network rules.  Please ensure your wallet is unlocked also.  )";
			if (!sError.empty()) sVoteNarr += " [" + sError + "]";
		}
		QMessageBox msgOutcome;
		msgOutcome.setWindowTitle(tr("Voting Outcome"));
		msgOutcome.setText(GUIUtil::TOQS(sVoteNarr));
		msgOutcome.setStandardButtons(QMessageBox::Ok);
		msgOutcome.setDefaultButton(QMessageBox::Ok);
		msgOutcome.exec();

		// Refresh

	    QString pString = GUIUtil::TOQS(GetActiveProposals());
	    QStringList pHeaders = GetHeaders();
		this->createUI(pHeaders, pString);
}

void Proposals::slotVoteAgainst()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Proposal"));
        msgBox.setText("Vote Against the Proposal?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        if (QMessageBox::Yes == msgBox.exec())
        {
			std::string gobject = GUIUtil::FROMQS(ui->tableWidget->item(row,0)->text());
			ProcessVote(gobject, "funding", "no");
        }
    }
}

void Proposals::slotVoteFor()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Proposal"));
        msgBox.setText("Vote For the Proposal ?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        if (QMessageBox::Yes == msgBox.exec())
        {
			std::string gobject = GUIUtil::FROMQS(ui->tableWidget->item(row,0)->text());
			ProcessVote(gobject, "funding", "yes");
       	}
    }
}

void Proposals::slotAbstainCount()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Proposal"));
        msgBox.setText("Do you want to Abstain from voting?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        if (QMessageBox::Yes == msgBox.exec())
        {
			std::string gobject = GUIUtil::FROMQS(ui->tableWidget->item(row,0)->text());
			ProcessVote(gobject, "funding", "abstain");
		}
    }
}


void Proposals::slotChartProposal()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        int voteFor = ui->tableWidget->item(row,5)->text().toInt();
        int voteAgainst = ui->tableWidget->item(row,6)->text().toInt();
        int abstainCount = ui->tableWidget->item(row,7)->text().toInt();
        // int total = voteFor + voteAgainst + abstainCount;
        SecDialog *secdialog = new SecDialog(this);
        secdialog->setGraphPts(voteFor, voteAgainst, abstainCount);
        secdialog->setWindowTitle(" ");
		secdialog->exec();

    }
}

void Proposals::slotViewProposal()
{
    int row = ui->tableWidget->selectionModel()->currentIndex().row();
    if(row >= 0)
    {
        QString Url = ui->tableWidget->item(row,8)->text();
        QUrl pUrl(Url);
        QDesktopServices::openUrl(pUrl);
    }
}

QVector<QVector<QString> > Proposals::SplitData(const QString &pStr)
{
        QStringList proposals = pStr.split(QRegExp("<proposal>"),QString::SkipEmptyParts);
        int nProposals = proposals.size();
        QVector<QVector<QString> > proposalMatrix;
        for (int i=0; i<nProposals; i++)
        {
            proposalMatrix.append(QVector<QString>());
            QStringList proposalDetail = proposals[i].split('|');
            int detailSize = proposalDetail.size();
            for (int j = 0; j < detailSize; j++)
			{
				QString sData = proposalDetail[j];
				if (j==2)
				{
					sData = BitcoinUnits::format(2, cdbl(GUIUtil::FROMQS(sData), 2) * 100, false, BitcoinUnits::separatorAlways);
				}
                proposalMatrix[i].append(sData);
			}
        }
		return proposalMatrix;
}
