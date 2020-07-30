#include "PushTable.h"

PushTable::PushTable(QObject *parent): QTableView(), chkflag(false)
{
	set_table();
}

void PushTable::set_table()
{
    tb_item = new QStandardItemModel(4, 6);
    this->setModel(tb_item);
    //this->horizontalHeader()->hide();                       // ����ˮƽ��ͷ
    this->verticalHeader()->hide(); // ���ش�ֱ��ͷ
    tabHeader = new HeaderView(Qt::Horizontal);
    
    this->setHorizontalHeader(tabHeader);

    QStringList column;
    column << QString::fromLocal8Bit("") 
        << QString::fromLocal8Bit("�豸����") 
        << QString::fromLocal8Bit("�ͺ�") 
        << QString::fromLocal8Bit("�豸IP")
        << QString::fromLocal8Bit("��ʼʱ��")
        << QString::fromLocal8Bit("��������");
    //row << "row 1" << "row 2" << "row 3" << "row 4";
    tb_item->setHorizontalHeaderLabels(column);                  // ����ˮƽ��ͷ��ǩ
    //tb_item->setVerticalHeaderLabels(row);                     // ���ô�ֱ��ͷ��ǩ


    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
           
            if (j == 0)
            {
                //QStandardItem* it = new QStandardItem(QString("%1").arg(i + j));
                //it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                //tb_item->setItem(i, j, it);
                QCheckBox* chbox = new QCheckBox();
                chbox->setProperty("rwid", i);
                
                checkblist.append(chbox);
                QWidget* wd = new QWidget;
                QHBoxLayout* boxLayout = new QHBoxLayout();
                boxLayout->addWidget(chbox);
                boxLayout->setMargin(0);
                boxLayout->setAlignment(wd, Qt::AlignCenter);
                boxLayout->setContentsMargins(40, 0, 20, 0);
                wd->setLayout(boxLayout);
                // ��ӵ�Ԫ��
                this->setIndexWidget(tb_item->index(i, j), wd);
            }
            else if (j == 5)
            {
                QPushButton* bt = new QPushButton();
                bt->setProperty("rowid", i);
                bt->setStyleSheet(QStringLiteral("QPushButton { background-color:transparent}"));
                QIcon icon;
                icon.addFile(QStringLiteral("res/play_button.png"), QSize(), QIcon::Normal, QIcon::Off);
                bt->setIcon(icon);
                bt->setIconSize(QSize(20, 20));
                this->setIndexWidget(tb_item->index(i, j), bt);
            }
            else
            {
                /*
                QStandardItem* it = new QStandardItem(QString("%1").arg(i + j));
                it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                tb_item->setItem(i, j, it);
                */
            }
        }     
    }

    QList<QCheckBox*>ls = this->findChildren<QCheckBox*>();
    QList<QPushButton*> bls = this->findChildren<QPushButton*>();
    for (auto i = 0; i < bls.size(); i++)
    {
        QStandardItem* it = new QStandardItem();
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        void(QCheckBox:: * pcheck)(int) = &QCheckBox::stateChanged;
        QObject::connect(ls.at(i), pcheck, this, [=](int state) {
            if (state == Qt::Checked) // "ѡ��"
            {
                checkblist.append(ls.at(i));
            }
            });
        QObject::connect(bls.at(i), &QPushButton::clicked, this, [=]() {

            int rwi = bls.at(i)->property("rowid").toInt();
            if (tb_item->item(rwi, 4) != NULL)
            {
                tb_item->takeItem(0, 0);
            }
            
            tb_item->setItem(rwi, 4, it);
            it->setText(getTime());
            btlist.append(bls.at(i));
            //ls.at(i)->setChecked(true);
            //checkblist.append(ls.at(i));
            //tb_item->setItem(rwi, 4, it);
            //tb_item->setData(tb_item->index(rwi, 4), getTime())
            });
        
    }
    void(HeaderView:: * pheader)(bool) = &HeaderView::onChangeclicked;
    QObject::connect(tabHeader, pheader, this, [=](bool ison) {
        if (ison)
        {
            for (auto ct = ls.begin(); ct != ls.end(); ct++)
            {
                QCheckBox* ch = *ct;
                ch->setChecked(true);
            }
            chkflag = true;
        }
        else
        {
            for (auto ct = ls.begin(); ct != ls.end(); ct++)
            {
                QCheckBox* ch = *ct;
                ch->setChecked(false);
            }
            chkflag = false;
            if (!checkblist.isEmpty())
            {
                checkblist.clear();
            }
            
            qDebug() << "dfasdfasdfasd" << ison;
        }
        
        //tabHeader->setisOn(true);
        });
    this->setColumnWidth(0, int(this->width()/12) * 2);
    this->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch); //�����п�����Ӧ
    this->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    this->setColumnWidth(5, int(this->width()/12) * 3);
    //this->horizontalHeader()->setStretchLastSection();  // �������һ��
    //this->setShowGrid(false);                               // ����������
    this->setFocusPolicy(Qt::NoFocus);                      // ȥ����ǰCell�ܱ����߿�
    this->setAlternatingRowColors(true);                    // ����������ɫ  
}

QString PushTable::getTime()
{
    time_t timep;
    time(&timep);
    char tmp[64];
    strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", localtime(&timep));
    return tmp;
}

PushTable::~PushTable()
{

}
