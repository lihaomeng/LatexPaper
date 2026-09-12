#pragma once
#include <lightoverleaf/kernel/kresult.h>
#include <memory>
#include <string>
namespace lightoverleaf::navigation {
class IKSyncTexDataSource; class IKSyncTexBackend;
struct KSourceLocation { std::string m_fileId; std::size_t m_line=1; std::size_t m_column=1; };
struct KPdfLocation { std::size_t m_page=1; double m_x=0; double m_y=0; };
class IKNavigation { public: virtual ~IKNavigation()=default;
 virtual KResult<KPdfLocation> forward(const std::string&,const KSourceLocation&) const=0;
 virtual KResult<KSourceLocation> reverse(const std::string&,const KPdfLocation&) const=0; };
std::shared_ptr<IKNavigation> createNavigation(std::shared_ptr<IKSyncTexDataSource>,std::shared_ptr<IKSyncTexBackend>);
}
