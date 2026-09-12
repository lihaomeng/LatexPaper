#pragma once
#include <memory>
#include <optional>
#include <string>
namespace lightoverleaf
{
class IKBrowserSurface;
class KQtRuntime
{
public:
    KQtRuntime();
    ~KQtRuntime();
    KQtRuntime(const KQtRuntime&) = delete;
    KQtRuntime& operator=(const KQtRuntime&) = delete;
    std::string cachePath() const;
    std::string resourcePath() const;
    std::optional<std::string> selectWorkspace() const;
    int run(IKBrowserSurface& surface, bool smokeTest);

private:
    class KImpl;
    std::unique_ptr<KImpl> m_impl;
};
}
