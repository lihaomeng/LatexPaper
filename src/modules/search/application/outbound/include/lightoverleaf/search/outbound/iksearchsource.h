#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <stop_token>
#include <string>
#include <vector>

namespace lightoverleaf::search
{
struct KSearchDocument
{
    std::string m_fileId;
    std::string m_content;
};
class IKSearchSource
{
public:
    virtual ~IKSearchSource() = default;
    virtual KResult<std::vector<KSearchDocument>> readAll(std::stop_token stop) = 0;
};
}
