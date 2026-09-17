#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace lightoverleaf::navigation {
struct KBackendSource { std::string m_fileId; std::size_t m_line=1; std::size_t m_column=1; };
struct KBackendPdf { std::size_t m_page=1; double m_x=0; double m_y=0; };
struct KSyncTexArtifact
{
    std::vector<std::uint8_t> m_pdf;
    std::vector<std::uint8_t> m_syncTex;
};
class IKSyncTexDataSource { public: virtual ~IKSyncTexDataSource()=default;
 virtual KResult<KSyncTexArtifact> read(const std::string&) const=0; };
class IKSyncTexBackend { public: virtual ~IKSyncTexBackend()=default;
 virtual KResult<KBackendPdf> forward(const KSyncTexArtifact&,const KBackendSource&) const=0;
 virtual KResult<KBackendSource> reverse(const KSyncTexArtifact&,const KBackendPdf&) const=0; };
}
