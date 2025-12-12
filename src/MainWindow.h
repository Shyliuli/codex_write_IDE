#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QTimer>

#include <memory>
#include <QPair>
#include <QVector>
#include <QHash>
#include <QSet>

#include "BuildManager.h"
#include "CppRusticHighlighter.h"
#include "GdbMiClient.h"
#include "LspClient.h"
#include "ProjectManager.h"

class CodeEditor;
class QPlainTextEdit;
class QLineEdit;
class QFileSystemModel;
class QTreeView;
class QTabWidget;
class QTreeWidget;
class QStackedWidget;
class FindReplaceDialog;
class ProjectSettingsDialog;
class GdbMiClient;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void documentModified(bool modified);

    void newProject();
    void openProject();
    void saveProject();
    void closeProject();
    void addSourceFileToProject();
    void addIncludeDirToProject();
    void fetchRusticLibrary();
    void showProjectSettings();

    void compileFile();
    void rebuildProject();
    void cleanProject();
    void runFile();
    void generateMakefile();
    void toggleAdvancedParsing(bool enabled);
    void runExternalTool();

    void setLightTheme();
    void setDarkTheme();

    void importColorScheme();
    void exportColorScheme();

    void showShortcutSettings();

    void sendTerminalCommand();

    void startDebug();
    void stopDebug();
    void sendDebugCommand();
    void continueDebug();
    void stepOverDebug();
    void stepIntoDebug();
    void stepOutDebug();
    void toggleBreakpointAtCursor();
    void restartDebug();
    void buildAndDebug();

    void addWatchExpression();
    void removeSelectedWatchExpression();

    void requestCompletion(int line, int character);
    void handleCompletionItems(const QList<LspCompletionItem> &items);
    void handleDiagnostics(const QString &filePath,
                           const QList<QTextEdit::ExtraSelection> &selections,
                           const QStringList &messages);
    void handleDocumentSymbols(const QString &filePath, const QJsonArray &symbols);
    void handleFoldingRanges(const QString &filePath, const QJsonArray &ranges);
    void handleSemanticTokens(const QString &filePath, const QJsonArray &data);
    void requestGotoDefinition(int line, int character);
    void handleDefinitionLocations(const QString &filePath, const QJsonArray &locations);
    void requestReferencesAtCursor();
    void handleReferencesLocations(const QString &filePath, const QJsonArray &locations);
    void renameSymbolAtCursor();
    void handleRenameEdits(const QString &filePath, const QJsonObject &edits);
    void navigateBack();
    void navigateForward();
    void foldAll();
    void unfoldAll();
    void showFindDialog();
    void showReplaceDialog();
    void findInFiles();
    void scheduleLspChange();
    void sendLspChange();

    void appendBuildOutput(const QString &text);
    void buildFinished(int exitCode, QProcess::ExitStatus status);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    bool maybeSave();
    void applyTheme(bool dark);
    void loadUiSettings();
    void saveUiSettings();
    void highlightDebugLine(const QString &filePath, int line);
    void refreshWatchExpressions();
    void startTerminalShell();
    QString detectTerminalProgram() const;

    struct OpenTab {
        CodeEditor *editor = nullptr;
        CppRusticHighlighter *highlighter = nullptr;
        QString filePath;
        QString displayName;
        bool isUntitled = true;
        QVector<QPair<int, int>> foldingRanges;
    };

    CodeEditor *currentEditor() const;
    OpenTab *currentTab();
    const OpenTab *currentTab() const;
    OpenTab *tabAt(int index);
    const OpenTab *tabAt(int index) const;
    int indexOfEditor(CodeEditor *editor) const;
    int indexOfFile(const QString &filePath) const;

    void createNewTab(const QString &filePath = QString(), const QString &content = QString());
    bool closeTab(int index);
    bool maybeSaveTab(int index);
    bool maybeSaveAllTabs();

    bool loadFileToTab(int index, const QString &path);
    bool writeTabToFile(int index, const QString &path);
    void updateTabTitle(int index);
    void updateWindowTitle();
    void jumpToFileLocation(const QString &filePath, int line, int character, bool recordHistory);
    void rebuildProjectTree();
    void showProjectGroupsView(bool enabled);

    QTabWidget *tabWidget_;
    QPlainTextEdit *output_;
    QPlainTextEdit *debugOutput_ = nullptr;
    QLineEdit *debugInput_ = nullptr;
    QDockWidget *debugInfoDock_ = nullptr;
    QTabWidget *debugInfoTabs_ = nullptr;
    QTreeWidget *breakpointsTree_ = nullptr;
    QTreeWidget *stackTree_ = nullptr;
    QTreeWidget *localsTree_ = nullptr;
    QTreeWidget *threadsTree_ = nullptr;
    QTreeWidget *watchTree_ = nullptr;

    QPlainTextEdit *terminalOutput_ = nullptr;
    QLineEdit *terminalInput_ = nullptr;
    QDockWidget *terminalDock_ = nullptr;
    QProcess *terminalProcess_ = nullptr;

    std::unique_ptr<BuildManager> buildManager_;
    std::unique_ptr<ProjectManager> projectManager_;
    std::unique_ptr<LspClient> lspClient_;
    std::unique_ptr<GdbMiClient> gdbClient_;

    QTimer *lspChangeTimer_ = nullptr;

    QString currentFile_;
    bool advancedParsingEnabled_ = false;
    bool darkThemeEnabled_ = false;

    QString debugExecFile_;
    int debugExecLine_ = -1;
    bool pendingDebugAfterBuild_ = false;

    bool firstShow_ = true;

    QStringList watchExpressions_;
    QHash<QString, QString> watchLastValues_;

    QVector<OpenTab> openTabs_;
    int untitledCounter_ = 1;

    QAction *newAct_ = nullptr;
    QAction *openAct_ = nullptr;
    QAction *saveAct_ = nullptr;
    QAction *saveAsAct_ = nullptr;
    QAction *exitAct_ = nullptr;

    QAction *compileAct_ = nullptr;
    QAction *rebuildAct_ = nullptr;
    QAction *cleanAct_ = nullptr;
    QAction *runAct_ = nullptr;
    QAction *makefileAct_ = nullptr;
    QAction *externalToolAct_ = nullptr;
    QAction *advancedParseAct_ = nullptr;
    QAction *themeLightAct_ = nullptr;
    QAction *themeDarkAct_ = nullptr;
    QAction *themeImportAct_ = nullptr;
    QAction *themeExportAct_ = nullptr;
    QAction *terminalAct_ = nullptr;
    QAction *shortcutSettingsAct_ = nullptr;
    QAction *debugStartAct_ = nullptr;
    QAction *debugStopAct_ = nullptr;
    QAction *debugContinueAct_ = nullptr;
    QAction *debugStepOverAct_ = nullptr;
    QAction *debugStepIntoAct_ = nullptr;
    QAction *debugStepOutAct_ = nullptr;
    QAction *debugToggleBpAct_ = nullptr;
    QAction *debugRestartAct_ = nullptr;
    QAction *debugBuildAndStartAct_ = nullptr;
    QAction *debugAddWatchAct_ = nullptr;
    QAction *debugRemoveWatchAct_ = nullptr;
    QAction *foldAllAct_ = nullptr;
    QAction *unfoldAllAct_ = nullptr;

    QAction *findAct_ = nullptr;
    QAction *replaceAct_ = nullptr;
    QAction *findInFilesAct_ = nullptr;

    QAction *navBackAct_ = nullptr;
    QAction *navForwardAct_ = nullptr;
    QAction *findReferencesAct_ = nullptr;
    QAction *renameSymbolAct_ = nullptr;

    QAction *newProjectAct_ = nullptr;
    QAction *openProjectAct_ = nullptr;
    QAction *saveProjectAct_ = nullptr;
    QAction *closeProjectAct_ = nullptr;
    QAction *addSourceAct_ = nullptr;
    QAction *addIncludeAct_ = nullptr;
    QAction *fetchRusticAct_ = nullptr;
    QAction *projectSettingsAct_ = nullptr;

    QTreeView *projectView_ = nullptr;
    QFileSystemModel *projectModel_ = nullptr;
    QTreeWidget *projectTree_ = nullptr;
    QStackedWidget *projectStack_ = nullptr;

    QTreeWidget *symbolTree_ = nullptr;
    QTreeWidget *searchResultsTree_ = nullptr;

    FindReplaceDialog *findDialog_ = nullptr;

    struct NavLocation {
        QString filePath;
        int line = 0;
        int character = 0;
    };
    QVector<NavLocation> backStack_;
    QVector<NavLocation> forwardStack_;

    QHash<QString, QSet<int>> breakpointsByFile_;
};
