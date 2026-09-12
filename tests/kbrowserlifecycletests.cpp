#include <lightoverleaf/platform/kbrowserlifecycle.h>
#include <iostream>

using namespace lightoverleaf;
int main()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        if (!condition) { std::cerr << name << '\n'; ++failures; }
    };
    KBrowserLifecycle normal;
    check(!normal.ready(), "cannot become ready before start");
    check(!normal.start(-1), "reject negative initial clock");
    check(normal.start(0), "start");
    check(!normal.start(1), "reject duplicate start");
    check(normal.ready(), "initial ready");
    check(!normal.ready(), "ignore duplicate ready");
    check(normal.tick(100000) == KBrowserAction::None, "ready has no startup timeout");
    check(normal.requestClose(100000) == KBrowserAction::Close, "close once");
    check(normal.requestClose(100001) == KBrowserAction::None, "duplicate close does not reset deadline");
    check(!normal.ready(), "late ready cannot cancel closing");
    check(normal.rendererFailed(100002) == KBrowserAction::None, "no reload during close");
    normal.closed();
    check(normal.tick(999999) == KBrowserAction::None, "closed never times out");

    KBrowserLifecycle timeout;
    timeout.start(10);
    check(timeout.tick(9) == KBrowserAction::None, "ignore backward clock");
    check(timeout.tick(20009) == KBrowserAction::None, "not timed out before boundary");
    check(timeout.tick(20010) == KBrowserAction::Close, "startup timeout closes browser");
    check(timeout.tick(30009) == KBrowserAction::None, "close grace period");
    check(timeout.tick(30010) == KBrowserAction::EmergencyExit, "bounded close timeout");
    check(timeout.tick(40000) == KBrowserAction::None, "emergency action only once");

    KBrowserLifecycle recovery;
    recovery.start(0);
    recovery.ready();
    check(recovery.rendererFailed(100) == KBrowserAction::Reload, "first crash reloads");
    check(recovery.ready(), "ready after recovery");
    check(recovery.rendererFailed(200) == KBrowserAction::Reload, "second crash reloads");
    recovery.ready();
    check(recovery.rendererFailed(300) == KBrowserAction::Close, "third crash closes without restart loop");
    check(recovery.recoveries() == 2, "bounded lifetime recovery budget");

    KBrowserLifecycle stuckRecovery;
    stuckRecovery.start(0);
    stuckRecovery.ready();
    stuckRecovery.rendererFailed(100);
    check(stuckRecovery.tick(20100) == KBrowserAction::Close, "recovery readiness timeout");
    return failures == 0 ? 0 : 1;
}
