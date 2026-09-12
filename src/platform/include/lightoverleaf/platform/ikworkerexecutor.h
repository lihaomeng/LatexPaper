#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

namespace lightoverleaf
{
using KWorkerTask = std::function<void(std::stop_token)>;
class IKWorkerExecutor
{
public:
    virtual ~IKWorkerExecutor() = default;
    virtual std::optional<std::uint64_t> submit(KWorkerTask task) = 0;
    virtual bool cancel(std::uint64_t taskId) = 0;
    virtual std::size_t pending() const = 0;
    // Shutdown-only: cooperative running tasks must honor stop requests.
    virtual void close() noexcept = 0;
};
std::unique_ptr<IKWorkerExecutor> createWorkerExecutor(std::size_t capacity, unsigned int threadCount);
}
