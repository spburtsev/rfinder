#ifndef __NETWORKING_HPP__
#define __NETWORKING_HPP__

#include "protocol.hpp"

namespace net {

    struct tcp_server final {
        const char* address;
        int port;

        void listen() const;
    };

} // net

#endif // __NETWORKING_HPP__