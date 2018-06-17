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

namespace Ui {
class Proposals;
}

class Proposals : public QWidget
{
    Q_OBJECT

public:
    explicit Proposals(QWidget *parent = 0);
    ~Proposals();

private Q_SLOTS:
    void slotVoteFor();
    void slotVoteAgainst();
    void slotAbstainCount();
    void slotChartProposal();
    void slotViewProposal();
	void slotCustomMenuRequested(QPoint pos);

private:
    Ui::Proposals *ui;

private:
    void createUI(const QStringList &headers, const QString &pStr);
    QVector<QVector<QString> > SplitData(const QString &pStr);
    void ProcessVote(std::string gobject, std::string signal, std::string outcome);
	QStringList GetHeaders();


};

#endif // PROPOSALS_H
