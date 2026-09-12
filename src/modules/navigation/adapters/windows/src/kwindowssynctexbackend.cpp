#include <lightoverleaf/navigation/adapters/kwindowssynctexbackend.h>
#include <lightoverleaf/navigation/domain/knavigationpolicy.h>

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

namespace lightoverleaf::navigation
{
namespace
{
namespace fs = std::filesystem;
constexpr std::size_t kMaxPdfBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxSyncTexBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaxOutputBytes = 64U * 1024U;
constexpr auto kProcessTimeout = std::chrono::seconds(5);

class KHandle final
{
public:
    KHandle() = default;
    explicit KHandle(HANDLE value) : m_value(value) {}
    ~KHandle()
    {
        close();
    }
    KHandle(const KHandle&) = delete;
    KHandle& operator=(const KHandle&) = delete;

    HANDLE get() const
    {
        return m_value;
    }
    HANDLE release()
    {
        HANDLE value = m_value;
        m_value = nullptr;
        return value;
    }
    void close()
    {
        if (m_value && m_value != INVALID_HANDLE_VALUE)
            CloseHandle(m_value);
        m_value = nullptr;
    }

private:
    HANDLE m_value = nullptr;
};

class KTemporaryDirectory final
{
public:
    explicit KTemporaryDirectory(fs::path path) : m_path(std::move(path)) {}
    ~KTemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    const fs::path& path() const
    {
        return m_path;
    }
    fs::path release()
    {
        return std::exchange(m_path, {});
    }

private:
    fs::path m_path;
};

std::wstring fromUtf8(const std::string& value)
{
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size) == size ? result : std::wstring{};
}

std::string toUtf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size ? result : std::string{};
}

std::wstring quote(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'\"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::optional<fs::path> findExecutable(const KWindowsSyncTexOptions& options)
{
    std::vector<fs::path> candidates;
    if (!options.m_executableUtf8.empty())
    {
        const std::wstring explicitPath = fromUtf8(options.m_executableUtf8);
        if (explicitPath.empty())
            return std::nullopt;
        candidates.emplace_back(explicitPath);
    }
    for (const std::string& rootValue : options.m_texRootsUtf8)
    {
        const std::wstring rootText = fromUtf8(rootValue);
        if (rootText.empty())
            continue;
        const fs::path root(rootText);
        candidates.push_back(root / L"synctex.exe");
        candidates.push_back(root / L"bin" / L"windows" / L"synctex.exe");
        candidates.push_back(root / L"bin" / L"win32" / L"synctex.exe");
        candidates.push_back(root / L"miktex" / L"bin" / L"x64" / L"synctex.exe");
    }
    for (const fs::path& candidate : candidates)
    {
        std::error_code error;
        if (!fs::is_regular_file(candidate, error))
            continue;
        const fs::path canonical = fs::weakly_canonical(candidate, error);
        if (!error)
            return canonical;
    }
    if (!options.m_executableUtf8.empty())
        return std::nullopt;
    const DWORD required = SearchPathW(nullptr, L"synctex.exe", nullptr, 0, nullptr, nullptr);
    if (required == 0)
        return std::nullopt;
    std::wstring buffer(static_cast<std::size_t>(required) + 1, L'\0');
    const DWORD written = SearchPathW(nullptr, L"synctex.exe", nullptr,
        static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size())
        return std::nullopt;
    buffer.resize(written);
    return fs::path(buffer);
}

void appendPipe(HANDLE pipe, std::string& output, bool& truncated)
{
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            return;
        char buffer[8192];
        DWORD read = 0;
        const DWORD requested = std::min<DWORD>(available, sizeof(buffer));
        if (!ReadFile(pipe, buffer, requested, &read, nullptr) || read == 0)
            return;
        const std::size_t remaining = output.size() < kMaxOutputBytes ?
            kMaxOutputBytes - output.size() : 0;
        output.append(buffer, std::min<std::size_t>(read, remaining));
        if (read > remaining)
            truncated = true;
    }
}

KResult<std::string> runProcess(const fs::path& executable,
    const std::vector<std::wstring>& arguments, const fs::path& workingDirectory)
{
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE readRaw = nullptr;
    HANDLE writeRaw = nullptr;
    if (!CreatePipe(&readRaw, &writeRaw, &attributes, 0))
        return KError{KErrorCode::Unavailable, "navigation.syncTexPipeFailure", true};
    KHandle readPipe(readRaw);
    KHandle writePipe(writeRaw);
    if (!SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0))
        return KError{KErrorCode::Unavailable, "navigation.syncTexPipeFailure", true};

    KHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.get())
        return KError{KErrorCode::Unavailable, "navigation.syncTexProcessFailure", true};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
        &limits, sizeof(limits)))
        return KError{KErrorCode::Unavailable, "navigation.syncTexProcessFailure", true};

    std::wstring commandLine = quote(executable.wstring());
    for (const std::wstring& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine.append(quote(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe.get();
    startup.hStdError = writePipe.get();
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(),
        &startup, &process))
        return KError{KErrorCode::Unavailable, "navigation.syncTexProcessFailure", true};
    KHandle processHandle(process.hProcess);
    KHandle threadHandle(process.hThread);
    if (!AssignProcessToJobObject(job.get(), processHandle.get()) ||
        ResumeThread(threadHandle.get()) == static_cast<DWORD>(-1))
    {
        TerminateProcess(processHandle.get(), 1);
        return KError{KErrorCode::Unavailable, "navigation.syncTexProcessFailure", true};
    }
    writePipe.close();

    std::string output;
    bool truncated = false;
    const auto deadline = std::chrono::steady_clock::now() + kProcessTimeout;
    for (;;)
    {
        appendPipe(readPipe.get(), output, truncated);
        if (WaitForSingleObject(processHandle.get(), 0) == WAIT_OBJECT_0)
            break;
        if (std::chrono::steady_clock::now() >= deadline)
        {
            TerminateJobObject(job.get(), 2);
            WaitForSingleObject(processHandle.get(), 2000);
            return KError{KErrorCode::Cancelled, "navigation.syncTexTimeout", true};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    appendPipe(readPipe.get(), output, truncated);
    if (truncated)
        return KError{KErrorCode::ResourceExhausted, "navigation.syncTexOutputTooLarge", false};
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processHandle.get(), &exitCode) || exitCode != 0)
        return KError{KErrorCode::Unavailable, "navigation.syncTexFailed", true};
    return output;
}

KResult<bool> writeBinary(const fs::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return KError{KErrorCode::Unavailable, "navigation.syncTexCacheFailure", true};
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!output)
        return KError{KErrorCode::Unavailable, "navigation.syncTexCacheFailure", true};
    return true;
}

std::optional<std::string> field(const std::string& output, std::string_view name)
{
    std::size_t start = 0;
    while (start <= output.size())
    {
        const std::size_t end = output.find('\n', start);
        std::string line = output.substr(start,
            (end == std::string::npos ? output.size() : end) - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.starts_with(name))
            return line.substr(name.size());
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return std::nullopt;
}

template<class TValue>
std::optional<TValue> parseNumber(const std::string& text)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    TValue value{};
    stream >> value;
    if (!stream)
        return std::nullopt;
    stream >> std::ws;
    if (stream.rdbuf()->sgetc() != std::char_traits<char>::eof())
        return std::nullopt;
    return value;
}

std::wstring number(double value)
{
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << value;
    return stream.str();
}

KResult<KBackendPdf> parseForward(const std::string& output)
{
    const std::optional<std::string> pageText = field(output, "Page:");
    const std::optional<std::string> xText = field(output, "x:");
    const std::optional<std::string> yText = field(output, "y:");
    if (!pageText || !xText || !yText)
        return KError{KErrorCode::NotFound, "navigation.mappingNotFound", false};
    const auto page = parseNumber<std::size_t>(*pageText);
    const auto x = parseNumber<double>(*xText);
    const auto y = parseNumber<double>(*yText);
    if (!page || !x || !y || *page == 0 || !std::isfinite(*x) ||
        !std::isfinite(*y) || *x < 0 || *y < 0)
        return KError{KErrorCode::Unavailable, "navigation.syncTexInvalidOutput", false};
    return KBackendPdf{*page, *x, *y};
}

KResult<KBackendSource> parseReverse(const std::string& output)
{
    const std::optional<std::string> inputText = field(output, "Input:");
    const std::optional<std::string> lineText = field(output, "Line:");
    const std::optional<std::string> columnText = field(output, "Column:");
    if (!inputText || !lineText)
        return KError{KErrorCode::NotFound, "navigation.mappingNotFound", false};
    std::string fileId = *inputText;
    std::replace(fileId.begin(), fileId.end(), '\\', '/');
    while (fileId.starts_with("./"))
        fileId.erase(0, 2);
    const auto line = parseNumber<std::size_t>(*lineText);
    const auto column = columnText ? parseNumber<std::size_t>(*columnText) :
        std::optional<std::size_t>{1};
    if (!validNavigationFileId(fileId) || fileId.find(':') != std::string::npos ||
        !line || !column || *line == 0 || *column == 0)
        return KError{KErrorCode::Unavailable, "navigation.syncTexInvalidOutput", false};
    return KBackendSource{std::move(fileId), *line, *column};
}

bool validArtifact(const KSyncTexArtifact& artifact)
{
    static constexpr std::uint8_t signature[] = {'%', 'P', 'D', 'F', '-'};
    return artifact.m_pdf.size() >= sizeof(signature) &&
        artifact.m_pdf.size() <= kMaxPdfBytes && !artifact.m_syncTex.empty() &&
        artifact.m_syncTex.size() <= kMaxSyncTexBytes &&
        std::equal(std::begin(signature), std::end(signature), artifact.m_pdf.begin());
}

KResult<fs::path> createQueryDirectory(const fs::path& root)
{
    static std::atomic_uint64_t sequence = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = root / (L"query-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(ticks) + L"-" +
        std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)));
    std::error_code error;
    if (!fs::create_directory(path, error) || error)
        return KError{KErrorCode::Unavailable, "navigation.syncTexCacheFailure", true};
    return path;
}
}

class KWindowsSyncTexBackend final : public IKSyncTexBackend
{
public:
    KWindowsSyncTexBackend(fs::path executable, fs::path cache)
        : m_executable(std::move(executable)), m_cache(std::move(cache)) {}

    KResult<KBackendPdf> forward(const KSyncTexArtifact& artifact,
        const KBackendSource& source) const override
    {
        if (!validArtifact(artifact) || !validNavigationFileId(source.m_fileId) ||
            source.m_line == 0 || source.m_column == 0)
            return KError{KErrorCode::InvalidArgument, "navigation.invalidArgument", false};
        const std::wstring file = fromUtf8(source.m_fileId);
        if (file.empty())
            return KError{KErrorCode::InvalidEncoding, "navigation.invalidEncoding", false};
        KResult<fs::path> prepared = prepare(artifact);
        if (const KError* error = std::get_if<KError>(&prepared))
            return *error;
        KTemporaryDirectory temporary(std::get<fs::path>(std::move(prepared)));
        const fs::path pdf = temporary.path() / L"document.pdf";
        const std::wstring query = std::to_wstring(source.m_line) + L":" +
            std::to_wstring(source.m_column) + L":" + file;
        KResult<std::string> output = runProcess(m_executable,
            {L"view", L"-i", query, L"-o", pdf.wstring()}, temporary.path());
        if (const KError* error = std::get_if<KError>(&output))
            return *error;
        return parseForward(std::get<std::string>(std::move(output)));
    }

    KResult<KBackendSource> reverse(const KSyncTexArtifact& artifact,
        const KBackendPdf& pdfLocation) const override
    {
        if (!validArtifact(artifact) || pdfLocation.m_page == 0 ||
            !std::isfinite(pdfLocation.m_x) || !std::isfinite(pdfLocation.m_y) ||
            pdfLocation.m_x < 0 || pdfLocation.m_y < 0)
            return KError{KErrorCode::InvalidArgument, "navigation.invalidArgument", false};
        KResult<fs::path> prepared = prepare(artifact);
        if (const KError* error = std::get_if<KError>(&prepared))
            return *error;
        KTemporaryDirectory temporary(std::get<fs::path>(std::move(prepared)));
        const fs::path pdf = temporary.path() / L"document.pdf";
        const std::wstring query = std::to_wstring(pdfLocation.m_page) + L":" +
            number(pdfLocation.m_x) + L":" + number(pdfLocation.m_y) + L":" +
            pdf.wstring();
        KResult<std::string> output = runProcess(m_executable,
            {L"edit", L"-o", query}, temporary.path());
        if (const KError* error = std::get_if<KError>(&output))
            return *error;
        return parseReverse(std::get<std::string>(std::move(output)));
    }

private:
    KResult<fs::path> prepare(const KSyncTexArtifact& artifact) const
    {
        KResult<fs::path> made = createQueryDirectory(m_cache);
        if (const KError* error = std::get_if<KError>(&made))
            return *error;
        fs::path directory = std::get<fs::path>(std::move(made));
        KTemporaryDirectory cleanup(directory);
        KResult<bool> pdf = writeBinary(directory / L"document.pdf", artifact.m_pdf);
        if (const KError* error = std::get_if<KError>(&pdf))
            return *error;
        KResult<bool> syncTex = writeBinary(directory / L"document.synctex.gz", artifact.m_syncTex);
        if (const KError* error = std::get_if<KError>(&syncTex))
            return *error;
        return cleanup.release();
    }

private:
    fs::path m_executable;
    fs::path m_cache;
};

KResult<std::shared_ptr<IKSyncTexBackend>> createWindowsSyncTexBackend(
    const KWindowsSyncTexOptions& options)
{
    try
    {
        const std::wstring cacheText = fromUtf8(options.m_cacheRootUtf8);
        if (cacheText.empty())
            return KError{KErrorCode::InvalidArgument, "navigation.invalidRoot", false};
        const std::optional<fs::path> executable = findExecutable(options);
        if (!executable)
            return KError{KErrorCode::Unavailable, "navigation.syncTexUnavailable", false};
        std::error_code error;
        fs::create_directories(cacheText, error);
        if (error)
            return KError{KErrorCode::Unavailable, "navigation.syncTexCacheFailure", true};
        const fs::path cache = fs::canonical(cacheText, error);
        if (error)
            return KError{KErrorCode::Unavailable, "navigation.syncTexCacheFailure", true};
        return std::shared_ptr<IKSyncTexBackend>(
            std::make_shared<KWindowsSyncTexBackend>(*executable, cache));
    }
    catch (...)
    {
        return KError{KErrorCode::Unavailable, "navigation.syncTexAdapterFailure", true};
    }
}
}
