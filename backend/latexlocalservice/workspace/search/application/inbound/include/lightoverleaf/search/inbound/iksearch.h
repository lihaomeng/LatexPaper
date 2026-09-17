#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace lightoverleaf::search
{
class IKSearchSource;
struct KSearchCommand
{
    std::string m_query;
    bool m_caseSensitive = false;
    std::size_t m_maxResults = 200;
};
struct KSearchHit
{
    std::string m_fileId;
    std::size_t m_line = 1;
    std::size_t m_column = 1;
    std::string m_preview;
};
struct KSearchResult
{
    std::vector<KSearchHit> m_hits;
    bool m_truncated = false;
};
class IKSearch
{
public:
    virtual ~IKSearch() = default;
    virtual KResult<KSearchResult> run(const KSearchCommand& command, std::stop_token stop = {}) = 0;
};
std::unique_ptr<IKSearch> createSearch(std::shared_ptr<IKSearchSource> source);
}
