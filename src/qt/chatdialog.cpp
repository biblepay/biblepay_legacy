// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chatdialog.h"
#include <QClipboard>
#include <QUrl>
#include <QtWidgets>
#include <QtGui>

std::string FromQStringW(QString qs);
QString ToQstring(std::string s);

 ChatDialog::ChatDialog(QWidget *parent, bool bPrivateChat, std::string sMyName) : QDialog(parent)
 {
     setupUi(this);
	 fPrivateChat = bPrivateChat;

     lineEdit->setFocusPolicy(Qt::StrongFocus);
     textEdit->setFocusPolicy(Qt::NoFocus);
     textEdit->setReadOnly(true);
     listWidget->setFocusPolicy(Qt::NoFocus);

     connect(lineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
 #ifdef Q_OS_SYMBIAN
     connect(sendButton, SIGNAL(clicked()), this, SLOT(returnPressed()));
 #endif
     connect(lineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
 
	 sNickName = sMyName;
	 
     newParticipant(sMyName);
     tableFormat.setBorder(0);
	 if (bPrivateChat) QTimer::singleShot(1000, this, SLOT(queryRecipientName()));
	 sRecipientName = "";
 }

void ChatDialog::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
	if(model && model->getOptionsModel())
    {
		connect(model, SIGNAL(signal_EmitChatMessage(std::string sMessage)), this, SLOT(dummyEvent(std::string sMessage)));
	}
}

 void ChatDialog::dummyEvent(std::string sMessage)
 {
	 appendMessage("test", sMessage);
 }

 void ChatDialog::appendMessage(std::string sFrom, std::string sMessage)
 {
     if (sFrom.empty() || sMessage.empty()) return;

     QTextCursor cursor(textEdit->textCursor());
     cursor.movePosition(QTextCursor::End);
     QTextTable *table = cursor.insertTable(1, 2, tableFormat);
     table->cellAt(0, 0).firstCursorPosition().insertText('<' + ToQstring(sFrom) + "> ");
     table->cellAt(0, 1).firstCursorPosition().insertText(ToQstring(sMessage));
     QScrollBar *bar = textEdit->verticalScrollBar();
     bar->setValue(bar->maximum());
 }

 void ChatDialog::returnPressed()
 {
     QString text = lineEdit->text();
     if (text.isEmpty())
         return;

     if (text.startsWith(QChar('/'))) 
	 {
         QColor color = textEdit->textColor();
         textEdit->setTextColor(Qt::red);
         textEdit->append(tr("! Unknown command: %1")
                          .arg(text.left(text.indexOf(' '))));
         textEdit->setTextColor(color);
     }
	 else 
	 {
         //client.sendMessage(myNickName, text);
         appendMessage(sNickName, FromQStringW(text));
     }

     lineEdit->clear();
 }

 void ChatDialog::newParticipant(std::string sTheirNickName)
 {
     if (sNickName.empty())  return;
     QColor color = textEdit->textColor();
     textEdit->setTextColor(Qt::gray);
     textEdit->append(tr("* %1 has joined").arg(ToQstring(sTheirNickName)));
     textEdit->setTextColor(color);
     listWidget->addItem(ToQstring(sTheirNickName));
 }

 void ChatDialog::participantLeft(std::string sTheirNickName)
 {
     if (sNickName.empty()) return;

     QList<QListWidgetItem *> items = listWidget->findItems(ToQstring(sTheirNickName), Qt::MatchExactly);
     if (items.isEmpty()) return;

     delete items.at(0);
     QColor color = textEdit->textColor();
     textEdit->setTextColor(Qt::gray);
     textEdit->append(tr("* %1 has left").arg(ToQstring(sTheirNickName)));
     textEdit->setTextColor(color);
 }

 void ChatDialog::queryRecipientName()
 {
	bool bOK = false;
	sRecipientName = FromQStringW(QInputDialog::getText(this, tr("BiblePay Chat - Private Messaging"),
                                          tr("Please enter the recipient name you would like to Page >"),
										  QLineEdit::Normal, "", &bOK));
 }