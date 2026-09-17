#pragma once
#include <lightoverleaf/export/outbound/ikexportstore.h>
namespace lightoverleaf::exporting {
std::shared_ptr<IKExportStore> createLocalExportStore();
}
