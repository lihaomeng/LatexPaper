#include <lightoverleaf/search/inbound/iksearch.h>
#include <lightoverleaf/search/outbound/iksearchsource.h>
#include <lightoverleaf/search/domain/ksearchpolicy.h>
#include <algorithm>
#include <cctype>

namespace lightoverleaf::search
{
namespace
{
std::string folded(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char byte)
    {
        return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : static_cast<char>(byte);
    });
    return value;
}
}

class KSearch final : public IKSearch
{
public:
    explicit KSearch(std::shared_ptr<IKSearchSource> source) : m_source(std::move(source)) {}
    KResult<KSearchResult> run(const KSearchCommand& command, std::stop_token stop) override
    {
        if (!validSearchText(command.m_query) || command.m_maxResults == 0 ||
            command.m_maxResults > kMaxSearchResults)
            return KError{KErrorCode::InvalidArgument, "search.invalidArgument", false};
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "search.cancelled", true};
        KResult<std::vector<KSearchDocument>> loaded = m_source->readAll(stop);
        if (const KError* error = std::get_if<KError>(&loaded)) return *error;
        auto documents = std::get<std::vector<KSearchDocument>>(std::move(loaded));
        if (documents.size() > kMaxSearchDocuments)
            return KError{KErrorCode::ResourceExhausted, "search.tooManyDocuments", false};
        KSearchResult result;
        const std::string needle = command.m_caseSensitive ? command.m_query : folded(command.m_query);
        for (KSearchDocument& document : documents)
        {
            if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "search.cancelled", true};
            if (document.m_fileId.empty() || document.m_fileId.size() > 4096 ||
                !validSearchDocumentText(document.m_fileId) || !validSearchDocumentText(document.m_content))
                return KError{KErrorCode::Internal, "search.sourceFailure", false};
            std::size_t lineNumber = 1;
            for (std::size_t start = 0; start <= document.m_content.size();)
            {
                const std::size_t end = document.m_content.find('\n', start);
                const std::size_t length = (end == std::string::npos ? document.m_content.size() : end) - start;
                std::string line = document.m_content.substr(start, length);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string haystack = command.m_caseSensitive ? line : folded(line);
                std::size_t position = 0;
                while ((position = haystack.find(needle, position)) != std::string::npos)
                {
                    if (result.m_hits.size() == command.m_maxResults)
                    {
                        result.m_truncated = true;
                        return result;
                    }
                    std::string preview = line;
                    if (preview.size() > 512)
                    {
                        preview.resize(512);
                        while (!preview.empty() && !validSearchDocumentText(preview)) preview.pop_back();
                    }
                    result.m_hits.push_back({document.m_fileId, lineNumber, position + 1, std::move(preview)});
                    position += std::max<std::size_t>(needle.size(), 1);
                }
                if (end == std::string::npos) break;
                start = end + 1;
                ++lineNumber;
            }
        }
        return result;
    }
private:
    std::shared_ptr<IKSearchSource> m_source;
};

std::unique_ptr<IKSearch> createSearch(std::shared_ptr<IKSearchSource> source)
{
    return source ? std::make_unique<KSearch>(std::move(source)) : nullptr;
}
}
