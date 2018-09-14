// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "contactadddialog.h"
#include "ui_contactadddialog.h"

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
#include "util.h"
#include "utilstrencodings.h"
#include <univalue.h>

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
std::string StoreBusinessObject(UniValue& oBusinessObject, std::string& sError);
UniValue GetBusinessObject(std::string sType, std::string sPrimaryKey, std::string& sError);
bool is_email_valid(const std::string& e);
std::string DefaultRecAddress(std::string sType);
bool AmIMasternode();
int GetNextSuperblock();

bool fUpdated = false;
std::string sAddress = "";
int iUpdates = 0;

ContactAddDialog::ContactAddDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ContactAddDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
    
    if (!platformStyle->getImagesOnButtons()) 
	{
        ui->btnSave->setIcon(QIcon());
    } else {
        ui->btnSave->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
    }

	ui->cmbContactType->clear();
 	ui->cmbContactType->addItem("User");
	ui->cmbContactType->addItem("Church");
	ui->cmbContactType->addItem("Vendor");
 }

QString GetValue(UniValue oObject, std::string sFieldName)
{
	
	if (oObject.size() > 0)
	{
		std::string sResult = oObject[sFieldName].get_str();
		return ToQstring(sResult);
	}
	else
	{
		return ToQstring("");
	}
}

void ContactAddDialog::UpdateDisplay()
{
		std::string sInfo = "Note: Longitude/Latitude is optional and used for our Great Tribulation Map (consider using the nearest street corner).  <br>E-Mail is optional and used for receiving BBP via e-mail.<br><br><font color=red>GDPR Regulation Compliance:  To delete your personal information, click Delete and the off-chain data will be deleted during <br>the IPFS cleanup cycle (BiblePay does not store personal information in the chain).</font>(" + RoundToString(iUpdates, 0) + ")  ";

		std::string sError = "";
		LogPrintf(" 800.5 \n");
		fprintf(stdout, "800.5 \n");
		iUpdates++;

		if (sAddress.empty()) sAddress = DefaultRecAddress(BUSINESS_OBJECTS);

		LogPrintf(" 801 ");

		UniValue oContact = GetBusinessObject("contact", sAddress, sError);
		LogPrintf(" 802 \n");

		if (sError.empty() && oContact.size() > 0)
		{
			LogPrintf(" 803 ");

			ui->txtContactName->setText(GetValue(oContact, "contact_name"));
			ui->txtCompanyName->setText(GetValue(oContact, "company_name"));
			ui->txtURL->setText(GetValue(oContact, "url"));
			ui->txtLongitude->setText(GetValue(oContact, "longitude"));
			LogPrintf(" 804 ");

			ui->txtLatitude->setText(GetValue(oContact, "latitude"));
			ui->txtAddress->setText(GetValue(oContact, "receiving_address"));
			ui->txtEmail->setText(GetValue(oContact, "email"));
			LogPrintf(" 805 ");

			int index = ui->cmbContactType->findText(GetValue(oContact, "contact_type"));
			if (index >= 0) ui->cmbContactType->setCurrentIndex(index);
			LogPrintf(" 806 ");

		}
		else
		{
			ui->txtAddress->setText(ToQstring(sAddress));
			sInfo += " [" + sError + "]";
		}
		ui->txtInfo->setText(ToQstring(sInfo));
   
}


void ContactAddDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		if (!fUpdated)		UpdateDisplay();
    }
}

ContactAddDialog::~ContactAddDialog()
{
    delete ui;
}

void ContactAddDialog::clear()
{
    ui->txtCompanyName->setText("");
	ui->txtContactName->setText("");
    ui->txtURL->setText("");
	ui->txtLongitude->setText("");
	ui->txtLatitude->setText("");
	ui->txtEmail->setText("");
}

void ContactAddDialog::on_btnDelete_clicked()
{
	if(!model || !model->getOptionsModel())
        return;
	clear();
	UniValue objContact(UniValue::VOBJ);
	objContact.push_back(Pair("contact_name", FromQStringW(ui->txtContactName->text())));
	objContact.push_back(Pair("company_name", FromQStringW(ui->txtCompanyName->text())));
	objContact.push_back(Pair("url", FromQStringW(ui->txtURL->text())));
	objContact.push_back(Pair("longitude", FromQStringW(ui->txtLongitude->text())));
	objContact.push_back(Pair("latitude", FromQStringW(ui->txtLatitude->text())));
	objContact.push_back(Pair("receiving_address", FromQStringW(ui->txtAddress->text())));
	objContact.push_back(Pair("email", FromQStringW(ui->txtEmail->text())));
	std::string sError = "";
	CBitcoinAddress address(objContact["receiving_address"].get_str());
	if (!address.IsValid()) sError += "Funding Address is invalid. ";
	std::string sContactType = FromQStringW(ui->cmbContactType->currentText());
	objContact.push_back(Pair("contact_type", sContactType));
	objContact.push_back(Pair("objecttype", "contact"));
	std::string sTxId = "";
	if (sError.empty())
			sTxId = StoreBusinessObject(objContact, sError);

	std::string sNarr = (!sError.empty()) ? "Business Object Store Failed: " + sError : sNarr = "Contact record deleted successfully.";
	QMessageBox::warning(this, tr("Contact Add Result"), ToQstring(sNarr), QMessageBox::Ok, QMessageBox::Ok);
 	UpdateDisplay();
}

void ContactAddDialog::on_btnSave_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	UniValue objContact(UniValue::VOBJ);
	objContact.push_back(Pair("contact_name", FromQStringW(ui->txtContactName->text())));
	objContact.push_back(Pair("company_name", FromQStringW(ui->txtCompanyName->text())));
	objContact.push_back(Pair("url", FromQStringW(ui->txtURL->text())));
	objContact.push_back(Pair("longitude", FromQStringW(ui->txtLongitude->text())));
	objContact.push_back(Pair("latitude", FromQStringW(ui->txtLatitude->text())));
	objContact.push_back(Pair("receiving_address", FromQStringW(ui->txtAddress->text())));
	objContact.push_back(Pair("email", FromQStringW(ui->txtEmail->text())));

	std::string sError = "";
	if (objContact["contact_name"].get_str().length() < 3) sError += "Contact Name must be populated. ";
	CBitcoinAddress address(objContact["receiving_address"].get_str());
	if (!address.IsValid()) sError += "Funding Address is invalid. ";
	std::string sContactType = FromQStringW(ui->cmbContactType->currentText());
	if (sContactType.empty()) sError += "Contact Type must be chosen. ";
	bool fEmailValid = is_email_valid(FromQStringW(ui->txtEmail->text()));
	if (!fEmailValid) sError += "E-Mail address is invalid. ";
	objContact.push_back(Pair("contact_type", sContactType));
	objContact.push_back(Pair("objecttype", "contact"));
	std::string sTxId = "";
	if (sError.empty())	sTxId = StoreBusinessObject(objContact, sError);

	std::string sNarr = (!sError.empty()) ? "Business Object Store Failed: " + sError : sNarr = "Contact record saved successfully.";
	QMessageBox::warning(this, tr("Contact Add Result"), ToQstring(sNarr), QMessageBox::Ok, QMessageBox::Ok);
}


