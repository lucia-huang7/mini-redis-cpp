#pragma once

namespace miniredis {

class TtlCleaner {
public:
    void start();
    void stop();
};

}  // namespace miniredis
