/*************************************************************************
> File Name: MyDB.h
> Author: SongLee
> E-mail: lisong.shine@qq.com
> Created Time: 2014��05��04�� ������ 23ʱ25��50��
> Personal Blog: http://songlee24.github.io
************************************************************************/
#pragma once
//#include <Winsock2.h>	// Windows�´���mysql���ݿ⣬�������ô�ͷ�ļ���Linux�²���Ҫ����
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
