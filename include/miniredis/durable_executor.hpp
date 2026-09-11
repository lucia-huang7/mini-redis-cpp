#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace miniredis {

class Aof;
class CommandDispatcher;

class DurableCommandExecutor {
public:
    DurableCommandExecutor(CommandDispatcher& dispatcher, const Aof* aof);

    std::string execute(const std::vector<std::string_view>& command);
    bool write_failed() const;

private:
    CommandDispatcher& dispatcher_;
    const Aof* aof_;
    mutable std::mutex write_mutex_;
    bool write_failed_ = false;
};

}  // namespace miniredis
