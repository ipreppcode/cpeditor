/*
 * Copyright (C) 2019-2021 Ashar Khan <ashar786khan@gmail.com>
 * Modified by Gemini for Browser-Based Submission
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

// --- NEW HEADERS FOR BROWSER SUPPORT ---
#include <QDesktopServices>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>
// ---------------------------------------

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
}

void CFTool::submit(const QString &filePath, const QString &url)
{
    // 1. Log the attempt
    LOG_INFO(INFO_OF(filePath) << INFO_OF(url));
    log->info(tr("CF Tool"), tr("Preparing browser submission..."));

    // 2. Parse URL to get Contest/Problem IDs (for the toast message)
    parseCfUrl(url, problemContestId, problemCode);

    // 3. Read the Source Code from the file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        log->error(tr("CF Tool"), tr("Failed to read source file: %1").arg(filePath));
        return;
    }
    QTextStream in(&file);
    QString sourceCode = in.readAll();
    file.close();

    // 4. Copy to Clipboard
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(sourceCode);
    log->info(tr("CF Tool"), tr("Code copied to clipboard."));

    // 5. Construct the Submit URL
    // Standard URL: https://codeforces.com/contest/1234/problem/C
    // Target URL:   https://codeforces.com/contest/1234/submit?problemIndex=C
    
    QString targetUrl = url;
    
    if (targetUrl.contains("/contest/") && targetUrl.contains("/problem/")) {
        // Handle Standard Contest URLs (Auto-select problem)
        QStringList parts = targetUrl.split("/problem/");
        // parts[0] is ".../contest/1234"
        // parts[1] is "C" (or "C1")
        if (parts.size() >= 2) {
            targetUrl = parts[0] + "/submit?problemIndex=" + parts[1];
        }
    }
    else if (targetUrl.contains("/problem/")) {
        // Fallback for Problemset or generic links
        targetUrl = targetUrl.split("/problem/")[0] + "/submit";
    }

    // 6. Open the Browser
    bool browserOpened = QDesktopServices::openUrl(QUrl(targetUrl));

    if (browserOpened) {
        log->info(tr("CF Tool"), tr("Browser opened to: %1").arg(targetUrl));
        showToastMessage("Copied & Opened Browser");
    } else {
        log->error(tr("CF Tool"), tr("Failed to open browser."));
        showToastMessage("Failed to open browser");
    }
}

bool CFTool::check(const QString &path)
{
    // ALWAYS return true so the button stays enabled.
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
    auto match =
        QRegularExpression(".*://codeforces.com/(?:gym|contest)/([1-9][0-9]*)/problem/(0|[A-Z][1-9]?)").match(url);
    if (match.hasMatch())
    {
        contestId = match.captured(1);
        problemCode = match.captured(2);
        return true;
    }
    match = QRegularExpression(".*://codeforces.com/problemset/problem/([1-9][0-9]*)/([A-Z][1-9]?)").match(url);
    if (match.hasMatch())
    {
        contestId = match.captured(1);
        problemCode = match.captured(2);
        return true;
    }
    return false;
}

void CFTool::onReadReady() {}
void CFTool::onFinished(int exitCode, QProcess::ExitStatus e) {}

void CFTool::showToastMessage(const QString &message)
{
    if (SettingsHelper::isCFShowToastMessages())
        emit requestToastMessage(tr("Contest %1 Problem %2").arg(problemContestId).arg(problemCode), message);
}

QString CFTool::getCFToolVersion() const
{
    return "1.0.0-custom";
}

} // namespace Extensions
