#pragma once

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlQueryModel>
#include <QDebug>
#include <QtSql/QSqlError>
#include <QApplication>
#include <QDir>

class SqliteOperate 
{
public:
	SqliteOperate();
	~SqliteOperate();
public:
	bool openDb();

    //�������ݱ�student��
    void createTable(void);

    //�ڱ���������µ��ֶ�
    void addNewcolumn(QString& columnNameAndproperty);

    //��ѯ����ʾ���
    void queryTable(QString& str);

    //�ж����ݱ��Ƿ����
    bool IsTaBexists(QString& Tabname);

    //��������
    void singleinsertdata();//���뵥������

    void Moreinsertdata();//�����������

    //ɾ������
    void deletedata();

    //�޸�����
    void updatedata();

    //�ر����ݿ�
    void closeDb(void);

private:
    QSqlDatabase db;//���ڽ��������ݿ������
};
