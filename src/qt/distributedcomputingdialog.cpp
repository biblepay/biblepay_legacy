// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "distributedcomputingdialog.h"
#include "ui_distributedcomputingdialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "util.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "walletmodel.h"
#include "main.h"
#include "podc.h"
#include "rpcpodc.h"
#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>


DistributedComputingDialog::DistributedComputingDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DistributedComputingDialog),
    model(0),
    platformStyle(platformStyle)
{
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
    
    if (!platformStyle->getImagesOnButtons()) {
        ui->btnAssociate->setIcon(QIcon());
    } else {
        ui->btnAssociate->setIcon(QIcon(":/icons/" + theme + "/receiving_addresses"));
    }

	std::string sProject = "Rosetta@Home";
	ui->cmbProjectName->clear();
 	ui->cmbProjectName->addItem(GUIUtil::TOQS(sProject));
	// Populate the CPIDs and Magnitude
	UpdateMagnitudeDisplay();
}


void DistributedComputingDialog::UpdateMagnitudeDisplay()
{
	std::string sPrimaryCPID = GetElement(msGlobalCPID, ";", 0);
	double dTaskWeight = GetTaskWeight(sPrimaryCPID);
	double dUTXOWeight = GetUTXOWeight(sPrimaryCPID);
	std::string sInfo = "<br> CPIDS: " + msGlobalCPID 
		+ "<br> Magnitude: " + RoundToString(mnMagnitude,2)
		+ "<br> Task Weight: " + RoundToString(dTaskWeight, 0) + "; UTXO Weight: " + RoundToString(dUTXOWeight, 0);
	ui->txtInfo->setText(GUIUtil::TOQS(sInfo));
	int nTasks = GetBoincTaskCount();

	ui->lcdTasks->display(nTasks);

}


void DistributedComputingDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateMagnitudeDisplay();
    }
}

DistributedComputingDialog::~DistributedComputingDialog()
{
    delete ui;
}

void DistributedComputingDialog::clear()
{
    ui->txtPassword->setText("");
    ui->txtEmail->setText("");
}

/*
void EditAddressDialog::setAddress(const QString &address)
{
    this->address = address;
    ui->addressEdit->setText(address);
}
*/


void DistributedComputingDialog::on_btnAssociate_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sEmail = GUIUtil::FROMQS(ui->txtEmail->text());
	std::string sPassword = GUIUtil::FROMQS(ui->txtPassword->text());
	std::string sError = AssociateDCAccount("project1", sEmail, sPassword, "", false);
	std::string sNarr = (sError.empty()) ? "Successfully advertised DC-Key.  Type exec getboincinfo to find more researcher information.  Welcome Aboard!  Thank you for donating your clock-cycles to help cure cancer!" : sError;
	QMessageBox::warning(this, tr("Boinc Researcher Association Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
    clear();
	UpdateMagnitudeDisplay();
}


void DistributedComputingDialog::on_btnFix_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sEmail = GUIUtil::FROMQS(ui->txtEmail->text());
	std::string sPassword = GUIUtil::FROMQS(ui->txtPassword->text());

	std::string sError = "";
	std::string sHTML = FixRosetta(sEmail, sPassword, sError);
	sHTML = strReplace(sHTML, "\n", "<br>");
	
	std::string sNarr = (sError.empty()) ? sHTML : sError;
	QMessageBox::warning(this, tr("Fix BOINC Configuration"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
    clear();
	UpdateMagnitudeDisplay();
}

void DistributedComputingDialog::on_btnDiagnostics_clicked()
{
    if(!model || !model->getOptionsModel())
        return;
	std::string sEmail = GUIUtil::FROMQS(ui->txtEmail->text());
	std::string sPassword = GUIUtil::FROMQS(ui->txtPassword->text());
	std::string sError = "";
	std::string sHTML = RosettaDiagnostics(sEmail, sPassword, sError);
	sHTML = strReplace(sHTML, "\n", "<br>");
	std::string sNarr = (sError.empty()) ? sHTML : sError;
	QMessageBox::warning(this, tr("BOINC Diagnostics Result"), GUIUtil::TOQS(sNarr), QMessageBox::Ok, QMessageBox::Ok);
    clear();
	UpdateMagnitudeDisplay();
}

