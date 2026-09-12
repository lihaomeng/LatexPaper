#include <lightoverleaf/preview/adapters/klocalartifactstore.h>
#include <lightoverleaf/preview/domain/kpreviewpolicy.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <random>

namespace lightoverleaf::preview
{
namespace fs = std::filesystem;
namespace
{
std::wstring fromUtf8(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size) == size ? result : std::wstring{};
}
KResult<std::vector<std::uint8_t>> readFile(const fs::path& path, std::size_t limit)
{
    std::error_code error; const std::uintmax_t size = fs::file_size(path, error);
    if (error) return KError{KErrorCode::NotFound, "preview.artifactNotFound", false};
    if (size > limit) return KError{KErrorCode::ResourceExhausted, "preview.artifactTooLarge", false};
    std::ifstream input(path, std::ios::binary); if (!input) return KError{KErrorCode::Unavailable, "preview.readFailure", true};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return KError{KErrorCode::Unavailable, "preview.readFailure", true}; return bytes;
}
bool writeAtomic(const fs::path& target, std::span<const std::uint8_t> bytes)
{
    const fs::path partial = target.wstring() + L".partial";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close(); if (!output) { std::error_code e; fs::remove(partial, e); return false; }
    std::error_code error; fs::rename(partial, target, error); if (error) { fs::remove(partial, error); return false; } return true;
}
}
class KLocalArtifactStore final : public IKArtifactStore
{
public:
    explicit KLocalArtifactStore(fs::path root) : m_root(std::move(root)) {}
    KResult<KStoredArtifact> put(const std::string& jobId, std::span<const std::uint8_t> pdf,
        std::span<const std::uint8_t> syncTex) override
    {
        std::random_device random;
        const std::string id = jobId + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(random());
        const fs::path directory = m_root / fromUtf8(id); std::error_code error;
        if (!fs::create_directory(directory, error) || error) return KError{KErrorCode::Unavailable, "preview.writeFailure", true};
        if (!writeAtomic(directory / L"document.pdf", pdf) || (!syncTex.empty() && !writeAtomic(directory / L"document.synctex.gz", syncTex)))
        { fs::remove_all(directory, error); return KError{KErrorCode::Unavailable, "preview.writeFailure", true}; }
        return KStoredArtifact{id, pdf.size(), !syncTex.empty()};
    }
    KResult<std::vector<std::uint8_t>> readPdf(const std::string& id) const override
    { return readFile(m_root / fromUtf8(id) / L"document.pdf", kMaxPreviewPdfBytes); }
    KResult<std::vector<std::uint8_t>> readSyncTex(const std::string& id) const override
    { return readFile(m_root / fromUtf8(id) / L"document.synctex.gz", kMaxPreviewSyncTexBytes); }
private: fs::path m_root;
};
KResult<std::shared_ptr<IKArtifactStore>> createLocalArtifactStore(const std::string& rootUtf8)
{
    const std::wstring wide = fromUtf8(rootUtf8); if (wide.empty()) return KError{KErrorCode::InvalidArgument, "preview.invalidRoot", false};
    std::error_code error; fs::create_directories(wide, error); if (error) return KError{KErrorCode::Unavailable, "preview.cacheFailure", true};
    fs::path root = fs::canonical(wide, error); if (error) return KError{KErrorCode::Unavailable, "preview.cacheFailure", true};
    return std::shared_ptr<IKArtifactStore>(std::make_shared<KLocalArtifactStore>(std::move(root)));
}
}
