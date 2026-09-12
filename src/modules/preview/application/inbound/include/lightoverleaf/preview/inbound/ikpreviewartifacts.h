#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lightoverleaf::preview
{
class IKArtifactStore;
struct KPreviewDescriptor { std::string m_artifactId; std::size_t m_pdfBytes = 0; bool m_syncTexAvailable = false; };
struct KPreviewChunk { std::size_t m_offset = 0; std::size_t m_totalBytes = 0; std::vector<std::uint8_t> m_bytes; };
class IKPreviewArtifacts
{
public:
    virtual ~IKPreviewArtifacts() = default;
    virtual KResult<KPreviewDescriptor> publish(const std::string& jobId,
        std::span<const std::uint8_t> pdf, std::span<const std::uint8_t> syncTex) = 0;
    virtual KResult<KPreviewChunk> readPdf(const std::string& artifactId,
        std::size_t offset, std::size_t count) const = 0;
    virtual KResult<std::vector<std::uint8_t>> readPdfAll(
        const std::string& artifactId) const = 0;
    virtual KResult<std::vector<std::uint8_t>> readSyncTex(const std::string& artifactId) const = 0;
};
std::shared_ptr<IKPreviewArtifacts> createPreviewArtifacts(std::shared_ptr<IKArtifactStore> store);
}
