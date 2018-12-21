// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Däsh Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chatdialog.h"
#include "chat.h"
#include "main.h"
#include <QClipboard>
#include <QUrl>
#include <QtWidgets>
#include <QtGui>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include "timedata.h"
#include <boost/algorithm/string.hpp>

std::string FromQStringW(QString qs);
QString ToQstring(std::string s);
void SendChat(CChat chat);

ChatDialog::ChatDialog(QWidget *parent, bool bPrivateChat, std::string sMyName, std::string sDestRoom) : QDialog(parent)
{
     setupUi(this);
	 fPrivateChat = bPrivateChat;
	 this->setAttribute(Qt::WA_DeleteOnClose);

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
	 if (fPrivateChat)
	 {
		if (sDestRoom.empty()) QTimer::singleShot(1000, this, SLOT(queryRecipientName()));
		sRecipientName = sDestRoom;
	 }
	 else
	 {
		sRecipientName = sDestRoom;
	 }
	 setTitle();
}

void ChatDialog::setTitle()
{
	std::string sPM = fPrivateChat ? "Private Messaging - " + sNickName + " & " + sRecipientName : "Public Chat - " + sRecipientName;
	this->setWindowTitle(ToQstring(sPM));
}

void ChatDialog::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Subscribe to chat signals
        connect(model, SIGNAL(chatEvent(QString)), this, SLOT(receivedEvent(QString)));
    }
}

void ChatDialog::receivedEvent(QString sMessage)
{
	 // Deserialize back into the chat object
	 CChat c(FromQStringW(sMessage));
	 if (sRecipientName.empty()) return;
	 // If this is ours
	 if ((fPrivateChat && boost::iequals(c.sDestination, sNickName) && boost::iequals(c.sFromNickName,  sRecipientName))
     	  || (!fPrivateChat && boost::iequals(c.sDestination, sRecipientName)))
	 {
		appendMessage(c.sFromNickName, c.sPayload, c.nPriority);
		QList<QListWidgetItem *> items = listWidget->findItems(ToQstring(c.sFromNickName), Qt::MatchExactly);
		if (items.isEmpty()) newParticipant(c.sFromNickName);
		// Clear any global rings
		if (fPrivateChat && c.sPayload == "<RING>")
		{
			mlPaged = 0;
			msPagedFrom = "";
		}
	 }
}

void ChatDialog::appendMessage(std::string sFrom, std::string sMessage, int nPriority)
{
     if (sFrom.empty() || sMessage.empty()) return;

     QTextCursor cursor(textEdit->textCursor());
     cursor.movePosition(QTextCursor::End);
     QTextTable *table = cursor.insertTable(1, 2, tableFormat);
     table->cellAt(0, 0).firstCursorPosition().insertText('<' + ToQstring(sFrom) + "> ");
	
     QTextTableCell cell = table->cellAt(0, 1);
	 QTextCharFormat format = cell.format();
	
	 QBrush my_brush;
	 QColor red(Qt::red);
	 QColor black(Qt::black);
	 QColor blue(Qt::blue);
	 if (nPriority < 3)
	 {
		format.setForeground(black);
	 }
	 else if (nPriority == 3 || nPriority == 4)
	 {
		format.setForeground(blue);
	 }
	 else if (nPriority == 5)
	 {
		format.setForeground(red);
	 }
	 cell.setFormat(format);
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
         textEdit->append(tr("! Unknown command: %1").arg(text.left(text.indexOf(' '))));
         textEdit->setTextColor(color);
     }
	 else 
	 {
		 CChat c;
		 c.nVersion = 1;
		 c.nTime = GetAdjustedTime();
		 c.nID = GetAdjustedTime();
		 c.bPrivate = fPrivateChat;
		 c.nPriority = 1;
		 c.sDestination = sRecipientName;
		 c.sFromNickName = sNickName;
		 c.sPayload = FromQStringW(text);
		 c.sToNickName = sRecipientName;
		 SendChat(c);
		 if (fPrivateChat) appendMessage(sNickName, FromQStringW(text), c.nPriority);
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
	if (sRecipientName.empty()) return;
	setTitle();
	// Page the recipient 
	for (int x = 1; x < 5; x++)
	{
		QTimer::singleShot(1000 * x, this, SLOT(ringRecipient()));
	}
 }

 void ChatDialog::ringRecipient()
 {
	CChat c;
	c.nVersion = 1;
	c.nTime = GetAdjustedTime();
	c.nID = GetAdjustedTime();
	c.bPrivate = fPrivateChat;
	c.nPriority = 1;
	c.sDestination = sRecipientName;
	c.sFromNickName = sNickName;
	c.sPayload = "<RING>";
	c.sToNickName = sRecipientName;
	SendChat(c);
	if (fPrivateChat) appendMessage(sNickName, c.sPayload, c.nPriority);
}