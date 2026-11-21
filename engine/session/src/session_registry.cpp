#include "editor/session_registry.hpp"
#include <limits>
#include <stdexcept>

namespace editor {
SessionRegistry::Handle SessionRegistry::create(std::string project_id) {
    auto session = std::make_shared<SessionCoordinator>(std::move(project_id));
    std::lock_guard lock(mutex_);
    if (next_ > static_cast<std::uint64_t>(std::numeric_limits<Handle>::max()))
        throw std::overflow_error("native session handle space exhausted");
    const auto handle = static_cast<Handle>(next_);
    sessions_.emplace(handle, std::move(session));
    ++next_; // only after successful insertion; no wrap or address reuse
    return handle;
}
std::shared_ptr<SessionCoordinator> SessionRegistry::acquire(Handle handle) const {
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(handle);
    if (found == sessions_.end()) throw std::invalid_argument("native engine session is closed or invalid");
    return found->second;
}
void SessionRegistry::close(Handle handle) {
    std::shared_ptr<SessionCoordinator> retiring;
    {
        std::lock_guard lock(mutex_);
        const auto found = sessions_.find(handle);
        if (found == sessions_.end()) return;
        retiring = std::move(found->second);
        sessions_.erase(found);
    } // expensive final destruction must not hold the registry mutex
}
} // namespace editor
