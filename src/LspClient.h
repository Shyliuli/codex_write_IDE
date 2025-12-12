#pragma once

#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QTextEdit>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

struct LspCompletionItem {
    QString label;
    QString insertText;
    QString sortText;
    int kind = 0;
};
Q_DECLARE_METATYPE(LspCompletionItem)

#include <QList>
#include <QPair>
#include <QHash>

class LspClient : public QObject {
    Q_OBJECT

public:
    explicit LspClient(QObject *parent = nullptr);
    ~LspClient() override;

    void start(const QString &rootDir);
    void stop();

    bool isRunning() const;

    void setCurrentDocument(QTextDocument *document, const QString &filePath);

    void openDocument(const QString &filePath, const QString &text);
    void changeDocument(const QString &filePath, const QString &text);
    void saveDocument(const QString &filePath);

    void requestCompletion(const QString &filePath, int line, int character);
    void requestDocumentSymbols(const QString &filePath);
    void requestFoldingRanges(const QString &filePath);
    void requestSemanticTokens(const QString &filePath);
    void requestDefinition(const QString &filePath, int line, int character);
    void requestReferences(const QString &filePath, int line, int character);
    void requestRename(const QString &filePath, int line, int character, const QString &newName);

    QStringList semanticTokenTypes() const;

signals:
    void diagnosticsUpdated(const QString &filePath,
                            const QList<QTextEdit::ExtraSelection> &selections,
                            const QStringList &messages);
    void completionItemsReady(const QList<LspCompletionItem> &items);
    void documentSymbolsReady(const QString &filePath, const QJsonArray &symbols);
    void foldingRangesReady(const QString &filePath, const QJsonArray &ranges);
    void semanticTokensReady(const QString &filePath, const QJsonArray &data);
    void definitionLocationsReady(const QString &filePath, const QJsonArray &locations);
    void referencesLocationsReady(const QString &filePath, const QJsonArray &locations);
    void renameEditsReady(const QString &filePath, const QJsonObject &edits);
    void serverLog(const QString &text);

private slots:
    void handleReadyRead();
    void handleError(QProcess::ProcessError error);

private:
    void initializeServer();
    void sendMessage(const QJsonObject &message);
    int sendRequest(const QString &method, const QJsonObject &params);
    void sendNotification(const QString &method, const QJsonObject &params);
    void parseBuffer();
    void handleMessage(const QJsonObject &message);

    QString pathToUri(const QString &filePath) const;
    int nextId();
    int currentVersion(const QString &filePath) const;
    int bumpVersion(const QString &filePath);

    QList<QTextEdit::ExtraSelection> buildDiagnosticSelections(const QJsonArray &diagnostics,
                                                              QStringList *messages) const;

    QProcess process_;
    QByteArray buffer_;
    int nextId_ = 1;
    bool initialized_ = false;
    QString rootDir_;

    QStringList semanticTokenTypes_;

    QPointer<QTextDocument> currentDocument_;
    QString currentFilePath_;

    QHash<int, QString> pendingRequests_;
    QHash<QString, int> docVersions_;

    QList<QPair<QString, QString>> pendingOpenDocs_;
};
