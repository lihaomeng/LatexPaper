#include <lightoverleaf/platform/kqtruntime.h>
#include <lightoverleaf/platform/ikbrowsersurface.h>
#include <lightoverleaf/platform/kbrowserlifecycle.h>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QPointer>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace lightoverleaf
{
class KQtRuntime::KImpl
{
public:
    int m_argc = 1;
    char m_name[14] = "LightOverLeaf";
    char* m_argv[2] = {m_name, nullptr};
    std::unique_ptr<QApplication> m_application;
    QPointer<QWidget> m_dialogParent;
};
namespace
{
class KShellWindow final : public QWidget
{
public:
    KShellWindow(IKBrowserSurface& surface, KBrowserLifecycle& lifecycle, QElapsedTimer& clock)
        : m_surface(surface), m_lifecycle(lifecycle), m_clock(clock)
    {
        setAttribute(Qt::WA_NativeWindow);
        setWindowTitle(QStringLiteral("LightOverLeaf"));
        QWidget::resize(1000, 700);
    }
    void requestClose()
    {
        if (m_lifecycle.requestClose(m_clock.elapsed()) == KBrowserAction::Close) m_surface.close();
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        event->ignore();
        requestClose();
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        m_surface.resize(event->size().width(), event->size().height());
    }

private:
    IKBrowserSurface& m_surface;
    KBrowserLifecycle& m_lifecycle;
    QElapsedTimer& m_clock;
};
}
KQtRuntime::KQtRuntime() : m_impl(std::make_unique<KImpl>())
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    m_impl->m_application = std::make_unique<QApplication>(m_impl->m_argc, m_impl->m_argv);
    QCoreApplication::setApplicationName(QStringLiteral("LightOverLeaf"));
    QCoreApplication::setOrganizationName(QStringLiteral("LightOverLeaf"));
    m_impl->m_application->setQuitOnLastWindowClosed(false);
}
KQtRuntime::~KQtRuntime() = default;
std::string KQtRuntime::cachePath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("cef")).toUtf8().toStdString();
}
std::string KQtRuntime::resourcePath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("web")).toUtf8().toStdString();
}
std::optional<std::string> KQtRuntime::selectWorkspace() const
{
    QWidget* const parent = m_impl->m_dialogParent.data();
    if (parent != nullptr)
    {
        parent->raise();
        parent->activateWindow();
    }
    const QString selected = QFileDialog::getExistingDirectory(parent,
        QStringLiteral("选择 LaTeX 项目文件夹"), QString{},
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selected.isEmpty()) return std::nullopt;
    return selected.toUtf8().toStdString();
}
std::optional<std::string> KQtRuntime::selectExportDestination() const
{
    QWidget* const parent = m_impl->m_dialogParent.data();
    if (parent != nullptr)
    {
        parent->raise();
        parent->activateWindow();
    }
    const QString selected = QFileDialog::getExistingDirectory(parent,
        QStringLiteral("选择空目录导出项目快照"), QString{},
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selected.isEmpty()) return std::nullopt;
    return selected.toUtf8().toStdString();
}int KQtRuntime::run(IKBrowserSurface& surface, bool smokeTest)
{
    QElapsedTimer clock;
    clock.start();
    KBrowserLifecycle lifecycle;
    lifecycle.start(clock.elapsed());
    KShellWindow window(surface, lifecycle, clock);
    m_impl->m_dialogParent = &window;
    const QPointer<KShellWindow> guard(&window);
    bool ready = false;
    int result = 0;
    KBrowserCallbacks callbacks;
    callbacks.m_ready = [&]
    {
        if (!lifecycle.ready()) return;
        ready = true;
        window.setWindowTitle(QStringLiteral("LightOverLeaf · Native Ready"));
        if (smokeTest) QTimer::singleShot(250, &window, [guard] { if (guard) guard->requestClose(); });
    };
    callbacks.m_failed = [&] { result = 2; window.requestClose(); };
    callbacks.m_closed = [&] { lifecycle.closed(); QApplication::exit(result); };
    callbacks.m_rendererTerminated = [&]
    {
        const KBrowserAction action = lifecycle.rendererFailed(clock.elapsed());
        if (action == KBrowserAction::Reload)
        {
            window.setWindowTitle(QStringLiteral("LightOverLeaf · 正在恢复页面"));
            // Defer reload until CEF finishes its termination callback.
            QTimer::singleShot(100, &window, [&]
            {
                if (lifecycle.state() == KBrowserState::Recovering) surface.reload();
            });
        }
        else if (action == KBrowserAction::Close)
        {
            result = 2;
            qCritical("Renderer recovery limit exceeded");
            surface.close();
        }
    };
    window.show();
    if (!surface.start(static_cast<std::uintptr_t>(window.winId()), window.width(), window.height(), std::move(callbacks)))
    {
        m_impl->m_dialogParent.clear();
        return 2;
    }
    QTimer pump;
    QObject::connect(&pump, &QTimer::timeout, &window, [&]
    {
        surface.pump();
        const KBrowserAction action = lifecycle.tick(clock.elapsed());
        if (action == KBrowserAction::Close)
        {
            result = 2;
            qCritical("Browser readiness timed out");
            surface.close();
        }
        else if (action == KBrowserAction::EmergencyExit)
        {
            qCritical("CEF close timed out; entry point must avoid unloading a live browser");
            QApplication::exit(3);
        }
    });
    pump.start(10);
    const int code = m_impl->m_application->exec();
    pump.stop();
    m_impl->m_dialogParent.clear();
    return code != 0 ? code : (ready ? 0 : 2);
}
}
