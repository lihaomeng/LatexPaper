#pragma once
#include <lightoverleaf/document/outbound/ikdocumentstore.h>
#include <cstddef>
#include <memory>

namespace lightoverleaf::document
{
struct KLocalDocumentStoreOptions
{
    std::string m_rootUtf8;
    std::size_t m_maxBytes = 4 * 1024 * 1024;
};
// Factory validates and canonicalizes the root. Construction of the returned store performs no I/O.
KResult<std::shared_ptr<IKDocumentStore>> createLocalDocumentStore(const KLocalDocumentStoreOptions& options);
}
