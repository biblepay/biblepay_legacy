// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CHATDIALOG_H
#define BITCOIN_QT_CHATDIALOG_H

#include "ui_chatdialog.h"
#include "walletmodel.h"

class ChatDialog : public QDialog, private Ui::ChatDialog
{
     Q_OBJECT

 public:
     ChatDialog(QWidget *parent = 0, bool fPrivateChat = false, std::string sMyNickname = "");
	 bool fPrivateChat;
	 std::string sRecipientName;
	 std::string sNickName;
     void setWalletModel(WalletModel *model);

public Q_SLOTS:
     void appendMessage(std::string sFrom, std::string sMessage);
     void newParticipant(std::string sNickName);
     void participantLeft(std::string sNickName);
 
 private Q_SLOTS:
     void returnPressed();
     void queryRecipientName();
	 void dummyEvent(std::string sMessage);

 private:
     //Client client;
     QTextTableFormat tableFormat;
	 WalletModel *walletModel;

};


#endif // BITCOIN_QT_CHATDIALOG_H
