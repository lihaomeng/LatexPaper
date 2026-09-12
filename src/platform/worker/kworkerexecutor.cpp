#include <lightoverleaf/platform/ikworkerexecutor.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace lightoverleaf
{
namespace
{
struct KWorkerJob
{
    std::uint64_t m_id = 0;
    KWorkerTask m_task;
    std::stop_source m_stop;
};

class KWorkerExecutor final : public IKWorkerExecutor
{
public:
    explicit KWorkerExecutor(std::size_t capacity) : m_capacity(capacity) {}
    ~KWorkerExecutor() override { close(); }
    bool start(unsigned int threadCount)
    {
        try
        {
            m_threads.reserve(threadCount);
            for (unsigned int index = 0; index < threadCount; ++index)
                m_threads.emplace_back([this](std::stop_token stop) { run(stop); });
            return true;
        }
        catch (...)
        {
            close();
            return false;
        }
    }
    std::optional<std::uint64_t> submit(KWorkerTask task) override
    {
        if (!task) return std::nullopt;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || m_jobs.size() >= m_capacity || m_nextId == 0) return std::nullopt;
        const std::uint64_t id = m_nextId++;
        auto job = std::make_shared<KWorkerJob>();
        job->m_id = id;
        job->m_task = std::move(task);
        m_jobs.emplace(id, job);
        m_queue.push_back(job);
        m_ready.notify_one();
        return id;
    }
    bool cancel(std::uint64_t taskId) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_jobs.find(taskId);
        if (found == m_jobs.end()) return false;
        return found->second->m_stop.request_stop();
    }
    std::size_t pending() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_jobs.size();
    }
    void close() noexcept override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed && m_threads.empty()) return;
            m_closed = true;
            for (const auto& [id, job] : m_jobs)
            {
                static_cast<void>(id);
                job->m_stop.request_stop();
            }
            m_queue.clear();
        }
        for (std::jthread& thread : m_threads) thread.request_stop();
        m_ready.notify_all();
        m_threads.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.clear();
    }

private:
    void run(std::stop_token executorStop)
    {
        while (!executorStop.stop_requested())
        {
            std::shared_ptr<KWorkerJob> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock, [this, executorStop]
                {
                    return m_closed || executorStop.stop_requested() || !m_queue.empty();
                });
                if ((m_closed || executorStop.stop_requested()) && m_queue.empty()) return;
                job = std::move(m_queue.front());
                m_queue.pop_front();
            }
            if (!job->m_stop.stop_requested())
            {
                try
                {
                    job->m_task(job->m_stop.get_token());
                }
                catch (...)
                {
                    // One untrusted task must not terminate the executor.
                }
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.erase(job->m_id);
        }
    }

private:
    const std::size_t m_capacity = 0;
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::shared_ptr<KWorkerJob>> m_queue;
    std::map<std::uint64_t, std::shared_ptr<KWorkerJob>> m_jobs;
    std::vector<std::jthread> m_threads;
    std::uint64_t m_nextId = 1;
    bool m_closed = false;
};
}

std::unique_ptr<IKWorkerExecutor> createWorkerExecutor(std::size_t capacity, unsigned int threadCount)
{
    if (capacity == 0 || capacity > 1024 || threadCount == 0 || threadCount > 16) return nullptr;
    auto executor = std::make_unique<KWorkerExecutor>(capacity);
    if (!executor->start(threadCount)) return nullptr;
    return executor;
}
}
