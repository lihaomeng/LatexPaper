#include <lightoverleaf/build/adapters/kwindowsbuildadapters.h>
#include <lightoverleaf/build/domain/kbuildpolicy.h>
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

namespace lightoverleaf::build
{
namespace fs = std::filesystem;
namespace
{
struct KHandle
{
    HANDLE value = nullptr;
    KHandle() = default;
    explicit KHandle(HANDLE handle) : value(handle) {}
    ~KHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    KHandle(const KHandle&) = delete;
    KHandle& operator=(const KHandle&) = delete;
};

std::wstring fromUtf8(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size) == size ? result : std::wstring{};
}
std::string toUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size ? result : std::string{};
}
KResult<std::vector<std::uint8_t>> readBoundedFile(const fs::path& path, std::size_t limit)
{
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) return KError{KErrorCode::NotFound, "build.artifactMissing", false};
    if (size > limit) return KError{KErrorCode::ResourceExhausted, "build.artifactTooLarge", false};
    std::ifstream input(path, std::ios::binary);
    if (!input) return KError{KErrorCode::Unavailable, "build.artifactReadFailure", true};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return KError{KErrorCode::Unavailable, "build.artifactReadFailure", true};
    return bytes;
}
std::string safeUtf8(const std::string& value, bool& truncated)
{
    std::string result;
    result.reserve(std::min(value.size(), kMaxBuildLogBytes));
    for (std::size_t index = 0; index < value.size();)
    {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        std::size_t count = first <= 0x7f ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        bool valid = count != 0 && index + count <= value.size();
        if (valid && count > 1)
        {
            for (std::size_t offset = 1; offset < count; ++offset)
            {
                const unsigned char next = static_cast<unsigned char>(value[index + offset]);
                if (next < 0x80 || next > 0xbf) valid = false;
            }
            const unsigned char second = static_cast<unsigned char>(value[index + 1]);
            if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||
                (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) valid = false;
        }
        const std::size_t encodedSize = valid ? count : 3;
        if (encodedSize > kMaxBuildLogBytes - result.size())
        {
            truncated = true;
            break;
        }
        if (valid) { result.append(value, index, count); index += count; }
        else { result.append("\xef\xbf\xbd"); ++index; }
    }
    return result;
}
bool belowOrEqual(const fs::path& root, const fs::path& path)
{
    const std::wstring left = root.native(), right = path.native();
    if (_wcsicmp(left.c_str(), right.c_str()) == 0) return true;
    return right.size() > left.size() && _wcsnicmp(left.c_str(), right.c_str(), left.size()) == 0 &&
        (right[left.size()] == L'\\' || right[left.size()] == L'/');
}
std::wstring engineName(KCompilerEngine engine)
{
    return engine == KCompilerEngine::PdfLatex ? L"pdflatex.exe" :
        engine == KCompilerEngine::XeLatex ? L"xelatex.exe" : L"lualatex.exe";
}
std::wstring quote(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
std::optional<fs::path> findNamedExecutable(const std::vector<fs::path>& roots,
    const std::wstring& name)
{
    for (const fs::path& root : roots)
    {
        for (const fs::path& candidate : {root / name, root / L"bin" / L"windows" / name,
             root / L"bin" / L"win32" / name, root / L"miktex" / L"bin" / L"x64" / name})
        {
            std::error_code error;
            if (fs::is_regular_file(candidate, error)) return fs::weakly_canonical(candidate, error);
        }
    }
    const DWORD required = SearchPathW(nullptr, name.c_str(), nullptr, 0, nullptr, nullptr);
    if (required == 0) return std::nullopt;
    std::wstring buffer(static_cast<std::size_t>(required) + 1, L'\0');
    const DWORD written = SearchPathW(nullptr, name.c_str(), nullptr,
        static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size()) return std::nullopt;
    buffer.resize(written);
    return fs::path(buffer);
}
std::optional<fs::path> findExecutable(const std::vector<fs::path>& roots,
    KCompilerEngine engine)
{
    return findNamedExecutable(roots, engineName(engine));
}
void appendPipe(HANDLE pipe, std::string& output, bool& truncated,
    const std::function<void(std::string_view, bool)>& onOutput)
{
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) return;
        char buffer[8192];
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr) || read == 0) return;
        const std::size_t remaining = kMaxBuildLogBytes - output.size();
        output.append(buffer, std::min<std::size_t>(read, remaining));
        if (read > remaining) truncated = true;
        if (onOutput)
        {
            bool conversionTruncated = false;
            const std::string safe = safeUtf8(output, conversionTruncated);
            onOutput(safe, truncated || conversionTruncated);
        }
    }
}
KCompilerDiagnosticSeverity severity(const std::string& message)
{
    const std::string lowered = [&]
    {
        std::string value = message;
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char byte)
        {
            return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') :
                static_cast<char>(byte);
        });
        return value;
    }();
    if (lowered.find("warning") != std::string::npos)
        return KCompilerDiagnosticSeverity::Warning;
    if (lowered.find("info") != std::string::npos)
        return KCompilerDiagnosticSeverity::Info;
    return KCompilerDiagnosticSeverity::Error;
}

std::vector<KCompilerDiagnostic> diagnostics(const std::string& output,
    const std::string& mainFileId)
{
    std::vector<KCompilerDiagnostic> result;
    std::size_t start = 0;
    while (start < output.size() && result.size() < 1000)
    {
        const std::size_t end = output.find('\n', start);
        std::string line = output.substr(start, (end == std::string::npos ? output.size() : end) - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t first = line.find(':');
        const std::size_t second = first == std::string::npos ? first : line.find(':', first + 1);
        if (first != std::string::npos && second != std::string::npos)
        {
            const std::string fileId = line.substr(0, first);
            const std::string number = line.substr(first + 1, second - first - 1);
            if (validBuildFileId(fileId) && !number.empty() &&
                std::all_of(number.begin(), number.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            {
                try
                {
                    const std::string message = line.substr(second + 1);
                    result.push_back({severity(message), fileId,
                        static_cast<std::size_t>(std::stoull(number)), message});
                }
                catch (...)
                {
                }
            }
        }
        else if (line.starts_with("!") || line.find("LaTeX Warning:") != std::string::npos ||
            line.find("Package ") != std::string::npos && line.find(" Warning:") != std::string::npos)
        {
            result.push_back({severity(line), mainFileId, 1, line});
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
}

class KLocalBuildSnapshotStore final : public IKBuildSnapshotStore
{
public:
    KLocalBuildSnapshotStore(fs::path workspace, fs::path cache)
        : m_workspace(std::move(workspace)), m_cache(std::move(cache)) {}
    KResult<bool> prepare(const std::string& snapshotId, std::stop_token stop) override
    {
        if (!validBuildToken(snapshotId)) return KError{KErrorCode::InvalidArgument, "build.invalidSnapshot", false};
        try
        {
            const fs::path final = m_cache / fromUtf8(snapshotId);
            const fs::path partial = m_cache / fromUtf8(snapshotId + ".partial");
            std::error_code error;
            if (fs::exists(final, error) || fs::exists(partial, error))
                return KError{KErrorCode::Conflict, "build.snapshotExists", false};
            fs::create_directory(partial, error);
            if (error) return KError{KErrorCode::Unavailable, "build.snapshotFailure", true};
            std::uintmax_t total = 0;
            for (fs::recursive_directory_iterator iterator(m_workspace,
                fs::directory_options::skip_permission_denied, error), end; iterator != end; iterator.increment(error))
            {
                if (stop.stop_requested()) { fs::remove_all(partial, error); return KError{KErrorCode::Cancelled, "build.cancelled", true}; }
                if (error) { fs::remove_all(partial, error); return KError{KErrorCode::Unavailable, "build.snapshotFailure", true}; }
                const fs::file_status status = iterator->symlink_status(error);
                const DWORD attributes = error ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(iterator->path().c_str());
                if (error || fs::is_symlink(status) || (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0))
                {
                    if (!error && attributes != INVALID_FILE_ATTRIBUTES &&
                        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) iterator.disable_recursion_pending();
                    error.clear(); continue;
                }
                const fs::path relative = fs::relative(iterator->path(), m_workspace, error);
                if (error) { fs::remove_all(partial, error); return KError{KErrorCode::Unavailable, "build.snapshotFailure", true}; }
                if (!relative.empty() && *relative.begin() == L".lightoverleaf-trash")
                {
                    if (iterator->is_directory(error)) iterator.disable_recursion_pending();
                    continue;
                }
                const fs::path destination = partial / relative;
                if (iterator->is_directory(error)) fs::create_directory(destination, error);
                else if (iterator->is_regular_file(error))
                {
                    const auto size = iterator->file_size(error);
                    if (error || size > kMaxBuildSnapshotBytes - total)
                    { fs::remove_all(partial, error); return KError{KErrorCode::ResourceExhausted, "build.snapshotTooLarge", false}; }
                    total += size;
                    fs::copy_file(iterator->path(), destination, fs::copy_options::none, error);
                }
                if (error) { fs::remove_all(partial, error); return KError{KErrorCode::Unavailable, "build.snapshotFailure", true}; }
            }
            fs::rename(partial, final, error);
            if (error) { fs::remove_all(partial, error); return KError{KErrorCode::Unavailable, "build.snapshotFailure", true}; }
            return true;
        }
        catch (...) { return KError{KErrorCode::Unavailable, "build.snapshotFailure", true}; }
    }
    void release(const std::string& snapshotId) noexcept override
    {
        if (!validBuildToken(snapshotId)) return;
        try
        {
            const fs::path target = m_cache / fromUtf8(snapshotId);
            std::error_code error;
            const fs::path normalized = fs::weakly_canonical(target, error);
            if (!error && belowOrEqual(m_cache, normalized) && normalized != m_cache) fs::remove_all(normalized, error);
        }
        catch (...) {}
    }
private:
    fs::path m_workspace;
    fs::path m_cache;
};

class KWindowsCompilerBackend final : public IKCompilerBackend
{
public:
    KWindowsCompilerBackend(fs::path cache, std::vector<fs::path> roots)
        : m_cache(std::move(cache)), m_roots(std::move(roots)) {}
    KResult<std::vector<KDetectedCompiler>> detect(std::stop_token stop) override
    {
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "build.cancelled", true};
        std::vector<KCompilerEngine> engines;
        for (const KCompilerEngine engine : {KCompilerEngine::PdfLatex, KCompilerEngine::XeLatex, KCompilerEngine::LuaLatex})
            if (findExecutable(m_roots, engine)) engines.push_back(engine);
        if (engines.empty()) return std::vector<KDetectedCompiler>{};
        const bool latexmk = findNamedExecutable(m_roots, L"latexmk.exe").has_value();
        return std::vector<KDetectedCompiler>{{"windows-tex", latexmk ?
            "Windows TeX (latexmk)" : "Windows TeX", std::move(engines)}};
    }
    KResult<KCompilerRunResult> run(const KCompilerRun& command, std::stop_token stop) override
    {
        const std::optional<fs::path> executable = findExecutable(m_roots, command.m_engine);
        if (!executable) return KCompilerRunResult{KCompilerTerminal::CompilerUnavailable, -1,
            "compiler not found", false, {}};
        const fs::path working = m_cache / fromUtf8(command.m_snapshotId);
        std::error_code pathError;
        const fs::path mainPath = fs::weakly_canonical(working / fromUtf8(command.m_mainFileId), pathError);
        if (pathError || !belowOrEqual(working, mainPath) || !fs::is_regular_file(mainPath, pathError))
            return KError{KErrorCode::NotFound, "build.mainFileNotFound", false};
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE readRaw = nullptr, writeRaw = nullptr;
        if (!CreatePipe(&readRaw, &writeRaw, &attributes, 0))
            return KError{KErrorCode::Unavailable, "build.pipeFailure", true};
        KHandle readPipe(readRaw), writePipe(writeRaw);
        SetHandleInformation(readPipe.value, HANDLE_FLAG_INHERIT, 0);
        KHandle job(CreateJobObjectW(nullptr, nullptr));
        if (!job.value) return KError{KErrorCode::Unavailable, "build.processFailure", true};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            return KError{KErrorCode::Unavailable, "build.processFailure", true};
        const std::optional<fs::path> latexmk = findNamedExecutable(m_roots, L"latexmk.exe");
        const fs::path launcher = latexmk.value_or(*executable);
        std::wstring commandLine = quote(launcher.wstring());
        if (latexmk)
        {
            commandLine += command.m_engine == KCompilerEngine::PdfLatex ? L" -pdf" :
                command.m_engine == KCompilerEngine::XeLatex ? L" -pdfxe" : L" -pdflua";
            commandLine += L" -use-make -interaction=nonstopmode -halt-on-error -file-line-error"
                L" -no-shell-escape -synctex=1 ";
        }
        else
        {
            commandLine += L" -interaction=nonstopmode -halt-on-error -file-line-error"
                L" -no-shell-escape -synctex=1 ";
        }
        commandLine += quote(fromUtf8(command.m_mainFileId));
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writePipe.value; startup.hStdError = writePipe.value;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(launcher.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, working.c_str(), &startup, &process))
            return KError{KErrorCode::Unavailable, "build.processFailure", true};
        KHandle processHandle(process.hProcess), threadHandle(process.hThread);
        if (!AssignProcessToJobObject(job.value, processHandle.value) || ResumeThread(threadHandle.value) == static_cast<DWORD>(-1))
        { TerminateProcess(processHandle.value, 1); return KError{KErrorCode::Unavailable, "build.processFailure", true}; }
        CloseHandle(writePipe.value); writePipe.value = nullptr;
        KCompilerRunResult result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(command.m_timeoutMs);
        for (;;)
        {
            appendPipe(readPipe.value, result.m_output, result.m_outputTruncated, command.m_onOutput);
            if (WaitForSingleObject(processHandle.value, 0) == WAIT_OBJECT_0) break;
            if (stop.stop_requested())
            { TerminateJobObject(job.value, 2); result.m_terminal = KCompilerTerminal::Cancelled; break; }
            if (std::chrono::steady_clock::now() >= deadline)
            { TerminateJobObject(job.value, 3); result.m_terminal = KCompilerTerminal::TimedOut; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        WaitForSingleObject(processHandle.value, 2000);
        appendPipe(readPipe.value, result.m_output, result.m_outputTruncated, command.m_onOutput);
        DWORD exitCode = 1;
        GetExitCodeProcess(processHandle.value, &exitCode);
        result.m_exitCode = static_cast<int>(exitCode);
        if (result.m_terminal != KCompilerTerminal::Cancelled && result.m_terminal != KCompilerTerminal::TimedOut)
            result.m_terminal = exitCode == 0 ? KCompilerTerminal::Succeeded : KCompilerTerminal::Failed;
        bool conversionTruncated = false;
        result.m_output = safeUtf8(result.m_output, conversionTruncated);
        result.m_outputTruncated = result.m_outputTruncated || conversionTruncated;
        result.m_diagnostics = diagnostics(result.m_output, command.m_mainFileId);
        if (result.m_terminal == KCompilerTerminal::Succeeded)
        {
            const fs::path stem = mainPath.parent_path() / mainPath.stem();
            KResult<std::vector<std::uint8_t>> pdf = readBoundedFile(stem.wstring() + L".pdf", kMaxPdfArtifactBytes);
            if (const KError* failure = std::get_if<KError>(&pdf)) return *failure;
            result.m_pdf = std::get<std::vector<std::uint8_t>>(std::move(pdf));
            const fs::path syncPath(stem.wstring() + L".synctex.gz");
            if (fs::is_regular_file(syncPath))
            {
                KResult<std::vector<std::uint8_t>> sync = readBoundedFile(syncPath, kMaxSyncTexArtifactBytes);
                if (const KError* failure = std::get_if<KError>(&sync)) return *failure;
                result.m_syncTex = std::get<std::vector<std::uint8_t>>(std::move(sync));
            }
        }
        return result;
    }
private:
    fs::path m_cache;
    std::vector<fs::path> m_roots;
};

KResult<KWindowsBuildAdapters> createWindowsBuildAdapters(const KWindowsBuildOptions& options)
{
    try
    {
        const std::wstring workspaceWide = fromUtf8(options.m_workspaceRootUtf8);
        const std::wstring cacheWide = fromUtf8(options.m_cacheRootUtf8);
        if (workspaceWide.empty() || cacheWide.empty())
            return KError{KErrorCode::InvalidArgument, "build.invalidRoot", false};
        std::error_code error;
        const fs::path workspace = fs::canonical(workspaceWide, error);
        if (error || !fs::is_directory(workspace, error))
            return KError{KErrorCode::InvalidArgument, "build.invalidRoot", false};
        fs::create_directories(cacheWide, error);
        if (error) return KError{KErrorCode::Unavailable, "build.cacheFailure", true};
        const fs::path cache = fs::canonical(cacheWide, error);
        if (error || belowOrEqual(workspace, cache) || belowOrEqual(cache, workspace))
            return KError{KErrorCode::InvalidArgument, "build.cacheOverlap", false};
        std::vector<fs::path> roots;
        for (const std::string& root : options.m_texRootsUtf8)
        {
            const std::wstring wide = fromUtf8(root);
            if (!wide.empty()) roots.emplace_back(wide);
        }
        KWindowsBuildAdapters result;
        result.m_snapshots = std::make_shared<KLocalBuildSnapshotStore>(workspace, cache);
        result.m_compiler = std::make_shared<KWindowsCompilerBackend>(cache, std::move(roots));
        return result;
    }
    catch (...) { return KError{KErrorCode::Unavailable, "build.adapterFailure", true}; }
}
}
