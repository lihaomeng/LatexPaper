#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/document/outbound/ikdocumentstore.h>
#include <lightoverleaf/document/domain/kdocumentpolicy.h>

namespace lightoverleaf::document
{
namespace
{
KError invalid() { return {KErrorCode::InvalidArgument, "document.invalidArgument", false}; }
KError cancelled() { return {KErrorCode::Cancelled, "document.cancelled", true}; }
KError internal() { return {KErrorCode::Internal, "document.storeFailure", false}; }
bool validStored(const KStoredDocument& value)
{
    return value.m_content.size() <= kMaxDocumentBytes && validUtf8(value.m_content) && validRevision(value.m_revision);
}
}
class KDocuments final : public IKDocuments
{
public:
    explicit KDocuments(std::shared_ptr<IKDocumentStore> store) : m_store(std::move(store)) {}
    KResult<KDocumentSnapshot> open(const std::string& fileId, std::stop_token stop) override
    {
        if (!validFileId(fileId)) return invalid();
        if (stop.stop_requested()) return cancelled();
        try
        {
            auto result = m_store->read(fileId, stop);
            if (const auto* error = std::get_if<KError>(&result)) return *error;
            auto& value = std::get<KStoredDocument>(result);
            if (!validStored(value)) return internal();
            if (stop.stop_requested()) return cancelled();
            return KDocumentSnapshot{fileId, std::move(value.m_content), std::move(value.m_revision), value.m_utf8Bom};
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<KDocumentSaved> save(const KSaveDocument& command, std::stop_token stop) override
    {
        if (!validFileId(command.m_fileId) || !validRevision(command.m_expectedRevision) ||
            command.m_clientSequence > 9007199254740991ULL) return invalid();
        if (command.m_content.size() > kMaxDocumentBytes)
            return KError{KErrorCode::ResourceExhausted, "document.tooLarge", false};
        if (!validUtf8(command.m_content)) return KError{KErrorCode::InvalidEncoding, "document.invalidEncoding", false};
        if (stop.stop_requested()) return cancelled();
        try
        {
            auto result = m_store->replace(command.m_fileId, command.m_content, command.m_expectedRevision, stop);
            if (const auto* error = std::get_if<KError>(&result)) return *error;
            auto& value = std::get<KStoredDocument>(result);
            if (!validStored(value) || value.m_content != command.m_content) return internal();
            // A successful commit must not be changed into a late cancellation failure.
            return KDocumentSaved{command.m_fileId, std::move(value.m_revision), command.m_clientSequence};
        }
        catch (...)
        {
            return internal();
        }
    }
    KResult<KDocumentSaved> saveAs(const KSaveDocumentAs& command, std::stop_token stop) override
    {
        if (!validFileId(command.m_fileId) || command.m_clientSequence > 9007199254740991ULL)
            return invalid();
        if (command.m_content.size() > kMaxDocumentBytes)
            return KError{KErrorCode::ResourceExhausted, "document.tooLarge", false};
        if (!validUtf8(command.m_content))
            return KError{KErrorCode::InvalidEncoding, "document.invalidEncoding", false};
        if (stop.stop_requested()) return cancelled();
        try
        {
            KResult<KStoredDocument> result = m_store->createExclusive(
                command.m_fileId, command.m_content, command.m_utf8Bom, stop);
            if (const KError* error = std::get_if<KError>(&result)) return *error;
            KStoredDocument& value = std::get<KStoredDocument>(result);
            if (!validStored(value) || value.m_content != command.m_content ||
                value.m_utf8Bom != command.m_utf8Bom)
                return internal();
            return KDocumentSaved{command.m_fileId, std::move(value.m_revision),
                command.m_clientSequence};
        }
        catch (...)
        {
            return internal();
        }
    }

private:
    std::shared_ptr<IKDocumentStore> m_store;
};
std::unique_ptr<IKDocuments> createDocuments(std::shared_ptr<IKDocumentStore> store)
{
    if (!store) return nullptr;
    return std::make_unique<KDocuments>(std::move(store));
}
}
