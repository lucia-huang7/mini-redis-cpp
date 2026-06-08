#include "miniredis/server.hpp"

#include <iostream>
#include <utility>

namespace miniredis {

Server::Server(Config config) : config_(std::move(config)) {}

int Server::run() {
    std::cout << "Mini Redis server skeleton listening on port " << config_.port << "\n";
    std::cout << "Next milestone: implement TCP accept loop and RESP command handling.\n";
    return 0;
}

}  // namespace miniredis
