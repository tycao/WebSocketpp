#define _WEBSOCKETPP_NOEXCEPT_ 
#define _WEBSOCKETPP_CPP11_CHRONO_ 
#define NOMINMAX 
#define _WEBSOCKETPP_CPP11_FUNCTIONAL_ 
#define _WEBSOCKETPP_CPP11_MEMORY_ 

#define _CRT_SECURE_NO_WARNINGS 
//#define BOOST_SPIRIT_THREADSAFE


#include <websocketpp/config/asio_no_tls.hpp>

#include <websocketpp/server.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include "rapidjson/document.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>




/*#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>*/
#include <websocketpp/common/thread.hpp>
#include "sql.h"

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

using websocketpp::lib::thread;
using websocketpp::lib::mutex;
using websocketpp::lib::unique_lock;
using websocketpp::lib::condition_variable;

using namespace rapidjson;



// 全局变量加锁
mutex data_lock;
std::string data_str;

/* on_open insert connection_hdl into channel
* on_close remove connection_hdl from channel
* on_message queue send to all channels
*/

enum action_type {
	SUBSCRIBE,
	UNSUBSCRIBE,
	MESSAGE
};

struct action {
	action(action_type t, connection_hdl h) : type(t), hdl(h) {}
	action(action_type t, server::message_ptr m) : type(t), msg(m) {}
	action(action_type t, server::message_ptr m, connection_hdl h) : type(t), msg(m), hdl(h) {}

	action_type type;
	websocketpp::connection_hdl hdl;
	server::message_ptr msg;
};

class broadcast_server {
public:
	broadcast_server() {
		// Initialize Asio Transport
		m_server.init_asio();

		// Register handler callbacks
		m_server.set_open_handler(bind(&broadcast_server::on_open, this, ::_1));
		m_server.set_close_handler(bind(&broadcast_server::on_close, this, ::_1));
		m_server.set_message_handler(bind(&broadcast_server::on_message, this, ::_1, ::_2));

		if (this->initDB("127.0.0.1", "root", "admin", "wsServer")) {
			std::cout << "db init succ!\n";
		}
		else {
			std::cout << "db init failed!\n";
		}
	}

	void run(uint16_t port) {
		// listen on specified port
		m_server.listen(port);

		// Start the server accept loop
		m_server.start_accept();

		// Start the ASIO io_service run loop
		try {
			m_server.run();
		}
		catch (const std::exception & e) {
			std::cout << e.what() << std::endl;
		}
		catch (websocketpp::lib::error_code e) {
			std::cout << e.message() << std::endl;
		}
		catch (...) {
			std::cout << "other exception" << std::endl;
		}
	}

	void on_open(connection_hdl hdl) {
		unique_lock<mutex> lock(m_action_lock);
		//std::cout << "on_open" << std::endl;
		m_actions.push(action(SUBSCRIBE, hdl));
		lock.unlock();
		m_action_cond.notify_one();
	}

	void on_close(connection_hdl hdl) {
		unique_lock<mutex> lock(m_action_lock);
		//std::cout << "on_close" << std::endl;
		m_actions.push(action(UNSUBSCRIBE, hdl));
		lock.unlock();
		m_action_cond.notify_one();
	}

	void on_message(connection_hdl hdl, server::message_ptr msg) {
		// queue message up for sending by processing thread
		unique_lock<mutex> lock(m_action_lock);
		//std::cout << "on_message" << std::endl;
		m_actions.push(action(MESSAGE, msg, hdl));
		lock.unlock();
		m_action_cond.notify_one();
		std::cout << msg->get_payload() << std::endl;
	}

	void process_messages() {
		while (1) {
			unique_lock<mutex> lock(m_action_lock);

			while (m_actions.empty()) {
				m_action_cond.wait(lock);
			}

			action a = m_actions.front();
			m_actions.pop();

			lock.unlock();

			if (a.type == SUBSCRIBE) {
				unique_lock<mutex> lock(m_connection_lock);
				m_connections.insert(a.hdl);
			}
			else if (a.type == UNSUBSCRIBE) {
				unique_lock<mutex> lock(m_connection_lock);
				m_connections.erase(a.hdl);
				lock.unlock();

				//// 通知其他用户，该用户已经下线；删除用户管理表中关于该用户的信息
				unique_lock<mutex> conn_user_lock(m_conn_user_lock);
				std::string msg;
				for (auto it = m_conn_user.begin(); it != m_conn_user.end(); ++it) {
					std::cout << "msg:" << it->first << "  use_count:" << it->second.use_count() << std::endl;
					if (it->second.lock() == a.hdl.lock()) {
						msg = it->first;
					}
				}
				if (!msg.empty()) {
					m_conn_user.erase(msg);
				}
				conn_user_lock.unlock();

				if (!msg.empty()) {
					// 从MYSQL数据库删除
					std::string type, content, cid, login_time, logout_time;
					Document document;
					ParseResult result = document.Parse(msg.c_str());
					std::cout << msg << std::endl;
					if (!result) {std::cerr << "Error in parsing document.Parse("  << std::endl;};
					type = document["type"].GetString();
					content = document["content"].GetString();
					cid = document["cid"].GetString();
					login_time = document["login_time"].GetString();
					logout_time = document["logout_time"].GetString();
					std::string sql = "delete from n_m_user_info where cid=\"" + cid + "\";";
					if (mydb.exeSQL(sql)) {
						std::cout << "n_m_user_info_his delete succ!\n";
					}

					if (type == "LOGIN") {
						rapidjson::StringBuffer buffer;
						rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
						writer.StartObject();
						writer.Key("type");
						writer.String("logout");
						writer.Key("content");
						writer.String(content.c_str());
						writer.Key("cid");
						writer.String(cid.c_str());
						writer.Key("login_time");
						writer.String(login_time.c_str());
						writer.Key("logout_time");
						writer.String(logout_time.c_str());
						writer.EndObject();
						std::string s = buffer.GetString();

						con_list::iterator it;
						unique_lock<mutex> connection_lock(m_connection_lock);
						for (it = m_connections.begin(); it != m_connections.end(); ++it) {
							//m_server.send(*it, a.msg);
							server::connection_ptr con = m_server.get_con_from_hdl(*it);
							con->send(s, websocketpp::frame::opcode::text);
						}
					}
				}
			}
			else if (a.type == MESSAGE) {
				std::string type, content, cid, login_time, logout_time;
				// 获取客户端传入的JSON
				std::string str = a.msg->get_raw_payload();
				Document document;
				document.Parse(str.c_str());
				type = document["type"].GetString();
				content = document["content"].GetString();
				cid = document["cid"].GetString();
				login_time = document["login_time"].GetString();
				logout_time = document["logout_time"].GetString();
				std::string sql, msg;

				if (type == "LOGIN") {
					// 用户名cid是否已经存在
					sql = "select * from n_m_user_info_his where cid = \"" + cid + "\";";
					if (!mydb.selectSQL(sql).empty()) {
						server::connection_ptr con = m_server.get_con_from_hdl(a.hdl);
						con->send(std::string("{\"type\":\"SAME\",\"cid\":\"" + cid + "\"}"), websocketpp::frame::opcode::text);
					}
					else {
						// 实时在线用户
						sql = "insert into n_m_user_info values(\"" + type + "\",\"" + content + "\",\"" + cid + "\",\"" + login_time + "\",\"" + logout_time + "\"" + ");";
						if (mydb.exeSQL(sql)) {
							std::cout << "insert n_m_user_info succ!\n";
						}

						// 历史在线用户
						sql = "insert into n_m_user_info_his values(\"" + type + "\",\"" + content + "\",\"" + cid + "\",\"" + login_time + "\",\"" + logout_time + "\"" + ");";
						if (mydb.exeSQL(sql)) {
							std::cout << "insert n_m_user_info_his succ!\n";
						}

						// 插入m_conn_user
						unique_lock<mutex> conn_user_lock(m_conn_user_lock);
						m_conn_user.insert(std::make_pair(str, a.hdl));
						conn_user_lock.unlock();

						unique_lock<mutex> lock(m_connection_lock);
						for (con_list::iterator it = m_connections.begin(); it != m_connections.end(); ++it) {
							m_server.send(*it, a.msg);
						}
					}
				}
				else if (type == "Query") {
					sql = "select * from n_m_user_info;";
					msg += "[";
					msg += mydb.selectSQL(sql);
					msg += "]";
					server::connection_ptr con = m_server.get_con_from_hdl(a.hdl);
					con->send(msg, websocketpp::frame::opcode::text);
				}
				else if (type == "QHis") {
					sql = "select * from n_m_user_info_his;";
					msg += "[";
					msg += mydb.selectSQL(sql);
					msg += "]";
					server::connection_ptr con = m_server.get_con_from_hdl(a.hdl);
					con->send(msg, websocketpp::frame::opcode::text);
				}
				else if (type == "MSG") {
					con_list::iterator it;
					unique_lock<mutex> connection_lock(m_connection_lock);
					for (it = m_connections.begin(); it != m_connections.end(); ++it) {
						//m_server.send(*it, a.msg);
						server::connection_ptr con = m_server.get_con_from_hdl(*it);
						con->send(str, websocketpp::frame::opcode::text);
					}
				}

			}
			else {
				// undefined.
			}
		}
	}

	bool initDB(string host, string user, string pwd, string db_name) {
		return mydb.initDB(host, user, pwd, db_name);
	}
private:
	typedef std::set<connection_hdl, std::owner_less<connection_hdl>> con_list;

	server m_server;
	con_list m_connections;
	std::queue<action> m_actions;

	mutex m_action_lock;
	mutex m_connection_lock;
	condition_variable m_action_cond;

	std::unordered_map<std::string, connection_hdl> m_conn_user;
	MyDB mydb;

	mutex m_conn_user_lock;
};

int main() {
	try {
		broadcast_server server;

		// Start a thread to run the processing loop
		thread t1(bind(&broadcast_server::process_messages, &server));

		// Run the asio loop with the main thread
		server.run(9001);

		t1.join();

	}
	catch (std::exception & e) {
		std::cout << e.what() << std::endl;
	}
}
