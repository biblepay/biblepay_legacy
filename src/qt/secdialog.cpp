#include "secdialog.h"
#include "ui_secdialog.h"
#include "rpcpog.h"
#include "guiutil.h"

SecDialog::SecDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SecDialog)
{
    ui->setupUi(this);
}

void SecDialog::closeForm()
{
	this->close();
}

void SecDialog::getGraphPts(int &voteFor, int &voteAgainst, int &abstainCount)
{
   voteFor = this->forCount;
   voteAgainst = this->againstCount;
   abstainCount = this->absCount;
}

void SecDialog::setGraphPts(int voteFor, int voteAgainst, int abstainCount)
{
    this->forCount = voteFor;
    this->againstCount = voteAgainst;
    this->absCount = abstainCount;
}

SecDialog::~SecDialog()
{
    delete ui;
}

void SecDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    QRectF rec(this->width() * 2/5, 10, this->width()/5, this->height()/30);
    painter.drawRect(rec);
    painter.drawText(rec, Qt::AlignCenter, "Voting Results");

    QRectF size = QRectF(this->width()/5, this->width()/5, this->width()/4, this->width()/4);
    int voteFor, voteAgainst, abstainCount;
    getGraphPts(voteFor, voteAgainst, abstainCount);
    int total = voteFor + voteAgainst + abstainCount;
	connect(ui->btnClose, SIGNAL(clicked()), this, SLOT(close()));

	if (total <= 0) return;

    int forSlice = qRound(360 * (double)voteFor/(double)total);

    int againstSlice = qRound(360 * (double)voteAgainst/(double)total);

    int abstainSlice = qRound(360 * (double)abstainCount/(double)total);

    painter.setBrush(Qt::darkGreen);
    painter.drawPie(size, 0, forSlice*16);

    painter.setBrush(Qt::red);
    painter.drawPie(size, forSlice*16, againstSlice*16);

    painter.setBrush(Qt::gray);
    painter.drawPie(size, (forSlice+againstSlice)*16, abstainSlice*16);

    QTextDocument doc;
    QRect legend (0, 0, this->width()/3, this->width()/3);
    painter.translate(this->width()*3/5, this->width()/3.5);
    doc.setTextWidth(legend.width());
    std::string s = "";
    if (voteFor)
	{
		s = "<table><tr><td width='5%'><div id='rectangle' style='background-color:green'></div></td><td width='30%'> Vote For Count: </td><td>" 
			+ RoundToString(100*(double)voteFor/(double)total, 2) + "%</td></tr></table>";
	}
    if (voteAgainst)
    {
       s += "<table><tr><td width='5%'><div id='rectangle' style='background-color:red'></div></td><td width='30%'> Vote Against Count: </td><td>"
		   + RoundToString(100*(double)voteAgainst/(double)total, 2) + "%</td></tr></table>";
	}
    if (abstainCount)
    {
        s += "<table><tr><td width='5%'><div id='rectangle' style='background-color:gray'></div></td><td width='30%'> Abstain Count: </td><td>" 
			+ RoundToString(100*(double)abstainCount/(double)total, 2) + "%</td></tr></table>";
    }
    doc.setHtml(GUIUtil::TOQS(s));
    doc.drawContents(&painter, legend);
}
