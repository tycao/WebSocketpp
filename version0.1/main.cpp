#define _WEBSOCKETPP_NOEXCEPT_ 
#define _WEBSOCKETPP_CPP11_CHRONO_ 
#define NOMINMAX 
#define _WEBSOCKETPP_CPP11_FUNCTIONAL_ 
#define _WEBSOCKETPP_CPP11_MEMORY_ 

#define _CRT_SECURE_NO_WARNINGS 


#include <websocketpp/config/asio_no_tls.hpp>

#include <websocketpp/server.hpp>

#include <iostream>
#include <unordered_set>
#include <unordered_map>

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
					// 从当前在线表中删除
					unique_lock<mutex> user_info_lock(m_user_info_lock);
					if (m_user_info.find(msg) != m_user_info.end()) {
						m_user_info.erase(msg);
					}
					user_info_lock.unlock();
					// 从MYSQL数据库删除
					// 插入MYSQL数据库
					std::string sql = "delete from m_user_info where login_info='" + msg + "';";
					if (mydb.exeSQL(sql)) {
						std::cout << "m_user_info_his insert succ!\n";
					}

					if (msg.find("LOGIN") != std::string::npos) {
						msg.replace(msg.find("LOGIN"), 5, "logout");	// 将"LOGIN"替换"logout"
					}

					con_list::iterator it;
					unique_lock<mutex> connection_lock(m_connection_lock);
					for (it = m_connections.begin(); it != m_connections.end(); ++it) {
						//m_server.send(*it, a.msg);
						server::connection_ptr con = m_server.get_con_from_hdl(*it);
						con->send(msg, websocketpp::frame::opcode::text);
					}
				}
			}
			else if (a.type == MESSAGE) {
				// 新用户登录，排除查询操作
				auto str = a.msg->get_raw_payload();
				auto strTmp = str;
				size_t pos = 0;
				while ((pos = strTmp.find("\"", pos)) != std::string::npos) {
					strTmp.replace(pos, 1, "\\\"");
					pos = strTmp.find("\"", pos) + 1;
				}
				if (str.find("oper") != std::string::npos)
				{
					if (str.find("Query") != std::string::npos) {
						std::cout << "Query exists in a.type == MESSAGE\n\n";
					}
					else if (str.find("QHis") != std::string::npos) {
						std::cout << "QHis exists in a.type == MESSAGE\n\n";
					}
					else {
						std::cout << "\n\na.type == MESSAGE:" << str << "\n\n";

						unique_lock<mutex> user_info_lock(m_user_info_lock);
						m_user_info.insert(str);	//当前在线
						// 插入MYSQL数据库
						std::string sql = "insert into m_user_info values(\"" + strTmp + "\");";
						if (mydb.exeSQL(sql)) {
							std::cout << "m_user_info insert succ!\n";
						}
													//std::cout << "\n=======================m_user_info=================\n";
													//for (auto it = m_user_info.begin(); it != m_user_info.end(); ++it) {
													//	std::cout << "\n\n" << *it << "\n\n";
													//}
						user_info_lock.unlock();

						unique_lock<mutex> user_info_his_lock(m_user_info_his_lock);
						m_user_info_his.insert(str);	//历史在线
						// 插入MYSQL数据库
						sql = "insert into m_user_info_his values(\"" + strTmp + "\");";
						if (mydb.exeSQL(sql)) {
							std::cout << "m_user_info_his insert succ!\n";
						}
														//std::cout << "\n=======================m_user_info_his=================\n";
														//for (auto it = m_user_info_his.begin(); it != m_user_info_his.end(); ++it) {
														//	std::cout << "\n\n" << *it << "\n\n";
														//}
						user_info_his_lock.unlock();

						unique_lock<mutex> conn_user_lock(m_conn_user_lock);
						m_conn_user.insert(std::make_pair(str, a.hdl));
						//std::cout << "\n=======================m_conn_user=================\n";
						//for (auto it = m_conn_user.begin(); it != m_conn_user.end(); ++it) {
						//	std::cout << "\n\n" << "it->first:" << it->first << "\nit->second:" << it->second << "\n\n";
						//}
						conn_user_lock.unlock();
					}

				}
				// 查询操作,返回json数组，展示在页面
				if (str.find("Query") != std::string::npos) {
					std::string msg = "[";
					msg += this->mydb.selectSQL("select * from m_user_info;");
					msg = msg.substr(0, msg.find_last_of(","));
					msg += "]";
					
					unique_lock<mutex> lock(data_lock);
					data_str = "[";
					for (auto iter = m_user_info.begin(); iter != m_user_info.end(); ++iter) {
						data_str += *iter + ",";
					}
					data_str = data_str.substr(0, data_str.find_last_of(","));
					data_str += "]";
					//a.msg->set_payload(data_str);
					lock.unlock();

					//std::cout << "===Query===\na.msg->get_payload(): " << a.msg->get_payload()
					//	<< "\na.msg->get_opcode(): " << a.msg->get_opcode() << std::endl;
					//m_server.send(a.hdl, a.msg);
					server::connection_ptr con = m_server.get_con_from_hdl(a.hdl);
					con->send(msg, websocketpp::frame::opcode::text);
				}
				else if (str.find("QHis") != std::string::npos) {
					std::string msg = "[";
					msg += this->mydb.selectSQL("select * from m_user_info_his;");
					msg = msg.substr(0, msg.find_last_of(","));
					msg += "]";
					unique_lock<mutex> lock(data_lock);
					data_str = "[";
					for (auto iter = m_user_info_his.begin(); iter != m_user_info_his.end(); ++iter) {
						data_str += *iter + ",";
					}
					data_str = data_str.substr(0, data_str.find_last_of(","));
					data_str += "]";
					//a.msg->set_payload(data_str);
					lock.unlock();

					//std::cout << "===QHis===\na.msg->get_payload(): " << a.msg->get_payload()
					//	<< "\na.msg->get_opcode(): " << a.msg->get_opcode() << std::endl;;
					//m_server.send(a.hdl, a.msg);
					server::connection_ptr con = m_server.get_con_from_hdl(a.hdl);
					con->send(msg, websocketpp::frame::opcode::text);

				}
				else {
					unique_lock<mutex> lock(m_connection_lock);
					for (con_list::iterator it = m_connections.begin(); it != m_connections.end(); ++it) {
						m_server.send(*it, a.msg);
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

	std::unordered_set<std::string> m_user_info;
	std::unordered_set<std::string> m_user_info_his;
	std::unordered_map<std::string, connection_hdl> m_conn_user;
	MyDB mydb;

	mutex m_user_info_lock;
	mutex m_user_info_his_lock;
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
