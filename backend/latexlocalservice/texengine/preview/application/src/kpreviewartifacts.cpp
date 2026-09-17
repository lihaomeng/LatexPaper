#include <lightoverleaf/preview/inbound/ikpreviewartifacts.h>
#include <lightoverleaf/preview/outbound/ikartifactstore.h>
#include <lightoverleaf/preview/domain/kpreviewpolicy.h>
#include <algorithm>

namespace lightoverleaf::preview
{
class KPreviewArtifacts final : public IKPreviewArtifacts
{
public:
    explicit KPreviewArtifacts(std::shared_ptr<IKArtifactStore> store) : m_store(std::move(store)) {}
    KResult<KPreviewDescriptor> publish(const std::string& jobId, std::span<const std::uint8_t> pdf,
        std::span<const std::uint8_t> syncTex) override
    {
        if (!validArtifactToken(jobId) || !validPdf(pdf))
            return KError{KErrorCode::InvalidArgument, "preview.invalidArtifact", false};
        if (pdf.size() > kMaxPreviewPdfBytes || syncTex.size() > kMaxPreviewSyncTexBytes)
            return KError{KErrorCode::ResourceExhausted, "preview.artifactTooLarge", false};
        KResult<KStoredArtifact> stored = m_store->put(jobId, pdf, syncTex);
        if (const KError* error = std::get_if<KError>(&stored)) return *error;
        KStoredArtifact value = std::get<KStoredArtifact>(std::move(stored));
        return KPreviewDescriptor{std::move(value.m_id), value.m_pdfBytes, value.m_hasSyncTex};
    }
    KResult<KPreviewChunk> readPdf(const std::string& id, std::size_t offset, std::size_t count) const override
    {
        if (!validArtifactToken(id) || count == 0 || count > kMaxPreviewChunkBytes)
            return KError{KErrorCode::InvalidArgument, "preview.invalidRead", false};
        KResult<std::vector<std::uint8_t>> loaded = m_store->readPdf(id);
        if (const KError* error = std::get_if<KError>(&loaded)) return *error;
        std::vector<std::uint8_t> bytes = std::get<std::vector<std::uint8_t>>(std::move(loaded));
        if (offset > bytes.size()) return KError{KErrorCode::InvalidArgument, "preview.invalidOffset", false};
        const std::size_t end = std::min(bytes.size(), offset + count);
        KPreviewChunk chunk{offset, bytes.size(), {}};
        chunk.m_bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(end));
        return chunk;
    }
    KResult<std::vector<std::uint8_t>> readPdfAll(const std::string& id) const override
    {
        if (!validArtifactToken(id))
            return KError{KErrorCode::InvalidArgument, "preview.invalidArtifact", false};
        return m_store->readPdf(id);
    }
    KResult<std::vector<std::uint8_t>> readSyncTex(const std::string& id) const override
    {
        if (!validArtifactToken(id)) return KError{KErrorCode::InvalidArgument, "preview.invalidArtifact", false};
        return m_store->readSyncTex(id);
    }
private:
    std::shared_ptr<IKArtifactStore> m_store;
};
std::shared_ptr<IKPreviewArtifacts> createPreviewArtifacts(std::shared_ptr<IKArtifactStore> store)
{
    return store ? std::make_shared<KPreviewArtifacts>(std::move(store)) : nullptr;
}
}
