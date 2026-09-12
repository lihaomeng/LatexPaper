#pragma once
#include <lightoverleaf/search/outbound/iksearchsource.h>
#include <memory>
#include <string>

namespace lightoverleaf::search
{
struct KLocalSearchSourceOptions
{
    std::string m_rootUtf8;
};
KResult<std::shared_ptr<IKSearchSource>> createLocalSearchSource(const KLocalSearchSourceOptions& options);
}
