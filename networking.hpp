#ifndef __NETWORKING_HPP__
#define __NETWORKING_HPP__

#include "protocol.hpp"

namespace net {

    struct server {
        const char* address;
        int port;
    };

    void listen(const server& server);

} // net

#endif // __NETWORKING_HPP__