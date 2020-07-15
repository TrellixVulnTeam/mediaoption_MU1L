#include "PushTable.h"

PushTable::PushTable(QWidget* parent): QTableView()
{
    set_tabel();
}
void PushTable::set_tabel()
{
    this->horizontalHeader()->setStyleSheet("QHeaderView::section {color: black;padding-left: 4px;border: 0px solid #6c6c6c;}");
    item_model = new QStandardItemModel(32, 4);  // 4��4��
    this->setModel(item_model);                  // ����view��model
    QStringList column;
    column << QString::fromLocal8Bit("���") << QString::fromLocal8Bit("�豸���")<< QString::fromLocal8Bit("�豸����") << QString::fromLocal8Bit("�豸�ͺ�") << QString::fromLocal8Bit("�豸IP") << QString::fromLocal8Bit("ѡ�������") << QString::fromLocal8Bit("��������");
    //row << "row 1" << "row 2" << "row 3" << "row 4";
    item_model->setHorizontalHeaderLabels(column);                // ����ˮƽ��ͷ��ǩ
    //item_model->setVerticalHeaderLabels(row);                     // ���ô�ֱ��ͷ��ǩ
    for (int i = 0; i < 32; ++i)
    {
        for (int j = 0; j < 7; ++j)
        {
            if (j == 0)
            {
                QStandardItem* it = new QStandardItem(QString("%1").arg(i + j));
                //it->setTextAlignment(Qt::AlignCenter);
                item_model->setItem(i, j, it);
                QWidget* widg = new QWidget;
                QCheckBox* chbox = new QCheckBox();
                chbox->setProperty("id", i);
                QHBoxLayout* layout = new QHBoxLayout;
                layout->addWidget(chbox);
                layout->setMargin(0);
                layout->setAlignment(chbox, Qt::AlignCenter);
                widg->setLayout(layout);
                this->setIndexWidget(item_model->index(i, j), widg);
            }
            else if (j == 6)
            {
                QPushButton* button = new QPushButton(QString::fromLocal8Bit("����"));
                button->setProperty("id", i);                               // set custom property
                this->setIndexWidget(item_model->index(i, j), button);
            }
            else
            {
                QStandardItem* it = new QStandardItem(QString("%1").arg(i + j));
                it->setTextAlignment(Qt::AlignCenter);
                item_model->setItem(i, j, it);
            } 
        }      
    }
    int wd = this->width();
    this->setColumnWidth(0, int(wd/24) * 3);
    this->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    this->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    this->setColumnWidth(6, int(wd / 12)*2);
    this->horizontalHeader()->setStretchLastSection(true);
    this->setShowGrid(false);                               // ����������
    this->setFocusPolicy(Qt::NoFocus);                      // ȥ����ǰCell�ܱ����߿�
    this->setAlternatingRowColors(true);                    // ����������ɫ
    //this->horizontalHeader()->hide();                       // ����ˮƽ��ͷ
    this->verticalHeader()->hide();                         //���ش�ֱͷ
    this->horizontalHeader()->setHighlightSections(false);
    this->setEditTriggers(QAbstractItemView::NoEditTriggers);   
}

void PushTable::set_tabLayout(PushTable* u, QWidget* g)
{
    QGridLayout* tablayout = new QGridLayout(g);
    tablayout->addWidget(u);
}

PushTable::~PushTable()
{

}
