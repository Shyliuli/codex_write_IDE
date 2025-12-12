#include "MainWindow.h"

#include "BuildManager.h"
#include "CodeEditor.h"
#include "CppRusticHighlighter.h"
#include "FindReplaceDialog.h"
#include "GdbMiClient.h"
#include "LspClient.h"
#include "ProjectManager.h"
#include "ProjectSettingsDialog.h"
#include "ShortcutSettingsDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QToolBar>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QSet>
#include <QScreen>

#include <functional>
#include <algorithm>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#include <cstdio> // DEBUG_STARTUP

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      tabWidget_(new QTabWidget(this)),
      output_(new QPlainTextEdit(this)) {
    std::fprintf(stderr, "[DEBUG_STARTUP] MainWindow ctor begin\n");
    std::fflush(stderr);
    tabWidget_->setTabsClosable(true);
    tabWidget_->setMovable(true);
    setCentralWidget(tabWidget_);
    output_->setReadOnly(true);

    connect(tabWidget_->tabBar(), &QTabBar::tabMoved, this, [this](int from, int to) {
        if (from < 0 || to < 0 || from >= openTabs_.size() || to >= openTabs_.size()) {
            return;
        }
        openTabs_.move(from, to);
    });

    createActions();
    std::fprintf(stderr, "[DEBUG_STARTUP] createActions done\n");
    std::fflush(stderr);
    createMenus();
    std::fprintf(stderr, "[DEBUG_STARTUP] createMenus done\n");
    std::fflush(stderr);
    createToolBar();
    std::fprintf(stderr, "[DEBUG_STARTUP] createToolBar done\n");
    std::fflush(stderr);
    createDocks();
    std::fprintf(stderr, "[DEBUG_STARTUP] createDocks done\n");
    std::fflush(stderr);

    loadUiSettings();
    std::fprintf(stderr, "[DEBUG_STARTUP] loadUiSettings done\n");
    std::fflush(stderr);

    findDialog_ = new FindReplaceDialog(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] findDialog created\n");
    std::fflush(stderr);

    // 基础高亮器会在每个标签页创建时绑定到对应 document。

    buildManager_ = std::make_unique<BuildManager>(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] buildManager created\n");
    std::fflush(stderr);
    connect(buildManager_.get(), &BuildManager::outputReady, this, &MainWindow::appendBuildOutput);
    connect(buildManager_.get(), &BuildManager::buildFinished, this, &MainWindow::buildFinished);

    projectManager_ = std::make_unique<ProjectManager>(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] projectManager created\n");
    std::fflush(stderr);
    lspClient_ = std::make_unique<LspClient>(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] lspClient created\n");
    std::fflush(stderr);
    gdbClient_ = std::make_unique<GdbMiClient>(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] gdbClient created\n");
    std::fflush(stderr);
    connect(projectManager_.get(), &ProjectManager::projectChanged, this, [this]() {
        if (!lspClient_) {
            return;
        }
        QString root;
        if (projectManager_->hasProject()) {
            root = projectManager_->rootDir();
        } else if (!currentFile_.isEmpty()) {
            root = QFileInfo(currentFile_).absolutePath();
        } else {
            root = QDir::currentPath();
        }

        if (lspClient_->isRunning()) {
            lspClient_->stop();
        }
        lspClient_->start(root);
        for (const auto &tab : openTabs_) {
            if (!tab.filePath.isEmpty()) {
                lspClient_->openDocument(tab.filePath, tab.editor->toPlainText());
            }
        }
    });
    connect(lspClient_.get(), &LspClient::diagnosticsUpdated, this, &MainWindow::handleDiagnostics);
    connect(lspClient_.get(), &LspClient::completionItemsReady, this, &MainWindow::handleCompletionItems);
    connect(lspClient_.get(), &LspClient::documentSymbolsReady, this, &MainWindow::handleDocumentSymbols);
    connect(lspClient_.get(), &LspClient::foldingRangesReady, this, &MainWindow::handleFoldingRanges);
    connect(lspClient_.get(), &LspClient::semanticTokensReady, this, &MainWindow::handleSemanticTokens);
    connect(lspClient_.get(), &LspClient::definitionLocationsReady, this, &MainWindow::handleDefinitionLocations);
    connect(lspClient_.get(), &LspClient::referencesLocationsReady, this, &MainWindow::handleReferencesLocations);
    connect(lspClient_.get(), &LspClient::renameEditsReady, this, &MainWindow::handleRenameEdits);
    connect(lspClient_.get(), &LspClient::serverLog, this, &MainWindow::appendBuildOutput);

    connect(gdbClient_.get(), &GdbMiClient::consoleOutput, this, [this](const QString &text) {
        if (debugOutput_) {
            debugOutput_->appendPlainText(text.trimmed());
        }
    });
    connect(gdbClient_.get(), &GdbMiClient::stopped, this, [this](const QString &file, int line) {
        if (debugInfoDock_) {
            debugInfoDock_->show();
        }
        if (!file.isEmpty()) {
            jumpToFileLocation(file, line - 1, 0, true);
            highlightDebugLine(file, line - 1);
        }
        refreshWatchExpressions();
    });
    connect(gdbClient_.get(), &GdbMiClient::breakpointsUpdated, this, [this](const QList<GdbBreakpoint> &bps) {
        breakpointsTree_->clear();
        breakpointsByFile_.clear();
        for (const auto &bp : bps) {
            const QString location = QStringLiteral("%1:%2").arg(QFileInfo(bp.file).fileName()).arg(bp.line);
            const QString enabledText = bp.enabled ? tr("是") : tr("否");
            QString extra = bp.condition;
            if (bp.ignoreCount > 0) {
                if (!extra.isEmpty()) {
                    extra += " | ";
                }
                extra += tr("命中 %1 次后暂停").arg(bp.ignoreCount);
            }
            auto *item = new QTreeWidgetItem(breakpointsTree_, QStringList{QString::number(bp.number), location, enabledText, extra});
            item->setData(0, Qt::UserRole, bp.number);
            item->setData(0, Qt::UserRole + 1, bp.file);
            item->setData(0, Qt::UserRole + 2, bp.enabled);
            item->setData(0, Qt::UserRole + 3, bp.ignoreCount);
            breakpointsByFile_[QFileInfo(bp.file).absoluteFilePath()].insert(bp.line - 1);
        }
        for (auto &tab : openTabs_) {
            if (!tab.filePath.isEmpty()) {
                const QString abs = QFileInfo(tab.filePath).absoluteFilePath();
                tab.editor->setBreakpoints(breakpointsByFile_.value(abs));
            }
        }
    });
    connect(gdbClient_.get(), &GdbMiClient::stackUpdated, this, [this](const QList<GdbStackFrame> &frames) {
        stackTree_->clear();
        for (const auto &f : frames) {
            auto *item = new QTreeWidgetItem(stackTree_,
                                            QStringList{QString::number(f.level), f.func, QStringLiteral("%1:%2").arg(QFileInfo(f.file).fileName()).arg(f.line)});
            item->setData(0, Qt::UserRole, f.file);
            item->setData(0, Qt::UserRole + 1, f.line - 1);
            item->setData(0, Qt::UserRole + 2, f.level);
        }
    });
    connect(gdbClient_.get(), &GdbMiClient::localsUpdated, this, [this](const QList<GdbVariable> &vars) {
        localsTree_->clear();
        for (const auto &v : vars) {
            new QTreeWidgetItem(localsTree_, QStringList{v.name, v.value});
        }
    });
    connect(gdbClient_.get(), &GdbMiClient::threadsUpdated, this, [this](const QList<GdbThread> &threads) {
        threadsTree_->clear();
        for (const auto &t : threads) {
            const QString loc = t.file.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(QFileInfo(t.file).fileName()).arg(t.line);
            auto *item = new QTreeWidgetItem(threadsTree_, QStringList{QString::number(t.id), t.state, loc});
            item->setData(0, Qt::UserRole, t.id);
            if (t.current) {
                item->setBackground(0, QColor(80, 120, 200, 60));
            }
        }
    });
    connect(gdbClient_.get(), &GdbMiClient::expressionEvaluated, this, [this](const QString &expr, const QString &value) {
        auto items = watchTree_->findItems(expr, Qt::MatchExactly, 0);
        QTreeWidgetItem *item = items.isEmpty() ? nullptr : items.first();
        if (!item) {
            item = new QTreeWidgetItem(watchTree_, QStringList{expr, value});
        }
        const QString last = watchLastValues_.value(expr);
        if (!last.isEmpty() && last != value) {
            item->setBackground(1, QColor(255, 230, 150));
        } else {
            item->setBackground(1, QBrush());
        }
        item->setText(1, value);
        watchLastValues_[expr] = value;
    });
    connect(gdbClient_.get(), &GdbMiClient::exited, this, [this](int code) {
        if (debugOutput_) {
            debugOutput_->appendPlainText(tr("调试结束，退出码：%1").arg(code));
        }
        highlightDebugLine(QString(), -1);
    });

    std::fprintf(stderr, "[DEBUG_STARTUP] gdb/lsp connections done\n");
    std::fflush(stderr);

    lspChangeTimer_ = new QTimer(this);
    std::fprintf(stderr, "[DEBUG_STARTUP] lspChangeTimer created\n");
    std::fflush(stderr);
    lspChangeTimer_->setSingleShot(true);
    lspChangeTimer_->setInterval(400);
    connect(lspChangeTimer_, &QTimer::timeout, this, &MainWindow::sendLspChange);

    connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabWidget_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index < 0) {
            currentFile_.clear();
            updateWindowTitle();
            return;
        }
        OpenTab *tab = tabAt(index);
        if (tab) {
            currentFile_ = tab->filePath;
            updateWindowTitle();
            if (!currentFile_.isEmpty()) {
                if (!lspClient_->isRunning()) {
                    const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QFileInfo(currentFile_).absolutePath();
                    lspClient_->start(root);
                }
                lspClient_->setCurrentDocument(tab->editor->document(), currentFile_);
                lspClient_->openDocument(currentFile_, tab->editor->toPlainText());
            }
        }
    });

    std::fprintf(stderr, "[DEBUG_STARTUP] tabWidget signals connected, calling createNewTab\n");
    std::fflush(stderr);
    createNewTab();
    std::fprintf(stderr, "[DEBUG_STARTUP] createNewTab done, ctor end\n");
    std::fflush(stderr);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSaveAllTabs()) {
        saveUiSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    std::fprintf(stderr, "[DEBUG_STARTUP] showEvent fired, firstShow=%d\n", firstShow_ ? 1 : 0);
    std::fflush(stderr);
    if (!firstShow_) {
        return;
    }
    firstShow_ = false;
    QTimer::singleShot(0, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
}

void MainWindow::setLightTheme() {
    applyTheme(false);
}

void MainWindow::setDarkTheme() {
    applyTheme(true);
}

void MainWindow::importColorScheme() {
    const QString path = QFileDialog::getOpenFileName(this, tr("导入配色方案"), QDir::currentPath(), tr("配色方案 (*.json)") );
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("导入失败"), tr("无法读取文件：%1").arg(path));
        return;
    }
    const QByteArray data = file.readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("导入失败"), tr("配色方案格式错误：%1").arg(err.errorString()));
        return;
    }
    const QJsonObject obj = doc.object();
    ColorScheme scheme = CppRusticHighlighter::defaultScheme();
    auto read = [&obj](const char *key, const QColor &fallback) {
        const QString val = obj.value(key).toString();
        return val.isEmpty() ? fallback : QColor(val);
    };
    scheme.keyword = read("keyword", scheme.keyword);
    scheme.rusticKeyword = read("rusticKeyword", scheme.rusticKeyword);
    scheme.rusticType = read("rusticType", scheme.rusticType);
    scheme.function = read("function", scheme.function);
    scheme.preprocessor = read("preprocessor", scheme.preprocessor);
    scheme.comment = read("comment", scheme.comment);
    scheme.stringLiteral = read("string", scheme.stringLiteral);
    scheme.number = read("number", scheme.number);

    CppRusticHighlighter::saveSchemeToSettings(scheme);
    for (auto &tab : openTabs_) {
        if (tab.highlighter) {
            tab.highlighter->setColorScheme(scheme);
        }
    }
    statusBar()->showMessage(tr("配色方案已导入"), 2000);
}

void MainWindow::exportColorScheme() {
    const QString path = QFileDialog::getSaveFileName(this, tr("导出配色方案"), QDir::currentPath(), tr("配色方案 (*.json)") );
    if (path.isEmpty()) {
        return;
    }
    const ColorScheme scheme = CppRusticHighlighter::loadSchemeFromSettings();
    QJsonObject obj;
    obj.insert("keyword", scheme.keyword.name());
    obj.insert("rusticKeyword", scheme.rusticKeyword.name());
    obj.insert("rusticType", scheme.rusticType.name());
    obj.insert("function", scheme.function.name());
    obj.insert("preprocessor", scheme.preprocessor.name());
    obj.insert("comment", scheme.comment.name());
    obj.insert("string", scheme.stringLiteral.name());
    obj.insert("number", scheme.number.name());

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(path));
        return;
    }
    QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Indented));
    statusBar()->showMessage(tr("配色方案已导出"), 2000);
}

void MainWindow::applyTheme(bool dark) {
    darkThemeEnabled_ = dark;

    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(35, 35, 35));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
    } else {
        palette = QApplication::style()->standardPalette();
    }
    QApplication::setPalette(palette);

    for (auto &tab : openTabs_) {
        if (tab.editor) {
            tab.editor->setDarkThemeEnabled(dark);
        }
    }

    if (themeLightAct_) {
        themeLightAct_->setChecked(!dark);
    }
    if (themeDarkAct_) {
        themeDarkAct_->setChecked(dark);
    }

    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    settings.setValue("theme/dark", dark);
}

void MainWindow::loadUiSettings() {
    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    const QByteArray geom = settings.value("ui/geometry").toByteArray();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    }
    const QByteArray state = settings.value("ui/state").toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }

    // 避免上次退出时处于最小化状态导致启动后看不到窗口。
    if (windowState() & Qt::WindowMinimized) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }

    const QRect current = geometry();
    bool onScreen = false;
    for (QScreen *screen : QApplication::screens()) {
        if (screen && screen->availableGeometry().intersects(current)) {
            onScreen = true;
            break;
        }
    }
    if (!onScreen) {
        if (QScreen *primary = QApplication::primaryScreen()) {
            const QRect avail = primary->availableGeometry();
            resize(1100, 720);
            move(avail.center() - QPoint(width() / 2, height() / 2));
        }
    }
    const bool dark = settings.value("theme/dark", false).toBool();
    applyTheme(dark);

    auto loadShortcut = [&settings](QAction *act) {
        if (!act) {
            return;
        }
        const QString key = act->objectName().isEmpty() ? act->text() : act->objectName();
        const QString seq = settings.value(QStringLiteral("shortcuts/") + key).toString();
        if (!seq.isEmpty()) {
            act->setShortcut(QKeySequence(seq));
        }
    };

    loadShortcut(newAct_);
    loadShortcut(openAct_);
    loadShortcut(saveAct_);
    loadShortcut(saveAsAct_);
    loadShortcut(exitAct_);
    loadShortcut(findAct_);
    loadShortcut(replaceAct_);
    loadShortcut(findInFilesAct_);
    loadShortcut(compileAct_);
    loadShortcut(rebuildAct_);
    loadShortcut(cleanAct_);
    loadShortcut(runAct_);
    loadShortcut(makefileAct_);
    loadShortcut(externalToolAct_);
    loadShortcut(debugStartAct_);
    loadShortcut(debugBuildAndStartAct_);
    loadShortcut(debugRestartAct_);
    loadShortcut(debugContinueAct_);
    loadShortcut(debugStepOverAct_);
    loadShortcut(debugStepIntoAct_);
    loadShortcut(debugStepOutAct_);
    loadShortcut(debugToggleBpAct_);
    loadShortcut(debugAddWatchAct_);
    loadShortcut(debugRemoveWatchAct_);
    loadShortcut(debugStopAct_);
    loadShortcut(navBackAct_);
    loadShortcut(navForwardAct_);
    loadShortcut(findReferencesAct_);
    loadShortcut(renameSymbolAct_);
    loadShortcut(newProjectAct_);
    loadShortcut(openProjectAct_);
    loadShortcut(saveProjectAct_);
    loadShortcut(closeProjectAct_);
    loadShortcut(addSourceAct_);
    loadShortcut(addIncludeAct_);
    loadShortcut(fetchRusticAct_);
    loadShortcut(projectSettingsAct_);
    loadShortcut(terminalAct_);
}

void MainWindow::saveUiSettings() {
    QSettings settings(QStringLiteral("RusticCppIDE"), QStringLiteral("RusticCppIDE"));
    settings.setValue("ui/geometry", saveGeometry());
    settings.setValue("ui/state", saveState());
    settings.setValue("theme/dark", darkThemeEnabled_);
}

void MainWindow::createActions() {
    newAct_ = new QAction(tr("新建"), this);
    newAct_->setObjectName("file.new");
    newAct_->setShortcuts(QKeySequence::New);
    connect(newAct_, &QAction::triggered, this, &MainWindow::newFile);

    openAct_ = new QAction(tr("打开..."), this);
    openAct_->setObjectName("file.open");
    openAct_->setShortcuts(QKeySequence::Open);
    connect(openAct_, &QAction::triggered, this, &MainWindow::openFile);

    saveAct_ = new QAction(tr("保存"), this);
    saveAct_->setObjectName("file.save");
    saveAct_->setShortcuts(QKeySequence::Save);
    connect(saveAct_, &QAction::triggered, this, &MainWindow::saveFile);

    saveAsAct_ = new QAction(tr("另存为..."), this);
    saveAsAct_->setObjectName("file.saveAs");
    saveAsAct_->setShortcuts(QKeySequence::SaveAs);
    connect(saveAsAct_, &QAction::triggered, this, &MainWindow::saveFileAs);

    findAct_ = new QAction(tr("查找..."), this);
    findAct_->setObjectName("edit.find");
    findAct_->setShortcuts(QKeySequence::Find);
    connect(findAct_, &QAction::triggered, this, &MainWindow::showFindDialog);

    replaceAct_ = new QAction(tr("替换..."), this);
    replaceAct_->setObjectName("edit.replace");
    replaceAct_->setShortcuts(QKeySequence::Replace);
    connect(replaceAct_, &QAction::triggered, this, &MainWindow::showReplaceDialog);

    findInFilesAct_ = new QAction(tr("全工程搜索..."), this);
    findInFilesAct_->setObjectName("edit.findInFiles");
    findInFilesAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(findInFilesAct_, &QAction::triggered, this, &MainWindow::findInFiles);

    navBackAct_ = new QAction(tr("后退"), this);
    navBackAct_->setObjectName("nav.back");
    navBackAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(navBackAct_, &QAction::triggered, this, &MainWindow::navigateBack);

    navForwardAct_ = new QAction(tr("前进"), this);
    navForwardAct_->setObjectName("nav.forward");
    navForwardAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(navForwardAct_, &QAction::triggered, this, &MainWindow::navigateForward);

    findReferencesAct_ = new QAction(tr("查找引用"), this);
    findReferencesAct_->setObjectName("nav.references");
    findReferencesAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    connect(findReferencesAct_, &QAction::triggered, this, &MainWindow::requestReferencesAtCursor);

    renameSymbolAct_ = new QAction(tr("重命名符号"), this);
    renameSymbolAct_->setObjectName("nav.rename");
    renameSymbolAct_->setShortcut(Qt::Key_F2);
    connect(renameSymbolAct_, &QAction::triggered, this, &MainWindow::renameSymbolAtCursor);

    exitAct_ = new QAction(tr("退出"), this);
    exitAct_->setObjectName("file.exit");
    exitAct_->setShortcut(QKeySequence::Quit);
    connect(exitAct_, &QAction::triggered, this, &QWidget::close);

    compileAct_ = new QAction(tr("一键编译"), this);
    compileAct_->setObjectName("build.compile");
    compileAct_->setShortcut(Qt::Key_F9);
    connect(compileAct_, &QAction::triggered, this, &MainWindow::compileFile);

    rebuildAct_ = new QAction(tr("重新编译(重建)"), this);
    rebuildAct_->setObjectName("build.rebuild");
    rebuildAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F9));
    connect(rebuildAct_, &QAction::triggered, this, &MainWindow::rebuildProject);

    cleanAct_ = new QAction(tr("清理输出"), this);
    cleanAct_->setObjectName("build.clean");
    connect(cleanAct_, &QAction::triggered, this, &MainWindow::cleanProject);

    runAct_ = new QAction(tr("运行"), this);
    runAct_->setObjectName("build.run");
    runAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F10));
    connect(runAct_, &QAction::triggered, this, &MainWindow::runFile);

    makefileAct_ = new QAction(tr("生成 Makefile"), this);
    makefileAct_->setObjectName("build.makefile");
    connect(makefileAct_, &QAction::triggered, this, &MainWindow::generateMakefile);

    externalToolAct_ = new QAction(tr("运行外部工具..."), this);
    externalToolAct_->setObjectName("build.externalTool");
    connect(externalToolAct_, &QAction::triggered, this, &MainWindow::runExternalTool);

    shortcutSettingsAct_ = new QAction(tr("快捷键设置..."), this);
    shortcutSettingsAct_->setObjectName("tools.shortcuts");
    connect(shortcutSettingsAct_, &QAction::triggered, this, &MainWindow::showShortcutSettings);

    debugStartAct_ = new QAction(tr("开始调试"), this);
    debugStartAct_->setObjectName("debug.start");
    debugStartAct_->setShortcut(Qt::Key_F5);
    connect(debugStartAct_, &QAction::triggered, this, &MainWindow::startDebug);

    debugStopAct_ = new QAction(tr("停止调试"), this);
    debugStopAct_->setObjectName("debug.stop");
    debugStopAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    connect(debugStopAct_, &QAction::triggered, this, &MainWindow::stopDebug);

    debugContinueAct_ = new QAction(tr("继续运行"), this);
    debugContinueAct_->setObjectName("debug.continue");
    debugContinueAct_->setShortcut(Qt::Key_F6);
    connect(debugContinueAct_, &QAction::triggered, this, &MainWindow::continueDebug);

    debugStepOverAct_ = new QAction(tr("单步跳过"), this);
    debugStepOverAct_->setObjectName("debug.stepOver");
    debugStepOverAct_->setShortcut(Qt::Key_F10);
    connect(debugStepOverAct_, &QAction::triggered, this, &MainWindow::stepOverDebug);

    debugStepIntoAct_ = new QAction(tr("单步进入"), this);
    debugStepIntoAct_->setObjectName("debug.stepInto");
    debugStepIntoAct_->setShortcut(Qt::Key_F11);
    connect(debugStepIntoAct_, &QAction::triggered, this, &MainWindow::stepIntoDebug);

    debugStepOutAct_ = new QAction(tr("单步跳出"), this);
    debugStepOutAct_->setObjectName("debug.stepOut");
    debugStepOutAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    connect(debugStepOutAct_, &QAction::triggered, this, &MainWindow::stepOutDebug);

    debugToggleBpAct_ = new QAction(tr("切换断点"), this);
    debugToggleBpAct_->setObjectName("debug.toggleBreakpoint");
    debugToggleBpAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F9));
    connect(debugToggleBpAct_, &QAction::triggered, this, &MainWindow::toggleBreakpointAtCursor);

    debugRestartAct_ = new QAction(tr("重新启动调试"), this);
    debugRestartAct_->setObjectName("debug.restart");
    debugRestartAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5));
    connect(debugRestartAct_, &QAction::triggered, this, &MainWindow::restartDebug);

    debugBuildAndStartAct_ = new QAction(tr("编译并调试"), this);
    debugBuildAndStartAct_->setObjectName("debug.buildAndStart");
    debugBuildAndStartAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    connect(debugBuildAndStartAct_, &QAction::triggered, this, &MainWindow::buildAndDebug);

    debugAddWatchAct_ = new QAction(tr("添加监视表达式..."), this);
    debugAddWatchAct_->setObjectName("debug.addWatch");
    connect(debugAddWatchAct_, &QAction::triggered, this, &MainWindow::addWatchExpression);

    debugRemoveWatchAct_ = new QAction(tr("移除监视表达式"), this);
    debugRemoveWatchAct_->setObjectName("debug.removeWatch");
    connect(debugRemoveWatchAct_, &QAction::triggered, this, &MainWindow::removeSelectedWatchExpression);

    advancedParseAct_ = new QAction(tr("启用 AST/clangd 解析(较慢)"), this);
    advancedParseAct_->setObjectName("view.advancedParse");
    advancedParseAct_->setCheckable(true);
    advancedParseAct_->setChecked(advancedParsingEnabled_);
    connect(advancedParseAct_, &QAction::toggled, this, &MainWindow::toggleAdvancedParsing);

    themeLightAct_ = new QAction(tr("浅色主题"), this);
    themeLightAct_->setObjectName("view.themeLight");
    themeLightAct_->setCheckable(true);
    themeDarkAct_ = new QAction(tr("深色主题"), this);
    themeDarkAct_->setObjectName("view.themeDark");
    themeDarkAct_->setCheckable(true);
    auto *themeGroup = new QActionGroup(this);
    themeGroup->addAction(themeLightAct_);
    themeGroup->addAction(themeDarkAct_);
    themeLightAct_->setChecked(true);
    connect(themeLightAct_, &QAction::triggered, this, &MainWindow::setLightTheme);
    connect(themeDarkAct_, &QAction::triggered, this, &MainWindow::setDarkTheme);

    themeImportAct_ = new QAction(tr("导入配色方案..."), this);
    themeImportAct_->setObjectName("view.importScheme");
    connect(themeImportAct_, &QAction::triggered, this, &MainWindow::importColorScheme);

    themeExportAct_ = new QAction(tr("导出配色方案..."), this);
    themeExportAct_->setObjectName("view.exportScheme");
    connect(themeExportAct_, &QAction::triggered, this, &MainWindow::exportColorScheme);

    terminalAct_ = new QAction(tr("终端"), this);
    terminalAct_->setObjectName("view.terminal");
    terminalAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    terminalAct_->setCheckable(true);
    terminalAct_->setChecked(false);
    connect(terminalAct_, &QAction::triggered, this, [this]() {
        if (terminalDock_) {
            terminalDock_->setVisible(terminalAct_->isChecked());
            if (terminalAct_->isChecked()) {
                if (!terminalProcess_ || terminalProcess_->state() == QProcess::NotRunning) {
                    startTerminalShell();
                }
                terminalDock_->raise();
            }
        }
    });

    foldAllAct_ = new QAction(tr("折叠全部"), this);
    foldAllAct_->setObjectName("view.foldAll");
    foldAllAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(foldAllAct_, &QAction::triggered, this, &MainWindow::foldAll);

    unfoldAllAct_ = new QAction(tr("展开全部"), this);
    unfoldAllAct_->setObjectName("view.unfoldAll");
    unfoldAllAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(unfoldAllAct_, &QAction::triggered, this, &MainWindow::unfoldAll);

    newProjectAct_ = new QAction(tr("新建工程..."), this);
    newProjectAct_->setObjectName("project.new");
    connect(newProjectAct_, &QAction::triggered, this, &MainWindow::newProject);

    openProjectAct_ = new QAction(tr("打开工程..."), this);
    openProjectAct_->setObjectName("project.open");
    connect(openProjectAct_, &QAction::triggered, this, &MainWindow::openProject);

    saveProjectAct_ = new QAction(tr("保存工程"), this);
    saveProjectAct_->setObjectName("project.save");
    connect(saveProjectAct_, &QAction::triggered, this, &MainWindow::saveProject);

    closeProjectAct_ = new QAction(tr("关闭工程"), this);
    closeProjectAct_->setObjectName("project.close");
    connect(closeProjectAct_, &QAction::triggered, this, &MainWindow::closeProject);

    addSourceAct_ = new QAction(tr("添加源文件..."), this);
    addSourceAct_->setObjectName("project.addSource");
    connect(addSourceAct_, &QAction::triggered, this, &MainWindow::addSourceFileToProject);

    addIncludeAct_ = new QAction(tr("添加 Include 目录..."), this);
    addIncludeAct_->setObjectName("project.addInclude");
    connect(addIncludeAct_, &QAction::triggered, this, &MainWindow::addIncludeDirToProject);

    fetchRusticAct_ = new QAction(tr("获取 rustic.hpp (GitHub)"), this);
    fetchRusticAct_->setObjectName("project.fetchRustic");
    connect(fetchRusticAct_, &QAction::triggered, this, &MainWindow::fetchRusticLibrary);

    projectSettingsAct_ = new QAction(tr("工程设置..."), this);
    projectSettingsAct_->setObjectName("project.settings");
    connect(projectSettingsAct_, &QAction::triggered, this, &MainWindow::showProjectSettings);
}

void MainWindow::createMenus() {
    auto fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(newAct_);
    fileMenu->addAction(openAct_);
    fileMenu->addAction(saveAct_);
    fileMenu->addAction(saveAsAct_);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAct_);

    auto editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(findAct_);
    editMenu->addAction(replaceAct_);
    editMenu->addSeparator();
    editMenu->addAction(findInFilesAct_);

    auto projectMenu = menuBar()->addMenu(tr("工程"));
    projectMenu->addAction(newProjectAct_);
    projectMenu->addAction(openProjectAct_);
    projectMenu->addAction(saveProjectAct_);
    projectMenu->addAction(closeProjectAct_);
    projectMenu->addSeparator();
    projectMenu->addAction(addSourceAct_);
    projectMenu->addAction(addIncludeAct_);
    projectMenu->addSeparator();
    projectMenu->addAction(fetchRusticAct_);
    projectMenu->addSeparator();
    projectMenu->addAction(projectSettingsAct_);

    auto buildMenu = menuBar()->addMenu(tr("编译"));
    buildMenu->addAction(compileAct_);
    buildMenu->addAction(rebuildAct_);
    buildMenu->addAction(cleanAct_);
    buildMenu->addAction(runAct_);
    buildMenu->addSeparator();
    buildMenu->addAction(makefileAct_);
    buildMenu->addSeparator();
    buildMenu->addAction(externalToolAct_);

    auto debugMenu = menuBar()->addMenu(tr("调试"));
    debugMenu->addAction(debugStartAct_);
    debugMenu->addAction(debugBuildAndStartAct_);
    debugMenu->addAction(debugRestartAct_);
    debugMenu->addAction(debugContinueAct_);
    debugMenu->addSeparator();
    debugMenu->addAction(debugStepOverAct_);
    debugMenu->addAction(debugStepIntoAct_);
    debugMenu->addAction(debugStepOutAct_);
    debugMenu->addSeparator();
    debugMenu->addAction(debugToggleBpAct_);
    debugMenu->addAction(debugAddWatchAct_);
    debugMenu->addAction(debugRemoveWatchAct_);
    debugMenu->addSeparator();
    debugMenu->addAction(debugStopAct_);

    auto viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(advancedParseAct_);
    auto themeMenu = viewMenu->addMenu(tr("主题"));
    themeMenu->addAction(themeLightAct_);
    themeMenu->addAction(themeDarkAct_);
    themeMenu->addSeparator();
    themeMenu->addAction(themeImportAct_);
    themeMenu->addAction(themeExportAct_);
    viewMenu->addSeparator();
    viewMenu->addAction(terminalAct_);
    viewMenu->addSeparator();
    viewMenu->addAction(foldAllAct_);
    viewMenu->addAction(unfoldAllAct_);

    auto navMenu = menuBar()->addMenu(tr("导航"));
    navMenu->addAction(navBackAct_);
    navMenu->addAction(navForwardAct_);
    navMenu->addSeparator();
    navMenu->addAction(findReferencesAct_);
    navMenu->addAction(renameSymbolAct_);

    auto toolsMenu = menuBar()->addMenu(tr("工具"));
    toolsMenu->addAction(shortcutSettingsAct_);
}

void MainWindow::createToolBar() {
    auto bar = addToolBar(tr("工具栏"));
    bar->setMovable(false);
    bar->addAction(newAct_);
    bar->addAction(openAct_);
    bar->addAction(saveAct_);
    bar->addSeparator();
    bar->addAction(newProjectAct_);
    bar->addAction(openProjectAct_);
    bar->addSeparator();
    bar->addAction(compileAct_);
    bar->addAction(runAct_);
    bar->addSeparator();
    bar->addAction(debugStartAct_);
    bar->addAction(debugContinueAct_);
    bar->addAction(debugStepOverAct_);
    bar->addAction(debugStepIntoAct_);
    bar->addAction(debugStepOutAct_);
    bar->addAction(debugStopAct_);
}

void MainWindow::createDocks() {
    auto outputDock = new QDockWidget(tr("输出"), this);
    outputDock->setObjectName(QStringLiteral("dock.output"));
    outputDock->setWidget(output_);
    addDockWidget(Qt::BottomDockWidgetArea, outputDock);

    auto *debugWidget = new QWidget(this);
    debugOutput_ = new QPlainTextEdit(debugWidget);
    debugOutput_->setReadOnly(true);
    debugInput_ = new QLineEdit(debugWidget);
    debugInput_->setPlaceholderText(tr("输入 gdb 命令并回车"));
    connect(debugInput_, &QLineEdit::returnPressed, this, &MainWindow::sendDebugCommand);

    auto *debugLayout = new QVBoxLayout(debugWidget);
    debugLayout->setContentsMargins(0, 0, 0, 0);
    debugLayout->addWidget(debugOutput_);
    debugLayout->addWidget(debugInput_);

    auto debugDock = new QDockWidget(tr("调试器"), this);
    debugDock->setObjectName(QStringLiteral("dock.debugger"));
    debugDock->setWidget(debugWidget);
    addDockWidget(Qt::BottomDockWidgetArea, debugDock);
    tabifyDockWidget(outputDock, debugDock);
    debugDock->hide();

    auto *terminalWidget = new QWidget(this);
    terminalOutput_ = new QPlainTextEdit(terminalWidget);
    terminalOutput_->setReadOnly(true);
    terminalInput_ = new QLineEdit(terminalWidget);
    terminalInput_->setPlaceholderText(tr("输入终端命令并回车"));
    connect(terminalInput_, &QLineEdit::returnPressed, this, &MainWindow::sendTerminalCommand);
    auto *termLayout = new QVBoxLayout(terminalWidget);
    termLayout->setContentsMargins(0, 0, 0, 0);
    termLayout->addWidget(terminalOutput_);
    termLayout->addWidget(terminalInput_);

    terminalDock_ = new QDockWidget(tr("终端"), this);
    terminalDock_->setObjectName(QStringLiteral("dock.terminal"));
    terminalDock_->setWidget(terminalWidget);
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock_);
    tabifyDockWidget(outputDock, terminalDock_);
    terminalDock_->hide();

    debugInfoTabs_ = new QTabWidget(this);
    breakpointsTree_ = new QTreeWidget(debugInfoTabs_);
    breakpointsTree_->setHeaderLabels({tr("编号"), tr("位置"), tr("启用"), tr("条件/命中")});
    breakpointsTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    stackTree_ = new QTreeWidget(debugInfoTabs_);
    stackTree_->setHeaderLabels({tr("层级"), tr("函数"), tr("位置")});
    localsTree_ = new QTreeWidget(debugInfoTabs_);
    localsTree_->setHeaderLabels({tr("变量"), tr("值")});

    threadsTree_ = new QTreeWidget(debugInfoTabs_);
    threadsTree_->setHeaderLabels({tr("ID"), tr("状态"), tr("位置")});

    watchTree_ = new QTreeWidget(debugInfoTabs_);
    watchTree_->setHeaderLabels({tr("表达式"), tr("值")});
    watchTree_->setContextMenuPolicy(Qt::CustomContextMenu);

    debugInfoTabs_->addTab(breakpointsTree_, tr("断点"));
    debugInfoTabs_->addTab(stackTree_, tr("调用栈"));
    debugInfoTabs_->addTab(localsTree_, tr("局部变量"));
    debugInfoTabs_->addTab(threadsTree_, tr("线程"));
    debugInfoTabs_->addTab(watchTree_, tr("监视"));

    debugInfoDock_ = new QDockWidget(tr("调试信息"), this);
    debugInfoDock_->setObjectName(QStringLiteral("dock.debugInfo"));
    debugInfoDock_->setWidget(debugInfoTabs_);
    addDockWidget(Qt::RightDockWidgetArea, debugInfoDock_);
    debugInfoDock_->hide();

    projectModel_ = new QFileSystemModel(this);
    projectModel_->setRootPath(QDir::currentPath());
    projectModel_->setNameFilterDisables(false);

    projectView_ = new QTreeView(this);
    projectView_->setModel(projectModel_);
    projectView_->setRootIndex(projectModel_->index(QDir::currentPath()));
    projectView_->setHeaderHidden(true);

    projectTree_ = new QTreeWidget(this);
    projectTree_->setHeaderHidden(true);
    projectTree_->setContextMenuPolicy(Qt::CustomContextMenu);

    projectStack_ = new QStackedWidget(this);
    projectStack_->addWidget(projectView_);
    projectStack_->addWidget(projectTree_);
    projectStack_->setCurrentWidget(projectView_);

    auto projectDock = new QDockWidget(tr("项目文件"), this);
    projectDock->setObjectName(QStringLiteral("dock.project"));
    projectDock->setWidget(projectStack_);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    symbolTree_ = new QTreeWidget(this);
    symbolTree_->setHeaderHidden(true);
    auto symbolDock = new QDockWidget(tr("代码结构"), this);
    symbolDock->setObjectName(QStringLiteral("dock.symbol"));
    symbolDock->setWidget(symbolTree_);
    addDockWidget(Qt::RightDockWidgetArea, symbolDock);

    searchResultsTree_ = new QTreeWidget(this);
    searchResultsTree_->setHeaderHidden(true);
    auto searchDock = new QDockWidget(tr("搜索结果"), this);
    searchDock->setObjectName(QStringLiteral("dock.search"));
    searchDock->setWidget(searchResultsTree_);
    addDockWidget(Qt::BottomDockWidgetArea, searchDock);
    tabifyDockWidget(outputDock, searchDock);
    searchDock->hide();

    connect(searchResultsTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) {
            return;
        }
        const QString file = item->data(0, Qt::UserRole).toString();
        const int line = item->data(0, Qt::UserRole + 1).toInt();
        if (file.isEmpty()) {
            return;
        }
        const int existingIndex = indexOfFile(file);
        if (existingIndex >= 0) {
            tabWidget_->setCurrentIndex(existingIndex);
        } else {
            createNewTab(file);
        }
        if (auto *editor = currentEditor()) {
            QTextBlock block = editor->document()->findBlockByNumber(line);
            if (block.isValid()) {
                QTextCursor cursor(block);
                editor->setTextCursor(cursor);
                editor->setFocus();
            }
        }
    });

    connect(symbolTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) {
            return;
        }
        const int line = item->data(0, Qt::UserRole).toInt();
        if (auto *editor = currentEditor()) {
            QTextBlock block = editor->document()->findBlockByNumber(line);
            if (block.isValid()) {
                QTextCursor cursor(block);
                editor->setTextCursor(cursor);
                editor->setFocus();
            }
        }
    });

    connect(projectView_, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QString path = projectModel_->filePath(index);
        if (QFileInfo(path).isFile()) {
            const QString absPath = QFileInfo(path).absoluteFilePath();
            const int existingIndex = indexOfFile(absPath);
            if (existingIndex >= 0) {
                tabWidget_->setCurrentIndex(existingIndex);
            } else {
                createNewTab(absPath);
            }
        }
    });

    connect(projectTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) {
            return;
        }
        const QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) {
            return;
        }
        const QString absPath = QFileInfo(path).absoluteFilePath();
        const int existingIndex = indexOfFile(absPath);
        if (existingIndex >= 0) {
            tabWidget_->setCurrentIndex(existingIndex);
        } else {
            createNewTab(absPath);
        }
    });

    connect(projectTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = projectTree_->itemAt(pos);
        QMenu menu(projectTree_);

        QAction *addGroupAct = menu.addAction(tr("新增分组..."));
        QAction *removeGroupAct = menu.addAction(tr("删除分组"));
        QAction *addFileAct = menu.addAction(tr("向分组添加文件..."));

        removeGroupAct->setEnabled(item && !item->parent());
        addFileAct->setEnabled(item && !item->parent());

        QAction *chosen = menu.exec(projectTree_->viewport()->mapToGlobal(pos));
        if (!chosen) {
            return;
        }
        if (chosen == addGroupAct) {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("新增分组"), tr("分组名称："), QLineEdit::Normal, tr("NewGroup"), &ok);
            if (ok && !name.trimmed().isEmpty()) {
                projectManager_->addGroup(name.trimmed());
                rebuildProjectTree();
            }
        } else if (chosen == removeGroupAct && item) {
            const QString groupName = item->text(0);
            projectManager_->removeGroup(groupName);
            rebuildProjectTree();
        } else if (chosen == addFileAct && item) {
            const QString groupName = item->text(0);
            const QStringList files = QFileDialog::getOpenFileNames(this, tr("添加文件到分组"), projectManager_->rootDir(), tr("C++ 文件 (*.cpp *.cc *.cxx *.h *.hpp);;所有文件 (*.*)"));
            for (const QString &file : files) {
                projectManager_->addFileToGroup(groupName, file);
            }
            rebuildProjectTree();
        }
    });

    connect(stackTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) {
            return;
        }
        const QString file = item->data(0, Qt::UserRole).toString();
        const int line = item->data(0, Qt::UserRole + 1).toInt();
        const int level = item->data(0, Qt::UserRole + 2).toInt();
        if (file.isEmpty()) {
            return;
        }
        if (gdbClient_ && gdbClient_->isRunning()) {
            gdbClient_->selectFrame(level);
        }
        jumpToFileLocation(file, line, 0, true);
    });

    connect(threadsTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        if (!item) {
            return;
        }
        const int id = item->data(0, Qt::UserRole).toInt();
        if (gdbClient_ && gdbClient_->isRunning()) {
            gdbClient_->selectThread(id);
        }
    });

    connect(breakpointsTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = breakpointsTree_->itemAt(pos);
        if (!item) {
            return;
        }
        const int number = item->data(0, Qt::UserRole).toInt();
        const bool enabled = item->data(0, Qt::UserRole + 2).toBool();

        QMenu menu(breakpointsTree_);
        QAction *delAct = menu.addAction(tr("删除断点"));
        QAction *toggleAct = menu.addAction(enabled ? tr("禁用断点") : tr("启用断点"));
        QAction *condAct = menu.addAction(tr("设置条件..."));
        QAction *hitAct = menu.addAction(tr("设置命中次数..."));
        QAction *logAct = menu.addAction(tr("设置日志断点..."));

        QAction *chosen = menu.exec(breakpointsTree_->viewport()->mapToGlobal(pos));
        if (!chosen || !gdbClient_) {
            return;
        }
        if (chosen == delAct) {
            gdbClient_->deleteBreakpoint(number);
        } else if (chosen == toggleAct) {
            gdbClient_->setBreakpointEnabled(number, !enabled);
        } else if (chosen == condAct) {
            bool ok = false;
            const QString cond = QInputDialog::getText(this, tr("断点条件"), tr("请输入条件(留空清除)："), QLineEdit::Normal, item->text(3), &ok);
            if (ok) {
                gdbClient_->setBreakpointCondition(number, cond.trimmed());
            }
        } else if (chosen == hitAct) {
            bool ok = false;
            const int count = QInputDialog::getInt(this, tr("命中次数"), tr("命中多少次后暂停(0 表示每次都暂停)："), item->data(0, Qt::UserRole + 3).toInt(), 0, 1000000, 1, &ok);
            if (ok) {
                gdbClient_->setBreakpointIgnoreCount(number, count);
            }
        } else if (chosen == logAct) {
            bool ok = false;
            const QString msg = QInputDialog::getText(this, tr("日志断点"), tr("请输入打印内容："), QLineEdit::Normal, QString(), &ok);
            if (ok && !msg.isEmpty()) {
                gdbClient_->setBreakpointLogMessage(number, msg);
            }
        }
    });

    connect(watchTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(watchTree_);
        QAction *addAct = menu.addAction(tr("添加监视表达式..."));
        QAction *removeAct = menu.addAction(tr("移除选中项"));
        removeAct->setEnabled(watchTree_->itemAt(pos) != nullptr);
        QAction *chosen = menu.exec(watchTree_->viewport()->mapToGlobal(pos));
        if (!chosen) {
            return;
        }
        if (chosen == addAct) {
            addWatchExpression();
        } else if (chosen == removeAct) {
            removeSelectedWatchExpression();
        }
    });
}

bool MainWindow::maybeSave() {
    const int index = tabWidget_->currentIndex();
    if (index < 0) {
        return true;
    }
    return maybeSaveTab(index);
}

void MainWindow::newFile() {
    createNewTab();
}

void MainWindow::openFile() {
    const QString baseDir = projectManager_->hasProject() ? projectManager_->rootDir() : QDir::currentPath();
    const QString path = QFileDialog::getOpenFileName(this, tr("打开 C++ 文件"), baseDir,
                                                     tr("C++ 文件 (*.cpp *.cc *.cxx *.h *.hpp);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    const QString absPath = QFileInfo(path).absoluteFilePath();
    const int existingIndex = indexOfFile(absPath);
    if (existingIndex >= 0) {
        tabWidget_->setCurrentIndex(existingIndex);
        return;
    }

    createNewTab(absPath);
}

bool MainWindow::saveFile() {
    const int index = tabWidget_->currentIndex();
    if (index < 0) {
        return false;
    }
    OpenTab *tab = tabAt(index);
    if (!tab) {
        return false;
    }
    if (tab->filePath.isEmpty()) {
        return saveFileAs();
    }
    return writeTabToFile(index, tab->filePath);
}

bool MainWindow::saveFileAs() {
    const int index = tabWidget_->currentIndex();
    if (index < 0) {
        return false;
    }
    const OpenTab *tab = tabAt(index);
    const QString suggested = tab && !tab->filePath.isEmpty()
                                  ? tab->filePath
                                  : (projectManager_->hasProject() ? projectManager_->rootDir() : QDir::currentPath());

    const QString path = QFileDialog::getSaveFileName(this, tr("保存为"), suggested,
                                                     tr("C++ 文件 (*.cpp *.cc *.cxx *.h *.hpp);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return false;
    }
    return writeTabToFile(index, QFileInfo(path).absoluteFilePath());
}

void MainWindow::newProject() {
    if (!maybeSaveAllTabs()) {
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择工程目录"), QDir::currentPath());
    if (dir.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("工程名称"), tr("请输入工程名称"), QLineEdit::Normal, "MyProject", &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    const QStringList templates = {
        tr("Console App"),
        tr("Qt Widgets App"),
        tr("rustic.hpp Console 示例"),
        tr("静态库"),
        tr("动态库"),
        tr("空工程")};
    const QString tmpl = QInputDialog::getItem(this, tr("工程模板"), tr("请选择工程模板："), templates, 0, false, &ok);
    if (!ok || tmpl.isEmpty()) {
        return;
    }

    if (!projectManager_->createNewProject(dir, name.trimmed())) {
        QMessageBox::warning(this, tr("创建失败"), tr("无法创建工程"));
        return;
    }

    const QString root = projectManager_->rootDir();
    QString mainPath;
    QString mainContent;

    if (tmpl == tr("Console App")) {
        mainPath = QDir(root).filePath("main.cpp");
        mainContent =
            "#include <iostream>\n\n"
            "int main(int argc, char **argv) {\n"
            "    (void)argc; (void)argv;\n"
            "    std::cout << \"Hello from Rustic C++ IDE!\\n\";\n"
            "    return 0;\n"
            "}\n";
    } else if (tmpl == tr("Qt Widgets App")) {
        mainPath = QDir(root).filePath("main.cpp");
        mainContent =
            "#include <QApplication>\n"
            "#include <QPushButton>\n\n"
            "int main(int argc, char *argv[]) {\n"
            "    QApplication app(argc, argv);\n"
            "    QPushButton btn(\"Hello Qt!\");\n"
            "    btn.resize(240, 60);\n"
            "    btn.show();\n"
            "    return app.exec();\n"
            "}\n";
    } else if (tmpl == tr("rustic.hpp Console 示例")) {
        mainPath = QDir(root).filePath("main.cpp");
        mainContent =
            "#include \"rustic.hpp\"\n\n"
            "fn main() {\n"
            "    println(\"Hello Rustic!\");\n"
            "    return 0;\n"
            "}\n";
        projectManager_->addIncludeDir("third_party/rustic.hpp");
    } else if (tmpl == tr("静态库") || tmpl == tr("动态库")) {
        const QString libH = QDir(root).filePath("library.h");
        const QString libCpp = QDir(root).filePath("library.cpp");
        QFile hFile(libH);
        if (hFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&hFile);
            out << "#pragma once\n\nint add(int a, int b);\n";
        }
        QFile cppFile(libCpp);
        if (cppFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&cppFile);
            out << "#include \"library.h\"\n\nint add(int a, int b) { return a + b; }\n";
        }
        projectManager_->addSourceFile(libCpp);
        ProjectGroup g;
        g.name = tr("Library");
        g.files = {"library.cpp", "library.h"};
        projectManager_->setGroups({g});

        BuildProfile rel = projectManager_->releaseProfile();
        BuildProfile dbg = projectManager_->debugProfile();
        if (tmpl == tr("动态库")) {
            rel.outputName = QStringLiteral("lib%1.so").arg(projectManager_->projectName());
            dbg.outputName = QStringLiteral("lib%1_debug.so").arg(projectManager_->projectName());
            rel.flags << "-shared" << "-fPIC";
            dbg.flags << "-shared" << "-fPIC";
            projectManager_->setReleaseProfile(rel);
            projectManager_->setDebugProfile(dbg);
        } else {
            rel.outputName = QStringLiteral("lib%1.a").arg(projectManager_->projectName());
            dbg.outputName = QStringLiteral("lib%1_debug.a").arg(projectManager_->projectName());
            projectManager_->setReleaseProfile(rel);
            projectManager_->setDebugProfile(dbg);
        }
    }

    if (!mainPath.isEmpty()) {
        QFile mainFile(mainPath);
        if (mainFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&mainFile);
            out << mainContent;
        }
        projectManager_->addSourceFile(mainPath);
        ProjectGroup g;
        g.name = tr("Sources");
        g.files = {"main.cpp"};
        projectManager_->setGroups({g});
        createNewTab(mainPath);
    }

    showProjectGroupsView(true);
    statusBar()->showMessage(tr("已创建工程：%1").arg(projectManager_->projectName()), 2000);

    if (advancedParsingEnabled_) {
        lspClient_->start(projectManager_->rootDir());
    }
}

void MainWindow::openProject() {
    if (!maybeSaveAllTabs()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, tr("打开工程"), QDir::currentPath(),
                                                     tr("Rustic C++ IDE 工程 (*.rcppide.json);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    if (!projectManager_->openProject(path)) {
        QMessageBox::warning(this, tr("打开失败"), tr("无法打开工程文件：%1").arg(path));
        return;
    }

    showProjectGroupsView(true);
    statusBar()->showMessage(tr("已打开工程：%1").arg(projectManager_->projectName()), 2000);

    if (advancedParsingEnabled_) {
        lspClient_->start(projectManager_->rootDir());
    }
}

void MainWindow::saveProject() {
    if (!projectManager_->hasProject()) {
        QMessageBox::information(this, tr("未打开工程"), tr("当前没有打开任何工程。"));
        return;
    }
    if (projectManager_->saveProject()) {
        statusBar()->showMessage(tr("工程已保存"), 1500);
    }
}

void MainWindow::closeProject() {
    if (!projectManager_->hasProject()) {
        return;
    }
    projectManager_->closeProject();
    showProjectGroupsView(false);
    if (lspClient_->isRunning()) {
        lspClient_->stop();
    }
    statusBar()->showMessage(tr("工程已关闭"), 1500);
}

void MainWindow::showProjectGroupsView(bool enabled) {
    if (!projectStack_ || !projectModel_ || !projectView_) {
        return;
    }
    if (enabled && projectManager_->hasProject()) {
        projectModel_->setRootPath(projectManager_->rootDir());
        projectView_->setRootIndex(projectModel_->index(projectManager_->rootDir()));
        rebuildProjectTree();
        projectStack_->setCurrentWidget(projectTree_);
    } else {
        projectModel_->setRootPath(QDir::currentPath());
        projectView_->setRootIndex(projectModel_->index(QDir::currentPath()));
        projectStack_->setCurrentWidget(projectView_);
    }
}

void MainWindow::rebuildProjectTree() {
    if (!projectTree_) {
        return;
    }
    projectTree_->clear();
    if (!projectManager_->hasProject()) {
        return;
    }

    const QString root = projectManager_->rootDir();
    QVector<ProjectGroup> groups = projectManager_->groups();
    if (groups.isEmpty()) {
        ProjectGroup defaultGroup;
        defaultGroup.name = tr("Sources");
        defaultGroup.files = projectManager_->sources();
        groups.append(defaultGroup);
    }

    QSet<QString> groupedFiles;
    for (const auto &g : groups) {
        auto *groupItem = new QTreeWidgetItem(projectTree_, QStringList{g.name});
        for (const QString &fileRel : g.files) {
            groupedFiles.insert(fileRel);
            const QString abs = QDir(root).absoluteFilePath(fileRel);
            auto *fileItem = new QTreeWidgetItem(groupItem, QStringList{QFileInfo(fileRel).fileName()});
            fileItem->setData(0, Qt::UserRole, abs);
            fileItem->setToolTip(0, fileRel);
        }
    }

    QStringList ungroupedSources;
    for (const QString &src : projectManager_->sources()) {
        if (!groupedFiles.contains(src)) {
            ungroupedSources.append(src);
        }
    }
    if (!ungroupedSources.isEmpty()) {
        auto *ungroupItem = new QTreeWidgetItem(projectTree_, QStringList{tr("未分组源文件")});
        for (const QString &fileRel : ungroupedSources) {
            const QString abs = QDir(root).absoluteFilePath(fileRel);
            auto *fileItem = new QTreeWidgetItem(ungroupItem, QStringList{QFileInfo(fileRel).fileName()});
            fileItem->setData(0, Qt::UserRole, abs);
            fileItem->setToolTip(0, fileRel);
        }
    }

    QStringList otherFiles;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = QDir(root).relativeFilePath(abs);
        if (rel.startsWith("build/") || rel.startsWith(".git/") || rel.startsWith("third_party/") || rel.endsWith(".rcppide.json") || rel == "compile_commands.json") {
            continue;
        }
        if (groupedFiles.contains(rel) || projectManager_->sources().contains(rel)) {
            continue;
        }
        otherFiles.append(rel);
    }
    otherFiles.sort();
    if (!otherFiles.isEmpty()) {
        auto *otherItem = new QTreeWidgetItem(projectTree_, QStringList{tr("其他文件")});
        for (const QString &fileRel : otherFiles) {
            const QString abs = QDir(root).absoluteFilePath(fileRel);
            auto *fileItem = new QTreeWidgetItem(otherItem, QStringList{QFileInfo(fileRel).fileName()});
            fileItem->setData(0, Qt::UserRole, abs);
            fileItem->setToolTip(0, fileRel);
        }
    }

    projectTree_->expandAll();
}

void MainWindow::addSourceFileToProject() {
    if (!projectManager_->hasProject()) {
        QMessageBox::information(this, tr("未打开工程"), tr("请先创建或打开工程。"));
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(this, tr("添加源文件"), projectManager_->rootDir(),
                                                           tr("C++ 文件 (*.cpp *.cc *.cxx);;所有文件 (*.*)"));
    for (const QString &file : files) {
        projectManager_->addSourceFile(file);
    }
}

void MainWindow::addIncludeDirToProject() {
    if (!projectManager_->hasProject()) {
        QMessageBox::information(this, tr("未打开工程"), tr("请先创建或打开工程。"));
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, tr("添加 Include 目录"), projectManager_->rootDir());
    if (!dir.isEmpty()) {
        projectManager_->addIncludeDir(dir);
    }
}

void MainWindow::fetchRusticLibrary() {
    if (!projectManager_->hasProject()) {
        QMessageBox::information(this, tr("未打开工程"), tr("请先创建或打开工程。"));
        return;
    }

    QString error;
    if (!projectManager_->downloadRusticLibrary(&error)) {
        QMessageBox::warning(this, tr("获取失败"), error);
        return;
    }
    appendBuildOutput(error + "\n");
}

void MainWindow::showProjectSettings() {
    if (!projectManager_->hasProject()) {
        QMessageBox::information(this, tr("未打开工程"), tr("请先创建或打开工程。"));
        return;
    }
    ProjectSettingsDialog dialog(projectManager_.get(), this);
    dialog.exec();
}

void MainWindow::showShortcutSettings() {
    QList<QAction *> acts;
    auto add = [&acts](QAction *act) {
        if (act) {
            acts.append(act);
        }
    };
    add(newAct_);
    add(openAct_);
    add(saveAct_);
    add(saveAsAct_);
    add(exitAct_);
    add(findAct_);
    add(replaceAct_);
    add(findInFilesAct_);
    add(compileAct_);
    add(rebuildAct_);
    add(cleanAct_);
    add(runAct_);
    add(makefileAct_);
    add(externalToolAct_);
    add(debugStartAct_);
    add(debugBuildAndStartAct_);
    add(debugRestartAct_);
    add(debugContinueAct_);
    add(debugStepOverAct_);
    add(debugStepIntoAct_);
    add(debugStepOutAct_);
    add(debugToggleBpAct_);
    add(debugAddWatchAct_);
    add(debugRemoveWatchAct_);
    add(debugStopAct_);
    add(navBackAct_);
    add(navForwardAct_);
    add(findReferencesAct_);
    add(renameSymbolAct_);
    add(newProjectAct_);
    add(openProjectAct_);
    add(saveProjectAct_);
    add(closeProjectAct_);
    add(addSourceAct_);
    add(addIncludeAct_);
    add(fetchRusticAct_);
    add(projectSettingsAct_);
    add(terminalAct_);

    ShortcutSettingsDialog dialog(acts, this);
    dialog.exec();
}

CodeEditor *MainWindow::currentEditor() const {
    return qobject_cast<CodeEditor *>(tabWidget_->currentWidget());
}

MainWindow::OpenTab *MainWindow::currentTab() {
    return tabAt(tabWidget_->currentIndex());
}

const MainWindow::OpenTab *MainWindow::currentTab() const {
    return tabAt(tabWidget_->currentIndex());
}

MainWindow::OpenTab *MainWindow::tabAt(int index) {
    if (index < 0 || index >= openTabs_.size()) {
        return nullptr;
    }
    return &openTabs_[index];
}

const MainWindow::OpenTab *MainWindow::tabAt(int index) const {
    if (index < 0 || index >= openTabs_.size()) {
        return nullptr;
    }
    return &openTabs_[index];
}

int MainWindow::indexOfEditor(CodeEditor *editor) const {
    for (int i = 0; i < openTabs_.size(); ++i) {
        if (openTabs_[i].editor == editor) {
            return i;
        }
    }
    return -1;
}

int MainWindow::indexOfFile(const QString &filePath) const {
    const QString abs = QFileInfo(filePath).absoluteFilePath();
    for (int i = 0; i < openTabs_.size(); ++i) {
        if (QFileInfo(openTabs_[i].filePath).absoluteFilePath() == abs) {
            return i;
        }
    }
    return -1;
}

void MainWindow::createNewTab(const QString &filePath, const QString &content) {
    std::fprintf(stderr, "[DEBUG_STARTUP] createNewTab begin, filePath='%s'\n", filePath.toLocal8Bit().constData());
    std::fflush(stderr);
    auto *editor = new CodeEditor(tabWidget_);
    std::fprintf(stderr, "[DEBUG_STARTUP] CodeEditor created\n");
    std::fflush(stderr);
    editor->setDarkThemeEnabled(darkThemeEnabled_);
    std::fprintf(stderr, "[DEBUG_STARTUP] setDarkThemeEnabled done\n");
    std::fflush(stderr);
    auto *highlighter = new CppRusticHighlighter(editor->document());
    std::fprintf(stderr, "[DEBUG_STARTUP] Highlighter created\n");
    std::fflush(stderr);
    highlighter->setAdvancedParsingEnabled(advancedParsingEnabled_);
    std::fprintf(stderr, "[DEBUG_STARTUP] setAdvancedParsingEnabled done\n");
    std::fflush(stderr);

    OpenTab tab;
    tab.editor = editor;
    tab.highlighter = highlighter;

    if (filePath.isEmpty()) {
        tab.isUntitled = true;
        tab.displayName = tr("未命名%1.cpp").arg(untitledCounter_++);
    } else {
        tab.isUntitled = false;
        tab.filePath = QFileInfo(filePath).absoluteFilePath();
        tab.displayName = QFileInfo(tab.filePath).fileName();
    }

    std::fprintf(stderr, "[DEBUG_STARTUP] displayName='%s'\n", tab.displayName.toLocal8Bit().constData());
    std::fflush(stderr);

    const int index = tabWidget_->addTab(editor, tab.displayName);
    std::fprintf(stderr, "[DEBUG_STARTUP] addTab done index=%d\n", index);
    std::fflush(stderr);
    openTabs_.insert(index, tab);
    std::fprintf(stderr, "[DEBUG_STARTUP] openTabs inserted size=%d\n", openTabs_.size());
    std::fflush(stderr);
    tabWidget_->setCurrentIndex(index);
    std::fprintf(stderr, "[DEBUG_STARTUP] setCurrentIndex done\n");
    std::fflush(stderr);

    connect(editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::documentModified);
    connect(editor, &CodeEditor::completionRequested, this, &MainWindow::requestCompletion);
    connect(editor, &CodeEditor::gotoDefinitionRequested, this, &MainWindow::requestGotoDefinition);
    connect(editor, &CodeEditor::breakpointToggled, this, [this, editor](int line, bool enabled) {
        const int idx = indexOfEditor(editor);
        OpenTab *tab = tabAt(idx);
        if (!tab || tab->filePath.isEmpty()) {
            return;
        }
        const QString absFile = QFileInfo(tab->filePath).absoluteFilePath();
        if (enabled) {
            breakpointsByFile_[absFile].insert(line);
            if (gdbClient_->isRunning()) {
                gdbClient_->insertBreakpoint(absFile, line + 1);
            }
        } else {
            breakpointsByFile_[absFile].remove(line);
            if (gdbClient_->isRunning()) {
                for (const auto &bp : gdbClient_->breakpoints()) {
                    if (QFileInfo(bp.file).absoluteFilePath() == absFile && bp.line - 1 == line) {
                        gdbClient_->deleteBreakpoint(bp.number);
                        break;
                    }
                }
            }
        }
    });
    connect(editor->document(), &QTextDocument::contentsChanged, this, &MainWindow::scheduleLspChange);

    std::fprintf(stderr, "[DEBUG_STARTUP] editor signals connected\n");
    std::fflush(stderr);

    if (!filePath.isEmpty()) {
        loadFileToTab(index, tab.filePath);
        const QString abs = QFileInfo(tab.filePath).absoluteFilePath();
        if (breakpointsByFile_.contains(abs)) {
            editor->setBreakpoints(breakpointsByFile_.value(abs));
        }
    } else if (!content.isEmpty()) {
        editor->setPlainText(content);
        editor->document()->setModified(false);
    }

    updateTabTitle(index);
    updateWindowTitle();

    std::fprintf(stderr, "[DEBUG_STARTUP] createNewTab end\n");
    std::fflush(stderr);
}

bool MainWindow::closeTab(int index) {
    if (!maybeSaveTab(index)) {
        return false;
    }

    OpenTab *tab = tabAt(index);
    if (!tab) {
        return false;
    }

    QWidget *widget = tab->editor;
    tabWidget_->removeTab(index);
    openTabs_.removeAt(index);
    if (widget) {
        widget->deleteLater();
    }

    if (openTabs_.isEmpty()) {
        createNewTab();
    } else {
        updateWindowTitle();
    }
    return true;
}

bool MainWindow::maybeSaveTab(int index) {
    OpenTab *tab = tabAt(index);
    if (!tab || !tab->editor->document()->isModified()) {
        return true;
    }

    const QString name = tab->displayName;
    const auto ret = QMessageBox::warning(
        this,
        tr("未保存的修改"),
        tr("文件 %1 已被修改，是否保存？").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Cancel) {
        return false;
    }
    if (ret == QMessageBox::Discard) {
        return true;
    }

    if (tab->filePath.isEmpty()) {
        const QString suggested = projectManager_->hasProject() ? projectManager_->rootDir() : QDir::currentPath();
        const QString path = QFileDialog::getSaveFileName(this, tr("保存为"), suggested,
                                                         tr("C++ 文件 (*.cpp *.cc *.cxx *.h *.hpp);;所有文件 (*.*)"));
        if (path.isEmpty()) {
            return false;
        }
        return writeTabToFile(index, QFileInfo(path).absoluteFilePath());
    }

    return writeTabToFile(index, tab->filePath);
}

bool MainWindow::maybeSaveAllTabs() {
    for (int i = 0; i < openTabs_.size(); ++i) {
        if (!maybeSaveTab(i)) {
            return false;
        }
    }
    return true;
}

bool MainWindow::loadFileToTab(int index, const QString &path) {
    OpenTab *tab = tabAt(index);
    if (!tab) {
        return false;
    }

    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("打开失败"), tr("无法打开文件：%1").arg(path));
        return false;
    }
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif
    tab->editor->setPlainText(in.readAll());
    tab->editor->document()->setModified(false);
    tab->filePath = QFileInfo(path).absoluteFilePath();
    tab->displayName = QFileInfo(tab->filePath).fileName();
    tab->isUntitled = false;
    updateTabTitle(index);
    if (index == tabWidget_->currentIndex()) {
        currentFile_ = tab->filePath;
        updateWindowTitle();
    }
    statusBar()->showMessage(tr("已打开：%1").arg(tab->filePath), 2000);

    if (!lspClient_->isRunning()) {
        const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QFileInfo(path).absolutePath();
        lspClient_->start(root);
    }
    lspClient_->setCurrentDocument(tab->editor->document(), tab->filePath);
    lspClient_->openDocument(tab->filePath, tab->editor->toPlainText());
    if (advancedParsingEnabled_) {
        lspClient_->requestDocumentSymbols(tab->filePath);
        lspClient_->requestFoldingRanges(tab->filePath);
        lspClient_->requestSemanticTokens(tab->filePath);
    }
    return true;
}

bool MainWindow::writeTabToFile(int index, const QString &path) {
    OpenTab *tab = tabAt(index);
    if (!tab) {
        return false;
    }

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：%1").arg(path));
        return false;
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif
    out << tab->editor->toPlainText();
    tab->editor->document()->setModified(false);
    tab->filePath = QFileInfo(path).absoluteFilePath();
    tab->displayName = QFileInfo(tab->filePath).fileName();
    tab->isUntitled = false;
    updateTabTitle(index);
    if (index == tabWidget_->currentIndex()) {
        currentFile_ = tab->filePath;
        updateWindowTitle();
    }
    statusBar()->showMessage(tr("已保存：%1").arg(tab->filePath), 2000);

    if (!lspClient_->isRunning()) {
        const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QFileInfo(path).absolutePath();
        lspClient_->start(root);
    }
    lspClient_->setCurrentDocument(tab->editor->document(), tab->filePath);
    lspClient_->openDocument(tab->filePath, tab->editor->toPlainText());
    lspClient_->saveDocument(tab->filePath);
    if (advancedParsingEnabled_) {
        lspClient_->requestDocumentSymbols(tab->filePath);
        lspClient_->requestFoldingRanges(tab->filePath);
        lspClient_->requestSemanticTokens(tab->filePath);
    }
    return true;
}

void MainWindow::updateTabTitle(int index) {
    OpenTab *tab = tabAt(index);
    if (!tab) {
        return;
    }
    const QString modified = tab->editor->document()->isModified() ? "*" : "";
    tabWidget_->setTabText(index, tab->displayName + modified);
}

void MainWindow::updateWindowTitle() {
    const OpenTab *tab = currentTab();
    const QString name = tab ? tab->displayName : tr("未命名.cpp");
    const QString modified = tab && tab->editor->document()->isModified() ? "*" : "";
    setWindowTitle(tr("Rustic C++ IDE - %1%2").arg(name, modified));
}

void MainWindow::documentModified(bool) {
    auto *doc = qobject_cast<QTextDocument *>(sender());
    int index = -1;
    if (doc) {
        for (int i = 0; i < openTabs_.size(); ++i) {
            if (openTabs_[i].editor && openTabs_[i].editor->document() == doc) {
                index = i;
                break;
            }
        }
    }

    if (index >= 0) {
        updateTabTitle(index);
    }
    updateWindowTitle();
}

void MainWindow::jumpToFileLocation(const QString &filePath, int line, int character, bool recordHistory) {
    if (filePath.isEmpty()) {
        return;
    }

    if (recordHistory) {
        OpenTab *tab = currentTab();
        if (tab && tab->editor && !tab->filePath.isEmpty()) {
            QTextCursor cur = tab->editor->textCursor();
            backStack_.append({tab->filePath, cur.blockNumber(), cur.positionInBlock()});
            forwardStack_.clear();
        }
    }

    const QString absPath = QFileInfo(filePath).absoluteFilePath();
    const int existingIndex = indexOfFile(absPath);
    if (existingIndex >= 0) {
        tabWidget_->setCurrentIndex(existingIndex);
    } else {
        createNewTab(absPath);
    }

    OpenTab *tab = currentTab();
    if (!tab || !tab->editor) {
        return;
    }

    QTextBlock block = tab->editor->document()->findBlockByNumber(line);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, character);
    tab->editor->setTextCursor(cursor);
    tab->editor->centerCursor();
    tab->editor->setFocus();
}

void MainWindow::compileFile() {
    if (!saveFile()) {
        return;
    }
    output_->clear();

    BuildManager::BuildConfig config;

    if (projectManager_->hasProject()) {
        config.sources = projectManager_->sourceFilesAbsolute();
        if (config.sources.isEmpty() && !currentFile_.isEmpty()) {
            config.sources.append(currentFile_);
        }
        config.includeDirs = projectManager_->includeDirsAbsolute();
        config.compiler = projectManager_->compiler();
        config.cxxStandard = projectManager_->cxxStandard();
        config.extraFlags = projectManager_->activeExtraFlags();
        config.outputPath = QDir(projectManager_->rootDir()).filePath(projectManager_->activeOutputName());
        config.workingDirectory = projectManager_->rootDir();
        appendBuildOutput(tr("开始编译工程：%1\n").arg(projectManager_->projectName()));
    } else {
        config.sources = {currentFile_};
        config.outputPath = QFileInfo(currentFile_).absolutePath() + QDir::separator() + QFileInfo(currentFile_).completeBaseName();
        config.workingDirectory = QFileInfo(currentFile_).absolutePath();
        appendBuildOutput(tr("开始编译：%1\n").arg(currentFile_));
    }

    buildManager_->compile(config);
}

void MainWindow::rebuildProject() {
    cleanProject();
    compileFile();
}

void MainWindow::cleanProject() {
    output_->clear();

    if (projectManager_->hasProject()) {
        const QString root = projectManager_->rootDir();
        const QString binaryDebug = QDir(root).filePath(projectManager_->debugProfile().outputName);
        const QString binaryRelease = QDir(root).filePath(projectManager_->releaseProfile().outputName);
        QFile::remove(binaryDebug);
        if (binaryRelease != binaryDebug) {
            QFile::remove(binaryRelease);
        }

        for (const QString &src : projectManager_->sourceFilesAbsolute()) {
            QFileInfo info(src);
            const QString obj = info.absolutePath() + QDir::separator() + info.completeBaseName() + ".o";
            QFile::remove(obj);
        }
        appendBuildOutput(tr("已清理 Debug/Release 输出：%1\n").arg(root));
    } else if (!currentFile_.isEmpty()) {
        const QString binary = QFileInfo(currentFile_).absolutePath() + QDir::separator() + QFileInfo(currentFile_).completeBaseName();
        QFile::remove(binary);
        appendBuildOutput(tr("已清理输出：%1\n").arg(binary));
    }
}

void MainWindow::runExternalTool() {
    const QString cmd = QInputDialog::getText(this, tr("运行外部工具"), tr("请输入命令："));
    if (cmd.trimmed().isEmpty()) {
        return;
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QDir::currentPath();
    proc.setWorkingDirectory(root);
    proc.start("bash", {"-lc", cmd});
    proc.waitForFinished(-1);
    appendBuildOutput(tr("外部命令输出：%1\n").arg(cmd));
    appendBuildOutput(QString::fromLocal8Bit(proc.readAllStandardOutput()));
}

void MainWindow::startDebug() {
    if (!gdbClient_) {
        return;
    }
    if (gdbClient_->isRunning()) {
        gdbClient_->stop();
    }

    const QString binaryPath = buildManager_->lastBinaryPath();
    if (binaryPath.isEmpty() || !QFileInfo::exists(binaryPath)) {
        appendBuildOutput(tr("没有可调试的可执行文件，请先编译。\n"));
        return;
    }

    const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QFileInfo(binaryPath).absolutePath();
    debugOutput_->clear();
    gdbClient_->start(binaryPath, root);

    for (auto it = breakpointsByFile_.cbegin(); it != breakpointsByFile_.cend(); ++it) {
        const QString file = it.key();
        for (int line : it.value()) {
            gdbClient_->insertBreakpoint(file, line + 1);
        }
    }

    gdbClient_->runExec();

    if (auto *dock = qobject_cast<QDockWidget *>(debugOutput_->parentWidget()->parentWidget())) {
        dock->show();
        dock->raise();
    }
    if (debugInfoDock_) {
        debugInfoDock_->show();
        debugInfoDock_->raise();
    }
}

void MainWindow::stopDebug() {
    if (gdbClient_ && gdbClient_->isRunning()) {
        gdbClient_->stop();
    }
}

void MainWindow::sendDebugCommand() {
    if (!gdbClient_ || !gdbClient_->isRunning()) {
        return;
    }
    const QString cmd = debugInput_->text().trimmed();
    if (cmd.isEmpty()) {
        return;
    }
    debugOutput_->appendPlainText("(gdb) " + cmd);
    gdbClient_->sendConsoleCommand(cmd);
    debugInput_->clear();
}

void MainWindow::continueDebug() {
    if (gdbClient_ && gdbClient_->isRunning()) {
        gdbClient_->continueExec();
    }
}

void MainWindow::stepOverDebug() {
    if (gdbClient_ && gdbClient_->isRunning()) {
        gdbClient_->stepOver();
    }
}

void MainWindow::stepIntoDebug() {
    if (gdbClient_ && gdbClient_->isRunning()) {
        gdbClient_->stepInto();
    }
}

void MainWindow::stepOutDebug() {
    if (gdbClient_ && gdbClient_->isRunning()) {
        gdbClient_->stepOut();
    }
}

void MainWindow::toggleBreakpointAtCursor() {
    if (auto *editor = currentEditor()) {
        const int line = editor->textCursor().blockNumber();
        editor->toggleBreakpointAtLine(line);
    }
}

void MainWindow::restartDebug() {
    stopDebug();
    startDebug();
}

void MainWindow::buildAndDebug() {
    pendingDebugAfterBuild_ = true;
    if (projectManager_->hasProject() && projectManager_->activeBuildProfile() != "Debug") {
        projectManager_->setActiveBuildProfile("Debug");
    }
    compileFile();
}

void MainWindow::addWatchExpression() {
    bool ok = false;
    const QString expr = QInputDialog::getText(this, tr("添加监视表达式"), tr("请输入表达式："), QLineEdit::Normal, QString(), &ok);
    if (!ok || expr.trimmed().isEmpty()) {
        return;
    }
    const QString trimmed = expr.trimmed();
    if (!watchExpressions_.contains(trimmed)) {
        watchExpressions_.append(trimmed);
        watchLastValues_.remove(trimmed);
    }
    refreshWatchExpressions();
    if (debugInfoDock_) {
        debugInfoDock_->show();
        debugInfoTabs_->setCurrentWidget(watchTree_);
    }
}

void MainWindow::removeSelectedWatchExpression() {
    QTreeWidgetItem *item = watchTree_ ? watchTree_->currentItem() : nullptr;
    if (!item) {
        return;
    }
    const QString expr = item->text(0);
    watchExpressions_.removeAll(expr);
    watchLastValues_.remove(expr);
    refreshWatchExpressions();
}

void MainWindow::runFile() {
    output_->clear();
    appendBuildOutput(tr("运行程序...\n"));
    if (projectManager_->hasProject()) {
        QString cwd = projectManager_->runWorkingDir();
        if (!cwd.isEmpty() && !QDir::isAbsolutePath(cwd)) {
            cwd = QDir(projectManager_->rootDir()).absoluteFilePath(cwd);
        }
        if (cwd.isEmpty()) {
            cwd = projectManager_->rootDir();
        }
        buildManager_->runLastBinary(projectManager_->runArgs(), cwd);
    } else {
        buildManager_->runLastBinary();
    }
}

void MainWindow::generateMakefile() {
    if (!saveFile()) {
        return;
    }

    BuildManager::BuildConfig config;
    QString makefilePath;

    if (projectManager_->hasProject()) {
        config.sources = projectManager_->sourceFilesAbsolute();
        if (config.sources.isEmpty() && !currentFile_.isEmpty()) {
            config.sources.append(currentFile_);
        }
        config.includeDirs = projectManager_->includeDirsAbsolute();
        config.compiler = projectManager_->compiler();
        config.cxxStandard = projectManager_->cxxStandard();
        config.extraFlags = projectManager_->activeExtraFlags();
        config.outputPath = QDir(projectManager_->rootDir()).filePath(projectManager_->activeOutputName());
        config.workingDirectory = projectManager_->rootDir();
        makefilePath = QDir(projectManager_->rootDir()).filePath("Makefile");
    } else {
        config.sources = {currentFile_};
        config.outputPath = QFileInfo(currentFile_).absolutePath() + QDir::separator() + QFileInfo(currentFile_).completeBaseName();
        config.workingDirectory = QFileInfo(currentFile_).absolutePath();
        makefilePath = QFileInfo(currentFile_).absolutePath() + QDir::separator() + "Makefile";
    }

    if (buildManager_->generateMakefile(config, makefilePath)) {
        appendBuildOutput(tr("Makefile 已生成：%1\n").arg(makefilePath));
    } else {
        appendBuildOutput(tr("生成 Makefile 失败。\n"));
    }
}

void MainWindow::toggleAdvancedParsing(bool enabled) {
    advancedParsingEnabled_ = enabled;
    for (OpenTab &tab : openTabs_) {
        if (tab.highlighter) {
            tab.highlighter->setAdvancedParsingEnabled(enabled);
        }
    }

    if (enabled) {
        sendLspChange();
    } else {
        for (OpenTab &tab : openTabs_) {
            if (tab.editor) {
                tab.editor->setSemanticSelections({});
            }
        }
        if (symbolTree_) {
            symbolTree_->clear();
        }
    }

    statusBar()->showMessage(enabled ? tr("已启用 AST/clangd 解析") : tr("已关闭 AST/clangd 解析"), 1500);
}

void MainWindow::requestCompletion(int line, int character) {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty()) {
        return;
    }
    currentFile_ = tab->filePath;
    sendLspChange();
    lspClient_->requestCompletion(tab->filePath, line, character);
}

void MainWindow::requestGotoDefinition(int line, int character) {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty()) {
        return;
    }
    currentFile_ = tab->filePath;
    sendLspChange();
    lspClient_->requestDefinition(tab->filePath, line, character);
}

void MainWindow::handleDefinitionLocations(const QString &filePath, const QJsonArray &locations) {
    if (locations.isEmpty()) {
        return;
    }

    OpenTab *tab = currentTab();
    if (tab && tab->editor && tab->filePath == filePath) {
        QTextCursor cur = tab->editor->textCursor();
        backStack_.append({filePath, cur.blockNumber(), cur.positionInBlock()});
        forwardStack_.clear();
    }

    const QJsonObject locObj = locations.first().toObject();
    QString uri = locObj.value("uri").toString();
    QJsonObject rangeObj = locObj.value("range").toObject();
    if (uri.isEmpty()) {
        uri = locObj.value("targetUri").toString();
        rangeObj = locObj.value("targetSelectionRange").toObject();
    }

    const QString targetFile = QUrl(uri).toLocalFile();
    const QJsonObject start = rangeObj.value("start").toObject();
    const int targetLine = start.value("line").toInt();
    const int targetChar = start.value("character").toInt();

    jumpToFileLocation(targetFile, targetLine, targetChar, false);
}

void MainWindow::requestReferencesAtCursor() {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty() || !tab->editor) {
        return;
    }
    QTextCursor cur = tab->editor->textCursor();
    currentFile_ = tab->filePath;
    sendLspChange();
    lspClient_->requestReferences(tab->filePath, cur.blockNumber(), cur.positionInBlock());
}

void MainWindow::handleReferencesLocations(const QString &, const QJsonArray &locations) {
    if (!searchResultsTree_) {
        return;
    }
    searchResultsTree_->clear();

    QHash<QString, QTreeWidgetItem *> fileItems;
    int total = 0;

    for (const auto &locVal : locations) {
        if (!locVal.isObject()) {
            continue;
        }
        const QJsonObject obj = locVal.toObject();
        const QString uri = obj.value("uri").toString();
        const QString targetFile = QUrl(uri).toLocalFile();
        const QJsonObject range = obj.value("range").toObject();
        const int line = range.value("start").toObject().value("line").toInt();

        QString snippet;
        QFile file(targetFile);
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            in.setCodec("UTF-8");
#else
            in.setEncoding(QStringConverter::Utf8);
#endif
            int currentLine = 0;
            while (!in.atEnd()) {
                const QString lineText = in.readLine();
                if (currentLine == line) {
                    snippet = lineText.trimmed();
                    break;
                }
                ++currentLine;
            }
        }

        QTreeWidgetItem *fileItem = fileItems.value(targetFile, nullptr);
        if (!fileItem) {
            fileItem = new QTreeWidgetItem(QStringList{QFileInfo(targetFile).fileName()});
            fileItem->setData(0, Qt::UserRole, targetFile);
            searchResultsTree_->addTopLevelItem(fileItem);
            fileItems.insert(targetFile, fileItem);
        }
        auto *matchItem = new QTreeWidgetItem(QStringList{tr("%1: %2").arg(line + 1).arg(snippet)});
        matchItem->setData(0, Qt::UserRole, targetFile);
        matchItem->setData(0, Qt::UserRole + 1, line);
        fileItem->addChild(matchItem);
        ++total;
    }

    if (auto *dock = qobject_cast<QDockWidget *>(searchResultsTree_->parentWidget())) {
        dock->setWindowTitle(tr("引用结果"));
        dock->show();
        dock->raise();
    }
    searchResultsTree_->expandAll();
    statusBar()->showMessage(tr("共找到 %1 处引用").arg(total), 3000);
}

void MainWindow::renameSymbolAtCursor() {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty() || !tab->editor) {
        return;
    }

    QTextCursor cur = tab->editor->textCursor();
    if (!cur.hasSelection()) {
        cur.select(QTextCursor::WordUnderCursor);
    }
    const QString oldName = cur.selectedText();
    if (oldName.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("重命名符号"), tr("新名称："), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName == oldName) {
        return;
    }

    currentFile_ = tab->filePath;
    sendLspChange();
    lspClient_->requestRename(tab->filePath, cur.blockNumber(), cur.positionInBlock(), newName.trimmed());
}

void MainWindow::handleRenameEdits(const QString &, const QJsonObject &edits) {
    const QJsonObject changes = edits.value("changes").toObject();
    if (changes.isEmpty()) {
        return;
    }

    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString uri = it.key();
        const QString filePath = QUrl(uri).toLocalFile();
        const QJsonArray editArray = it.value().toArray();
        if (filePath.isEmpty() || editArray.isEmpty()) {
            continue;
        }

        const int tabIndex = indexOfFile(filePath);
        if (tabIndex >= 0) {
            OpenTab *tab = tabAt(tabIndex);
            if (!tab || !tab->editor) {
                continue;
            }
            QTextDocument *doc = tab->editor->document();

            struct EditItem { int startPos; int endPos; QString newText; };
            QVector<EditItem> items;
            for (const auto &val : editArray) {
                if (!val.isObject()) {
                    continue;
                }
                const QJsonObject obj = val.toObject();
                const QJsonObject range = obj.value("range").toObject();
                const QJsonObject start = range.value("start").toObject();
                const QJsonObject end = range.value("end").toObject();
                const int startLine = start.value("line").toInt();
                const int startChar = start.value("character").toInt();
                const int endLine = end.value("line").toInt(startLine);
                const int endChar = end.value("character").toInt(startChar);

                QTextBlock startBlock = doc->findBlockByNumber(startLine);
                QTextBlock endBlock = doc->findBlockByNumber(endLine);
                if (!startBlock.isValid() || !endBlock.isValid()) {
                    continue;
                }
                const int startPos = startBlock.position() + startChar;
                const int endPos = endBlock.position() + endChar;
                items.append({startPos, endPos, obj.value("newText").toString()});
            }

            std::sort(items.begin(), items.end(), [](const EditItem &a, const EditItem &b) { return a.startPos > b.startPos; });

            QTextCursor cursor(doc);
            cursor.beginEditBlock();
            for (const auto &item : items) {
                QTextCursor c(doc);
                c.setPosition(item.startPos);
                c.setPosition(item.endPos, QTextCursor::KeepAnchor);
                c.insertText(item.newText);
            }
            cursor.endEditBlock();
            updateTabTitle(tabIndex);
        } else {
            QFile file(filePath);
            if (!file.open(QFile::ReadOnly | QFile::Text)) {
                continue;
            }
            QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            in.setCodec("UTF-8");
#else
            in.setEncoding(QStringConverter::Utf8);
#endif
            QString text = in.readAll();
            file.close();

            QVector<int> lineOffsets;
            lineOffsets.reserve(text.count('\n') + 2);
            lineOffsets.append(0);
            for (int i = 0; i < text.size(); ++i) {
                if (text.at(i) == '\n') {
                    lineOffsets.append(i + 1);
                }
            }

            struct EditItem { int startPos; int endPos; QString newText; };
            QVector<EditItem> items;
            for (const auto &val : editArray) {
                if (!val.isObject()) {
                    continue;
                }
                const QJsonObject obj = val.toObject();
                const QJsonObject range = obj.value("range").toObject();
                const QJsonObject start = range.value("start").toObject();
                const QJsonObject end = range.value("end").toObject();
                const int startLine = start.value("line").toInt();
                const int startChar = start.value("character").toInt();
                const int endLine = end.value("line").toInt(startLine);
                const int endChar = end.value("character").toInt(startChar);

                if (startLine >= lineOffsets.size() || endLine >= lineOffsets.size()) {
                    continue;
                }
                const int startPos = lineOffsets[startLine] + startChar;
                const int endPos = lineOffsets[endLine] + endChar;
                items.append({startPos, endPos, obj.value("newText").toString()});
            }
            std::sort(items.begin(), items.end(), [](const EditItem &a, const EditItem &b) { return a.startPos > b.startPos; });

            for (const auto &item : items) {
                text.replace(item.startPos, item.endPos - item.startPos, item.newText);
            }

            if (file.open(QFile::WriteOnly | QFile::Text)) {
                QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                out.setCodec("UTF-8");
#else
                out.setEncoding(QStringConverter::Utf8);
#endif
                out << text;
            }
        }
    }

    statusBar()->showMessage(tr("重命名完成"), 2000);
}

void MainWindow::navigateBack() {
    if (backStack_.isEmpty()) {
        return;
    }
    OpenTab *tab = currentTab();
    if (tab && tab->editor && !tab->filePath.isEmpty()) {
        QTextCursor cur = tab->editor->textCursor();
        forwardStack_.append({tab->filePath, cur.blockNumber(), cur.positionInBlock()});
    }
    const NavLocation target = backStack_.takeLast();
    jumpToFileLocation(target.filePath, target.line, target.character, false);
}

void MainWindow::navigateForward() {
    if (forwardStack_.isEmpty()) {
        return;
    }
    OpenTab *tab = currentTab();
    if (tab && tab->editor && !tab->filePath.isEmpty()) {
        QTextCursor cur = tab->editor->textCursor();
        backStack_.append({tab->filePath, cur.blockNumber(), cur.positionInBlock()});
    }
    const NavLocation target = forwardStack_.takeLast();
    jumpToFileLocation(target.filePath, target.line, target.character, false);
}

void MainWindow::handleCompletionItems(const QList<LspCompletionItem> &items) {
    if (!items.isEmpty()) {
        if (auto *editor = currentEditor()) {
            editor->showCompletions(items);
        }
    }
}

void MainWindow::handleDiagnostics(const QString &filePath,
                                   const QList<QTextEdit::ExtraSelection> &selections,
                                   const QStringList &messages) {
    if (filePath != currentFile_) {
        return;
    }
    if (auto *editor = currentEditor()) {
        editor->setDiagnosticSelections(selections);
    }
    if (!messages.isEmpty()) {
        appendBuildOutput(tr("clangd 诊断：\n"));
        for (const QString &msg : messages) {
            appendBuildOutput("- " + msg);
        }
    }
}

void MainWindow::handleDocumentSymbols(const QString &filePath, const QJsonArray &symbols) {
    if (filePath != currentFile_ || !symbolTree_) {
        return;
    }
    symbolTree_->clear();

    std::function<void(const QJsonObject &, QTreeWidgetItem *)> addDocSymbol;
    addDocSymbol = [&](const QJsonObject &obj, QTreeWidgetItem *parent) {
        const QString name = obj.value("name").toString();
        const int kind = obj.value("kind").toInt();
        QJsonObject selRange = obj.value("selectionRange").toObject();
        if (selRange.isEmpty()) {
            selRange = obj.value("range").toObject();
        }
        const QJsonObject start = selRange.value("start").toObject();
        const int line = start.value("line").toInt();

        auto *item = new QTreeWidgetItem(QStringList{name});
        item->setData(0, Qt::UserRole, line);
        item->setData(0, Qt::UserRole + 1, kind);

        if (parent) {
            parent->addChild(item);
        } else {
            symbolTree_->addTopLevelItem(item);
        }

        const QJsonArray children = obj.value("children").toArray();
        for (const auto &childVal : children) {
            if (childVal.isObject()) {
                addDocSymbol(childVal.toObject(), item);
            }
        }
    };

    for (const auto &symbolVal : symbols) {
        if (!symbolVal.isObject()) {
            continue;
        }
        const QJsonObject obj = symbolVal.toObject();
        if (obj.contains("location")) {
            const QString name = obj.value("name").toString();
            const int kind = obj.value("kind").toInt();
            const QJsonObject range = obj.value("location").toObject().value("range").toObject();
            const int line = range.value("start").toObject().value("line").toInt();
            auto *item = new QTreeWidgetItem(QStringList{name});
            item->setData(0, Qt::UserRole, line);
            item->setData(0, Qt::UserRole + 1, kind);
            symbolTree_->addTopLevelItem(item);
        } else {
            addDocSymbol(obj, nullptr);
        }
    }

    symbolTree_->expandToDepth(1);
}

void MainWindow::handleFoldingRanges(const QString &filePath, const QJsonArray &ranges) {
    const int index = indexOfFile(filePath);
    if (index < 0) {
        return;
    }
    OpenTab *tab = tabAt(index);
    if (!tab) {
        return;
    }

    tab->foldingRanges.clear();
    for (const auto &rangeVal : ranges) {
        if (!rangeVal.isObject()) {
            continue;
        }
        const QJsonObject obj = rangeVal.toObject();
        const int startLine = obj.value("startLine").toInt();
        const int endLine = obj.value("endLine").toInt(startLine);
        if (endLine > startLine) {
            tab->foldingRanges.append({startLine, endLine});
        }
    }
}

void MainWindow::handleSemanticTokens(const QString &filePath, const QJsonArray &data) {
    if (filePath != currentFile_) {
        return;
    }
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath != filePath) {
        return;
    }
    CodeEditor *editor = tab->editor;
    if (!editor) {
        return;
    }

    const QStringList tokenTypes = lspClient_->semanticTokenTypes();
    if (tokenTypes.isEmpty()) {
        return;
    }

    QList<QTextEdit::ExtraSelection> selections;
    int line = 0;
    int character = 0;
    for (int i = 0; i + 4 < data.size(); i += 5) {
        const int deltaLine = data.at(i).toInt();
        const int deltaStart = data.at(i + 1).toInt();
        const int length = data.at(i + 2).toInt();
        const int typeIdx = data.at(i + 3).toInt();

        line += deltaLine;
        if (deltaLine == 0) {
            character += deltaStart;
        } else {
            character = deltaStart;
        }

        if (typeIdx < 0 || typeIdx >= tokenTypes.size()) {
            continue;
        }
        const QString typeName = tokenTypes.at(typeIdx);
        if (typeName == QLatin1String("keyword")) {
            continue;
        }

        QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid()) {
            continue;
        }
        const int startPos = block.position() + character;
        QTextCursor cursor(editor->document());
        cursor.setPosition(startPos);
        cursor.setPosition(startPos + length, QTextCursor::KeepAnchor);

        QTextCharFormat fmt;
        if (typeName == QLatin1String("class") || typeName == QLatin1String("struct") || typeName == QLatin1String("enum")) {
            fmt.setForeground(QColor(0, 70, 140));
            fmt.setFontWeight(QFont::Bold);
        } else if (typeName == QLatin1String("function") || typeName == QLatin1String("method")) {
            fmt.setForeground(QColor(20, 20, 20));
            fmt.setFontWeight(QFont::Bold);
        } else if (typeName == QLatin1String("namespace")) {
            fmt.setForeground(QColor(100, 40, 140));
        } else if (typeName == QLatin1String("macro")) {
            fmt.setForeground(QColor(0, 110, 0));
            fmt.setFontWeight(QFont::Bold);
        } else if (typeName == QLatin1String("parameter") || typeName == QLatin1String("variable")) {
            fmt.setForeground(QColor(80, 80, 80));
        }

        if (fmt.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format = fmt;
            selections.append(sel);
        }
    }

    editor->setSemanticSelections(selections);
}

void MainWindow::foldAll() {
    OpenTab *tab = currentTab();
    if (!tab) {
        return;
    }
    if (tab->foldingRanges.isEmpty() && advancedParsingEnabled_ && !tab->filePath.isEmpty()) {
        lspClient_->requestFoldingRanges(tab->filePath);
    }

    QTextDocument *doc = tab->editor->document();
    for (const auto &range : tab->foldingRanges) {
        for (int line = range.first + 1; line <= range.second; ++line) {
            QTextBlock block = doc->findBlockByNumber(line);
            if (block.isValid()) {
                block.setVisible(false);
                block.setLineCount(0);
            }
        }
    }
    doc->markContentsDirty(0, doc->characterCount());
    tab->editor->viewport()->update();
}

void MainWindow::unfoldAll() {
    OpenTab *tab = currentTab();
    if (!tab) {
        return;
    }

    QTextDocument *doc = tab->editor->document();
    QTextBlock block = doc->firstBlock();
    while (block.isValid()) {
        if (!block.isVisible()) {
            block.setVisible(true);
            block.setLineCount(1);
        }
        block = block.next();
    }
    doc->markContentsDirty(0, doc->characterCount());
    tab->editor->viewport()->update();
}

void MainWindow::showFindDialog() {
    if (!findDialog_) {
        return;
    }
    if (auto *editor = currentEditor()) {
        findDialog_->setEditor(editor);
    }
    findDialog_->showFind();
}

void MainWindow::showReplaceDialog() {
    if (!findDialog_) {
        return;
    }
    if (auto *editor = currentEditor()) {
        findDialog_->setEditor(editor);
    }
    findDialog_->showReplace();
}

void MainWindow::findInFiles() {
    const QString query = QInputDialog::getText(this, tr("全工程搜索"), tr("请输入搜索内容："));
    if (query.trimmed().isEmpty() || !searchResultsTree_) {
        return;
    }

    searchResultsTree_->clear();

    const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QDir::currentPath();
    QDirIterator it(root, {"*.cpp", "*.cc", "*.cxx", "*.h", "*.hpp"}, QDir::Files, QDirIterator::Subdirectories);

    QHash<QString, QTreeWidgetItem *> fileItems;
    int totalMatches = 0;

    while (it.hasNext()) {
        const QString filePath = it.next();
        QFile file(filePath);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            continue;
        }
        QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        in.setCodec("UTF-8");
#else
        in.setEncoding(QStringConverter::Utf8);
#endif
        int lineNum = 0;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.contains(query, Qt::CaseInsensitive)) {
                QTreeWidgetItem *fileItem = fileItems.value(filePath, nullptr);
                if (!fileItem) {
                    fileItem = new QTreeWidgetItem(QStringList{QFileInfo(filePath).fileName()});
                    fileItem->setData(0, Qt::UserRole, filePath);
                    searchResultsTree_->addTopLevelItem(fileItem);
                    fileItems.insert(filePath, fileItem);
                }
                const QString snippet = line.trimmed();
                auto *matchItem = new QTreeWidgetItem(QStringList{tr("%1: %2").arg(lineNum + 1).arg(snippet)});
                matchItem->setData(0, Qt::UserRole, filePath);
                matchItem->setData(0, Qt::UserRole + 1, lineNum);
                fileItem->addChild(matchItem);
                ++totalMatches;
            }
            ++lineNum;
        }
    }

    if (auto *dock = qobject_cast<QDockWidget *>(searchResultsTree_->parentWidget())) {
        dock->show();
        dock->raise();
    }
    searchResultsTree_->expandAll();
    statusBar()->showMessage(tr("搜索完成，共找到 %1 处匹配").arg(totalMatches), 3000);
}

void MainWindow::scheduleLspChange() {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty()) {
        return;
    }
    currentFile_ = tab->filePath;
    lspChangeTimer_->start();
}

void MainWindow::sendLspChange() {
    OpenTab *tab = currentTab();
    if (!tab || tab->filePath.isEmpty()) {
        return;
    }
    currentFile_ = tab->filePath;
    if (!lspClient_->isRunning()) {
        const QString root = projectManager_->hasProject() ? projectManager_->rootDir() : QFileInfo(currentFile_).absolutePath();
        lspClient_->start(root);
    }
    lspClient_->changeDocument(tab->filePath, tab->editor->toPlainText());
    if (advancedParsingEnabled_) {
        lspClient_->requestDocumentSymbols(tab->filePath);
        lspClient_->requestFoldingRanges(tab->filePath);
        lspClient_->requestSemanticTokens(tab->filePath);
    }
}

void MainWindow::appendBuildOutput(const QString &text) {
    output_->appendPlainText(text);
}

void MainWindow::buildFinished(int exitCode, QProcess::ExitStatus status) {
    if (status == QProcess::NormalExit && exitCode == 0) {
        appendBuildOutput(tr("编译成功。\n"));
        if (pendingDebugAfterBuild_) {
            pendingDebugAfterBuild_ = false;
            startDebug();
        }
    } else {
        appendBuildOutput(tr("编译失败，退出码：%1\n").arg(exitCode));
        pendingDebugAfterBuild_ = false;
    }
}

void MainWindow::highlightDebugLine(const QString &filePath, int line) {
    debugExecFile_ = QFileInfo(filePath).absoluteFilePath();
    debugExecLine_ = line;

    for (auto &tab : openTabs_) {
        if (!tab.editor) {
            continue;
        }
        QList<QTextEdit::ExtraSelection> selections;
        const QString abs = QFileInfo(tab.filePath).absoluteFilePath();
        if (!debugExecFile_.isEmpty() && abs == debugExecFile_ && line >= 0) {
            QTextBlock block = tab.editor->document()->findBlockByNumber(line);
            if (block.isValid()) {
                QTextCursor cur(block);
                QTextEdit::ExtraSelection sel;
                sel.cursor = cur;
                sel.format.setProperty(QTextFormat::FullWidthSelection, true);
                sel.format.setBackground(QColor(200, 255, 200));
                selections.append(sel);
            }
        }
        tab.editor->setDebugSelections(selections);
    }
}

void MainWindow::refreshWatchExpressions() {
    if (!watchTree_) {
        return;
    }
    watchTree_->clear();
    for (const QString &expr : watchExpressions_) {
        auto *item = new QTreeWidgetItem(watchTree_, QStringList{expr, QString()});
        item->setData(0, Qt::UserRole, expr);
        const QString last = watchLastValues_.value(expr);
        if (!last.isEmpty()) {
            item->setText(1, last);
        }
        if (gdbClient_ && gdbClient_->isRunning()) {
            gdbClient_->evaluateExpression(expr);
        }
    }
    watchTree_->expandAll();
}

QString MainWindow::detectTerminalProgram() const {
#ifdef Q_OS_WIN
    return QStringLiteral("powershell");
#else
    const QString envShell = qEnvironmentVariable("SHELL");
    if (!envShell.isEmpty() && QFileInfo(envShell).exists()) {
        return envShell;
    }
    if (QFileInfo(QStringLiteral("/bin/zsh")).exists()) {
        return QStringLiteral("/bin/zsh");
    }
    return QStringLiteral("/bin/bash");
#endif
}

void MainWindow::startTerminalShell() {
    if (!terminalOutput_ || !terminalInput_) {
        return;
    }
    if (terminalProcess_) {
        terminalProcess_->kill();
        terminalProcess_->deleteLater();
        terminalProcess_ = nullptr;
    }

    terminalProcess_ = new QProcess(this);
    terminalProcess_->setProcessChannelMode(QProcess::MergedChannels);
    QString root = QDir::currentPath();
    if (projectManager_ && projectManager_->hasProject()) {
        root = projectManager_->rootDir();
    }
    terminalProcess_->setWorkingDirectory(root);

    const QString program = detectTerminalProgram();
    QStringList args;
#ifdef Q_OS_WIN
    args << "-NoLogo" << "-NoExit";
#else
    args << "-i";
#endif
    terminalProcess_->start(program, args);

    connect(terminalProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        terminalOutput_->appendPlainText(QString::fromLocal8Bit(terminalProcess_->readAllStandardOutput()));
    });
    connect(terminalProcess_, &QProcess::readyReadStandardError, this, [this]() {
        terminalOutput_->appendPlainText(QString::fromLocal8Bit(terminalProcess_->readAllStandardError()));
    });
    connect(terminalProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        terminalOutput_->appendPlainText(tr("终端已退出，退出码：%1").arg(code));
    });

    terminalOutput_->appendPlainText(tr("终端已启动：%1").arg(program));
}

void MainWindow::sendTerminalCommand() {
    if (!terminalProcess_ || terminalProcess_->state() == QProcess::NotRunning) {
        startTerminalShell();
    }
    if (!terminalProcess_) {
        return;
    }
    const QString cmd = terminalInput_->text();
    if (cmd.trimmed().isEmpty()) {
        return;
    }
    terminalOutput_->appendPlainText(QStringLiteral("> %1").arg(cmd));
    terminalProcess_->write((cmd + "\n").toLocal8Bit());
    terminalInput_->clear();
}
