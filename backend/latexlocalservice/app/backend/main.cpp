#include <lightoverleaf/desktop/kapplicationcomposition.h>
#include <lightoverleaf/transport/kjsontransport.h>
#include <array>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

namespace
{
using namespace lightoverleaf;
using KValue = rpc::KValue;
struct KDispatchQueue
{
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::function<void()>> m_tasks;
    std::size_t m_bytes = 0;
    bool m_eof = false;
};

bool writeFrame(const KValue& value)
{
    const auto wire = writeJsonValue(value);
    if (!wire) return false;
    const auto length = static_cast<std::uint32_t>(wire->size());
    std::array<char, 4> header{};
    for (unsigned int index = 0; index < 4; ++index)
        header[index] = static_cast<char>((length >> (index * 8)) & 255);
    std::cout.write(header.data(), header.size());
    std::cout.write(wire->data(), static_cast<std::streamsize>(wire->size()));
    std::cout.flush();
    return std::cout.good();
}

int run(const std::string& sessionId)
{
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
    KDispatchQueue queue;
    std::map<std::string, std::optional<std::string>> selections;
    std::map<std::int64_t, std::string> selectedQueries;
    auto composition = createApplicationComposition([] { return std::optional<std::string>{}; }, {},
        [&selections](const std::string& id)
        {
            const auto found = selections.find(id);
            return found == selections.end() ? std::optional<std::string>{} : found->second;
        });
    if (!composition) return 3;
    const KNativeSchedule schedule = [&queue](std::function<void()> task)
    {
        std::lock_guard<std::mutex> lock(queue.m_mutex);
        if (queue.m_eof || queue.m_tasks.size() >= 256) return false;
        queue.m_tasks.push_back(std::move(task));
        queue.m_ready.notify_one();
        return true;
    };
    auto endpoint = composition->endpointFactory()(schedule, sessionId);
    if (!endpoint) return 4;
    const auto consume = [&](std::string wire)
    {
        auto parsed = parseJsonValue(wire);
        const auto* object = parsed ? std::get_if<KValue::KObject>(&parsed->m_value) : nullptr;
        if (!object) return;
        const auto query = object->find("queryId");
        const auto operation = object->find("op");
        const double* number = query == object->end() ? nullptr : std::get_if<double>(&query->second.m_value);
        const std::string* op = operation == object->end() ? nullptr : std::get_if<std::string>(&operation->second.m_value);
        if (!number || *number < 1 || *number > 2147483647 || std::floor(*number) != *number || !op) return;
        const auto id = static_cast<std::int64_t>(*number);
        if (*op == "cancel")
        {
            if (const auto selected = selectedQueries.find(id); selected != selectedQueries.end())
            {
                selections.erase(selected->second);
                selectedQueries.erase(selected);
            }
            endpoint->cancel(id);
            return;
        }
        const auto send = [id](KNativeMessageReply reply)
        {
            if (!writeFrame(KValue{KValue::KObject{
                {"queryId", KValue{static_cast<double>(id)}},
                {"success", KValue{reply.m_success}}, {"payload", KValue{std::move(reply.m_payload)}}}}))
            {
                writeFrame(KValue{KValue::KObject{
                    {"queryId", KValue{static_cast<double>(id)}}, {"success", KValue{false}},
                    {"payload", KValue{std::string("RESOURCE_EXHAUSTED")}}}});
            }
        };
        const auto request = object->find("request");
        const auto persistent = object->find("persistent");
        const std::string* text = request == object->end() ? nullptr : std::get_if<std::string>(&request->second.m_value);
        const bool* flag = persistent == object->end() ? nullptr : std::get_if<bool>(&persistent->second.m_value);
        if (*op != "request" || !text || !flag || object->size() > 5)
        {
            send({false, "INVALID_ARGUMENT"});
            return;
        }
        std::string requestId;
        const auto body = parseJsonValue(*text);
        if (body)
        {
            const auto* fields = std::get_if<KValue::KObject>(&body->m_value);
            if (fields)
            {
                const auto found = fields->find("id");
                if (found != fields->end())
                    if (const auto* value = std::get_if<std::string>(&found->second.m_value)) requestId = *value;
            }
        }
        if (requestId.empty() || selectedQueries.contains(id))
        {
            send({false, "REQUEST_DENIED"});
            return;
        }
        std::optional<std::string> selection;
        if (const auto found = object->find("selection"); found != object->end())
        {
            if (const auto* value = std::get_if<std::string>(&found->second.m_value))
            {
                if (value->size() > 131072) { send({false, "INVALID_ARGUMENT"}); return; }
                selection = *value;
            }
            else if (!std::holds_alternative<std::nullptr_t>(found->second.m_value))
            {
                send({false, "INVALID_ARGUMENT"});
                return;
            }
        }
        const bool selected = selection.has_value();
        if (selected)
        {
            if (*flag || selections.size() >= 64 || selections.contains(requestId))
            {
                send({false, "REQUEST_DENIED"});
                return;
            }
            selections.emplace(requestId, std::move(selection));
            selectedQueries.emplace(id, requestId);
        }
        endpoint->request(id, *text, *flag, [&, id, selected, requestId, send](KNativeMessageReply reply)
        {
            if (selected)
            {
                selections.erase(requestId);
                selectedQueries.erase(id);
            }
            send(std::move(reply));
        });
    };
    std::thread reader([&]
    {
        try
        {
            while (true)
            {
                std::array<unsigned char, 4> header{};
                if (!std::cin.read(reinterpret_cast<char*>(header.data()), header.size())) break;
                std::uint32_t length = 0;
                for (unsigned int index = 0; index < 4; ++index)
                    length |= static_cast<std::uint32_t>(header[index]) << (index * 8);
                if (length == 0 || length > kMaxPipeBytes) break;
                {
                    std::unique_lock<std::mutex> lock(queue.m_mutex);
                    queue.m_ready.wait(lock, [&] { return queue.m_tasks.size() < 128 &&
                        queue.m_bytes + length <= 2 * kMaxPipeBytes; });
                    queue.m_bytes += length;
                }
                std::string wire(length, '\0');
                if (!std::cin.read(wire.data(), length)) break;
                {
                    std::lock_guard<std::mutex> lock(queue.m_mutex);
                    queue.m_tasks.push_back([&, length, wire = std::move(wire)]() mutable
                    {
                        {
                            std::lock_guard<std::mutex> taskLock(queue.m_mutex);
                            queue.m_bytes -= length;
                        }
                        queue.m_ready.notify_all();
                        consume(std::move(wire));
                    });
                }
                queue.m_ready.notify_all();
            }
        }
        catch (...)
        {
            // EOF/failure follows the same cancellation and cleanup path.
        }
        {
            std::lock_guard<std::mutex> lock(queue.m_mutex);
            queue.m_eof = true;
        }
        queue.m_ready.notify_all();
    });
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue.m_mutex);
            queue.m_ready.wait(lock, [&] { return queue.m_eof || !queue.m_tasks.empty(); });
            if (queue.m_eof) break;
            task = std::move(queue.m_tasks.front());
            queue.m_tasks.pop_front();
        }
        queue.m_ready.notify_all();
        try { task(); }
        catch (...) { std::cerr << "Backend dispatch failed\n"; }
    }
    reader.join();
    endpoint->close();
    composition->close();
    return 0;
}
}
int main(int argc, char* argv[])
{
    if (argc != 2 || !argv[1]) return 2;
    try { return run(argv[1]); }
    catch (...) { std::cerr << "Backend startup failed\n"; return 1; }
}
