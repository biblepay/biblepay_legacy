// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "distributedcomputingdialog.h"
#include "ui_distributedcomputingdialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "receiverequestdialog.h"
#include "recentrequeststablemodel.h"
#include "walletmodel.h"
#include "main.h"

#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
QString ToQstring(std::string s);
std::string FromQStringW(QString qs);
std::string AssociateDCAccount(std::string sProjectId, std::string sBoincEmail, std::string sBoincPassword, bool fForce);
std::string RoundToString(double d, int place);

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
 	ui->cmbProjectName->addItem(ToQstring(sProject));
	// Populate the CPIDs and Magnitude

	UpdateMagnitudeDisplay();

    // context menu signals
 
    connect(ui->btnAssociate, SIGNAL(clicked()), this, SLOT(clicked()));
}


void DistributedComputingDialog::UpdateMagnitudeDisplay()
{
	
	std::string sInfo = "<br> CPIDS: " + msGlobalCPID 
		+ "<br> Magnitude: " + RoundToString(mnMagnitude,2);
	ui->txtInfo->setText(ToQstring(sInfo));
}


void DistributedComputingDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
		UpdateMagnitudeDisplay();
		LogPrintf(". model set .\n");
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
	std::string sEmail = FromQStringW(ui->txtEmail->text());
	std::string sPassword = FromQStringW(ui->txtPassword->text());
	std::string sError = AssociateDCAccount("project1", sEmail, sPassword, false);
	std::string sNarr = (sError.empty()) ? "Successfully advertised DC-Key.  Type exec getboincinfo to find more researcher information.  Welcome Aboard!  Thank you for donating your clock-cycles to help cure cancer!" : sError;
	QMessageBox::warning(this, tr("Boinc Researcher Association Result"), ToQstring(sNarr), QMessageBox::Ok, QMessageBox::Ok);
    clear();
	UpdateMagnitudeDisplay();
}


