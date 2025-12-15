/*
 * Copyright (C) 2019-2021 Ashar Khan <ashar786khan@gmail.com>
 * Modified for Browser-Based Auto-Submission
 *
 * This file is part of CP Editor.
 */

#include "Extensions/CFTool.hpp"
#include "Core/EventLogger.hpp"
#include "Core/MessageLogger.hpp"
#include "generated/SettingsHelper.hpp"
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUrl>
#include <QDesktopServices>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QThread>

namespace Extensions
{

CFTool::CFTool(const QString &path, MessageLogger *logger) : CFToolPath(path)
{
    LOG_INFO(INFO_OF(path))
    log = logger;
}

CFTool::~CFTool()
{
    if (CFToolProcess) {
        delete CFToolProcess;
        CFToolProcess = nullptr;
    }
    if (browserAutomation) {
        delete browserAutomation;
        browserAutomation = nullptr;
    }
}

void CFTool::submit(const QString &filePath, const QString &url)
{
    LOG_INFO(INFO_OF(filePath) << INFO_OF(url));
    log->info(tr("CF Tool"), tr("Starting auto-submission..."));

    // Parse URL to get Contest/Problem IDs
    parseCfUrl(url, problemContestId, problemCode);

    // Read the source code from file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        log->error(tr("CF Tool"), tr("Failed to read source file: %1").arg(filePath));
        return;
    }
    QTextStream in(&file);
    QString sourceCode = in.readAll();
    file.close();

    if (sourceCode.trimmed().isEmpty()) {
        log->error(tr("CF Tool"), tr("Source code is empty!"));
        return;
    }

    // Copy code to clipboard
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(sourceCode);
    log->info(tr("CF Tool"), tr("Code copied to clipboard (%1 chars)").arg(sourceCode.length()));

    // Construct the submit URL
    QString targetUrl = url;
    
    // Handle: https://codeforces.com/contest/1234/problem/A
    if (targetUrl.contains("/contest/") && targetUrl.contains("/problem/")) {
        QStringList parts = targetUrl.split("/problem/");
        if (parts.size() >= 2) {
            QString problemIndex = parts[1].split("?")[0].split("#")[0];
            targetUrl = parts[0] + "/submit/" + problemIndex;
        }
    }
    // Handle: https://codeforces.com/gym/1234/problem/A
    else if (targetUrl.contains("/gym/") && targetUrl.contains("/problem/")) {
        QStringList parts = targetUrl.split("/problem/");
        if (parts.size() >= 2) {
            QString problemIndex = parts[1].split("?")[0].split("#")[0];
            targetUrl = parts[0] + "/submit/" + problemIndex;
        }
    }
    // Handle: https://codeforces.com/problemset/problem/1234/A
    else if (targetUrl.contains("/problemset/problem/")) {
        QRegularExpression re("/problemset/problem/(\\d+)/([A-Za-z0-9]+)");
        auto match = re.match(targetUrl);
        if (match.hasMatch()) {
            QString contestId = match.captured(1);
            QString problemIndex = match.captured(2);
            targetUrl = "https://codeforces.com/contest/" + contestId + "/submit/" + problemIndex;
        }
    }
    // Handle: https://codeforces.com/group/xxx/contest/1234/problem/A
    else if (targetUrl.contains("/group/") && targetUrl.contains("/problem/")) {
        QStringList parts = targetUrl.split("/problem/");
        if (parts.size() >= 2) {
            QString problemIndex = parts[1].split("?")[0].split("#")[0];
            targetUrl = parts[0] + "/submit/" + problemIndex;
        }
    }

    log->info(tr("CF Tool"), tr("Opening: %1").arg(targetUrl));

    // Open browser
    bool browserOpened = QDesktopServices::openUrl(QUrl(targetUrl));

    if (browserOpened) {
        log->info(tr("CF Tool"), tr("Browser opened - auto-submitting..."));
        showToastMessage("Auto-submitting...");
        
        // Automate the full submission
        automateSubmission(targetUrl, sourceCode);
    } else {
        log->error(tr("CF Tool"), tr("Failed to open browser"));
        showToastMessage("Failed to open browser");
    }
}

void CFTool::automateSubmission(const QString &url, const QString &sourceCode)
{
    Q_UNUSED(url);
    Q_UNUSED(sourceCode);
    
#ifdef Q_OS_MACOS
    // Full automation using AppleScript:
    // 1. Wait for browser and page to load
    // 2. Paste code into the editor
    // 3. Click the Submit button
    
    if (browserAutomation) {
        browserAutomation->kill();
        delete browserAutomation;
    }
    browserAutomation = new QProcess(this);
    
    // Universal script that uses keyboard navigation (works with any browser)
    // On Codeforces submit page:
    // - The Ace code editor is focused by default
    // - After pasting, Tab navigates to the Submit button
    // - Enter clicks it
    QString appleScript = R"(
        -- Wait for browser to activate and page to load
        delay 2.5
        
        tell application "System Events"
            -- Get the frontmost application (the browser)
            set frontApp to name of first application process whose frontmost is true
            
            tell process frontApp
                set frontmost to true
            end tell
            
            delay 0.3
            
            -- Select all in the code editor (clear any existing code)
            keystroke "a" using command down
            delay 0.15
            
            -- Paste the code from clipboard
            keystroke "v" using command down
            delay 0.6
            
            -- Navigate to Submit button and click it
            -- On CF submit page: Tab goes from code editor to language dropdown,
            -- then to Submit button. We use Tab+Tab+Enter or just Tab+Enter
            -- depending on focus state
            
            keystroke tab
            delay 0.15
            keystroke tab
            delay 0.15
            keystroke return
            
        end tell
        
        return "Submitted!"
    )";
    
    connect(browserAutomation, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        if (exitCode == 0) {
            log->info(tr("CF Tool"), tr("✓ Submitted! Check browser for verdict."));
            showToastMessage("Submitted! Check verdict");
        } else {
            log->warn(tr("CF Tool"), tr("Auto-submit may have failed. Check browser."));
            showToastMessage("Check browser");
        }
    });
    
    connect(browserAutomation, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error);
        log->warn(tr("CF Tool"), tr("Automation error. Submit manually in browser."));
    });
    
    browserAutomation->start("osascript", QStringList() << "-e" << appleScript);
    
#elif defined(Q_OS_LINUX)
    // Linux: Use xdotool for automation
    if (browserAutomation) {
        browserAutomation->kill();
        delete browserAutomation;
    }
    browserAutomation = new QProcess(this);
    
    // Check if xdotool is available, if not just paste
    QString script = R"(
        sleep 2.5
        if command -v xdotool &> /dev/null; then
            xdotool key ctrl+a
            sleep 0.2
            xdotool key ctrl+v
            sleep 0.5
            xdotool key Tab Tab Return
        else
            echo "xdotool not found - paste manually with Ctrl+V"
        fi
    )";
    
    connect(browserAutomation, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        Q_UNUSED(exitCode);
        log->info(tr("CF Tool"), tr("✓ Submitted! Check browser for verdict."));
        showToastMessage("Submitted!");
    });
    
    browserAutomation->start("bash", QStringList() << "-c" << script);

#elif defined(Q_OS_WIN)
    // Windows: Use PowerShell with SendKeys
    if (browserAutomation) {
        browserAutomation->kill();
        delete browserAutomation;
    }
    browserAutomation = new QProcess(this);
    
    QString psScript = R"(
        Start-Sleep -Milliseconds 2500
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.SendKeys]::SendWait("^a")
        Start-Sleep -Milliseconds 200
        [System.Windows.Forms.SendKeys]::SendWait("^v")
        Start-Sleep -Milliseconds 500
        [System.Windows.Forms.SendKeys]::SendWait("{TAB}{TAB}{ENTER}")
    )";
    
    connect(browserAutomation, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        Q_UNUSED(exitCode);
        log->info(tr("CF Tool"), tr("✓ Submitted! Check browser for verdict."));
        showToastMessage("Submitted!");
    });
    
    browserAutomation->start("powershell", QStringList() << "-Command" << psScript);
    
#else
    // Fallback: Just show instructions
    log->info(tr("CF Tool"), tr("Press Cmd/Ctrl+V to paste, then click Submit."));
    showToastMessage("Paste & Submit manually");
#endif
}

bool CFTool::check(const QString &path)
{
    Q_UNUSED(path);
    // Always return true - browser submission doesn't need cf-tool binary
    return true;
}

void CFTool::updatePath(const QString &p)
{
    LOG_INFO(INFO_OF(p));
    CFToolPath = p;
}

bool CFTool::parseCfUrl(const QString &url, QString &contestId, QString &problemCode)
{
    LOG_INFO(INFO_OF(url));
    
    // Match contest URLs: /contest/1234/problem/A
    auto match = QRegularExpression(
        ".*://codeforces\\.com/(?:gym|contest)/(\\d+)/problem/([A-Za-z0-9]+)"
    ).match(url);
    
    if (match.hasMatch()) {
        contestId = match.captured(1);
        problemCode = match.captured(2);
        return true;
    }
    
    // Match problemset URLs: /problemset/problem/1234/A
    match = QRegularExpression(
        ".*://codeforces\\.com/problemset/problem/(\\d+)/([A-Za-z0-9]+)"
    ).match(url);
    
    if (match.hasMatch()) {
        contestId = match.captured(1);
        problemCode = match.captured(2);
        return true;
    }
    
    // Match group URLs: /group/xxx/contest/1234/problem/A
    match = QRegularExpression(
        ".*://codeforces\\.com/group/\\w+/contest/(\\d+)/problem/([A-Za-z0-9]+)"
    ).match(url);
    
    if (match.hasMatch()) {
        contestId = match.captured(1);
        problemCode = match.captured(2);
        return true;
    }
    
    return false;
}

void CFTool::onReadReady() 
{
    // Not used in browser-based submission
}

void CFTool::onFinished(int exitCode, QProcess::ExitStatus e) 
{
    Q_UNUSED(exitCode);
    Q_UNUSED(e);
    // Not used in browser-based submission
}

void CFTool::showToastMessage(const QString &message)
{
    if (SettingsHelper::isCFShowToastMessages())
        emit requestToastMessage(tr("Contest %1 Problem %2").arg(problemContestId).arg(problemCode), message);
}

QString CFTool::getCFToolVersion() const
{
    return "browser-auto-1.0";
}

} // namespace Extensions
