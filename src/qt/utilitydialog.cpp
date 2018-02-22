// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilitydialog.h"
#include "ui_helpmessagedialog.h"

#include "bitcoingui.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "intro.h"
#include "paymentrequestplus.h"
#include "guiutil.h"
#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "kjv.cpp"
#include <stdio.h>
#include <QCloseEvent>
#include <QLabel>
#include <QRegExp>
#include <QTextTable>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QInputDialog>

// For BiblePay News Reader
// #include <QWebView>
#include <QUrl>

std::string GetPrayer(int iPrayerNumber, std::string& out_Title);
std::string GetVerse(std::string sBook, int iChapter, int iVerse, int iBookStart, int iBookEnd);
std::string GetBook(int iBookNumber);
std::string GetBookByName(std::string sName);
std::string RoundToString(double d, int place);
void GetBookStartEnd(std::string sBook, int& iStart, int& iEnd);
std::string AddBlockchainMessages(std::string sAddress, std::string sType, std::string sPrimaryKey, std::string sHTML, CAmount nAmount, std::string& sError);

QString FromEscapedToHTML(QString qsp);
QString FromHTMLToEscaped(QString qsp);
QString FleeceBoilerPlate(QString qsp);
std::string ExtractXML(std::string XMLdata, std::string key, std::string key_end);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
QString ToQstring(std::string s);
std::string FromQStringW(QString qs);
std::string strReplace(std::string& str, const std::string& oldStr, const std::string& newStr);
std::string GetTxNews(uint256 hash, std::string& sHeadline);
std::string ReadCache(std::string section, std::string key);
void WriteCache(std::string section, std::string key, std::string value, int64_t locktime);

/** "Help message" or "About" dialog box */
HelpMessageDialog::HelpMessageDialog(QWidget *parent, HelpMode helpMode, int iPrayer, uint256 txid, std::string sPreview) :
    QDialog(parent),
    ui(new Ui::HelpMessageDialog)
{
    ui->setupUi(this);

    QString version = tr("Biblepay Core") + " " + tr("version") + " " + QString::fromStdString(FormatFullVersion());
    /* On x86 add a bit specifier to the version so that users can distinguish between
     * 32 and 64 bit builds. On other architectures, 32/64 bit may be more ambigious.
     */
#if defined(__x86_64__)
    version += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__ )
    version += " " + tr("(%1-bit)").arg(32);
#endif

	if (helpMode == prayer)
	{
		std::string sTitle = "";
		std::string sPrayer = GetPrayer(iPrayer, sTitle);
	    setWindowTitle(QString::fromStdString(sTitle));
		ui->btnPublish->setVisible(false);
		ui->btnPreview->setVisible(false);
	    QString qsp = QString::fromStdString(sPrayer);
        // Make URLs clickable
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true);
        qsp.replace(uri, "<a href=\"\\1\">\\1</a>");
        // Replace newlines with HTML breaks
		qsp.replace("|","<br><br>");
        qsp.replace("\n\n", "<br><br>");
        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->aboutMessage->setText(qsp);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
	}
	else if (helpMode == readnews)
	{
		// Retrieve the news article
		//std::string sHeadline = "";
		//std::string sNews = GetTxNews(txid, sHeadline);
	}
	else if (helpMode == previewnews)
	{
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
		ui->btnPublish->setVisible(true);
		ui->btnPreview->setVisible(true);
		connect(ui->btnPublish, SIGNAL(clicked()), this, SLOT(on_btnPublishClicked()));
		connect(ui->btnPreview, SIGNAL(clicked()), this, SLOT(on_btnPreviewClicked()));
		std::string sTitle = "Preview News Article";
		setWindowTitle(QString::fromStdString(sTitle));
        ui->aboutMessage->setEnabled(true);
		ui->aboutMessage->setVisible(false);
        ui->helpMessage->setVisible(true);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		ui->aboutMessage->setGeometry(ui->helpMessage->x(), ui->helpMessage->y(), ui->helpMessage->width(), 1);
		ui->helpMessage->setReadOnly(false);
		ui->helpMessage->setEnabled(true);
		LogPrintf(" preview %s ",sPreview.c_str());
		QString qsPreview = QString::fromStdString(sPreview);
		QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true);
        qsPreview.replace(uri, "<a href=\"\\1\">\\1</a>");
        qsPreview.replace("\n\n", "<br><br>");
	    ui->helpMessage->setText(qsPreview);
	}
	else if (helpMode == createnews)
	{
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
		ui->btnPublish->setVisible(true);
		ui->btnPreview->setVisible(true);
		connect(ui->btnPublish, SIGNAL(clicked()), this, SLOT(on_btnPublishClicked()));
		connect(ui->btnPreview, SIGNAL(clicked()), this, SLOT(on_btnPreviewClicked()));
		std::string sTitle = "Create a News Article";
		std::string sPage = "Create a News Article";
	    setWindowTitle(QString::fromStdString(sTitle));
        QString qsp = QString::fromStdString(sPage);
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true);
        qsp.replace(uri, "<a href=\"\\1\">\\1</a>");
		qsp.replace("|","<br><br>");
        qsp.replace("\n\n", "<br><br>");
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		ui->aboutMessage->setEnabled(true);
		ui->aboutMessage->setVisible(false);
        ui->helpMessage->setVisible(true);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->helpMessage->setText(qsp);
		ui->aboutMessage->setGeometry(ui->helpMessage->x(), ui->helpMessage->y(), ui->helpMessage->width(), 1);
		ui->helpMessage->setGeometry(ui->helpMessage->x(), ui->helpMessage->y(), ui->helpMessage->width(), ui->helpMessage->height() + 500);
		ui->helpMessage->setReadOnly(false);
		ui->helpMessage->setEnabled(true);
	}
	else if (helpMode == readbible)
	{
		// Make the combobox for choosing the Book of the Bible and the Chapter visible:
		ui->comboBook->setVisible(true);
		ui->btnPublish->setVisible(false);
		ui->btnPreview->setVisible(false);
		ui->comboChapter->setVisible(true);
		ui->lblChapter->setVisible(true);
		ui->lblBook->setVisible(true);
		// Load the books of the bible in
		for (int i = 0; i <= 65; i++)
		{
			std::string sBook = GetBook(i);
			std::string sBookName = GetBookByName(sBook);
			std::string sBookEntry = sBook + " - " + sBookName;
			ui->comboBook->addItem(ToQstring(sBook));
		}
		ui->comboChapter->addItem(ToQstring("Chapter 1"));

		connect(ui->comboBook, SIGNAL(currentIndexChanged(int)), SLOT(on_comboBookClicked(int)));
		connect(ui->comboChapter, SIGNAL(currentIndexChanged(int)), SLOT(on_comboChapterClicked(int)));
		std::string sTitle = "READ THE BIBLE";
		std::string sPage = "Choose a Bible Book and Chapter below.";
	    setWindowTitle(QString::fromStdString(sTitle));
        QString qsp = QString::fromStdString(sPage);
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true);
        qsp.replace(uri, "<a href=\"\\1\">\\1</a>");
        // Replace newlines with HTML breaks
		qsp.replace("|","<br><br>");
        qsp.replace("\n\n", "<br><br>");
        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->aboutMessage->setText(qsp);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
		on_comboBookClicked(1);
	}
    else if (helpMode == about)
    {
        setWindowTitle(tr("About Biblepay Core"));
        /// HTML-format the license message from the core
        QString licenseInfo = QString::fromStdString(LicenseInfo());
        QString licenseInfoHTML = licenseInfo;
		ui->btnPublish->setVisible(false);
		ui->btnPreview->setVisible(false);

        // Make URLs clickable
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true); // use non-greedy matching
        licenseInfoHTML.replace(uri, "<a href=\"\\1\">\\1</a>");
        // Replace newlines with HTML breaks
        licenseInfoHTML.replace("\n\n", "<br><br>");
        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        text = version + "\n" + licenseInfo;
        ui->aboutMessage->setText(version + "<br><br>" + licenseInfoHTML);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
    } 
	else if (helpMode == cmdline) 
	{
        setWindowTitle(tr("Command-line options"));
        QString header = tr("Usage:") + "\n" +
            "  biblepay-qt [" + tr("command-line options") + "]                     " + "\n";
        QTextCursor cursor(ui->helpMessage->document());
        cursor.insertText(version);
        cursor.insertBlock();
        cursor.insertText(header);
        cursor.insertBlock();
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
   		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
		ui->btnPublish->setVisible(false);
		ui->btnPreview->setVisible(false);
	
        std::string strUsage = HelpMessage(HMM_BITCOIN_QT);
        const bool showDebug = GetBoolArg("-help-debug", false);
        strUsage += HelpMessageGroup(tr("UI Options:").toStdString());
        if (showDebug) {
            strUsage += HelpMessageOpt("-allowselfsignedrootcertificates", strprintf("Allow self signed root certificates (default: %u)", DEFAULT_SELFSIGNED_ROOTCERTS));
        }
        strUsage += HelpMessageOpt("-choosedatadir", strprintf(tr("Choose data directory on startup (default: %u)").toStdString(), DEFAULT_CHOOSE_DATADIR));
        strUsage += HelpMessageOpt("-lang=<lang>", tr("Set language, for example \"de_DE\" (default: system locale)").toStdString());
        strUsage += HelpMessageOpt("-min", tr("Start minimized").toStdString());
        strUsage += HelpMessageOpt("-rootcertificates=<file>", tr("Set SSL root certificates for payment request (default: -system-)").toStdString());
        strUsage += HelpMessageOpt("-splash", strprintf(tr("Show splash screen on startup (default: %u)").toStdString(), DEFAULT_SPLASHSCREEN));
        strUsage += HelpMessageOpt("-resetguisettings", tr("Reset all settings changes made over the GUI").toStdString());
        if (showDebug) 
		{
            strUsage += HelpMessageOpt("-uiplatform", strprintf("Select platform to customize UI for (one of windows, macosx, other; default: %s)", BitcoinGUI::DEFAULT_UIPLATFORM));
        }
        QString coreOptions = QString::fromStdString(strUsage);
        text = version + "\n" + header + "\n" + coreOptions;

        QTextTableFormat tf;
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellPadding(2);
        QVector<QTextLength> widths;
        widths << QTextLength(QTextLength::PercentageLength, 35);
        widths << QTextLength(QTextLength::PercentageLength, 65);
        tf.setColumnWidthConstraints(widths);

        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);

        Q_FOREACH (const QString &line, coreOptions.split("\n")) {
            if (line.startsWith("  -"))
            {
                cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::PreviousCell);
                cursor.movePosition(QTextCursor::NextRow);
                cursor.insertText(line.trimmed());
                cursor.movePosition(QTextCursor::NextCell);
            } else if (line.startsWith("   ")) {
                cursor.insertText(line.trimmed()+' ');
            } else if (line.size() > 0) {
                //Title of a group
                if (cursor.currentTable())
                    cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::Down);
                cursor.insertText(line.trimmed(), bold);
                cursor.insertTable(1, 2, tf);
            }
        }

        ui->helpMessage->moveCursor(QTextCursor::Start);
        ui->scrollArea->setVisible(false);
        ui->aboutLogo->setVisible(false);
    }
	else if (helpMode == pshelp) 
	{
        setWindowTitle(tr("PrivateSend information"));
		ui->btnPublish->setVisible(false);
		ui->btnPreview->setVisible(false);
		
        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->aboutMessage->setText(tr("\
<h3>PrivateSend Basics</h3> \
PrivateSend gives you true financial privacy by obscuring the origins of your funds. \
All the Biblepay in your wallet is comprised of different \"inputs\" which you can think of as separate, discrete coins.<br> \
PrivateSend uses an innovative process to mix your inputs with the inputs of two other people, without having your coins ever leave your wallet. \
You retain control of your money at all times..<hr> \
<b>The PrivateSend process works like this:</b>\
<ol type=\"1\"> \
<li>PrivateSend begins by breaking your transaction inputs down into standard denominations. \
These denominations are 0.01 biblepay, 0.1 biblepay, 1 biblepay and 10 biblepay -- sort of like the paper money you use every day.</li> \
<li>Your wallet then sends requests to specially configured software nodes on the network, called \"masternodes.\" \
These masternodes are informed then that you are interested in mixing a certain denomination. \
No identifiable information is sent to the masternodes, so they never know \"who\" you are.</li> \
<li>When two other people send similar messages, indicating that they wish to mix the same denomination, a mixing session begins. \
The masternode mixes up the inputs and instructs all three users' wallets to pay the now-transformed input back to themselves. \
Your wallet pays that denomination directly to itself, but in a different address (called a change address).</li> \
<li>In order to fully obscure your funds, your wallet must repeat this process a number of times with each denomination. \
Each time the process is completed, it's called a \"round.\" Each round of PrivateSend makes it exponentially more difficult to determine where your funds originated.</li> \
<li>This mixing process happens in the background without any intervention on your part. When you wish to make a transaction, \
your funds will already be anonymized. No additional waiting is required.</li> \
</ol> <hr>\
<b>IMPORTANT:</b> Your wallet only contains 1000 of these \"change addresses.\" Every time a mixing event happens, up to 9 of your addresses are used up. \
This means those 1000 addresses last for about 100 mixing events. When 900 of them are used, your wallet must create more addresses. \
It can only do this, however, if you have automatic backups enabled.<br> \
Consequently, users who have backups disabled will also have PrivateSend disabled. <hr>\
For more info see <a href=\"https://biblepaypay.atlassian.net/wiki/display/DOC/PrivateSend\">https://biblepaypay.atlassian.net/wiki/display/DOC/PrivateSend</a> \
        "));
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
        ui->aboutLogo->setVisible(false);
		ui->comboBook->setVisible(false);
		ui->comboChapter->setVisible(false);
		ui->lblChapter->setVisible(false);
		ui->lblBook->setVisible(false);
    }
    // Theme dependent Gfx in About popup
    QString helpMessageGfx = ":/images/" + GUIUtil::getThemeName() + "/about";
    QPixmap pixmap = QPixmap(helpMessageGfx);
    ui->aboutLogo->setPixmap(pixmap);
}


HelpMessageDialog::~HelpMessageDialog()
{
    delete ui;
}

void HelpMessageDialog::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    fprintf(stdout, "%s\n", qPrintable(text));
}

void HelpMessageDialog::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}

void HelpMessageDialog::on_okButton_accepted()
{
    close();
}


std::string ReplaceLineBreaksInUL(std::string sUL)
{
	sUL = strReplace(sUL,"</p>","");
	sUL = strReplace(sUL,"<p style=\" margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">","");
	sUL = strReplace(sUL,"<br />","");
	sUL = strReplace(sUL,"<br>","");
	sUL = strReplace(sUL,"\n","");
	return sUL;
}


QString FleeceBoilerPlate(QString qsp)
{
	qsp.replace("<pre>","");
	qsp.replace("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">","");
	qsp.replace("<html><head><meta name=\"qrichtext\" content=\"1\" />","");
	qsp.replace("<style type=\"text/css\"> p, li { white-space: pre-wrap; } </style>","");
	qsp.replace("</head><body style=\" font-family:'Cantarell'; font-size:11pt; font-weight:400; font-style:normal;\">","");
	qsp.replace("<p style=\"-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">","");
	qsp.replace("<br /></p></body></html>","");
	qsp.replace("</pre>","");
	qsp.replace("<p style=\" margin-top:12px; margin-bottom:12px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;\">","<p>\r\n");
	qsp.replace("\"\"","\"");
	qsp.replace("\"\"","\"");
	return qsp;
}

QString FromEscapedToHTML(QString qsp)
{
	// The data is stored in qt5 Rich QTextEdit control, with escaped < > / characters.  Convert back to HTML so the user can PREVIEW
	qsp.replace("&lt;","<");
	qsp.replace("&gt;",">"); //&amp;
	qsp.replace("\"\"","\"");
	// Unfortunately, no \n's can exist between the <ul>,<li>, and </ul>, as QT will render this as Text, and without the \n's, the user is forced to jumble the entire <ul> together in one line (not very friendly for news articles)
	// So, lets fleece out all \n's between the unordered lists so the user can leave them in
	std::string sPreview = FromQStringW(qsp);
	std::vector<std::string> vPreview = Split(sPreview.c_str(),"<ul>");
	std::string sOut = "";
	if (vPreview.size() > 0)
	{
		for (int i=0; i < (int)vPreview.size(); i++)
		{
			std::string s1 = "<ul>" + vPreview[i];
			std::string sOriginal = "<ul>" + ExtractXML(s1,"<ul>","</ul>") + "</ul>";
			std::string sNew = ReplaceLineBreaksInUL(sOriginal);
			sPreview = strReplace(sPreview,sOriginal,sNew);
		}
	}
	qsp = ToQstring(sPreview);
	return qsp;
}


void HelpMessageDialog::on_btnPublishClicked()
{
	if (fProd) return;
	bool ok;
	QString qsp = ui->helpMessage->toHtml();
	qsp = FleeceBoilerPlate(qsp);
	qsp = FromEscapedToHTML(qsp);
	std::string sPreview = FromQStringW(qsp);
	QString qsHeadline = QInputDialog::getText(this, tr("Publish New Block Chain News Article"), tr("Enter Article Name (Full Headline Name):"), QLineEdit::Normal,"", &ok);
    if (ok && !qsHeadline.isEmpty() && qsHeadline.length() > 4 && sPreview.length() > 80)
	{
     	// Verify Cost is OK with user
		LogPrintf("\r\n COST HTML %s \r\n", sPreview.c_str());
		double dCost = cdbl(RoundToString(((sPreview.length()+500) / 1000) * 500,0), 0); 
		std::string sNarr = "The cost to publish the article (bytesize=" + RoundToString(sPreview.length(),0) + ") is " 
			+ RoundToString(dCost,2) + " BBP.  Press <OK> To Accept and Publish, otherwise press Cancel.";
		int ret = QMessageBox::warning(this, tr("Verify Publishing Costs"), ToQstring(sNarr), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
		if (ret==QMessageBox::Ok)
		{
			// Publish the article
			const Consensus::Params& consensusParams = Params().GetConsensus();
			std::string sFoundation = consensusParams.FoundationAddress;
			std::string sError = "";
			std::string sTXID = AddBlockchainMessages(sFoundation, "NEWS", FromQStringW(qsHeadline), sPreview, dCost * COIN, sError);

			std::string sPublished = sTXID.empty() ? "Article not published successfully. " + sError + " [Unlock wallet]." : "Article published successfully.  TXID: " + sTXID;
			QMessageBox::warning(this, tr("Publishing Result"), ToQstring(sPublished),QMessageBox::Ok, QMessageBox::Ok);
		}
		else
		{
			LogPrintf(" USER DECLINED FEE. ");
		}
	}
}

std::string GetSANDirectory()
{
	 boost::filesystem::path pathConfigFile(GetArg("-conf", "biblepay.conf"));
     if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
	 boost::filesystem::path dir = pathConfigFile.parent_path();
	 std::string sDir = dir.string() + "/SAN/";
	 boost::filesystem::path pathSAN(sDir);
	 LogPrintf(" Checking if dir %s exists ",sDir.c_str());
	 if (!boost::filesystem::exists(pathSAN))
	 {
		 boost::filesystem::create_directory(pathSAN);
	 }
	 return sDir;
}

bool FileExists(std::string sPath)
{
	if (!sPath.empty())
	{
		try
		{
			boost::filesystem::path pathImg(sPath);
			return (boost::filesystem::exists(pathImg));
		}
		catch(const boost::filesystem::filesystem_error& e)
		{
			LogPrintf(" BOOST FILESYSTEM ERR ");
			return false;
		}
		catch (const std::exception& e) 
		{
			return false;
		}
		catch(...)
		{
			return false;
		}

	}
	return false;
}
	

QString FromHTMLToEscaped(QString qsp)
{
	// The data is stored in qt5 Rich QTextEdit control, with escaped < > / characters.  Convert back to HTML so the user can PREVIEW
	qsp.replace("<","&lt;");
	qsp.replace(">","&gt;");
	qsp.replace("\"\"","\"");
	return qsp;
}



void HelpMessageDialog::on_btnPreviewClicked()
{
	if (fProd) return;
	// The users uses this to preview the news page as they create it
	QString qsp = ui->helpMessage->toHtml();
	qsp = FleeceBoilerPlate(qsp);
	qsp = FromEscapedToHTML(qsp);
	std::string sPreview = FromQStringW(qsp);
	WriteCache("preview-news","preview",sPreview,GetAdjustedTime());
	WriteCache("preview-news","headline","BiblePay News Preview - Headline",GetAdjustedTime());
 	NewsPreviewWindow::showNewsPreviewWindow(this);
}


std::string NameFromURL(std::string sURL)
{
	std::vector<std::string> vURL = Split(sURL.c_str(),"/");
	std::string sOut = "";
	if (vURL.size() > 0)
	{
		std::string sName = vURL[vURL.size()-1];
		sName=strReplace(sName,"'","");
		std::string sDir = GetSANDirectory();
		std::string sPath = sDir + sName;
		return sPath;
	}
	return "";
}


void HelpMessageDialog::on_comboBookClicked(int iClick)
{
	// Based on chosen Book, populate chapters
	std::string sBook = FromQStringW(ui->comboBook->currentText());
	int iStart = 0;
	int iEnd = 0;
	std::string sReversed = GetBookByName(sBook);
	GetBookStartEnd(sReversed, iStart, iEnd);
	ui->comboChapter->clear();
	for (int iChapter = 1; iChapter < 99; iChapter++)
	{
		std::string sVerse = GetVerse(sReversed,iChapter,1,iStart,iEnd);
		if (!sVerse.empty())
		{
			ui->comboChapter->addItem(ToQstring("Chapter " + RoundToString(iChapter,0)));
		}
		else
		{
			break;
		}
	}
	on_comboChapterClicked(0);
}

void HelpMessageDialog::on_comboChapterClicked(int iClick)
{
	// Based on chosen Chapter, show the Bible Text and based on chosen Book, populate chapters
	std::string sChapter = FromQStringW(ui->comboChapter->currentText());
	sChapter = strReplace(sChapter,"Chapter ","");
	int iChapter = (int)cdbl(sChapter,0);
	int iStart = 0;
	int iEnd = 0;
	std::string sBook = FromQStringW(ui->comboBook->currentText());
	std::string sReversed = GetBookByName(sBook);
	LogPrintf(" book chosen %s reversed %s ",sBook.c_str(),sReversed.c_str());
	GetBookStartEnd(sReversed, iStart, iEnd);
	std::string sText = "";
	for (int iVerse = 1; iVerse < 99; iVerse++)
	{
		std::string sVerse = GetVerse(sReversed,iChapter,iVerse,iStart,iEnd);
		if (!sVerse.empty())
		{
			sText += RoundToString(iChapter,0) + ":" + RoundToString(iVerse,0) + "     &nbsp;     " + sVerse + "\n\n";
		}
		else
		{
			break;
		}
	}
	std::string sTitle = sBook + " - Chapter " + sChapter;
    setWindowTitle(QString::fromStdString(sTitle));
    QString qsp = QString::fromStdString(sText);
	qsp.replace("|","<br><br>");
	qsp.replace("~","<br><br>");
    qsp.replace("\n\n", "<br><br>");
	qsp.replace("\r\n", "<br><br>");
    ui->aboutMessage->setTextFormat(Qt::RichText);
    ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->aboutMessage->setText(qsp);
    ui->aboutMessage->setWordWrap(true);
}

/** "Shutdown" window */
ShutdownWindow::ShutdownWindow(QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(new QLabel(
        tr("Biblepay Core is shutting down...") + "<br /><br />" +
        tr("Do not shut down the computer until this window disappears.")));
    setLayout(layout);
}

void ShutdownWindow::showShutdownWindow(BitcoinGUI *window)
{
    if (!window)
        return;

    // Show a simple window indicating shutdown status
    QWidget *shutdownWindow = new ShutdownWindow();
    // We don't hold a direct pointer to the shutdown window after creation, so use
    shutdownWindow->setAttribute(Qt::WA_DeleteOnClose);
    shutdownWindow->setWindowTitle(window->windowTitle());
    // Center shutdown window at where main window was
    const QPoint global = window->mapToGlobal(window->rect().center());
    shutdownWindow->move(global.x() - shutdownWindow->width() / 2, global.y() - shutdownWindow->height() / 2);
    shutdownWindow->show();
}

void ShutdownWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
}


/** News Preview window */
NewsPreviewWindow *newsPreviewWindow;
    
NewsPreviewWindow::NewsPreviewWindow(QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    QVBoxLayout *layout = new QVBoxLayout();
	QTextEdit *txt = new QTextEdit();
	txt = new QTextEdit();
	std::string sNews = ReadCache("preview-news","preview");
	QString qsNews = QString::fromStdString(sNews);
	qsNews = FromEscapedToHTML(qsNews);
	qsNews = ReplaceImageTags(qsNews);
	txt->setHtml(qsNews);
    layout->addWidget(txt);
    setLayout(layout);
}

	
void NewsPreviewWindow::downloadFinished(QNetworkReply *reply)
{
	if (reply->error() != QNetworkReply::NoError) 
	{
		LogPrintf(" UNABLE TO DOWNLOAD IMAGE ");
		return;
	}

	QUrl url = reply->url();
	std::string sURL = url.toEncoded().constData();
	std::string sName = NameFromURL(sURL);
	QByteArray jpegData = reply->readAll();
	QFile file(ToQstring(sName));
	file.open(QIODevice::WriteOnly);
	file.write(jpegData);
	file.close();
}

	
bool NewsPreviewWindow::DownloadImage(std::string sURL)
{
	std::string sPath = NameFromURL(sURL);
	LogPrintf(" IMG NAME %s FROM URL %s ",sPath.c_str(),sURL.c_str());
	bool fExists = FileExists(sPath);
	if (fExists) return true;
	QNetworkAccessManager *manager = new QNetworkAccessManager(this);
	connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(downloadFinished(QNetworkReply*)));
	manager->get(QNetworkRequest(QUrl(ToQstring(sURL))));
	return true;
}



QString NewsPreviewWindow::ReplaceImageTags(QString qsp)
{
	std::string sHTML = FromQStringW(qsp);
	std::vector<std::string> vHTML = Split(sHTML.c_str(),"<img");
	std::string sOut = "";
	if (vHTML.size() > 0)
	{
		for (int i = 0; i < (int)vHTML.size(); i++)
		{
			std::string sImg = "<img" + vHTML[i];
			std::string sEle = ExtractXML(sImg,"<img",">");
			std::string sOldImg = "<img " + sEle + ">";
			sOldImg = strReplace(sOldImg,"  "," ");
			sOldImg = strReplace(sOldImg,"src='src='","src='");
			sOldImg = strReplace(sOldImg,"''","'");
			LogPrintf(" Original OldImg %s ",sOldImg.c_str());
			std::string sSrc = ExtractXML(sEle,"src='","'");
			sSrc = strReplace(sSrc,"'","");
			if (!sEle.empty() && !sSrc.empty())
			{
				LogPrintf(" downloading %s ",sSrc.c_str());
				// Download image into cache
				DownloadImage(sSrc);
				std::string sName = NameFromURL(sSrc);
				std::string sNew = "src='" + sName + "'";
				std::string sNewEle = "<img " + strReplace(sEle,sSrc,sNew) + ">";
				sNewEle=strReplace(sNewEle,"  "," ");
				sNewEle=strReplace(sNewEle,"src='src='","src='");
				sNewEle = strReplace(sNewEle,"''","'");
				LogPrintf(" Img src %s, old %s, sNew %s ",sSrc.c_str(),sOldImg.c_str(),sNew.c_str());
				sHTML = strReplace(sHTML,sOldImg,sNewEle);
			}
		}
	}
	return ToQstring(sHTML);
}



void NewsPreviewWindow::showNewsWindow(QWidget *myparent, std::string sTxID)
{
	if (!myparent) return;
	if (newsPreviewWindow != NULL) newsPreviewWindow->close();
    
	std::string sHeadline="";
	uint256 uTXID = uint256S("0x" + sTxID);
	std::string sNews = GetTxNews(uTXID, sHeadline);
	sHeadline += " - PREVIEW";
	WriteCache("preview-news","preview",sNews,GetAdjustedTime());
	WriteCache("preview-news","headline",sHeadline,GetAdjustedTime());
	newsPreviewWindow = new NewsPreviewWindow();
	newsPreviewWindow->setAttribute(Qt::WA_DeleteOnClose);
    newsPreviewWindow->setWindowTitle(ToQstring(sHeadline));
    // Position window to the left of the Writing window
    newsPreviewWindow->move(50, 50);
	newsPreviewWindow->setGeometry(20,20,770,770);
    newsPreviewWindow->show();
}

void NewsPreviewWindow::showNewsPreviewWindow(QWidget *myparent)
{
    if (!myparent) return;
    // Allow the user to preview the HTML they are creating in the Create News 
	if (newsPreviewWindow != NULL) newsPreviewWindow->close();
    std::string sHeadline = ReadCache("preview-news","headline");
    // Position window to the left of the Writing window
	newsPreviewWindow = new NewsPreviewWindow();
    newsPreviewWindow->setAttribute(Qt::WA_DeleteOnClose);
	newsPreviewWindow->setWindowTitle(ToQstring(sHeadline));
    newsPreviewWindow->move(50, 50);
	newsPreviewWindow->setGeometry(20,20,770,770);
    newsPreviewWindow->show();
}

void NewsPreviewWindow::closeEvent(QCloseEvent *event)
{
   newsPreviewWindow = NULL;
}



