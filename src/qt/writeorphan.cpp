#include "writeorphan.h"
#include "bitcoinunits.h"
#include "ui_writeorphan.h"
#include "secdialog.h"
#include "ui_secdialog.h"
#include "guiutil.h"
#include "walletmodel.h"
#include <QPainter>
#include <QTableWidget>
#include <QGridLayout>
#include <QUrl>
#include <univalue.h>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <QDir>


WriteOrphan::WriteOrphan(QWidget *parent) : QDialog(parent),ui(new Ui::WriteOrphan)
{
    ui->setupUi(this);
	std::string sTitle = "Write Orphan";
    setWindowTitle(QString::fromStdString(sTitle));
	std::string sText = "My Orphan text";
    QString qsp = QString::fromStdString(sText);
	qsp.replace("|","<br><br>");
	qsp.replace("~","<br><br>");
    qsp.replace("\n\n", "<br><br>");
	qsp.replace("\r\n", "<br><br>");
    ui->txtOrphan->setText(qsp);
	ui->listPics->setViewMode(QListWidget::IconMode);
	ui->listPics->setIconSize(QSize(210, 180));
	ui->listPics->setResizeMode(QListWidget::Adjust);
	std::string sTarget = GetSANDirectory2();
	QDir directory(GUIUtil::TOQS(sTarget)); 
	directory.setNameFilters(QStringList() << "*.png" << "*.jpg" << "*.jpeg");
	//PICS

	QStringList fileList = directory.entryList();
    for (int i=0; i<fileList.count(); i++)
    {
		QListWidgetItem *item = new QListWidgetItem(QIcon(fileList[i]), fileList[i]);
		ui->listPics->addItem(item);
	}

	//BIO
	std::string sPath = sTarget + "bio.htm";
	QFile file(GUIUtil::TOQS(sPath));
	file.open(QFile::ReadOnly | QFile::Text);
	QTextStream stream(&file);
	ui->txtBio->setHtml(stream.readAll());
	file.close();
}

WriteOrphan::~WriteOrphan()
{
    delete ui;
}

void WriteOrphan::setModel(WalletModel *model)
{
    this->model = model;
}

