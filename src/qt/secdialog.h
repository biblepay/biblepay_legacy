#ifndef SECDIALOG_H
#define SECDIALOG_H

#include <QDialog>
#include <QPainter>
#include <QDebug>
#include <QTextDocument>

namespace Ui {
class SecDialog;
}

class SecDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SecDialog(QWidget *parent = 0);
    void getGraphPts(int &voteFor, int &voteAgainst, int &abstainCount);
    void setGraphPts(int voteFor=0, int voteAgainst=0, int abstainCount=0);
    ~SecDialog();

protected:
    void paintEvent(QPaintEvent *);
	void closeForm();

private:
    Ui::SecDialog *ui;
    int forCount, againstCount, absCount;

};

#endif // SECDIALOG_H
