#include <lightoverleaf/workspace/adapters/klocalworkspacestore.h>
#include <lightoverleaf/workspace/inbound/ikworkspaces.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace lightoverleaf;
using namespace lightoverleaf::workspace;

namespace
{
class KTestDirectory
{
public:
    KTestDirectory()
    {
        m_path = std::filesystem::temp_directory_path() /
            (L"LightOverLeafWorkspace-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(m_path / L"章节");
    }
    ~KTestDirectory()
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
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void write(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
    if (!stream) throw std::runtime_error("fixture write");
}

template<class T> bool failed(const KResult<T>& result, KErrorCode code)
{
    const KError* error = std::get_if<KError>(&result);
    return error && error->m_code == code;
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool valid, const char* label)
    {
        if (!valid) { ++failures; std::cerr << label << '\n'; }
    };
    KTestDirectory root;
    write(root.path() / L"main.tex", "first");
    write(root.path() / L"章节" / L"正文.tex", "中文");
    const std::filesystem::path outside = root.path().parent_path() /
        (root.path().filename().wstring() + L"-outside.txt");
    write(outside, "outside");
    const std::filesystem::path link = root.path() / L"outside-link.txt";
    const bool linkCreated = CreateSymbolicLinkW(link.c_str(), outside.c_str(),
        SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
    std::cout << "reparse-fixture=" << (linkCreated ? "verified" : "unavailable") << '\n';
    const auto store = createLocalWorkspaceStore();
    check(static_cast<bool>(store), "factory");
    const auto workspaces = createWorkspaces(store);
    const KResult<KWorkspaceState> opened = workspaces->open(utf8(root.path()));
    check(std::holds_alternative<KWorkspaceState>(opened), "open local workspace");
    const KWorkspaceState state = std::get<KWorkspaceState>(opened);
    check(state.m_displayName.starts_with("LightOverLeafWorkspace-"), "display name only");
    check(state.m_id.starts_with("workspace-") && state.m_revision.starts_with("revision-"), "opaque ids");
    check(state.m_entries.size() == 3, "directory and files scanned");
    bool hasChinese = false;
    bool leakedAbsolute = false;
    bool followedLink = false;
    for (const KWorkspaceEntry& entry : state.m_entries)
    {
        hasChinese = hasChinese || entry.m_fileId == "章节/正文.tex";
        leakedAbsolute = leakedAbsolute || entry.m_fileId.find(':') != std::string::npos;
        followedLink = followedLink || entry.m_fileId == "outside-link.txt";
    }
    check(hasChinese && !leakedAbsolute, "relative UTF-8 tree");
    if (linkCreated) check(!followedLink, "reparse point skipped");
    write(root.path() / L"main.tex", "changed-content");
    const KResult<KWorkspaceState> rescanned = workspaces->refresh();
    check(std::holds_alternative<KWorkspaceState>(rescanned) &&
        std::get<KWorkspaceState>(rescanned).m_revision != state.m_revision, "tree revision changes");
    std::stop_source stop;
    stop.request_stop();
    check(failed(store->open(utf8(root.path()), stop.get_token()), KErrorCode::Cancelled), "adapter cancellation");
    const auto bounded = createLocalWorkspaceStore({2, 32});
    check(failed(bounded->open(utf8(root.path()), {}), KErrorCode::ResourceExhausted), "entry bound");
    std::filesystem::create_directories(root.path() / L"deep" / L"nested");
    const auto shallow = createLocalWorkspaceStore({100, 1});
    check(failed(shallow->open(utf8(root.path()), {}), KErrorCode::ResourceExhausted), "depth bound");
    check(!createLocalWorkspaceStore({0, 1}), "invalid limits");
    check(failed(store->open(utf8(root.path() / L"missing"), {}), KErrorCode::NotFound), "missing root");
    std::error_code cleanupError;
    const std::string workspaceId = state.m_id;
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty,
        workspaceId, "章节/新建.tex", ""})), "create empty local file");
    check(std::filesystem::exists(root.path() / L"章节/新建.tex"), "created on disk");
    write(root.path() / L"章节/新建.tex", "must-survive");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty, workspaceId, "章节/新建.tex", ""}),
        KErrorCode::Conflict), "create never overwrites");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty, workspaceId, "../escape.tex", ""}),
        KErrorCode::InvalidArgument), "traversal rejected");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty, "stale-workspace", "wrong.tex", ""}),
        KErrorCode::InvalidArgument), "stale workspace rejected");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty, workspaceId, "cancel.tex", ""}, stop.get_token()),
        KErrorCode::Cancelled) && !std::filesystem::exists(root.path() / L"cancel.tex"), "cancel has no write");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::RenameFile, workspaceId, "章节/新建.tex", "main.tex"}),
        KErrorCode::Conflict), "rename never overwrites target");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutate({KWorkspaceMutationKind::RenameFile,
        workspaceId, "章节/新建.tex", "章节/改名.tex"})), "rename chinese file");
    check(!std::filesystem::exists(root.path() / L"章节/新建.tex") &&
        std::filesystem::exists(root.path() / L"章节/改名.tex"), "rename disk state");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutate({KWorkspaceMutationKind::RemoveFile,
        workspaceId, "章节/改名.tex", ""})), "recoverable deletion");
    check(!std::filesystem::exists(root.path() / L"章节/改名.tex"), "original path removed");
    bool recovered = false;
    if (std::filesystem::exists(root.path() / L".lightoverleaf-trash"))
    {
        for (const auto& item : std::filesystem::directory_iterator(root.path() / L".lightoverleaf-trash"))
        {
            std::ifstream input(item.path(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            recovered = recovered || content == "must-survive";
        }
    }
    check(recovered, "deleted bytes recoverable in trash");
    KResult<std::vector<KWorkspaceTrashEntry>> trash = workspaces->listTrash(workspaceId);
    check(std::holds_alternative<std::vector<KWorkspaceTrashEntry>>(trash) &&
        std::get<std::vector<KWorkspaceTrashEntry>>(trash).size() == 1 &&
        std::get<std::vector<KWorkspaceTrashEntry>>(trash)[0].m_originalFileId == "章节/改名.tex" &&
        !std::get<std::vector<KWorkspaceTrashEntry>>(trash)[0].m_directory,
        "indexed file deletion");
    if (const auto* entries = std::get_if<std::vector<KWorkspaceTrashEntry>>(&trash); entries && !entries->empty())
    {
        check(std::holds_alternative<KWorkspaceState>(workspaces->restoreTrash(
            {workspaceId, entries->front().m_trashId})), "restore deleted file");
        std::ifstream restored(root.path() / L"章节/改名.tex", std::ios::binary);
        const std::string restoredContent((std::istreambuf_iterator<char>(restored)),
            std::istreambuf_iterator<char>());
        check(restoredContent == "must-survive", "restored file bytes");
        check(failed(workspaces->restoreTrash({workspaceId, entries->front().m_trashId}),
            KErrorCode::NotFound), "trash token is single use");
    }
    check(failed(workspaces->mutate({KWorkspaceMutationKind::CreateEmpty, workspaceId, ".lightoverleaf-trash/forged.tex", ""}),
        KErrorCode::InvalidArgument), "reserved trash protected");
    check(failed(workspaces->mutate({KWorkspaceMutationKind::RemoveFile, workspaceId, "章节", ""}),
        KErrorCode::InvalidArgument), "directory deletion prohibited");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Create, workspaceId, "附录资料", ""})),
        "create directory");
    check(std::filesystem::is_directory(root.path() / L"附录资料"), "created directory exists");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutate(
        {KWorkspaceMutationKind::CreateEmpty, workspaceId, "附录资料/note.tex", ""})),
        "create file below managed directory");
    write(root.path() / L"附录资料" / L"note.tex", "directory-recovery");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Rename, workspaceId, "附录资料", "材料"})),
        "rename directory");
    check(!std::filesystem::exists(root.path() / L"附录资料") &&
        std::filesystem::is_regular_file(root.path() / L"材料" / L"note.tex"),
        "directory rename preserves descendants");
    check(failed(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Rename, workspaceId, "材料", "材料/内部"}),
        KErrorCode::InvalidArgument), "directory cannot move into itself");
    check(failed(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Remove, workspaceId, "main.tex", ""}),
        KErrorCode::InvalidArgument), "directory command rejects regular file");
    check(failed(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Create, workspaceId, ".lightoverleaf-trash/escape", ""}),
        KErrorCode::InvalidArgument), "directory command protects trash");
    check(failed(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Create, workspaceId, "cancelled-directory", ""},
        stop.get_token()), KErrorCode::Cancelled), "directory create cancellation");
    check(std::holds_alternative<KWorkspaceState>(workspaces->mutateDirectory(
        {KWorkspaceDirectoryMutationKind::Remove, workspaceId, "材料", ""})),
        "recoverable directory removal");
    bool recoveredDirectory = false;
    for (const auto& item : std::filesystem::directory_iterator(root.path() / L".lightoverleaf-trash"))
    {
        if (!item.is_directory()) continue;
        std::ifstream input(item.path() / L"note.tex", std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        recoveredDirectory = recoveredDirectory || content == "directory-recovery";
    }
    check(recoveredDirectory && !std::filesystem::exists(root.path() / L"材料"),
        "removed directory subtree is recoverable");
    trash = workspaces->listTrash(workspaceId);
    if (const auto* entries = std::get_if<std::vector<KWorkspaceTrashEntry>>(&trash); entries)
    {
        const auto directory = std::find_if(entries->begin(), entries->end(),
            [](const KWorkspaceTrashEntry& entry) { return entry.m_directory; });
        check(directory != entries->end() && directory->m_originalFileId == "材料",
            "indexed directory deletion");
        if (directory != entries->end())
        {
            check(std::holds_alternative<KWorkspaceState>(workspaces->restoreTrash(
                {workspaceId, directory->m_trashId})) &&
                std::filesystem::is_regular_file(root.path() / L"材料/note.tex"),
                "restore deleted directory tree");
        }
    }
    write(root.path() / L"watch-event.tex", "change");
    bool changed = false;
    for (int attempt = 0; attempt < 100 && !changed; ++attempt)
    {
        KResult<bool> event = workspaces->pollChanges(workspaceId);
        changed = std::holds_alternative<bool>(event) && std::get<bool>(event);
        if (!changed) Sleep(10);
    }
    check(changed, "native watcher observes subtree change");
    bool drained = false;
    for (int attempt = 0; attempt < 100 && !drained; ++attempt)
    {
        const KResult<bool> next = workspaces->pollChanges(workspaceId);
        drained = std::holds_alternative<bool>(next) && !std::get<bool>(next);
    }
    check(drained, "native watcher drains notification burst");
    std::filesystem::remove(outside, cleanupError);
    return failures == 0 ? 0 : 1;
}
