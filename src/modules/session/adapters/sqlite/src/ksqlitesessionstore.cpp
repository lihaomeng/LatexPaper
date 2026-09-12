#include <lightoverleaf/session/adapters/ksqlitesessionstore.h>

#include <sqlite3.h>

#include <mutex>

namespace lightoverleaf::session
{
namespace
{
bool hasColumn(sqlite3* database, const char* name)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "PRAGMA table_info(session_state)", -1,
        &statement, nullptr) != SQLITE_OK)
        return false;
    struct KFinalize
    {
        sqlite3_stmt* m_statement = nullptr;
        ~KFinalize() { sqlite3_finalize(m_statement); }
    } finalize{statement};
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const unsigned char* value = sqlite3_column_text(statement, 1);
        if (value && std::string(reinterpret_cast<const char*>(value)) == name)
            return true;
    }
    return false;
}

bool ensureColumn(sqlite3* database, const char* name, const char* sql)
{
    return hasColumn(database, name) || sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}
}

class KStore final : public IKSessionStore
{
public:
    explicit KStore(sqlite3* database) : m_database(database) {}
    ~KStore() override { sqlite3_close(m_database); }

    KResult<std::optional<KStoredSession>> load() const override
    {
        std::scoped_lock lock(m_mutex);
        sqlite3_stmt* statement = nullptr;
        constexpr char query[] = "SELECT workspace_root,open_files,active_file,sidebar_width,"
            "editor_width,preview_open,active_line,active_column,preview_zoom "
            "FROM session_state WHERE id=1";
        if (sqlite3_prepare_v2(m_database, query, -1, &statement, nullptr) != SQLITE_OK)
            return error();
        struct KFinalize
        {
            sqlite3_stmt* m_statement = nullptr;
            ~KFinalize() { sqlite3_finalize(m_statement); }
        } finalize{statement};
        if (sqlite3_step(statement) != SQLITE_ROW) return std::optional<KStoredSession>{};
        KStoredSession value;
        value.m_workspaceRoot = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        value.m_openFiles = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        value.m_activeFile = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        value.m_sidebarWidth = static_cast<unsigned int>(sqlite3_column_int(statement, 3));
        value.m_editorWidth = static_cast<unsigned int>(sqlite3_column_int(statement, 4));
        value.m_previewOpen = sqlite3_column_int(statement, 5) != 0;
        value.m_activeLine = static_cast<std::size_t>(sqlite3_column_int64(statement, 6));
        value.m_activeColumn = static_cast<std::size_t>(sqlite3_column_int64(statement, 7));
        value.m_previewZoom = static_cast<unsigned int>(sqlite3_column_int(statement, 8));
        return std::optional<KStoredSession>{std::move(value)};
    }

    KResult<bool> save(const KStoredSession& value) override
    {
        std::scoped_lock lock(m_mutex);
        if (sqlite3_exec(m_database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
            return error();
        sqlite3_stmt* statement = nullptr;
        constexpr char query[] = "INSERT INTO session_state(id,workspace_root,open_files,active_file,"
            "sidebar_width,editor_width,preview_open,active_line,active_column,preview_zoom) "
            "VALUES(1,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
            "workspace_root=excluded.workspace_root,open_files=excluded.open_files,"
            "active_file=excluded.active_file,sidebar_width=excluded.sidebar_width,"
            "editor_width=excluded.editor_width,preview_open=excluded.preview_open,"
            "active_line=excluded.active_line,active_column=excluded.active_column,"
            "preview_zoom=excluded.preview_zoom";
        if (sqlite3_prepare_v2(m_database, query, -1, &statement, nullptr) != SQLITE_OK)
        {
            sqlite3_exec(m_database, "ROLLBACK", nullptr, nullptr, nullptr);
            return error();
        }
        sqlite3_bind_text(statement, 1, value.m_workspaceRoot.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, value.m_openFiles.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, value.m_activeFile.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 4, static_cast<int>(value.m_sidebarWidth));
        sqlite3_bind_int(statement, 5, static_cast<int>(value.m_editorWidth));
        sqlite3_bind_int(statement, 6, value.m_previewOpen ? 1 : 0);
        sqlite3_bind_int64(statement, 7, static_cast<sqlite3_int64>(value.m_activeLine));
        sqlite3_bind_int64(statement, 8, static_cast<sqlite3_int64>(value.m_activeColumn));
        sqlite3_bind_int(statement, 9, static_cast<int>(value.m_previewZoom));
        bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if (succeeded && !value.m_workspaceRoot.empty())
        {
            sqlite3_stmt* history = nullptr;
            constexpr char historyQuery[] = "INSERT INTO workspace_history(root,last_opened) "
                "VALUES(?,unixepoch()) ON CONFLICT(root) DO UPDATE SET last_opened=excluded.last_opened";
            succeeded = sqlite3_prepare_v2(m_database, historyQuery, -1, &history, nullptr) == SQLITE_OK;
            if (succeeded)
            {
                sqlite3_bind_text(history, 1, value.m_workspaceRoot.c_str(), -1, SQLITE_TRANSIENT);
                succeeded = sqlite3_step(history) == SQLITE_DONE;
            }
            sqlite3_finalize(history);
        }
        sqlite3_exec(m_database, succeeded ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
        return succeeded ? KResult<bool>{true} : KResult<bool>{error()};
    }

    KResult<std::vector<std::string>> history() const override
    {
        std::scoped_lock lock(m_mutex);
        sqlite3_stmt* statement = nullptr;
        constexpr char query[] = "SELECT root FROM workspace_history WHERE root<>'' "
            "ORDER BY last_opened DESC LIMIT 20";
        if (sqlite3_prepare_v2(m_database, query, -1, &statement, nullptr) != SQLITE_OK)
            return error();
        struct KFinalize
        {
            sqlite3_stmt* m_statement = nullptr;
            ~KFinalize() { sqlite3_finalize(m_statement); }
        } finalize{statement};
        std::vector<std::string> result;
        while (sqlite3_step(statement) == SQLITE_ROW)
            result.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
        return result;
    }

private:
    KError error() const
    {
        return {KErrorCode::Unavailable, "session.storageFailure", true};
    }

private:
    sqlite3* m_database = nullptr;
    mutable std::mutex m_mutex;
};

KResult<std::shared_ptr<IKSessionStore>> createSqliteSessionStore(const std::string& path)
{
    sqlite3* database = nullptr;
    if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        if (database) sqlite3_close(database);
        return KError{KErrorCode::Unavailable, "session.storageFailure", true};
    }
    constexpr char ddl[] = "CREATE TABLE IF NOT EXISTS session_state("
        "id INTEGER PRIMARY KEY,workspace_root TEXT NOT NULL,open_files TEXT NOT NULL,"
        "active_file TEXT NOT NULL,sidebar_width INTEGER NOT NULL,editor_width INTEGER NOT NULL DEFAULT 720,"
        "preview_open INTEGER NOT NULL,active_line INTEGER NOT NULL DEFAULT 1,"
        "active_column INTEGER NOT NULL DEFAULT 1,preview_zoom INTEGER NOT NULL DEFAULT 125);"
        "CREATE TABLE IF NOT EXISTS workspace_history(root TEXT PRIMARY KEY,last_opened INTEGER NOT NULL)";
    const bool ready = sqlite3_exec(database, ddl, nullptr, nullptr, nullptr) == SQLITE_OK &&
        ensureColumn(database, "editor_width", "ALTER TABLE session_state ADD COLUMN editor_width INTEGER NOT NULL DEFAULT 720") &&
        ensureColumn(database, "active_line", "ALTER TABLE session_state ADD COLUMN active_line INTEGER NOT NULL DEFAULT 1") &&
        ensureColumn(database, "active_column", "ALTER TABLE session_state ADD COLUMN active_column INTEGER NOT NULL DEFAULT 1") &&
        ensureColumn(database, "preview_zoom", "ALTER TABLE session_state ADD COLUMN preview_zoom INTEGER NOT NULL DEFAULT 125");
    if (!ready)
    {
        sqlite3_close(database);
        return KError{KErrorCode::Unavailable, "session.storageFailure", true};
    }
    return std::shared_ptr<IKSessionStore>(std::make_shared<KStore>(database));
}
}
