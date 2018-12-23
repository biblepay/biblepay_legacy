// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CHATDIALOG_H
#define BITCOIN_QT_CHATDIALOG_H

#include "ui_chatdialog.h"
#include "clientmodel.h"

class ChatDialog : public QDialog, private Ui::ChatDialog
{
     Q_OBJECT

 public:
     ChatDialog(QWidget *parent = 0, bool fPrivateChat = false, std::string sMyNickname = "", std::string sDestRoom = "");
	 bool fPrivateChat;
	 std::string sRecipientName;
	 std::string sNickName;
	 std::string sRoom;
     void setClientModel(ClientModel *model);

public Q_SLOTS:
     void appendMessage(std::string sFrom, std::string sMessage, int nPriority);
     void newParticipant(std::string sNickName);
     void participantLeft(std::string sNickName);
 
 private Q_SLOTS:
     void returnPressed();
     void queryRecipientName();
	 void receivedEvent(QString sMessage);
	 void ringRecipient();
	 void closeEvent(QCloseEvent *event);

 private:
     QTextTableFormat tableFormat;
	 ClientModel *clientModel;
	 void setTitle();

};


#endif // BITCOIN_QT_CHATDIALOG_H
