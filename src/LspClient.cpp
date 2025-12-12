#include "LspClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>

#include <algorithm>
#include <cstring>

LspClient::LspClient(QObject *parent) : QObject(parent) {
    qRegisterMetaType<QList<LspCompletionItem>>("QList<LspCompletionItem>");
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &LspClient::handleReadyRead);
    connect(&process_, &QProcess::errorOccurred, this, &LspClient::handleError);
}

LspClient::~LspClient() {
    stop();
}

bool LspClient::isRunning() const {
    return process_.state() != QProcess::NotRunning;
}

void LspClient::start(const QString &rootDir) {
    stop();

    rootDir_ = rootDir;
    initialized_ = false;
    buffer_.clear();
    pendingRequests_.clear();
    docVersions_.clear();
    pendingOpenDocs_.clear();

    QStringList args;
    args << "--background-index" << "--clang-tidy" << "--offset-encoding=utf-16";
    if (!rootDir_.isEmpty()) {
        args << ("--compile-commands-dir=" + rootDir_);
    }

    process_.setProgram("clangd");
    process_.setArguments(args);
    if (!rootDir_.isEmpty()) {
        process_.setWorkingDirectory(rootDir_);
    }
    process_.start();

    if (!process_.waitForStarted(3000)) {
        emit serverLog(tr("无法启动 clangd"));
        return;
    }
    initializeServer();
}

void LspClient::stop() {
    if (process_.state() == QProcess::NotRunning) {
        return;
    }

    if (initialized_) {
        sendRequest("shutdown", QJsonObject());
        sendNotification("exit", QJsonObject());
    }

    process_.kill();
    process_.waitForFinished(2000);
    initialized_ = false;
}

void LspClient::setCurrentDocument(QTextDocument *document, const QString &filePath) {
    currentDocument_ = document;
    currentFilePath_ = filePath;
}

void LspClient::initializeServer() {
    QJsonObject capabilities;
    QJsonObject sync;
    sync.insert("didSave", true);
    QJsonObject semanticCaps;
    semanticCaps.insert("requests", QJsonObject{{"full", true}});
    semanticCaps.insert("formats", QJsonArray{QStringLiteral("relative")});
    capabilities.insert("textDocument",
                        QJsonObject{{"synchronization", sync},
                                    {"completion", QJsonObject()},
                                    {"documentSymbol", QJsonObject()},
                                    {"foldingRange", QJsonObject()},
                                    {"semanticTokens", semanticCaps}});
    capabilities.insert("workspace", QJsonObject{{"workspaceFolders", true}});

    QJsonObject params;
    params.insert("processId", static_cast<int>(QCoreApplication::applicationPid()));
    params.insert("rootUri", QUrl::fromLocalFile(rootDir_).toString());
    params.insert("capabilities", capabilities);

    sendRequest("initialize", params);
}

void LspClient::openDocument(const QString &filePath, const QString &text) {
    if (!initialized_) {
        pendingOpenDocs_.append({filePath, text});
        return;
    }

    const int version = bumpVersion(filePath);
    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    doc.insert("languageId", "cpp");
    doc.insert("version", version);
    doc.insert("text", text);

    sendNotification("textDocument/didOpen", QJsonObject{{"textDocument", doc}});
}

void LspClient::changeDocument(const QString &filePath, const QString &text) {
    if (!initialized_) {
        return;
    }

    const int version = bumpVersion(filePath);
    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    doc.insert("version", version);

    QJsonArray changes;
    changes.append(QJsonObject{{"text", text}});

    QJsonObject params;
    params.insert("textDocument", doc);
    params.insert("contentChanges", changes);
    sendNotification("textDocument/didChange", params);
}

void LspClient::saveDocument(const QString &filePath) {
    if (!initialized_) {
        return;
    }
    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    sendNotification("textDocument/didSave", QJsonObject{{"textDocument", doc}});
}

void LspClient::requestCompletion(const QString &filePath, int line, int character) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));

    QJsonObject position;
    position.insert("line", line);
    position.insert("character", character);

    QJsonObject params;
    params.insert("textDocument", doc);
    params.insert("position", position);

    sendRequest("textDocument/completion", params);
}

void LspClient::requestDocumentSymbols(const QString &filePath) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject params;
    params.insert("textDocument", doc);

    const int id = sendRequest("textDocument/documentSymbol", params);
    pendingRequests_.insert(id, "textDocument/documentSymbol|" + filePath);
}

void LspClient::requestFoldingRanges(const QString &filePath) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject params;
    params.insert("textDocument", doc);

    const int id = sendRequest("textDocument/foldingRange", params);
    pendingRequests_.insert(id, "textDocument/foldingRange|" + filePath);
}

void LspClient::requestSemanticTokens(const QString &filePath) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject params;
    params.insert("textDocument", doc);

    const int id = sendRequest("textDocument/semanticTokens/full", params);
    pendingRequests_.insert(id, "textDocument/semanticTokens/full|" + filePath);
}

void LspClient::requestDefinition(const QString &filePath, int line, int character) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject position;
    position.insert("line", line);
    position.insert("character", character);

    QJsonObject params;
    params.insert("textDocument", doc);
    params.insert("position", position);

    const int id = sendRequest("textDocument/definition", params);
    pendingRequests_.insert(id, "textDocument/definition|" + filePath);
}

void LspClient::requestReferences(const QString &filePath, int line, int character) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject position;
    position.insert("line", line);
    position.insert("character", character);

    QJsonObject context;
    context.insert("includeDeclaration", true);

    QJsonObject params;
    params.insert("textDocument", doc);
    params.insert("position", position);
    params.insert("context", context);

    const int id = sendRequest("textDocument/references", params);
    pendingRequests_.insert(id, "textDocument/references|" + filePath);
}

void LspClient::requestRename(const QString &filePath, int line, int character, const QString &newName) {
    if (!initialized_) {
        return;
    }

    QJsonObject doc;
    doc.insert("uri", pathToUri(filePath));
    QJsonObject position;
    position.insert("line", line);
    position.insert("character", character);

    QJsonObject params;
    params.insert("textDocument", doc);
    params.insert("position", position);
    params.insert("newName", newName);

    const int id = sendRequest("textDocument/rename", params);
    pendingRequests_.insert(id, "textDocument/rename|" + filePath);
}

void LspClient::handleReadyRead() {
    buffer_.append(process_.readAllStandardOutput());
    parseBuffer();
}

void LspClient::handleError(QProcess::ProcessError error) {
    emit serverLog(tr("clangd 进程错误：%1").arg(static_cast<int>(error)));
}

void LspClient::parseBuffer() {
    while (true) {
        const int headerEnd = buffer_.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }

        const QByteArray header = buffer_.left(headerEnd);
        int contentLength = 0;
        const QList<QByteArray> lines = header.split('\n');
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) {
                contentLength = trimmed.mid(strlen("content-length:")).trimmed().toInt();
            }
        }

        if (contentLength <= 0) {
            buffer_.remove(0, headerEnd + 4);
            continue;
        }

        const int totalLength = headerEnd + 4 + contentLength;
        if (buffer_.size() < totalLength) {
            return;
        }

        const QByteArray body = buffer_.mid(headerEnd + 4, contentLength);
        buffer_.remove(0, totalLength);

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit serverLog(tr("clangd 消息解析失败：%1").arg(parseError.errorString()));
            continue;
        }
        handleMessage(doc.object());
    }
}

void LspClient::handleMessage(const QJsonObject &message) {
    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        const int id = message.value("id").toInt();
        QString fullMethod = pendingRequests_.take(id);
        QString filePath;
        const int sepIndex = fullMethod.indexOf('|');
        if (sepIndex >= 0) {
            filePath = fullMethod.mid(sepIndex + 1);
            fullMethod = fullMethod.left(sepIndex);
        }

        if (fullMethod == "initialize") {
            initialized_ = true;

            const QJsonObject resultObj = message.value("result").toObject();
            const QJsonObject capsObj = resultObj.value("capabilities").toObject();
            const QJsonObject semProvider = capsObj.value("semanticTokensProvider").toObject();
            const QJsonObject legend = semProvider.value("legend").toObject();
            if (!legend.isEmpty()) {
                semanticTokenTypes_.clear();
                const QJsonArray types = legend.value("tokenTypes").toArray();
                for (const auto &typeVal : types) {
                    semanticTokenTypes_.append(typeVal.toString());
                }
            }

            sendNotification("initialized", QJsonObject());
            for (const auto &pair : pendingOpenDocs_) {
                openDocument(pair.first, pair.second);
            }
            pendingOpenDocs_.clear();
            return;
        }

        if (fullMethod == "textDocument/completion") {
            QList<LspCompletionItem> items;
            const QJsonValue resultVal = message.value("result");
            QJsonArray arr;
            if (resultVal.isObject()) {
                arr = resultVal.toObject().value("items").toArray();
            } else if (resultVal.isArray()) {
                arr = resultVal.toArray();
            }
            items.reserve(arr.size());
            for (const auto &it : arr) {
                const QJsonObject obj = it.toObject();
                LspCompletionItem item;
                item.label = obj.value("label").toString();
                item.insertText = obj.value("insertText").toString();
                item.sortText = obj.value("sortText").toString();
                item.kind = obj.value("kind").toInt();
                items.append(item);
            }
            std::sort(items.begin(), items.end(), [](const LspCompletionItem &a, const LspCompletionItem &b) {
                if (!a.sortText.isEmpty() && !b.sortText.isEmpty()) {
                    return a.sortText < b.sortText;
                }
                return a.label < b.label;
            });
            emit completionItemsReady(items);
            return;
        }

        if (fullMethod == "textDocument/documentSymbol") {
            const QJsonArray symbols = message.value("result").toArray();
            emit documentSymbolsReady(filePath, symbols);
            return;
        }

        if (fullMethod == "textDocument/foldingRange") {
            const QJsonArray ranges = message.value("result").toArray();
            emit foldingRangesReady(filePath, ranges);
            return;
        }

        if (fullMethod == "textDocument/semanticTokens/full") {
            const QJsonObject resultObj = message.value("result").toObject();
            const QJsonArray data = resultObj.value("data").toArray();
            emit semanticTokensReady(filePath, data);
            return;
        }

        if (fullMethod == "textDocument/definition") {
            const QJsonValue resultVal = message.value("result");
            QJsonArray locations;
            if (resultVal.isArray()) {
                locations = resultVal.toArray();
            } else if (resultVal.isObject()) {
                locations.append(resultVal.toObject());
            }
            emit definitionLocationsReady(filePath, locations);
            return;
        }

        if (fullMethod == "textDocument/references") {
            const QJsonArray locations = message.value("result").toArray();
            emit referencesLocationsReady(filePath, locations);
            return;
        }

        if (fullMethod == "textDocument/rename") {
            const QJsonObject edits = message.value("result").toObject();
            emit renameEditsReady(filePath, edits);
            return;
        }
        return;
    }

    if (!message.contains("method")) {
        return;
    }

    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    if (method == "textDocument/publishDiagnostics") {
        const QString uri = params.value("uri").toString();
        const QString filePath = QUrl(uri).toLocalFile();
        if (filePath != currentFilePath_ || !currentDocument_) {
            return;
        }
        QStringList messages;
        const QList<QTextEdit::ExtraSelection> selections = buildDiagnosticSelections(params.value("diagnostics").toArray(), &messages);
        emit diagnosticsUpdated(filePath, selections, messages);
        return;
    }

    if (method == "window/logMessage" || method == "window/showMessage") {
        emit serverLog(params.value("message").toString());
        return;
    }
}

void LspClient::sendMessage(const QJsonObject &message) {
    const QJsonDocument doc(message);
    const QByteArray body = doc.toJson(QJsonDocument::Compact);
    const QByteArray header = "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    process_.write(header);
    process_.write(body);
}

int LspClient::sendRequest(const QString &method, const QJsonObject &params) {
    const int id = nextId();
    pendingRequests_.insert(id, method);
    QJsonObject message;
    message.insert("jsonrpc", "2.0");
    message.insert("id", id);
    message.insert("method", method);
    message.insert("params", params);
    sendMessage(message);
    return id;
}

void LspClient::sendNotification(const QString &method, const QJsonObject &params) {
    QJsonObject message;
    message.insert("jsonrpc", "2.0");
    message.insert("method", method);
    message.insert("params", params);
    sendMessage(message);
}

QString LspClient::pathToUri(const QString &filePath) const {
    return QUrl::fromLocalFile(filePath).toString();
}

int LspClient::nextId() {
    return nextId_++;
}

int LspClient::currentVersion(const QString &filePath) const {
    return docVersions_.value(filePath, 0);
}

int LspClient::bumpVersion(const QString &filePath) {
    const int next = currentVersion(filePath) + 1;
    docVersions_.insert(filePath, next);
    return next;
}

QStringList LspClient::semanticTokenTypes() const {
    return semanticTokenTypes_;
}

QList<QTextEdit::ExtraSelection> LspClient::buildDiagnosticSelections(const QJsonArray &diagnostics,
                                                                     QStringList *messages) const {
    QList<QTextEdit::ExtraSelection> selections;
    if (!currentDocument_) {
        return selections;
    }

    QTextCharFormat fmt;
    fmt.setUnderlineColor(Qt::red);
    fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);

    for (const auto &diagVal : diagnostics) {
        QJsonObject diagObj = diagVal.toObject();
        if (messages) {
            messages->append(diagObj.value("message").toString());
        }
        const QJsonObject range = diagObj.value("range").toObject();
        const QJsonObject start = range.value("start").toObject();
        const QJsonObject end = range.value("end").toObject();

        const int startLine = start.value("line").toInt();
        const int startChar = start.value("character").toInt();
        const int endLine = end.value("line").toInt(startLine);
        const int endChar = end.value("character").toInt(startChar + 1);

        QTextBlock startBlock = currentDocument_->findBlockByNumber(startLine);
        QTextBlock endBlock = currentDocument_->findBlockByNumber(endLine);
        if (!startBlock.isValid()) {
            continue;
        }

        const int startPos = startBlock.position() + qMin(startChar, startBlock.length());
        int endPos = startPos + 1;
        if (endBlock.isValid()) {
            endPos = endBlock.position() + qMin(endChar, endBlock.length());
        }

        QTextCursor cursor(currentDocument_);
        cursor.setPosition(startPos);
        cursor.setPosition(endPos, QTextCursor::KeepAnchor);

        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format = fmt;
        selections.append(sel);
    }

    return selections;
}
