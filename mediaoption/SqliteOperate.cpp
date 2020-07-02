#include "SqliteOperate.h"

SqliteOperate::SqliteOperate()
{
	QSqlDatabase database;
	if (QSqlDatabase::contains("qt_sql_default_connection"))
	{
		database = QSqlDatabase::database("qt_sql_default_connection");
	}
	else
	{
		//������sqlite���ݵ�����
		db = QSqlDatabase::addDatabase("QSQLITE");
		//�������ݿ��ļ�������
		QString dbname = QDir::currentPath() + QString("/") + QString("vms.db");
		db.setDatabaseName(dbname);
	}
}
bool SqliteOperate::openDb()
{
	
	if (!db.open())
	{
		qDebug() << "open lose";
		return false;
	}
	return true;
}

void SqliteOperate::createTable(void)
{
	QSqlQuery query;
	bool ok1 = query.exec("CREATE TABLE IF NOT EXISTS rtspserver (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(30) NOT NULL, deviceid INTEGER NULL, ipaddr VARCHAR(15) NOT NULL, port INT NOT NULL)");
	
	bool ok2 = query.exec("CREATE TABLE IF NOT EXISTS devicetab (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(30) NOT NULL, devicetype VARCHAR(30) NOT NULL, deviceid  INTEGER NULL, username VARCHAR(30) NOT NULL, passwd VARCHAR(30) NOT NULL, ipaddr VARCHAR(15) NOT NULL, port INT NOT NULL)");
	
	bool ok3 = query.exec("CREATE TABLE IF NOT EXISTS filetab (id INTEGER PRIMARY KEY AUTOINCREMENT, filename VARCHAR(60) NOT NULL, username VARCHAR(30) NOT NULL, passwd VARCHAR(30) NOT NULL)");

	bool ok4 = query.exec("CREATE TABLE IF NOT EXISTS  devicetype(id INTEGER PRIMARY KEY AUTOINCREMENT, typename VARCHAR(30) NOT NULL, typeid INTEGER NULL)");
	if (ok1 && ok2 && ok3 && ok4)
	{
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('����IPC', 1);"));
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('��IPC', 2);"));
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('����IPC', 3);"));
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('����NVR', 4);"));
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('��NVR', 5);"));
		query.exec(QString::fromLocal8Bit("INSERT INTO devicetype (typename, typeid) VALUES('����NVR', 6);"));
		qDebug() << "ceate table partition success/n";
	}
	else
	{
		qDebug() << "ceate table partition failed/n";
	}
}

void SqliteOperate::addNewcolumn(QString& columnNameAndproperty)
{

}

void SqliteOperate::queryTable(QString& str)
{
	QSqlQuery query;
	query.exec(str);
	while (query.next())
	{
		QString typenames = query.value(1).toString();
		//QString typeids = query.value(2).toInt();
		

		qDebug() << typenames;
	}
}

bool SqliteOperate::IsTaBexists(QString& Tabname)
{
	//QSqlDatabase db = QSqlDatabase::database();
	if (db.tables().contains(Tabname))
	{
		return true;
	}
	return false;
}

void SqliteOperate::singleinsertdata()
{

}

void SqliteOperate::Moreinsertdata()
{

}

void SqliteOperate::deletedata()
{

}

void SqliteOperate::updatedata()
{

}

void SqliteOperate::closeDb(void)
{
	db.close();
}


SqliteOperate::~SqliteOperate()
{
	db.close();
}
