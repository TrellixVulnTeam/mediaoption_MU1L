#include "TreeView.h"

TreeView::TreeView(QObject* parent): QTreeView()
{
    model = new QStandardItemModel(4, 1);
    //QStringList headers;
    //headers << QStringLiteral("��ƵԴ");
    header()->hide();
    //model->setHorizontalHeaderLabels(headers);
    QStandardItem* item1 = new QStandardItem(QString::fromLocal8Bit("�豸"));
    QStandardItem* item2 = new QStandardItem(QString::fromLocal8Bit("��Ƶ�ļ�"));
   

    model->setItem(0, 0, item1);
    model->setItem(1, 0, item2);
    QStandardItem* item3 = new QStandardItem(QString::fromLocal8Bit("����"));
    QStandardItem* item4 = new QStandardItem(QString::fromLocal8Bit("��"));
    QStandardItem* item5 = new QStandardItem(QString::fromLocal8Bit("����"));
    QStandardItem* item6 = new QStandardItem(QString::fromLocal8Bit("������Ƶ�ļ�"));
    item1->appendRow(item3);
    item1->appendRow(item4);
    item1->appendRow(item5);
    item2->appendRow(item6);
    setModel(model);
    
}

QList<QStandardItem*> TreeView::returnTheItems()
{
    return model->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
}

void TreeView::iterateOverItems()
{
    QList<QStandardItem*> list = returnTheItems();

    foreach(QStandardItem * item, list) {
        qDebug() << item->text();
    }
}

TreeView::~TreeView()
{
}
