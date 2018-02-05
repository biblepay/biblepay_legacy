// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_DISTRIBUTEDCOMPUTINGDIALOG_H
#define BITCOIN_QT_DISTRIBUTEDCOMPUTINGDIALOG_H

#include "guiutil.h"

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QVariant>

class OptionsModel;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class DistributedComputingDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for requesting payment of bitcoins */
class DistributedComputingDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 130,
        LABEL_COLUMN_WIDTH = 120,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 160,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit DistributedComputingDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~DistributedComputingDialog();

    void setModel(WalletModel *model);
	void UpdateMagnitudeDisplay();

public Q_SLOTS:
    void clear();
    //void accept();

protected:
   

private:
    Ui::DistributedComputingDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;

    //void copyColumnToClipboard(int column);
    //virtual void resizeEvent(QResizeEvent *event);

private Q_SLOTS:
    void on_btnAssociate_clicked();
};

#endif // BITCOIN_QT_DISTRIBUTEDCOMPUTINGDIALOG_H
