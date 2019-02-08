#ifndef POGCOINREPORT_H
#define POGCOINREPORT_H

#include <QWidget>
#include <QCoreApplication>
#include <QString>
#include <QDebug>
#include <QVector>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QTableWidget>

class OptionsModel;
class PlatformStyle;
class WalletModel;
class UniValue;

namespace Ui {
class PoGCoinReport;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class PoGCoinReport : public QWidget
{
    Q_OBJECT

public:
    explicit PoGCoinReport(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~PoGCoinReport();
	void setModel(WalletModel *model);

private:
    Ui::PoGCoinReport *ui;
	WalletModel *model;

    void Refresh();
    UniValue CallRPC(std::string args);
    void DimensionalReport( std::string sType, double min_coin_age, double min_coin_amt /*, QColor Color = QColor(255,255,255) */);

private Q_SLOTS:


};

#endif // POGCOINREPORT_H
