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
	void UpdateObject(std::string objType);


private:
    Ui::PoGCoinReport *ui;
	WalletModel *model;
    void createUI(const QStringList &headers, const QString &pStr);
    QVector<QVector<QString> > SplitData(const QString &pStr);
	QStringList GetHeaders(std::string sFields);
	void addFooterRow(int& rows, int& iFooterRow, std::string sCaption, std::string sValue);
	std::string sHeaderFields;
	std::string ObjectType;
	std::string GetHtmlForm(std::string PK);
	int GetUrlColumn(std::string sTarget);

private Q_SLOTS:
    void slotNavigateTo();
	void slotCustomMenuRequested(QPoint pos);
	void slotView();
	void slotList();
	void RefreshPogLeaderboard();

};

#endif // POGCOINREPORT_H
