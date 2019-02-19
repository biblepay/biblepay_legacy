#include "pogcoinreport.h"
#include "bitcoinunits.h"
#include "ui_pogcoinreport.h"
#include "masternode-sync.h"
#include "walletmodel.h"
#include "guiutil.h"
#include "rpcclient.h"
#include "rpcserver.h"
#include "rpcpog.h"
#include "main.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QTableWidget>
#include <QMessageBox>
#include <QGridLayout>
#include <QUrl>
#include <QTimer>
#include <univalue.h>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp>

using namespace std;

bool bSlotsCreated = false;

PoGCoinReport::PoGCoinReport(const PlatformStyle *platformStyle, QWidget *parent) : ui(new Ui::PoGCoinReport)
{
    ui->setupUi(this);

    // Coin table
    int columnStatusWidth = 200;
    int columnAmounWidth = 100;
    int columnAgeWidth = 100;

    ui->coinsTable->setColumnWidth(0, columnStatusWidth);
    ui->coinsTable->setColumnWidth(1, columnAmounWidth);
    ui->coinsTable->setColumnWidth(2, columnAgeWidth);

    Refresh();

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateCoinReport()));
    timer->start(POGCOINREPORT_UPDATE_SECONDS*1000);
}

PoGCoinReport::~PoGCoinReport()
{
    delete ui;
}

void PoGCoinReport::setModel(WalletModel *model)
{
    this->model = model;
}


void PoGCoinReport::updateCoinReport()
{
    if (masternodeSync.IsBlockchainSynced())
    {
        Refresh();
        ui->label_debug->setText(tr("Synced"));
    }
    else {
        ui->label_debug->setText(tr("Out of sync"));
    }
}

void PoGCoinReport::Refresh()
{
    try {
        ui->coinsTable->setSortingEnabled(false);
        ui->coinsTable->setRowCount(0);

        UniValue jsonVal = CallRPC( string("titheinfo") );
        double min_coin_age = 0 , min_coin_amt = 0, max_tithe_amt = 0, tithe_cap = 0;

        if ( jsonVal.isObject() )
        {
            QString tithability;
            
            UniValue json_min_coin_age = find_value(jsonVal.get_obj(), "min_coin_age");
            if ( json_min_coin_age.isNum() )    {
                min_coin_age = json_min_coin_age.get_real();
                ui->label_coinAge->setText(QString::number(min_coin_age, 'g', 8));
            }
            UniValue json_min_coin_amt = find_value(jsonVal.get_obj(), "min_coin_amt");
            if ( json_min_coin_amt.isNum() )    {
                min_coin_amt = json_min_coin_amt.get_real();
                ui->label_coinAmount->setText(QString::number(min_coin_amt, 'g', 8));
            }
            UniValue json_tithability = find_value(jsonVal.get_obj(), "Tithability_Summary");
            if ( json_tithability.isStr() )    {
                tithability = QString::fromStdString(json_tithability.get_str());
                ui->label_canTithe->setText( tithability );
                QString style = (tithability=="YES")?"QLabel { background-color : green; }":"QLabel { background-color : red; }";
                ui->label_canTithe->setStyleSheet( style );
            }

            QString WalletUnlocked = (pwalletMain->IsLocked())?"ŸES":"NO";
            ui->label_WalletUnlocked->setText( WalletUnlocked );
            QString style = (WalletUnlocked=="YES")?"QLabel { background-color : green; }":"QLabel { background-color : red; }";
            ui->label_WalletUnlocked->setStyleSheet( style );

            int64_t nPOGTitheFrequency = cdbl(GetSporkValue("pogtithefrequency"), 0);

            if (nPOGTitheFrequency == 0) nPOGTitheFrequency = (60 * 60 * 4);
            double dUserTitheFrequency = cdbl(GetArg("-tithe", "0"), 2);
            if (dUserTitheFrequency == 0)
            {
                nPOGTitheFrequency = -1;
            }
            else if (dUserTitheFrequency == 1)
            {
                // Use the Defaults set by the spork
            }
            else if (dUserTitheFrequency > 1)
            {
                nPOGTitheFrequency = dUserTitheFrequency * 60;
            }

            ui->label_TitheFrequency->setText( QString::number(nPOGTitheFrequency/60) + " minutes" );

            UniValue json_max_tithe_amt = find_value(jsonVal.get_obj(), "max_tithe_amount");
            if ( json_max_tithe_amt.isNum() )    {
                max_tithe_amt = json_max_tithe_amt.get_real();
                ui->label_maxTitheAmt->setText(QString::number(max_tithe_amt, 'g', 8));
            }

            UniValue json_tithe_cap = find_value(jsonVal.get_obj(), "Tithe_Cap");
            if ( json_tithe_cap.isNum() )    {
                tithe_cap = json_tithe_cap.get_real();
                ui->label_TitheCap->setText(QString::number(tithe_cap, 'g', 8));
            }

        }
        
        jsonVal = CallRPC( string("pogpool") );
        double my_tithes = 0 , total_tithes = 0, total_participants = 0;

        if ( jsonVal.isObject() )
        {
            UniValue json_my_tithes = find_value(jsonVal.get_obj(), "My Tithes");
            if ( json_my_tithes.isNum() )    {
                my_tithes = json_my_tithes.get_real();
                ui->label_MyTithes->setText(QString::number(my_tithes, 'g', 8));
            }
            UniValue json_total_tithes = find_value(jsonVal.get_obj(), "Total Tithes");
            if ( json_total_tithes.isNum() )    {
                total_tithes = json_total_tithes.get_real();
                ui->label_TotalTithes->setText(QString::number(total_tithes, 'g', 8));
            }
            UniValue json_total_participants = find_value(jsonVal.get_obj(), "Total Participants");
            if ( json_total_participants.isNum() )    {
                total_participants = json_total_participants.get_real();
                ui->label_TotalParticipants->setText(QString::number(total_participants, 'g', 8));
            }
        }

        int custom_age = ui->editCustomAge->value();
        int custom_amt = ui->editCustomAmount->value();
        if ( min_coin_age > 0 && min_coin_amt > 0 ) {
            if (ui->chkListTithable->isChecked())   DimensionalReport( "Tithable coins",  min_coin_age, min_coin_amt, MAX_AGE, MAX_AMOUNT, Qt::green );
            if (ui->chkListAge->isChecked())        DimensionalReport( "Aged but not amount",  min_coin_age, 0.01, MAX_AGE, min_coin_amt, Qt::yellow );
            if (ui->chkListAmount->isChecked())     DimensionalReport( "Amount but not age",  0.01, min_coin_amt, min_coin_age, MAX_AMOUNT, Qt::magenta );
            if (ui->chkListCustom->isChecked())     DimensionalReport( "Custom range",  custom_age, custom_amt, MAX_AGE, MAX_AMOUNT, Qt::cyan  );
        }

        ui->coinsTable->setSortingEnabled(true);
    }
    catch ( runtime_error e)
    {
        return;
    }
}

void PoGCoinReport::DimensionalReport( string sType, double min_coin_age, double min_coin_amt, double max_coin_age, double max_coin_amt, QColor Color )
{
    try {
        std::ostringstream cmd;
        cmd << "exec getdimensionalbalance " << min_coin_age << " " << min_coin_amt << " " << max_coin_age << " " << fixed << max_coin_amt;
        UniValue jsonVal = CallRPC( string( cmd.str() ) );
        if ( jsonVal.isObject() )
        {
            vector<string> key_vector = jsonVal.getKeys();
            vector<string>::iterator it;
            int i = 0;  // counter.
            double total = 0;
            string key, value, coin_amount, coin_age;
            for(it = key_vector.begin(); it != key_vector.end(); it++,i++ )    {
                key = *it;
                UniValue json_value = find_value(jsonVal.get_obj(), *it);
                if ( json_value.isNum() && key.compare("Total") == 0 )    {
                    total = json_value.get_real();
                }
                else if ( key.compare("Command") == 0 ) {
                    continue; // pass along
                }
                else {
                    value = json_value.get_str();
                    coin_amount = key.substr(7);
                    coin_age = value.substr(4);

                    QString strType = QString::fromStdString(sType);
                    QString strAmount = QString::fromStdString(coin_amount);
                    QString strAge = QString::fromStdString(coin_age);

                    ui->coinsTable->insertRow(0);

                    QTableWidgetItem* TypeItem = new QTableWidgetItem(strType);
                    QTableWidgetItem* AmountItem = new NumericTableWidgetItem(strAmount);
                    QTableWidgetItem* AgeItem = new NumericTableWidgetItem(strAge);

                    ui->coinsTable->setItem(0, 0, TypeItem);
                    TypeItem->setBackground( QBrush( Color ) );
                    ui->coinsTable->setItem(0, 1, AmountItem);
                    ui->coinsTable->setItem(0, 2, AgeItem);
                }
            }
        }
    }
    catch ( runtime_error e)
    {
        ui->label_debug->setText(QString::fromStdString("runtime error: ") + QString::fromStdString(e.what()));
        return;
    }
}

UniValue PoGCoinReport::CallRPC(string args)
{
    vector<string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    UniValue params = RPCConvertValues(strMethod, vArgs);

    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        UniValue result = (*method)(params, false);
        return result;
    }
    catch (const UniValue& objError) {
        throw runtime_error(find_value(objError, "message").get_str());
    }
}

void PoGCoinReport::on_editAutorefreshSeconds_editingFinished()
{
    timer->setInterval(ui->editAutorefreshSeconds->text().toInt()*1000);
}

void PoGCoinReport::on_btnCreateBankroll_clicked()
{
    try {
        int bankroll_notes=0, bankroll_denomination=0;

        bankroll_notes = ui->editBRNotes->value();
        bankroll_denomination = ui->editBRAmount->value();

        if (bankroll_notes>0 && bankroll_denomination>0)    {
            std::ostringstream cmd;
            cmd << "exec bankroll " << bankroll_notes << " " << bankroll_denomination;
            UniValue jsonVal = CallRPC( string( cmd.str() ) );
            if ( jsonVal.isObject() )
            {
                UniValue json_error = find_value(jsonVal.get_obj(), "Error");
                if ( json_error.isStr() )    {
                    QMessageBox::warning(this, tr("Error"), QString::fromStdString(json_error.get_str()), QMessageBox::Ok, QMessageBox::Ok);
                }

                UniValue json_txid = find_value(jsonVal.get_obj(), "TXID");
                if ( json_txid.isStr() )    {
                    ui->label_debug->setText(QString::fromStdString(json_txid.get_str()));
                }
            }
        }
    }
    catch ( runtime_error e)
    {
        return;
    }
}


void PoGCoinReport::on_btnTitheSend_clicked()
{
    try {
        int tithe_amount=0, tithe_min_age=0, tithe_min_amount=0;

        tithe_amount = ui->editTitheAmount->value();
        tithe_min_age = ui->editTitheMinAge->value();
        tithe_min_amount = ui->editTitheMinAmount->value();

        if (tithe_amount>0)    {
            std::ostringstream cmd;
            cmd << "tithe " << tithe_amount << " " << tithe_min_age << " " << tithe_min_amount;
            UniValue jsonVal = CallRPC( string( cmd.str() ) );
            if ( jsonVal.isObject() )
            {
                UniValue json_error = find_value(jsonVal.get_obj(), "Error");
                if ( json_error.isStr() )    {
                    QMessageBox::warning(this, tr("Error"), QString::fromStdString(json_error.get_str()), QMessageBox::Ok, QMessageBox::Ok);
                }

                UniValue json_txid = find_value(jsonVal.get_obj(), "TXID");
                if ( json_txid.isStr() )    {
                    ui->label_debug->setText(QString::fromStdString(json_txid.get_str()));
                }
            }
        }
    }
    catch ( runtime_error e)
    {
        return;
    }
}

void PoGCoinReport::on_chkListCustom_stateChanged(int arg1)
{
    updateCoinReport();
}

void PoGCoinReport::on_chkListTithable_3_stateChanged(int arg1)
{
    updateCoinReport();
}

void PoGCoinReport::on_chkListAge_stateChanged(int arg1)
{
    updateCoinReport();
}

void PoGCoinReport::on_chkListAmount_stateChanged(int arg1)
{
    updateCoinReport();
}

