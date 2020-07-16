#include "PushTable.h"

PushTable::PushTable(QObject *parent): QTableView()
{
	set_table();
}
void PushTable::set_table()
{
    tb_item = new QStandardItemModel(4, 6);
    this->setModel(tb_item);
    //    table_view->horizontalHeader()->hide();                       // ����ˮƽ��ͷ
    this->verticalHeader()->hide();                                     // ���ش�ֱ��ͷ
    

    QStringList column;
    column << QString::fromLocal8Bit("���") 
        << QString::fromLocal8Bit("�豸����") 
        << QString::fromLocal8Bit("�ͺ�") 
        << QString::fromLocal8Bit("�豸IP")
        << QString::fromLocal8Bit("��ʼʱ��")
        << QString::fromLocal8Bit("��������");
    //row << "row 1" << "row 2" << "row 3" << "row 4";
    tb_item->setHorizontalHeaderLabels(column);                  // ����ˮƽ��ͷ��ǩ
    //tb_item->setVerticalHeaderLabels(row);                     // ���ô�ֱ��ͷ��ǩ
    // ���item��model
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 6; ++j)
        {
            if (j == 0)
            {
                QStandardItem* it = new QStandardItem(QString("%1").arg(i + j));
                //it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                tb_item->setItem(i, j, it);
                QCheckBox* chbox = new QCheckBox();
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
                bt->setProperty("id", i);
                bt->setStyleSheet(QStringLiteral("QPushButton { background-color:transparent}"));
                QIcon icon;
                icon.addFile(QStringLiteral("res/play_button.png"), QSize(), QIcon::Normal, QIcon::Off);
                bt->setIcon(icon);
                bt->setIconSize(QSize(20, 20));
                QObject::connect(bt, &QPushButton::clicked, this, [=]() {

                    QStandardItem* it = new QStandardItem(getTime());
                    it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
                    tb_item->setItem(i, j-1, it);
                    //qDebug() << getTime();
                    });
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
    this->horizontalHeader()->setStyleSheet("QHeaderView::section {color: black;padding-left: 4px;border: 0px solid #6c6c6c;}");
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
