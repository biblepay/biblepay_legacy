// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaladddialog.h"
#include "ui_proposaladddialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "timedata.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "governance.h"
#include "governance-vote.h"
#include "governance-classes.h"

#include "walletmodel.h"
#include "main.h"
#include "podc.h"
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
QString ToQstring(std::string s);
std::string FromQStringW(QString qs);
std::string RoundToString(double d, int place);
std::string CreateGovernanceCollateral(uint256 GovObjHash, CAmount nFee, std::string& sError);
bool AmIMasternode();
int GetNextSuperblock();

ProposalAddDialog::ProposalAddDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ProposalAddDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
    
    if (!platformStyle->getImagesOnButtons()) {
        ui->btnSubmit->setIcon(QIcon());
    } else {
        ui->btnSubmit->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
    }

	ui->cmbExpenseType->clear();
 	ui->cmbExpenseType->addItem("Charity");
	ui->cmbExpenseType->addItem("PR");
	ui->cmbExpenseType->addItem("P2P");
	ui->cmbExpenseType->addItem("IT");
 }


void ProposalAddDialog::UpdateDisplay()
{
	int nNextHeight = GetNextSuperblock();

	std::string sInfo = "Note: Proposal Cost is 2500 BBP.  Next Superblock at height: " + RoundToString(nNextHeight, 0);
	if (fProposalNeedsSubmitted)
	{
		sInfo += "<br>NOTE: You have a proposal waiting to be submitted.  Status: " + msProposalResult;
	}
	else if (!msProposalResult.empty())
	{
		sInfo = "<br>NOTE: Your last proposal has been submitted.  Status: " + msProposalResult;
	}

	ui->txtInfo->setText(ToQstring(sInfo));
}


void ProposalAddDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateDisplay();
    }
}

ProposalAddDialog::~ProposalAddDialog()
{
    delete ui;
}

void ProposalAddDialog::clear()
{
    ui->txtName->setText("");
    ui->txtURL->setText("");
	ui->txtAmount->setText("");
	ui->txtAddress->setText("");
}


void ProposalAddDialog::on_btnSubmit_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sName = FromQStringW(ui->txtName->text());
	std::string sAddress = FromQStringW(ui->txtAddress->text());
	std::string sAmount = FromQStringW(ui->txtAmount->text());
	std::string sURL = FromQStringW(ui->txtURL->text());
	std::string sError = "";
	if (sName.length() < 3) sError += "Proposal Name must be populated. ";
	CBitcoinAddress address(sAddress);
	if (!address.IsValid()) sError += "Proposal Funding Address is invalid. ";
	if (cdbl(sAmount,0) < 100) sError += "Proposal Amount is too low. ";
	if (sURL.length() < 10) sError += "You must enter a discussion URL. ";
	std::string sExpenseType = FromQStringW(ui->cmbExpenseType->currentText());
	if (sExpenseType.empty()) sError += "Expense Type must be chosen. ";
	if (fProposalNeedsSubmitted) 
	{
		sError += "There is a proposal already being submitted (" + msProposalResult + ").  Please wait until this proposal is sent before creating a new one. ";
	}
	
	std::string sPrepareTxId = "";
	std::string sHex = "";
	int64_t unixEndTimestamp = GetAdjustedTime();
	int64_t unixStartTimestamp = GetAdjustedTime();
		
	if (sError.empty())
	{
		// gobject prepare 0 1 EPOCH_TIME HEX
		std::string sType = "1"; //Proposal
		std::string sQ = "\"";
		std::string sJson = "[[" + sQ + "proposal" + sQ + ",{";
		sJson += GJE("end_epoch", RoundToString(unixEndTimestamp, 0), true, true);
		sJson += GJE("name", sName, true, true);
		sJson += GJE("payment_address",sAddress, true, true);
		sJson += GJE("payment_amount",sAmount, true, true);
		sJson += GJE("start_epoch", RoundToString(unixStartTimestamp, 0), true, true);
		sJson += GJE("type",sType, true, false);
		sJson += GJE("expensetype", sExpenseType, true, true);
		sJson += GJE("url", sURL, false, true);
		sJson += "}]]";
		// make into hex
		std::vector<unsigned char> vchJson = vector<unsigned char>(sJson.begin(), sJson.end());
		sHex = HexStr(vchJson.begin(), vchJson.end());
		// ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS
		uint256 hashParent = uint256();
		int nRevision = 1;
		// CREATE A NEW COLLATERAL TRANSACTION FOR THIS SPECIFIC OBJECT
		CGovernanceObject govobj(hashParent, nRevision, unixStartTimestamp, uint256(), sHex);
		if((govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) || (govobj.GetObjectType() == GOVERNANCE_OBJECT_WATCHDOG)) 
		{
			sError = "Trigger and watchdog objects cannot be created from the UI yet.";
		}
		if (sError.empty())
		{
			if(!govobj.IsValidLocally(sError, false))
			{
				 sError = "Governance object is not valid - " + govobj.GetHash().ToString();
			}

			if (sError.empty())
			{
				sPrepareTxId = CreateGovernanceCollateral(govobj.GetHash(), govobj.GetMinCollateralFee(), sError);
			}
		}
	}
	std::string sNarr = (sError.empty()) ? "Successfully Prepared Proposal " + sPrepareTxId + ".   NOTE: You must wait 6 confirms for the proposal to be submitted.  Please check back on this page periodically to ensure a successful transmission and that no error message is listed in the bottom area of the page.  Thank you for using the BiblePay Governance System." : sError;
	if (sError.empty())
	{
		// Set the proposal up to be submitted after 6 confirms using Biblepay Governance Service:
		nProposalPrepareHeight = chainActive.Tip()->nHeight;
		msProposalResult = "Submitting Proposal in 6 blocks...";
		uTxIdFee = uint256S(sPrepareTxId);
		nProposalStartTime = unixStartTimestamp;
		msProposalHex = sHex;
		fProposalNeedsSubmitted = true;
	}
 	QMessageBox::warning(this, tr("Proposal Add Result"), ToQstring(sNarr), QMessageBox::Ok, QMessageBox::Ok);
    clear();
 	UpdateDisplay();
}


