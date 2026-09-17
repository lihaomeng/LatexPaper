#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <stop_token>

namespace lightoverleaf::document
{
class IKDocumentStore;
struct KDocumentSnapshot
{
    std::string m_fileId;
    std::string m_content;
    std::string m_revision;
    bool m_utf8Bom = false;
};
struct KSaveDocument
{
    std::string m_fileId;
    std::string m_content;
    std::string m_expectedRevision;
    std::uint64_t m_clientSequence = 0;
};
struct KSaveDocumentAs
{
    std::string m_fileId;
    std::string m_content;
    bool m_utf8Bom = false;
    std::uint64_t m_clientSequence = 0;
};
struct KDocumentSaved
{
    std::string m_fileId;
    std::string m_revision;
    std::uint64_t m_clientSequence = 0;
};
class IKDocuments
{
public:
    virtual ~IKDocuments() = default;
    // Execute on an owned worker, not on a UI dispatcher.
    virtual KResult<KDocumentSnapshot> open(const std::string& fileId, std::stop_token stop = {}) = 0;
    virtual KResult<KDocumentSaved> save(const KSaveDocument& command, std::stop_token stop = {}) = 0;
    virtual KResult<KDocumentSaved> saveAs(const KSaveDocumentAs& command, std::stop_token stop = {}) = 0;
};
std::unique_ptr<IKDocuments> createDocuments(std::shared_ptr<IKDocumentStore> store);
}
