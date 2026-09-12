#include <lightoverleaf/platform/kcefruntime.h>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string_view>
#include <algorithm>
#include <filesystem>
#include <utility>
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "include/wrapper/cef_byte_read_handler.h"
#include "include/wrapper/cef_message_router.h"

namespace lightoverleaf
{
namespace
{
constexpr char kAppUrl[] = "https://app.lightoverleaf.local/";
constexpr char kRpcSmokeUrl[] = "https://app.lightoverleaf.local/#rpc-smoke";

class KRendererApp final : public CefApp, public CefRenderProcessHandler
{
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
    void OnWebKitInitialized() override
    {
        m_router = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
    }
    void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override
    {
        if (m_router && frame->IsMain() &&
            (frame->GetURL().ToString() == kAppUrl || frame->GetURL().ToString() == kRpcSmokeUrl))
            m_router->OnContextCreated(browser, frame, context);
    }
    void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override
    {
        if (m_router) m_router->OnContextReleased(browser, frame, context);
    }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefProcessId source, CefRefPtr<CefProcessMessage> message) override
    {
        return m_router && m_router->OnProcessMessageReceived(browser, frame, source, message);
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> m_router;
    IMPLEMENT_REFCOUNTING(KRendererApp);
};

class KHtmlBytes final : public CefBaseRefCounted
{
public:
    explicit KHtmlBytes(std::string bytes) : m_bytes(std::move(bytes)) {}
    const std::string& bytes() const { return m_bytes; }

private:
    const std::string m_bytes;
    IMPLEMENT_REFCOUNTING(KHtmlBytes);
};

class KResourceFactory final : public CefSchemeHandlerFactory
{
public:
    explicit KResourceFactory(std::filesystem::path root) : m_root(std::move(root)) {}
    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
        const CefString&, CefRefPtr<CefRequest> request) override
    {
        CefURLParts parts;
        const bool parsed = CefParseURL(request->GetURL(), parts);
        const std::string path = CefString(&parts.path).ToString();
        const bool allowedPath = path == "/" || path == "/index.html" ||
            (path.rfind("/assets/", 0) == 0 && path.size() < 256 &&
             path.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") == std::string::npos &&
             path.find("..") == std::string::npos && path.find('/', 8) == std::string::npos);
        if (!parsed || request->GetMethod() != "GET" || !allowedPath) return notFound();
        const std::filesystem::path file = m_root / (path == "/" ? "index.html" : path.substr(1));
        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::canonical(file, error);
        if (error) return notFound();
        const std::filesystem::path relative = canonical.lexically_relative(m_root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return notFound();
        constexpr std::uintmax_t maxAssetBytes = 8 * 1024 * 1024;
        const std::uintmax_t size = std::filesystem::file_size(canonical, error);
        if (error || size > maxAssetBytes) return notFound();
        CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(canonical.wstring());
        if (!stream) return notFound();
        const std::string extension = canonical.extension().string();
        std::string nonce;
        if (extension == ".html")
        {
            std::array<unsigned char, 32> randomBytes{};
            if (BCryptGenRandom(nullptr, randomBytes.data(), static_cast<ULONG>(randomBytes.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return notFound();
            constexpr char hex[] = "0123456789abcdef";
            for (const unsigned char byte : randomBytes)
            {
                nonce += hex[byte >> 4];
                nonce += hex[byte & 15];
            }
            std::string html(static_cast<std::size_t>(size), '\0');
            if (stream->Read(html.data(), 1, html.size()) != html.size()) return notFound();
            constexpr std::string_view token = "__LIGHTOVERLEAF_STYLE_NONCE__";
            std::size_t position = html.find(token);
            if (position == std::string::npos) return notFound();
            while (position != std::string::npos)
            {
                html.replace(position, token.size(), nonce);
                position = html.find(token, position + nonce.size());
            }
            CefRefPtr<KHtmlBytes> owner = new KHtmlBytes(std::move(html));
            stream = CefStreamReader::CreateForHandler(new CefByteReadHandler(
                reinterpret_cast<const unsigned char*>(owner->bytes().data()), owner->bytes().size(), owner));
            if (!stream) return notFound();
        }
        const std::string mime = extension == ".html" ? "text/html" :
            extension == ".js" ? "text/javascript" : extension == ".css" ? "text/css" :
            extension == ".ttf" ? "font/ttf" : extension == ".woff2" ? "font/woff2" : "application/octet-stream";
        CefResponse::HeaderMap headers;
        const std::string styleSource = nonce.empty() ? "style-src 'self'; " : "style-src 'self' 'nonce-" + nonce + "'; ";
        headers.emplace("Content-Security-Policy", "default-src 'self'; script-src 'self'; " + styleSource +
            "style-src-attr 'unsafe-inline'; worker-src 'self'; font-src 'self'; connect-src 'none'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
        if (extension == ".html") headers.emplace("Cache-Control", "no-store");
        headers.emplace("X-Content-Type-Options", "nosniff");
        return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
    }

private:
    static CefRefPtr<CefResourceHandler> notFound()
    {
        static char text[] = "Not found";
        return new CefStreamResourceHandler(404, "Not Found", "text/plain", {},
            CefStreamReader::CreateForData(text, sizeof(text) - 1));
    }

private:
    const std::filesystem::path m_root;
    IMPLEMENT_REFCOUNTING(KResourceFactory);
};

class KCefFunctionTask final : public CefTask
{
public:
    explicit KCefFunctionTask(std::function<void()> function) : m_function(std::move(function)) {}
    void Execute() override { m_function(); }

private:
    std::function<void()> m_function;
    IMPLEMENT_REFCOUNTING(KCefFunctionTask);
};

class KBrowserClient final : public CefClient, public CefLifeSpanHandler,
    public CefDisplayHandler, public CefLoadHandler, public CefRequestHandler,
    public CefMessageRouterBrowserSide::Handler
{
public:
    KBrowserClient(KBrowserCallbacks callbacks, KNativeEndpointFactory endpointFactory, bool crashSmokeTest, bool rpcSmokeTest)
        : m_callbacks(std::move(callbacks)), m_endpointFactory(std::move(endpointFactory)),
          m_crashSmokeTest(crashSmokeTest), m_rpcSmokeTest(rpcSmokeTest)
    {
        m_router = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
        m_router->AddHandler(this, false);
    }
    ~KBrowserClient() override { closeEndpoint(); m_router->RemoveHandler(this); }
    bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int64_t queryId,
        const CefString& request, bool persistent, CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) override
    {
        if (!frame || !frame->IsMain() || frame->GetURL().ToString() != (m_rpcSmokeTest ? kRpcSmokeUrl : kAppUrl) || !m_endpoint)
        {
            callback->Failure(1, "REQUEST_DENIED");
            return true;
        }
        const auto endpoint = m_endpoint;
        endpoint->request(queryId, request.ToString(), persistent, [callback](KNativeMessageReply reply)
        {
            if (reply.m_success) callback->Success(reply.m_payload);
            else callback->Failure(2, reply.m_payload);
        });
        return true;
    }
    void OnQueryCanceled(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int64_t queryId) override
    {
        if (m_endpoint) m_endpoint->cancel(queryId);
    }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefProcessId source, CefRefPtr<CefProcessMessage> message) override
    {
        return m_router->OnProcessMessageReceived(browser, frame, source, message);
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { m_browser = browser; }
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        closeEndpoint();
        m_router->OnBeforeClose(browser);
        m_browser = nullptr;
        if (m_callbacks.m_closed) m_callbacks.m_closed();
    }
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override
    {
        if (!m_ready && title.ToString() == "LightOverLeaf · Native Ready")
        {
            m_ready = true;
            if (m_crashSmokeTest && !m_crashInjected)
            {
                m_crashInjected = true;
                // Explicit CLI-only test: crash this application's renderer.
                // Readiness is withheld until termination and successful reload.
                if (browser->GetHost()->ExecuteDevToolsMethod(0, "Page.crash", nullptr) == 0 && m_callbacks.m_failed)
                    m_callbacks.m_failed();
                return;
            }
            if (m_callbacks.m_ready) m_callbacks.m_ready();
        }
    }
    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
        const CefString&, const CefString&) override
    {
        if (frame->IsMain() && code != ERR_ABORTED && m_callbacks.m_failed) m_callbacks.m_failed();
    }
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request, bool, bool) override
    {
        if (request->GetURL().ToString() != (m_rpcSmokeTest ? kRpcSmokeUrl : kAppUrl)) return true;
        if (frame->IsMain()) closeEndpoint();
        m_router->OnBeforeBrowse(browser, frame);
        if (frame->IsMain()) openEndpoint();
        return false;
    }
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
        const CefString&, const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool,
        const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
        CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override
    {
        return true;
    }
    CefRefPtr<CefBrowser> browser() const { return m_browser; }
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus,
        int, const CefString&) override
    {
        m_ready = false;
        closeEndpoint();
        m_router->OnRenderProcessTerminated(browser);
        if (m_callbacks.m_rendererTerminated) m_callbacks.m_rendererTerminated();
    }

private:
    void closeEndpoint()
    {
        if (m_endpoint) m_endpoint->close();
        m_endpoint.reset();
    }
    void openEndpoint()
    {
        if (!m_endpointFactory) return;
        std::array<unsigned char, 32> randomBytes{};
        if (BCryptGenRandom(nullptr, randomBytes.data(), static_cast<ULONG>(randomBytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return;
        constexpr char hex[] = "0123456789abcdef";
        std::string sessionId;
        for (const unsigned char byte : randomBytes)
        {
            sessionId += hex[byte >> 4];
            sessionId += hex[byte & 15];
        }
        m_endpoint = m_endpointFactory([delay = m_rpcSmokeTest](std::function<void()> function)
        {
            // CLI-only fault injection leaves time for real renderer cancellation IPC.
            if (delay) return CefPostDelayedTask(TID_UI, new KCefFunctionTask(std::move(function)), 1000);
            return CefPostTask(TID_UI, new KCefFunctionTask(std::move(function)));
        }, sessionId);
    }

private:
    KBrowserCallbacks m_callbacks;
    KNativeEndpointFactory m_endpointFactory;
    std::shared_ptr<IKNativeMessageEndpoint> m_endpoint;
    CefRefPtr<CefMessageRouterBrowserSide> m_router;
    CefRefPtr<CefBrowser> m_browser = nullptr;
    bool m_ready = false;
    bool m_crashSmokeTest = false;
    bool m_crashInjected = false;
    bool m_rpcSmokeTest = false;
    IMPLEMENT_REFCOUNTING(KBrowserClient);
};

class KCefSurface final : public IKBrowserSurface
{
public:
    KCefSurface(KNativeEndpointFactory endpointFactory, bool crashSmokeTest, bool rpcSmokeTest)
        : m_endpointFactory(std::move(endpointFactory)), m_crashSmokeTest(crashSmokeTest), m_rpcSmokeTest(rpcSmokeTest) {}
    ~KCefSurface() override
    {
        m_client = nullptr;
        CefClearSchemeHandlerFactories();
        CefShutdown();
    }
    bool start(std::uintptr_t parent, int width, int height, KBrowserCallbacks callbacks) override
    {
        if (parent == 0 || width <= 0 || height <= 0 || m_client) return false;
        m_client = new KBrowserClient(std::move(callbacks), m_endpointFactory, m_crashSmokeTest, m_rpcSmokeTest);
        CefWindowInfo window;
        window.SetAsChild(reinterpret_cast<HWND>(parent), CefRect(0, 0, width, height));
        CefBrowserSettings settings;
        return CefBrowserHost::CreateBrowserSync(window, m_client, m_rpcSmokeTest ? kRpcSmokeUrl : kAppUrl, settings, nullptr, nullptr) != nullptr;
    }
    void resize(int width, int height) override
    {
        if (!m_client || !m_client->browser()) return;
        const HWND handle = m_client->browser()->GetHost()->GetWindowHandle();
        if (handle) SetWindowPos(handle, nullptr, 0, 0, std::max(width, 1), std::max(height, 1), SWP_NOACTIVATE | SWP_NOZORDER);
    }
    void close() override
    {
        if (m_client && m_client->browser()) m_client->browser()->GetHost()->CloseBrowser(true);
    }
    void pump() override { CefDoMessageLoopWork(); }
    bool isClosed() const override { return !m_client || !m_client->browser(); }
    void reload() override
    {
        if (m_client && m_client->browser()) m_client->browser()->ReloadIgnoreCache();
    }

private:
    CefRefPtr<KBrowserClient> m_client = nullptr;
    KNativeEndpointFactory m_endpointFactory;
    bool m_crashSmokeTest = false;
    bool m_rpcSmokeTest = false;
};
}
int executeCefProcess(std::uintptr_t instance, void* sandbox)
{
    if (instance == 0) return 2;
    return CefExecuteProcess(CefMainArgs(reinterpret_cast<HINSTANCE>(instance)), new KRendererApp(), sandbox);
}
std::unique_ptr<IKBrowserSurface> createCefSurface(std::uintptr_t instance, void* sandbox,
    const std::string& cachePath, const std::string& resourcePath,
    KNativeEndpointFactory endpointFactory, bool crashSmokeTest, bool rpcSmokeTest)
{
    if (instance == 0 || cachePath.empty() || resourcePath.empty()) return nullptr;
    std::error_code error;
    const auto root = std::filesystem::canonical(std::filesystem::path(std::u8string(resourcePath.begin(), resourcePath.end())), error);
    if (error || !std::filesystem::is_regular_file(root / "index.html", error)) return nullptr;
    CefSettings settings;
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.root_cache_path) = cachePath;
    CefString(&settings.cache_path) = cachePath;
    CefString(&settings.log_file) = cachePath + "/cef.log";
    if (!CefInitialize(CefMainArgs(reinterpret_cast<HINSTANCE>(instance)), settings, nullptr, sandbox)) return nullptr;
    if (!CefRegisterSchemeHandlerFactory("https", "app.lightoverleaf.local", new KResourceFactory(root)))
    {
        CefShutdown();
        return nullptr;
    }
    return std::make_unique<KCefSurface>(std::move(endpointFactory), crashSmokeTest, rpcSmokeTest);
}
}
