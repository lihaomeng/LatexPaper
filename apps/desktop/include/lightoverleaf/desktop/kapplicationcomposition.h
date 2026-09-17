#pragma once
#include <lightoverleaf/platform/knativemessage.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lightoverleaf
{
using KNativeSelectionLookup = std::function<std::optional<std::string>(const std::string&)>;
using KWorkspacePicker = std::function<std::optional<std::string>()>;

class KApplicationComposition
{
public:
    class KImpl;
    explicit KApplicationComposition(std::shared_ptr<KImpl> impl);
    ~KApplicationComposition();
    KApplicationComposition(const KApplicationComposition&) = delete;
    KApplicationComposition& operator=(const KApplicationComposition&) = delete;
    KNativeEndpointFactory endpointFactory() const;
    void close() noexcept;

private:
    std::shared_ptr<KImpl> m_impl;
};

std::unique_ptr<KApplicationComposition> createApplicationComposition(KWorkspacePicker workspacePicker,
    KWorkspacePicker exportPicker = {}, KNativeSelectionLookup selectionLookup = {});
}
