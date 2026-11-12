#pragma once

#include "editor/session_coordinator.hpp"
#include <memory>
#include <unordered_map>

namespace editor {
// Process-local opaque handles, never addresses. Tokens are positive, never reused.
// acquire() pins lifetime for an entire native call; close prevents new acquisitions
// while allowing already-acquired calls to finish. Not an Activity/task executor.
class SessionRegistry final {
public:
    using Handle = std::int64_t;
    [[nodiscard]] Handle create(std::string project_id);
    [[nodiscard]] std::shared_ptr<SessionCoordinator> acquire(Handle handle) const;
    void close(Handle handle); // idempotent, including stale/zero handles
private:
    mutable std::mutex mutex_;
    std::unordered_map<Handle, std::shared_ptr<SessionCoordinator>> sessions_;
    std::uint64_t next_{1};
};
} // namespace editor
