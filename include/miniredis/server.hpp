#pragma once

#include "miniredis/config.hpp"

namespace miniredis {

class Server {
public:
    explicit Server(Config config);

    int run();

private:
    Config config_;
};

}  // namespace miniredis
