//
// Created by zhenkai on 2020/4/9.
//

#include "connectionpool.h"
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <ctime>

namespace motan_channel {

    Peer::Peer(const std::string &m_host, int m_port, int m_conn_timeo) :
    host_(m_host),
    port_(m_port),
    conn_timeo_(m_conn_timeo)
    {
        if (conn_timeo_ <= 0) {
            conn_timeo_ = 100;
        }
    }

    Peer::~Peer() {}

    int Peer::get_port() {
        return port_;
    }

    const std::string & Peer::get_host() {
        return host_;
    }

    int Peer::get_conn_timeo() {
        return conn_timeo_;
    }

    Connection::Connection(const Peer &m_peer) :
    peer_(m_peer)
    {
        sock_ = -1;
        last_active_ = 0;
        reuse_times_ = 0;
    }

    Connection::~Connection() {
        close_sock();
    }

    static bool set_socket_noblock(int sock) {
        int flags, flag_t;
        while ((flags = fcntl(sock, F_GETFL, 0)) == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        flag_t = flags;
        if (flag_t & O_NONBLOCK) {
            return true;
        }
        flags |= O_NONBLOCK;
        while (fcntl(sock, F_SETFL, flags) == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        return true;
    }

    static bool set_socket_block(int sock) {
        int flags, flag_t;
        while ((flags = fcntl(sock, F_GETFL, 0)) == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        flag_t = flags;
        if (!(flag_t & O_NONBLOCK)) {
            return true;
        }
        flags &= ~O_NONBLOCK;
        while (fcntl(sock, F_SETFL, flags) == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        return true;
    }

    static int create_socket() {
        int sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) {
            return -1;
        }
        socklen_t optVal = 1024 * 1024;
        socklen_t optLen = sizeof(socklen_t);
        setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, static_cast<void *>(&optVal), optLen);
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, static_cast<void *>(&optVal), optLen);

        return sock_;
    }

    /*
     *        The socket is nonblocking and the connection cannot be completed immediately.  It is possible to  select(2)
              or  poll(2) for completion by selecting the socket for writing.  After select(2) indicates writability, use
              getsockopt(2) to read the SO_ERROR option at level SOL_SOCKET to determine whether connect() completed suc‐
              cessfully  (SO_ERROR  is  zero)  or  unsuccessfully  (SO_ERROR is one of the usual error codes listed here,
              explaining the reason for the failure).
     */
    bool Connection::connect_sock(int nonblock) {
        int on = 1;
        int error = 0;
        int ret, n;
        sock_ = create_socket();
        if (sock_ <= 0) {
            return false;
        }
        if (!set_socket_noblock(sock_)) {
            goto ERR;
        }
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        inet_pton(AF_INET, peer_.get_host().c_str(), &sin.sin_addr);
        sin.sin_port = htons(peer_.get_port());

        if ((ret = connect(sock_, (const struct sockaddr *) &sin, sizeof(sin))) == 0) {
            goto DONE;
        } else if (ret < 0) {
            if (errno != EINPROGRESS) {
                goto ERR;
            }
        }

        struct pollfd pfds[1];
        pfds[0].fd = sock_;
        pfds[0].events = POLLOUT;
        pfds[0].revents = 0;

        if ((n = poll(pfds, 1, peer_.get_conn_timeo())) == 0) {
            goto ERR;
        } else if (n < 0) {
            if (errno != EINTR) {
                goto ERR;
            }
        }

        if (pfds[0].revents & (POLLOUT)) {
            socklen_t len = sizeof(error);
            if (getsockopt(sock_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                goto ERR;
            }
        } else {
            goto ERR;
        }
DONE:
        if (!nonblock) {
            set_socket_block(sock_);
        }
        setsockopt(sock_, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        return true;
ERR:
        close_sock();
        return false;
    }

    bool Connection::is_valid() {
        if (sock_ <= 0) {
            return false;
        }
        struct pollfd pfds[1];
        pfds[0].fd = sock_;
        pfds[0].events = POLLERR | POLLIN | POLLHUP | POLLOUT;
        pfds[0].revents = 0;
        char buf[10240];
        int read_len = 0;
        int try_times = 3;
        while (try_times --) {
            if (poll(pfds, 1, 0) < 0) {
                if (errno == EINTR)
                    continue;
            }
            if (pfds[0].revents & (POLLERR|POLLHUP)) {
                return false;
            } else if (pfds[0].revents & POLLIN) {
                read_len = read(sock_, buf, sizeof(buf)); // discard unread data
                if (read_len <= 0) {
                    return false;
                }
                continue;
            }
            if (pfds[0].revents & POLLOUT) {
                return true;
            }
        }

        return false;
    }

    int Connection::get_sock() {
        return sock_;
    }

    int Connection::get_connect_sock() {
        if (!is_valid()) {
            close_sock();
            if (!connect_sock(1)) {
                close_sock();
                return -1;
            }
        }
        reuse_times_ += 1;
        last_active_ = time(nullptr);

        return get_sock();
    }

    void Connection::close_sock() {
        if (sock_ > 0) {
            while (close(sock_) < 0 && errno == EINTR);
        }
        sock_ = -1;
    }

    int Connection::get_last_active() {
        return last_active_;
    }

    int Connection::get_reuse_times() {
        return reuse_times_;
    }

    int Connection::sock_to_ip(char *ipaddr, socklen_t len) {
        sockaddr_in newname;
        socklen_t newname_len = sizeof(newname);
        memset(&newname, 0, newname_len);
        memset(ipaddr, 0, len);
        getpeername(sock_, (struct sockaddr *)&newname, &newname_len);
        inet_ntop(AF_INET, &newname.sin_addr, ipaddr, len);

        return 0;
    }

    ConnectionPool::ConnectionPool(const std::string &m_host, int m_port, int m_pool_size, int m_keepalive_timeout, int m_conn_timeo) :
    peer_(m_host, m_port, m_conn_timeo),
    pool_size_(m_pool_size),
    keepalive_timeout_(m_keepalive_timeout)
    {
        pool_size_ = pool_size_ <= 0 ? 3 : pool_size_;
        real_size_ = 0;
        idle_size_ = 0;

        pthread_mutex_init(&mutex_, nullptr);
    }

    ConnectionPool::~ConnectionPool()
    {
        while (!connect_pool_.empty()) {
            delete connect_pool_.front();
            connect_pool_.pop_front();
        }
        pthread_mutex_destroy(&mutex_);
    }

    Connection * ConnectionPool::fetch() {
        Connection *conn = nullptr;
        pthread_mutex_lock(&mutex_);
        if (connect_pool_.empty()) {
            conn = new (std::nothrow) Connection(peer_);
            if (conn != nullptr) {
                real_size_ ++ ;
                idle_size_ ++;
                connect_pool_.push_back(conn);
            }
        }
        conn = connect_pool_.front();
        connect_pool_.pop_front();
        idle_size_--;

        pthread_mutex_unlock(&mutex_);

        return conn;
    }

    bool ConnectionPool::release(Connection *conn) {
        if (conn == nullptr) {
            return false;
        }
        pthread_mutex_lock(&mutex_);
        int last_active_time;
        std::deque<Connection *>::iterator iter;
        if (real_size_ > pool_size_ && idle_size_ > 0) {
            for (iter = connect_pool_.begin(); iter != connect_pool_.end(); ++iter) {
                last_active_time = (*iter)->get_last_active();
                if (last_active_time > 0) {
                    if (time(nullptr) - last_active_time > keepalive_timeout_) {
                        idle_size_--;
                        real_size_--;
                        delete *iter;
                        connect_pool_.erase(iter);
                        break;
                    }
                }
            }
        }

        connect_pool_.push_back(conn);
        real_size_++;

        pthread_mutex_unlock(&mutex_);

        return true;
    }


}

