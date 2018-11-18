#pragma once
//#include <Winsock2.h>  // windows�²���mysql���ݿ�ʱ�����������ͷ�ļ������ "fd δ��������"�Ĵ���Linux����������
#include<iostream>
#include<string>
#include<mysql.h>
using namespace std;

class MyDB
{
public:
	MyDB();
	~MyDB();
	bool initDB(string host, string user, string pwd, string db_name);
	bool exeSQL(string sql);
	std::string selectSQL(std::string sql);
private:
	MYSQL * connection;
	MYSQL_RES *result;
	MYSQL_ROW row;
};
