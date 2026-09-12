#include <lightoverleaf/document/inbound/ikdocuments.h>
#include <lightoverleaf/document/outbound/ikdocumentstore.h>
#include <lightoverleaf/document/domain/kdocumentpolicy.h>
#include <iostream>
#include <stdexcept>

using namespace lightoverleaf;
using namespace lightoverleaf::document;

namespace
{
class KFakeDocumentStore final : public IKDocumentStore
{
public:
    KResult<KStoredDocument> read(const std::string&, std::stop_token) override
    {
        ++m_reads;
        if (m_throw) throw std::runtime_error("fake");
        return m_value;
    }
    KResult<KStoredDocument> replace(const std::string&, const std::string& content,
        const std::string& revision, std::stop_token stop) override
    {
        ++m_writes;
        if (stop.stop_requested()) return KError{KErrorCode::Cancelled, "document.cancelled", true};
        if (revision != m_value.m_revision) return KError{KErrorCode::Conflict, "document.conflict", false};
        m_value = {content, "revision-" + std::to_string(++m_revision), m_value.m_utf8Bom};
        if (m_afterCommit) m_afterCommit->request_stop();
        return m_value;
    }

public:
    KStoredDocument m_value{"original", "revision-1", true};
    unsigned int m_reads = 0;
    unsigned int m_writes = 0;
    unsigned int m_revision = 1;
    bool m_throw = false;
    std::stop_source* m_afterCommit = nullptr;
};
template<class T> bool failed(const KResult<T>& result, KErrorCode code)
{
    const auto* error = std::get_if<KError>(&result);
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
    const auto store = std::make_shared<KFakeDocumentStore>();
    const auto documents = createDocuments(store);
    check(store->m_reads == 0 && store->m_writes == 0, "construction has no I/O");
    const auto opened = documents->open("chapter/main.tex");
    check(std::holds_alternative<KDocumentSnapshot>(opened), "open");
    check(std::get<KDocumentSnapshot>(opened).m_utf8Bom, "BOM metadata mapped");
    const auto saved = documents->save({"chapter/main.tex", "new content", "revision-1", 42});
    check(std::holds_alternative<KDocumentSaved>(saved) && std::get<KDocumentSaved>(saved).m_clientSequence == 42, "save correlation");
    check(store->m_reads == 1 && store->m_writes == 1, "save delegates one compare-and-replace without read");
    check(failed(documents->save({"chapter/main.tex", "overwrite", "revision-1", 43}), KErrorCode::Conflict), "stale revision");
    check(store->m_value.m_content == "new content", "conflict never overwrites");
    for (const auto* path : {"../escape.tex", "/root.tex", "C:/a.tex", "a\\b.tex", "a//b.tex", "a/./b.tex", "a/", "a. /b"})
        check(failed(documents->open(path), KErrorCode::InvalidArgument), "invalid relative path");
    check(validFileId("章节/正文.tex"), "UTF-8 relative name");
    check(!validFileId("CON.tex") && !validFileId("nested/lpt1.log"), "device names rejected");
    check(validUtf8("你好，LaTeX"), "UTF-8 text");
    for (const auto& text : {std::string("\xC0\xAF"), std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"), std::string("a\0b", 3)})
        check(!validUtf8(text), "invalid UTF-8/NUL rejected");
    check(failed(documents->save({"main.tex", std::string(kMaxDocumentBytes + 1, 'x'), "revision-2", 0}),
        KErrorCode::ResourceExhausted), "size cap");
    std::stop_source stop;
    stop.request_stop();
    const auto writes = store->m_writes;
    check(failed(documents->save({"main.tex", "no", "revision-2", 0}, stop.get_token()), KErrorCode::Cancelled), "early cancellation");
    check(store->m_writes == writes, "cancel avoids store");
    std::stop_source late;
    store->m_afterCommit = &late;
    check(std::holds_alternative<KDocumentSaved>(documents->save({"main.tex", "committed", "revision-2", 44}, late.get_token())),
        "late cancellation preserves successful commit");
    store->m_throw = true;
    check(failed(documents->open("main.tex"), KErrorCode::Internal), "adapter exception mapped");
    check(!createDocuments(nullptr), "missing store rejected");
    return failures == 0 ? 0 : 1;
}
