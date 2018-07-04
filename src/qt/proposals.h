#ifndef PROPOSALS_H
#define PROPOSALS_H

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
class Proposals;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class Proposals : public QWidget
{
    Q_OBJECT

public:
    explicit Proposals(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~Proposals();
	void setModel(WalletModel *model);
	void UpdateDisplay();

private Q_SLOTS:
    void slotVoteFor();
    void slotVoteAgainst();
    void slotAbstainCount();
    void slotChartProposal();
    void slotViewProposal();
	void slotCustomMenuRequested(QPoint pos);

private:
    Ui::Proposals *ui;
	WalletModel *model;
    
private:
    void createUI(const QStringList &headers, const QString &pStr);
    QVector<QVector<QString> > SplitData(const QString &pStr);
    void ProcessVote(std::string gobject, std::string signal, std::string outcome);
	QStringList GetHeaders();


};

#endif // PROPOSALS_H
