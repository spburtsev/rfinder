#ifndef __NETWORKING_HPP__
#define __NETWORKING_HPP__

#include <cstdint>
#include "protocol.hpp"

namespace net {

    struct tcp_server final {
        const char* address;
        uint16_t port;

        void listen() const;
    };

} // net

#endif // __NETWORKING_HPP__