#ifndef WRITEORPHAN_H
#define WRITEORPHAN_H

#include <QDialog>
#include <QObject>
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
	class WriteOrphan;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class WriteOrphan :  public QDialog
{
    Q_OBJECT

public:
    explicit WriteOrphan(QWidget *parent = 0);
    ~WriteOrphan();
	void setModel(WalletModel *model);
	void UpdateObject(std::string objType);

private Q_SLOTS:

private:
    Ui::WriteOrphan *ui;
	WalletModel *model;
    
private:
    void createUI(const QStringList &headers, const QString &pStr);
    QVector<QVector<QString> > SplitData(const QString &pStr);

};

#endif // WRITEORPHAN_H
