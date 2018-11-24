CXXFLAGS= -DRELEASE

WEBSOCKET_DIR=./websocketpp-experimental ./websocketpp-experimental/websocketpp
MYSQL_DIR=/usr/include/mysql
BOOST_DIR=./boost/include/
RAPIDJSON_DIR=./rapidjson/include
SRC_LIST=./main.cpp ./sql.cpp
BOOST_LIBS_DIR=./boost/lib/
MYSQL_LIBS_DIR=/usr/lib64/mysql/

include_dirs=$(addprefix -I, $(WEBSOCKET_DIR) $(BOOST_DIR) $(MYSQL_DIR) $(RAPIDJSON_DIR))
libs_dirs=$(addprefix -L, $(BOOST_LIBS_DIR) $(MYSQL_LIBS_DIR))
run:
#	gcc --std=c++11 -D_WEBSOCKETPP_CPP11_STL_ -DASIO_STANDALONE $(include_dirs) -L/usr/lib/x86_64-linux-gnu/ $(SRC_LIST) $(libs_dirs) -lboost_system -lboost_chrono -lpthread  -lrt -o wsServer
	g++ --std=c++11 $(CXXFLAGS)  -D_WEBSOCKETPP_CPP11_STL_ $(include_dirs) $(SRC_LIST) $(libs_dirs) -lmysqlclient  -o wsServer -lboost_system -lboost_chrono -lpthread -lrt -ldl

clean:
	rm -rf *.o wsServer
