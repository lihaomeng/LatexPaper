#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lightoverleaf::preview
{
struct KStoredArtifact { std::string m_id; std::size_t m_pdfBytes = 0; bool m_hasSyncTex = false; };
class IKArtifactStore
{
public:
    virtual ~IKArtifactStore() = default;
    virtual KResult<KStoredArtifact> put(const std::string& jobId,
        std::span<const std::uint8_t> pdf, std::span<const std::uint8_t> syncTex) = 0;
    virtual KResult<std::vector<std::uint8_t>> readPdf(const std::string& id) const = 0;
    virtual KResult<std::vector<std::uint8_t>> readSyncTex(const std::string& id) const = 0;
};
}
