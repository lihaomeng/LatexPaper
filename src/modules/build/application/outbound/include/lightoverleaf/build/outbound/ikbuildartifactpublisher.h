#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <span>
#include <string>

namespace lightoverleaf::build
{
struct KPublishedBuildArtifact
{
    std::string m_artifactId;
    bool m_syncTexAvailable = false;
};
class IKBuildArtifactPublisher
{
public:
    virtual ~IKBuildArtifactPublisher() = default;
    virtual KResult<KPublishedBuildArtifact> publish(const std::string& jobId,
        std::span<const std::uint8_t> pdf, std::span<const std::uint8_t> syncTex) = 0;
};
}
