//
// Created by zhenkai on 2020/4/9.
//

#ifndef MOTAN_CPP_CONNECTIONPOOL_H
#define MOTAN_CPP_CONNECTIONPOOL_H

#include <iostream>
#include <unistd.h>
#include <deque>
#include <pthread.h>
#include <atomic>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <cstring>

#define DISABLE_COPY(Class) \
Class(const Class &); \
Class &operator=(const Class &);


class Peer {
private:
    int port_;
    std::string host_;
    int conn_timeo_;
public:
    Peer(const std::string &m_host, int m_port, int m_conn_timeo);
    virtual ~Peer();
    int get_port();
    const std::string &get_host();
    int get_conn_timeo();

};

class Connection {
private:
    int sock_;
    Peer peer_;
    int last_active_;
    std::atomic<int> reuse_times_;
    bool connect_sock(int nonblock = 1);
    DISABLE_COPY(Connection);

public:
    Connection(const Peer &m_peer);
    virtual ~Connection();
    int get_connect_sock();
    int get_sock();
    int get_last_active();
    int get_reuse_times();
    bool is_valid();
    void close_sock();

    int sock_to_ip(char *ipaddr, socklen_t len);

};

class ConnectionPool {
private:
    Peer peer_;
    int pool_size_;
    std::atomic<int> real_size_;
    std::atomic<int> idle_size_;
    int keepalive_timeout_;
    std::deque<Connection *> connect_pool_{};
    pthread_mutex_t mutex_;
    DISABLE_COPY(ConnectionPool);

public:
    ConnectionPool(const std::string &m_host, int m_port, int m_pool_size, int m_keepalive_timeout, int m_conn_timeo = 100);
    virtual ~ConnectionPool();
    Connection *fetch();
    bool release(Connection *conn);

};



#endif //MOTAN_CPP_CONNECTIONPOOL_H
