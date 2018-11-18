#pragma once
//#include <Winsock2.h>  // windows下操作mysql数据库时，必须引入此头文件，解决 "fd 未声明符号"的错误；Linux下无须引用
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
