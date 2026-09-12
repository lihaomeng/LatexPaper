#include <lightoverleaf/desktop/kapplicationcomposition.h>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>

using namespace lightoverleaf;

namespace
{
class KTempDirectory final
{
public:
    explicit KTempDirectory(std::filesystem::path path) : m_path(std::move(path)) {}
    ~KTempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main()
{
    if (!cef_api_hash(CEF_API_VERSION, 0)) return 2;
    int failures = 0;
    const auto check = [&failures](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        (L"LightOverLeaf-composition-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    KTempDirectory cleanup(root);
    std::error_code error;
    std::filesystem::create_directories(root / L"章节", error);
    check(!error && write(root / L"章节" / L"引言.tex", "初始内容"), "fixture created");

    auto composition = createApplicationComposition([selection = utf8(root)]
    {
        return std::optional<std::string>{selection};
    });
    check(composition != nullptr, "composition created");
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> queue;
    const KNativeSchedule schedule = [&mutex, &ready, &queue](std::function<void()> task)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(task));
        ready.notify_one();
        return true;
    };
    const auto endpoint = composition->endpointFactory()(schedule, "composition-test");
    check(endpoint != nullptr, "native endpoint created");
    const auto exchange = [&](std::int64_t queryId, const std::string& request)
    {
        std::optional<KNativeMessageReply> result;
        endpoint->request(queryId, request, false,
            [&result](KNativeMessageReply reply) { result = std::move(reply); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!result && std::chrono::steady_clock::now() < deadline)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait_until(lock, deadline, [&queue] { return !queue.empty(); });
                if (!queue.empty())
                {
                    task = std::move(queue.front());
                    queue.pop_front();
                }
            }
            if (task) task();
        }
        return result.value_or(KNativeMessageReply{false, "TEST_TIMEOUT"});
    };

    KNativeMessageReply reply = exchange(1,
        R"({"version":2,"id":"open-workspace","clientSequence":1,"method":"workspace.open","params":{}})");
    check(reply.m_success && reply.m_payload.find("章节/引言.tex") != std::string::npos,
        "workspace tree crosses CEF codec");
    CefRefPtr<CefValue> parsed = CefParseJSON(reply.m_payload, JSON_PARSER_RFC);
    CefRefPtr<CefDictionaryValue> workspaceResponse = parsed ? parsed->GetDictionary() : nullptr;
    CefRefPtr<CefDictionaryValue> workspaceResult = workspaceResponse ?
        workspaceResponse->GetDictionary("result") : nullptr;
    CefRefPtr<CefListValue> entries = workspaceResult ? workspaceResult->GetList("entries") : nullptr;
    check(entries && entries->GetSize() == 2,
        "workspace response contains bounded entry array");

    reply = exchange(2,
        R"({"version":2,"id":"open-document","clientSequence":2,"method":"document.open","params":{"fileId":"章节/引言.tex"}})");
    parsed = CefParseJSON(reply.m_payload, JSON_PARSER_RFC);
    check(reply.m_success && parsed && parsed->GetDictionary(), "document open crosses native endpoint");
    CefRefPtr<CefDictionaryValue> documentResponse = parsed ? parsed->GetDictionary() : nullptr;
    CefRefPtr<CefDictionaryValue> opened = documentResponse ?
        documentResponse->GetDictionary("result") : nullptr;
    const std::string revision = opened ? opened->GetString("revision").ToString() : std::string{};
    check(opened && opened->GetString("content").ToString() == "初始内容" && !revision.empty(),
        "document content and revision returned");

    reply = exchange(3, std::string(R"({"version":2,"id":"save-document","clientSequence":3,"method":"document.save","params":{"fileId":"章节/引言.tex","content":"修改内容","expectedRevision":")") +
        revision + R"("}})");
    check(reply.m_success && reply.m_payload.find("document.save") != std::string::npos &&
        read(root / L"章节" / L"引言.tex") == "修改内容", "document save commits through worker");

    check(write(root / L"章节" / L"引言.tex", "外部修改"), "external edit fixture");
    reply = exchange(4, std::string(R"({"version":2,"id":"save-conflict","clientSequence":4,"method":"document.save","params":{"fileId":"章节/引言.tex","content":"不得覆盖","expectedRevision":")") +
        revision + R"("}})");
    check(reply.m_success && reply.m_payload.find("FILE_CONFLICT") != std::string::npos &&
        read(root / L"章节" / L"引言.tex") == "外部修改", "conflict never overwrites external edit");

    const std::string workspaceId = workspaceResult ? workspaceResult->GetString("workspaceId").ToString() : "invalid";
    const auto manage = [&](int sequence, const std::string& operation, const std::string& fileId, const std::string& destination)
    {
        return exchange(sequence, std::string("{\"version\":2,\"id\":\"manage-") + std::to_string(sequence) +
            "\",\"clientSequence\":" + std::to_string(sequence) + ",\"method\":\"workspace.manageFile\",\"params\":{\"workspaceId\":\"" +
            workspaceId + "\",\"operation\":\"" + operation + "\",\"fileId\":\"" + fileId + "\",\"destination\":\"" + destination + "\"}}");
    };
    reply = manage(5, "create", "章节/新稿.tex", "");
    check(reply.m_success && reply.m_payload.find("\"ok\":true") != std::string::npos &&
        std::filesystem::exists(root / L"章节/新稿.tex"), "RPC creates local file");
    check(write(root / L"章节/新稿.tex", "保留正文"), "mutation payload fixture");
    reply = manage(6, "rename", "章节/新稿.tex", "改名.tex");
    check(reply.m_success && read(root / L"改名.tex") == "保留正文", "RPC renames without changing content");
    reply = manage(7, "remove", "改名.tex", "");
    check(reply.m_success && !std::filesystem::exists(root / L"改名.tex") &&
        std::filesystem::exists(root / L".lightoverleaf-trash"), "RPC removes recoverably");
    const auto manageDirectory = [&](int sequence, const std::string& operation,
        const std::string& directoryId, const std::string& destination)
    {
        return exchange(sequence, std::string("{\"version\":2,\"id\":\"manage-directory-") +
            std::to_string(sequence) + "\",\"clientSequence\":" + std::to_string(sequence) +
            ",\"method\":\"workspace.manageDirectory\",\"params\":{\"workspaceId\":\"" +
            workspaceId + "\",\"operation\":\"" + operation + "\",\"directoryId\":\"" +
            directoryId + "\",\"destination\":\"" + destination + "\"}}");
    };
    reply = manageDirectory(8, "create", "补充", "");
    check(reply.m_success && std::filesystem::is_directory(root / L"补充"),
        "RPC creates local directory");
    reply = manage(9, "create", "补充/note.txt", "");
    check(reply.m_success && write(root / L"补充" / L"note.txt", "directory-content"),
        "RPC creates file below managed directory");
    reply = manageDirectory(10, "rename", "补充", "材料");
    check(reply.m_success && read(root / L"材料" / L"note.txt") == "directory-content",
        "RPC renames directory with descendants");
    reply = manageDirectory(11, "remove", "材料", "");
    check(reply.m_success && !std::filesystem::exists(root / L"材料"),
        "RPC removes directory recoverably");
    check(write(root / L"外部.tex", "external"), "external tree change fixture");
    reply = exchange(12,
        R"({"version":2,"id":"refresh-workspace","clientSequence":12,"method":"workspace.refresh","params":{}})");
    check(reply.m_success && reply.m_payload.find("外部.tex") != std::string::npos,
        "RPC refresh publishes external tree change");
    reply = exchange(13,
        R"({"version":2,"id":"close-workspace","clientSequence":13,"method":"workspace.close","params":{}})");
    check(reply.m_success && reply.m_payload.find("workspace.close") != std::string::npos,
        "workspace close releases session");
    endpoint->close();
    composition->close();
    return failures == 0 ? 0 : 1;
}
