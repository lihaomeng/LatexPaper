#include <lightoverleaf/platform/ikworkerexecutor.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace lightoverleaf;

namespace
{
bool waitUntil(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!condition() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return condition();
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    check(!createWorkerExecutor(0, 1) && !createWorkerExecutor(1, 0) &&
        !createWorkerExecutor(1025, 1) && !createWorkerExecutor(1, 17), "bounds");
    auto executor = createWorkerExecutor(2, 1);
    check(static_cast<bool>(executor), "factory");
    std::atomic_bool firstStarted = false;
    std::atomic_bool firstStopped = false;
    std::atomic_bool secondRan = false;
    const auto first = executor->submit([&](std::stop_token stop)
    {
        firstStarted = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        firstStopped = true;
    });
    check(first && waitUntil([&] { return firstStarted.load(); }), "first started");
    const auto second = executor->submit([&](std::stop_token) { secondRan = true; });
    check(second && executor->pending() == 2, "running and queued count");
    check(!executor->submit([](std::stop_token) {}), "capacity");
    check(executor->cancel(*first) && !executor->cancel(*first), "cancellation is single transition");
    check(waitUntil([&] { return firstStopped.load() && secondRan.load() && executor->pending() == 0; }),
        "cancelled running task releases queued task");
    check(!executor->cancel(*first), "completed id rejected");
    std::atomic_bool afterThrow = false;
    check(executor->submit([](std::stop_token) { throw 1; }).has_value(), "throwing task admitted");
    check(executor->submit([&](std::stop_token) { afterThrow = true; }).has_value(), "post-throw task admitted");
    check(waitUntil([&] { return afterThrow.load() && executor->pending() == 0; }), "exception isolated");
    std::atomic_bool shutdownStarted = false;
    std::atomic_bool shutdownStopped = false;
    check(executor->submit([&](std::stop_token stop)
    {
        shutdownStarted = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        shutdownStopped = true;
    }).has_value(), "shutdown task admitted");
    check(waitUntil([&] { return shutdownStarted.load(); }), "shutdown task started");
    executor->close();
    check(shutdownStopped && executor->pending() == 0, "close requests stop and joins");
    executor->close();
    check(!executor->submit([](std::stop_token) {}) && !executor->cancel(999), "closed executor rejects work");
    return failures == 0 ? 0 : 1;
}
