#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <stop_token>

namespace lightoverleaf::document
{
struct KStoredDocument
{
    std::string m_content;
    std::string m_revision;
    bool m_utf8Bom = false;
};
class IKDocumentStore
{
public:
    virtual ~IKDocumentStore() = default;
    // Worker-only. Read at most the configured document byte limit.
    virtual KResult<KStoredDocument> read(const std::string& fileId, std::stop_token stop) = 0;
    // Compare and replace is ONE store operation, never read-then-write in the use case.
    // On failure before commit the original file is unchanged. After commit return success,
    // even if cancellation arrives late, so callers do not retry an already committed write.
    virtual KResult<KStoredDocument> replace(const std::string& fileId, const std::string& content,
        const std::string& expectedRevision, std::stop_token stop) = 0;
    // Create is one exclusive store operation. It must never overwrite an existing target
    // and must remove temporary output if cancellation or an I/O failure happens before commit.
    virtual KResult<KStoredDocument> createExclusive(const std::string& fileId,
        const std::string& content, bool utf8Bom, std::stop_token stop) = 0;
};
}
