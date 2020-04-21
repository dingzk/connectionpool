# connectionpool
链接池

# example:
    #include "connectionpool.h"
    // 链接池实例化的时候并不会进行链接的初始化，惰性进行socket链接
    std::shared_ptr<ConnectionPool> cp (new ConnectionPool(host, port, kDefaultPoolSize, kDefaultKeepAliveTimeout, kDefaultConnectTimeout));
    // 选择一个conn
    motan_channel::Connection * conn = pool_->fetch();
    // 一个conn对应一个可用的socket(可以扩展到n个)
    int fd = cp->get_connect_sock();// 这时候才会进行真正的网络链接
    
# tips:
- 单线程，线程安全。
- 网络编程要处理很多边界问题。针对边界问题进行了一一验证。
- 长链接，但是每次链接获取都进行了poll，以防链接失效。

# TODO:
- 可以使用工厂模式改造下，不光能对tcp做链接池，也可以对数据库之类的做链接池。
- 每个conn下目前只有一个链接，可以针对不同的域名做链接池。
