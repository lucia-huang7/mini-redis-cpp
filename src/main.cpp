#include "miniredis/config.hpp"
#include "miniredis/server.hpp"

int main(int argc, char** argv) {
    auto config = miniredis::parse_config(argc, argv);
    miniredis::Server server(config);
    return server.run();
}
