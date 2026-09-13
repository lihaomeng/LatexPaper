#include <windows.h>

#include <array>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t kApplicationName[] = L"LightOverLeaf";
constexpr wchar_t kArchiveName[] = L"payload.7z";
constexpr wchar_t kExtractorName[] = L"7z.exe";
constexpr wchar_t kRuntimeExecutableName[] = L"LightOverLeaf.exe";

class KTemporaryDirectory final
{
public:
    KTemporaryDirectory()
    {
        const std::optional<std::filesystem::path> temporaryRoot = systemTemporaryRoot();
        if (!temporaryRoot) return;

        try
        {
            std::random_device random;
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                std::wostringstream name;
                name << L"LightOverLeaf-" << GetCurrentProcessId() << L'-' << GetTickCount64();
                for (int part = 0; part < 4; ++part)
                    name << L'-' << std::hex << std::setw(8) << std::setfill(L'0') << random();

                const std::filesystem::path candidate = *temporaryRoot / name.str();
                if (!CreateDirectoryW(candidate.c_str(), nullptr))
                {
                    if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
                    return;
                }

                const DWORD attributes = GetFileAttributesW(candidate.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    RemoveDirectoryW(candidate.c_str());
                    return;
                }

                m_path = candidate;
                m_valid = true;
                return;
            }
        }
        catch (...)
        {
            m_valid = false;
        }
    }

    ~KTemporaryDirectory()
    {
        if (!m_valid || m_path.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    bool valid() const { return m_valid; }
    const std::filesystem::path& path() const { return m_path; }

private:
    static std::optional<std::filesystem::path> systemTemporaryRoot()
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0 || length >= buffer.size()) return std::nullopt;
        return std::filesystem::path(buffer.data());
    }

private:
    std::filesystem::path m_path;
    bool m_valid = false;
};

std::optional<std::filesystem::path> moduleDirectory()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    return std::filesystem::path(buffer.data()).parent_path();
}

bool isRegularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::wstring quoteArgument(const std::wstring& value)
{
    return L'"' + value + L'"';
}

std::optional<DWORD> runProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workingDirectory,
    bool hidden)
{
    std::wstring commandLine = quoteArgument(executable.wstring());
    for (const std::wstring& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine += quoteArgument(argument);
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden)
    {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION process{};
    const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | (hidden ? CREATE_NO_WINDOW : 0);
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            creationFlags, nullptr, workingDirectory.c_str(), &startup, &process))
        return std::nullopt;

    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = ERROR_GEN_FAILURE;
    const bool completed = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (!completed) return std::nullopt;
    return exitCode;
}

bool packageSmokeEnabled()
{
    std::array<wchar_t, 2> value{};
    return GetEnvironmentVariableW(L"LIGHTOVERLEAF_PACKAGE_SMOKE", value.data(),
        static_cast<DWORD>(value.size())) == 1 && value[0] == L'1';
}

int fail(const wchar_t* message)
{
    MessageBoxW(nullptr, message, kApplicationName, MB_OK | MB_ICONERROR);
    return 2;
}
}

int runLauncher()
{
    const std::optional<std::filesystem::path> packageDirectory = moduleDirectory();
    if (!packageDirectory) return fail(L"无法定位安装包运行目录。");

    KTemporaryDirectory runtime;
    if (!runtime.valid()) return fail(L"无法创建安全的临时运行目录。");

    const std::filesystem::path extractor = *packageDirectory / kExtractorName;
    const std::filesystem::path archive = *packageDirectory / kArchiveName;
    if (!isRegularFile(extractor) || !isRegularFile(archive))
        return fail(L"安装包载荷不完整。");

    const std::vector<std::wstring> extractArguments = {
        L"x", archive.wstring(), L"-o" + runtime.path().wstring(), L"-y"};
    const std::optional<DWORD> extractCode = runProcess(extractor, extractArguments,
        *packageDirectory, true);
    if (!extractCode || *extractCode != 0) return fail(L"无法解压 LightOverLeaf 运行文件。");

    const std::filesystem::path application = runtime.path() / kRuntimeExecutableName;
    if (!isRegularFile(application)) return fail(L"LightOverLeaf 主程序缺失。");

    const bool smoke = packageSmokeEnabled();
    const std::vector<std::wstring> applicationArguments = smoke
        ? std::vector<std::wstring>{L"--smoke-test"} : std::vector<std::wstring>{};
    const std::optional<DWORD> applicationCode = runProcess(application, applicationArguments,
        runtime.path(), smoke);
    if (!applicationCode) return fail(L"无法启动 LightOverLeaf 主程序。");
    if (*applicationCode > static_cast<DWORD>((std::numeric_limits<int>::max)())) return 2;
    return static_cast<int>(*applicationCode);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        return runLauncher();
    }
    catch (...)
    {
        return fail(L"启动 LightOverLeaf 时发生内部错误。");
    }
}
