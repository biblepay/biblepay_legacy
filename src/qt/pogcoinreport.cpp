#include "pogcoinreport.h"
#include "bitcoinunits.h"
#include "ui_pogcoinreport.h"
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
}

PoGCoinReport::~PoGCoinReport()
{
    delete ui;
}

void PoGCoinReport::setModel(WalletModel *model)
{
    this->model = model;
}

void PoGCoinReport::Refresh()
{
    try {
        UniValue jsonVal = CallRPC( string("titheinfo") );
        double min_coin_age = 0 , min_coin_amt = 0;

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
        }
        
        if ( min_coin_age > 0 && min_coin_amt > 0 ) {
            DimensionalReport( "Tithable coins",  min_coin_age, min_coin_amt );
            DimensionalReport( "Aged but not amount",  min_coin_age, 0.01 );
            DimensionalReport( "Amount but not age",  0.01, min_coin_amt );
        }

    }
    catch ( runtime_error e)
    {
        return;
    }
}

void PoGCoinReport::DimensionalReport( string sType, double min_coin_age, double min_coin_amt /*, QColor Color = QColor(255,255,255)*/ )
{
    try {
        std::ostringstream cmd;
        cmd << "exec getdimensionalbalance " << min_coin_age << " " << min_coin_amt;
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
                    ui->label_debug->setText( QString::number(total, 'g', 8) );
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
                    QTableWidgetItem* AmountItem = new QTableWidgetItem(strAmount);;
                    QTableWidgetItem* AgeItem = new QTableWidgetItem(strAge);

                    ui->coinsTable->setItem(0, 0, TypeItem);
                    ui->coinsTable->setItem(0, 1, AmountItem);
                    ui->coinsTable->setItem(0, 2, AgeItem);
                }
            }
        }
    }
    catch ( runtime_error e)
    {
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
