// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "guiutil.h"
#include "transactiondescdialog.h"
#include "ui_transactiondescdialog.h"
#include "util.h"
#include "clientversion.h"
#include "transactiontablemodel.h"
#include "rpcpog.h"

#include <QModelIndex>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QDesktopServices>  //Added for openURL()

TransactionDescDialog::TransactionDescDialog(const QModelIndex &idx, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TransactionDescDialog)
{
    ui->setupUi(this);

    /* Open CSS when configured */
    this->setStyleSheet(GUIUtil::loadStyleSheet());
	// Clear this cache entry in case user has moved away
	WriteCache("ipfs","openlink","",GetTime());
    QString desc = idx.data(TransactionTableModel::LongDescriptionRole).toString();
	//	(using this doesnt help make links clickable) ui->detailText->setTextInteractionFlags(ui->detailText->textInteractionFlags() | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    ui->detailText->setHtml(desc);
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(on_btnCloseClicked()));
	connect(ui->btnOpen, SIGNAL(clicked()), this, SLOT(on_btnOpenClicked()));
	connect(ui->detailText, SIGNAL(TransactionDescDialog::clicked()), 	this, SLOT(on_detailTextClicked(int a, int b)));
	connect(ui->detailText, SIGNAL(TransactionDescDialog::anchorAt()), this, SLOT(on_AnchorClicked()));
	std::string sURL = ReadCache("ipfs", "openlink");
	ui->btnOpen->setVisible(!sURL.empty());
}

void TransactionDescDialog::on_AnchorClicked()
{
	//		QString QTextEdit::anchorAt ( const QPoint & pos, AnchorAttribute attr )
	std::string sURL = ReadCache("ipfs", "openlink");
	LogPrintf("URL %s",sURL.c_str());
	if (!sURL.empty())
	{
		QUrl pUrl(GUIUtil::TOQS(sURL));
		QDesktopServices::openUrl(pUrl);
	}
}

void TransactionDescDialog::on_detailTextClicked(int a, int b)
{
	std::string sURL = ReadCache("ipfs", "openlink");
	LogPrintf("URL %s",sURL.c_str());
	if (!sURL.empty())
	{
		QUrl pUrl(GUIUtil::TOQS(sURL));
		QDesktopServices::openUrl(pUrl);
	}
}

void TransactionDescDialog::on_btnCloseClicked()
{
    close();
}

void TransactionDescDialog::on_btnOpenClicked()
{
	std::string sURL = ReadCache("ipfs", "openlink");
	if (!sURL.empty())
	{
		QUrl pUrl(GUIUtil::TOQS(sURL));
		QDesktopServices::openUrl(pUrl);
	}
}

TransactionDescDialog::~TransactionDescDialog()
{
    delete ui;
}
