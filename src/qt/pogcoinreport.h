#ifndef POGCOINREPORT_H
#define POGCOINREPORT_H

#include "amount.h"
#include <QWidget>
#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <QVector>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QTimer>
#include <QTableWidget>

class OptionsModel;
class PlatformStyle;
class WalletModel;
class UniValue;

#define POGCOINREPORT_UPDATE_SECONDS                    15

namespace Ui {
class PoGCoinReport;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class PoGCoinReport : public QWidget
{
    Q_OBJECT

    static const int MAX_AGE = 999999.0;
    static const int MAX_AMOUNT = 99999999.0;

public:
    explicit PoGCoinReport(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~PoGCoinReport();
	void setModel(WalletModel *model);

private:
    QTimer *timer;
    Ui::PoGCoinReport *ui;
	WalletModel *model;

    void Refresh();
    UniValue CallRPC(std::string args);
    void DimensionalReport( std::string sType, double min_coin_age, double min_coin_amt, double max_coin_age=MAX_AGE, double max_coin_amt=MAX_AMOUNT, QColor Color = Qt::red);

private Q_SLOTS:
    void updateCoinReport();

    void on_editAutorefreshSeconds_editingFinished();
    void on_btnCreateBankroll_clicked();
    void on_chkListCustom_stateChanged(int arg1);
    void on_chkListTithable_3_stateChanged(int arg1);
    void on_chkListAge_stateChanged(int arg1);
    void on_chkListAmount_stateChanged(int arg1);
    void on_btnTitheSend_clicked();
};

class NumericTableWidgetItem : public QTableWidgetItem {
    public:
        NumericTableWidgetItem(const QString &s):QTableWidgetItem(s) {
        }

        bool operator <(const QTableWidgetItem &other) const
        {
            return text().toDouble() < other.text().toDouble();
        }
};

#endif // POGCOINREPORT_H
