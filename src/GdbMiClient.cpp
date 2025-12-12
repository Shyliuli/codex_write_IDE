#include "GdbMiClient.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
void skipDelimiters(const QString &text, int &pos) {
    while (pos < text.size() && text.at(pos).isSpace()) {
        ++pos;
    }
}
}

GdbMiClient::GdbMiClient(QObject *parent) : QObject(parent) {
    qRegisterMetaType<QList<GdbBreakpoint>>("QList<GdbBreakpoint>");
    qRegisterMetaType<QList<GdbStackFrame>>("QList<GdbStackFrame>");
    qRegisterMetaType<QList<GdbVariable>>("QList<GdbVariable>");
    qRegisterMetaType<QList<GdbThread>>("QList<GdbThread>");
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &GdbMiClient::handleReadyRead);
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GdbMiClient::handleFinished);
}

void GdbMiClient::start(const QString &binaryPath, const QString &workingDir) {
    stop();

    buffer_.clear();
    pendingTags_.clear();
    breakpoints_.clear();
    nextToken_ = 1;

    process_.setProgram(QStringLiteral("gdb"));
    process_.setArguments({"--interpreter=mi2", "-q", binaryPath});
    if (!workingDir.isEmpty()) {
        process_.setWorkingDirectory(workingDir);
    }
    process_.start();
    if (!process_.waitForStarted(3000)) {
        emit consoleOutput(tr("无法启动 gdb。\n"));
        return;
    }

    sendMiCommand("-gdb-set target-async on", "init");
    refreshBreakpoints();
}

void GdbMiClient::stop() {
    if (process_.state() == QProcess::NotRunning) {
        return;
    }
    process_.write("-gdb-exit\n");
    process_.waitForFinished(1000);
    process_.kill();
    process_.waitForFinished(1000);
}

bool GdbMiClient::isRunning() const {
    return process_.state() != QProcess::NotRunning;
}

void GdbMiClient::continueExec() {
    sendMiCommand("-exec-continue", "continue");
}

void GdbMiClient::runExec() {
    sendMiCommand("-exec-run", "run");
}

void GdbMiClient::stepOver() {
    sendMiCommand("-exec-next", "next");
}

void GdbMiClient::stepInto() {
    sendMiCommand("-exec-step", "step");
}

void GdbMiClient::stepOut() {
    sendMiCommand("-exec-finish", "finish");
}

void GdbMiClient::insertBreakpoint(const QString &file, int line) {
    const QString cmd = QStringLiteral("-break-insert %1:%2").arg(file).arg(line);
    sendMiCommand(cmd, "break-insert");
}

void GdbMiClient::deleteBreakpoint(int number) {
    const QString cmd = QStringLiteral("-break-delete %1").arg(number);
    sendMiCommand(cmd, "break-delete");
}

void GdbMiClient::refreshBreakpoints() {
    sendMiCommand("-break-list", "break-list");
}

void GdbMiClient::refreshStack() {
    sendMiCommand("-stack-list-frames", "stack-list");
}

void GdbMiClient::refreshLocals() {
    sendMiCommand("-stack-list-variables --simple-values", "locals-list");
}

void GdbMiClient::refreshThreads() {
    sendMiCommand("-thread-info", "threads-list");
}

void GdbMiClient::selectThread(int id) {
    sendMiCommand(QStringLiteral("-thread-select %1").arg(id), "thread-select");
}

void GdbMiClient::selectFrame(int level) {
    sendMiCommand(QStringLiteral("-stack-select-frame %1").arg(level), "frame-select");
}

void GdbMiClient::setBreakpointEnabled(int number, bool enabled) {
    sendMiCommand(QStringLiteral("-break-%1 %2").arg(enabled ? "enable" : "disable").arg(number), "break-toggle");
}

void GdbMiClient::setBreakpointCondition(int number, const QString &condition) {
    if (condition.trimmed().isEmpty()) {
        sendMiCommand(QStringLiteral("-break-condition %1").arg(number), "break-cond");
    } else {
        QString escaped = condition;
        escaped.replace('"', "\\\"");
        sendMiCommand(QStringLiteral("-break-condition %1 \"%2\"").arg(number).arg(escaped), "break-cond");
    }
}

void GdbMiClient::setBreakpointIgnoreCount(int number, int count) {
    sendMiCommand(QStringLiteral("-break-after %1 %2").arg(number).arg(count), "break-after");
}

void GdbMiClient::setBreakpointLogMessage(int number, const QString &message) {
    QString escaped = message;
    escaped.replace('"', "\\\"");
    const QString cmd = QStringLiteral("commands %1\nsilent\nprintf \"%2\\n\"\ncontinue\nend").arg(number).arg(escaped);
    sendConsoleCommand(cmd);
    refreshBreakpoints();
}

void GdbMiClient::evaluateExpression(const QString &expr) {
    QString escaped = expr;
    escaped.replace('"', "\\\"");
    sendMiCommand(QStringLiteral("-data-evaluate-expression \"%1\"").arg(escaped), "eval|" + expr);
}

void GdbMiClient::sendConsoleCommand(const QString &cmd) {
    if (cmd.trimmed().isEmpty()) {
        return;
    }
    QString escaped = cmd;
    escaped.replace('"', "\\\"");
    sendMiCommand(QStringLiteral("-interpreter-exec console \"%1\"").arg(escaped), "console");
}

QList<GdbBreakpoint> GdbMiClient::breakpoints() const {
    return breakpoints_;
}

void GdbMiClient::handleReadyRead() {
    buffer_.append(process_.readAllStandardOutput());
    int idx;
    while ((idx = buffer_.indexOf('\n')) >= 0) {
        const QByteArray lineData = buffer_.left(idx);
        buffer_.remove(0, idx + 1);
        const QString line = QString::fromLocal8Bit(lineData).trimmed();
        if (!line.isEmpty()) {
            parseLine(line);
        }
    }
}

void GdbMiClient::handleFinished(int code, QProcess::ExitStatus) {
    emit exited(code);
}

void GdbMiClient::sendMiCommand(const QString &command, const QString &tag) {
    if (!isRunning()) {
        return;
    }
    const int token = nextToken_++;
    pendingTags_.insert(token, tag);
    const QString line = QString::number(token) + command + "\n";
    process_.write(line.toLocal8Bit());
}

void GdbMiClient::parseLine(const QString &line) {
    if (line.startsWith('*')) {
        if (line.startsWith("*stopped")) {
            QRegularExpression re("file=\"([^\"]+)\".*line=\"(\\d+)\"");
            QRegularExpressionMatch m = re.match(line);
            if (m.hasMatch()) {
                emit stopped(m.captured(1), m.captured(2).toInt());
                refreshStack();
                refreshLocals();
                refreshThreads();
            }
        }
        emit consoleOutput(decodeMiString(line) + "\n");
        return;
    }

    if (line.startsWith('~') || line.startsWith('@') || line.startsWith('&')) {
        emit consoleOutput(decodeMiString(line) + "\n");
        return;
    }

    QRegularExpression tokenRe(QStringLiteral("^(\\d+)([\\^\\*].*)$"));
    QRegularExpressionMatch match = tokenRe.match(line);
    if (match.hasMatch()) {
        const int token = match.captured(1).toInt();
        const QString rest = match.captured(2);
        const QString tag = pendingTags_.take(token);
        handleResultForTag(tag, rest);
        return;
    }

    emit consoleOutput(decodeMiString(line) + "\n");
}

void GdbMiClient::handleResultForTag(const QString &tag, const QString &line) {
    if (tag == "break-list") {
        parseBreakpointList(line);
    } else if (tag == "stack-list") {
        parseStackList(line);
    } else if (tag == "locals-list") {
        parseLocalsList(line);
    } else if (tag == "threads-list") {
        parseThreadsList(line);
    } else if (tag.startsWith("eval|")) {
        QString payload = line;
        const int comma = payload.indexOf(',');
        if (comma >= 0) {
            payload = payload.mid(comma + 1);
        } else {
            payload.clear();
        }
        const QVariantMap res = parseMiResults(payload);
        const QString value = res.value("value").toString();
        emit expressionEvaluated(tag.mid(5), value);
    } else if (tag == "thread-select" || tag == "frame-select") {
        refreshStack();
        refreshLocals();
        refreshThreads();
    } else if (tag.startsWith("break")) {
        refreshBreakpoints();
    }
    emit consoleOutput(decodeMiString(line) + "\n");
}

void GdbMiClient::parseBreakpointList(const QString &line) {
    breakpoints_.clear();

    QString payload = line;
    const int comma = payload.indexOf(',');
    if (comma >= 0) {
        payload = payload.mid(comma + 1);
    } else {
        payload.clear();
    }

    const QVariantMap res = parseMiResults(payload);

    QVariantList bkptEntries;
    if (res.contains("BreakpointTable")) {
        const QVariantMap table = res.value("BreakpointTable").toMap();
        const QVariant bodyVar = table.value("body");
        bkptEntries = bodyVar.toList();
    } else if (res.contains("bkpt")) {
        const QVariant bkptVar = res.value("bkpt");
        if (bkptVar.typeId() == QMetaType::QVariantList) {
            bkptEntries = bkptVar.toList();
        } else {
            bkptEntries = {bkptVar};
        }
    }

    auto processBkpt = [this](const QVariantMap &map) {
        GdbBreakpoint bp;
        bp.number = map.value("number").toString().toInt();
        bp.line = map.value("line").toString().toInt();
        QString file = map.value("fullname").toString();
        if (file.isEmpty()) {
            file = map.value("file").toString();
        }
        if (!file.isEmpty() && !QFileInfo(file).isAbsolute()) {
            file = QDir(process_.workingDirectory()).absoluteFilePath(file);
        }
        bp.file = file;
        const QString enabledStr = map.value("enabled").toString();
        bp.enabled = enabledStr.isEmpty() || enabledStr == "y" || enabledStr == "1" || enabledStr == "true";
        bp.condition = map.value("cond").toString();
        bp.ignoreCount = map.value("ignore").toString().toInt();
        if (bp.number > 0) {
            breakpoints_.append(bp);
        }
    };

    for (const QVariant &entry : bkptEntries) {
        const QVariantMap entryMap = entry.toMap();
        if (entryMap.contains("bkpt")) {
            processBkpt(entryMap.value("bkpt").toMap());
        } else {
            processBkpt(entryMap);
        }
    }

    emit breakpointsUpdated(breakpoints_);
}

void GdbMiClient::parseStackList(const QString &line) {
    QList<GdbStackFrame> frames;
    QString payload = line;
    const int comma = payload.indexOf(',');
    if (comma >= 0) {
        payload = payload.mid(comma + 1);
    } else {
        payload.clear();
    }

    const QVariantMap res = parseMiResults(payload);
    const QVariantList stackList = res.value("stack").toList();
    for (const QVariant &frameVar : stackList) {
        QVariantMap frameMap = frameVar.toMap();
        if (frameMap.contains("frame")) {
            frameMap = frameMap.value("frame").toMap();
        }
        GdbStackFrame f;
        f.level = frameMap.value("level").toString().toInt();
        f.func = frameMap.value("func").toString();
        QString file = frameMap.value("fullname").toString();
        if (file.isEmpty()) {
            file = frameMap.value("file").toString();
        }
        if (!file.isEmpty() && !QFileInfo(file).isAbsolute()) {
            file = QDir(process_.workingDirectory()).absoluteFilePath(file);
        }
        f.file = file;
        f.line = frameMap.value("line").toString().toInt();
        frames.append(f);
    }
    emit stackUpdated(frames);
}

void GdbMiClient::parseLocalsList(const QString &line) {
    QList<GdbVariable> vars;
    QString payload = line;
    const int comma = payload.indexOf(',');
    if (comma >= 0) {
        payload = payload.mid(comma + 1);
    } else {
        payload.clear();
    }

    const QVariantMap res = parseMiResults(payload);
    const QVariantList list = res.value("variables").toList();
    for (const QVariant &vVar : list) {
        const QVariantMap vMap = vVar.toMap();
        GdbVariable v;
        v.name = vMap.value("name").toString();
        v.value = vMap.value("value").toString();
        vars.append(v);
    }
    emit localsUpdated(vars);
}

void GdbMiClient::parseThreadsList(const QString &line) {
    QList<GdbThread> threads;
    QString payload = line;
    const int comma = payload.indexOf(',');
    if (comma >= 0) {
        payload = payload.mid(comma + 1);
    } else {
        payload.clear();
    }

    const QVariantMap res = parseMiResults(payload);
    const QString currentId = res.value("current-thread-id").toString();
    const QVariantList list = res.value("threads").toList();
    for (const QVariant &tVar : list) {
        const QVariantMap tMap = tVar.toMap();
        GdbThread t;
        t.id = tMap.value("id").toString().toInt();
        t.state = tMap.value("state").toString();
        t.current = currentId.toInt() == t.id;
        QVariantMap frameMap = tMap.value("frame").toMap();
        if (!frameMap.isEmpty()) {
            QString file = frameMap.value("fullname").toString();
            if (file.isEmpty()) {
                file = frameMap.value("file").toString();
            }
            if (!file.isEmpty() && !QFileInfo(file).isAbsolute()) {
                file = QDir(process_.workingDirectory()).absoluteFilePath(file);
            }
            t.file = file;
            t.line = frameMap.value("line").toString().toInt();
        }
        threads.append(t);
    }
    emit threadsUpdated(threads);
}

QVariant GdbMiClient::parseMiValue(const QString &text, int &pos) const {
    skipDelimiters(text, pos);
    if (pos >= text.size()) {
        return {};
    }

    const QChar c = text.at(pos);
    if (c == '"') {
        ++pos;
        QString out;
        while (pos < text.size()) {
            const QChar ch = text.at(pos++);
            if (ch == '"') {
                break;
            }
            if (ch == '\\' && pos < text.size()) {
                const QChar esc = text.at(pos++);
                if (esc == 'n') out.append('\n');
                else if (esc == 't') out.append('\t');
                else out.append(esc);
            } else {
                out.append(ch);
            }
        }
        return out;
    }
    if (c == '{') {
        ++pos;
        return parseMiTuple(text, pos);
    }
    if (c == '[') {
        ++pos;
        return parseMiList(text, pos);
    }

    const QString word = parseMiWord(text, pos);
    return word;
}

QVariantMap GdbMiClient::parseMiTuple(const QString &text, int &pos) const {
    QVariantMap map;
    skipDelimiters(text, pos);
    while (pos < text.size() && text.at(pos) != '}') {
        const QString name = parseMiWord(text, pos);
        QVariant value;
        if (pos < text.size() && text.at(pos) == '=') {
            ++pos;
            value = parseMiValue(text, pos);
        }
        if (!name.isEmpty()) {
            map.insert(name, value);
        }
        skipDelimiters(text, pos);
        if (pos < text.size() && text.at(pos) == ',') {
            ++pos;
        }
        skipDelimiters(text, pos);
    }
    if (pos < text.size() && text.at(pos) == '}') {
        ++pos;
    }
    return map;
}

QVariantList GdbMiClient::parseMiList(const QString &text, int &pos) const {
    QVariantList list;
    skipDelimiters(text, pos);
    while (pos < text.size() && text.at(pos) != ']') {
        skipDelimiters(text, pos);
        const QChar startChar = pos < text.size() ? text.at(pos) : QChar();
        if (startChar == '{' || startChar == '[' || startChar == '"') {
            list.append(parseMiValue(text, pos));
        } else {
            int savePos = pos;
            const QString name = parseMiWord(text, pos);
            skipDelimiters(text, pos);
            if (pos < text.size() && text.at(pos) == '=') {
                ++pos;
                const QVariant value = parseMiValue(text, pos);
                QVariantMap pair;
                pair.insert(name, value);
                list.append(pair);
            } else {
                pos = savePos;
                list.append(parseMiValue(text, pos));
            }
        }
        skipDelimiters(text, pos);
        if (pos < text.size() && text.at(pos) == ',') {
            ++pos;
        }
        skipDelimiters(text, pos);
    }
    if (pos < text.size() && text.at(pos) == ']') {
        ++pos;
    }
    return list;
}

QString GdbMiClient::parseMiWord(const QString &text, int &pos) const {
    skipDelimiters(text, pos);
    const int start = pos;
    while (pos < text.size()) {
        const QChar c = text.at(pos);
        if (c == '=' || c == ',' || c == '}' || c == ']' ) {
            break;
        }
        ++pos;
    }
    return text.mid(start, pos - start);
}

QVariantMap GdbMiClient::parseMiResults(const QString &text) const {
    QVariantMap map;
    int pos = 0;
    skipDelimiters(text, pos);
    while (pos < text.size()) {
        const QString name = parseMiWord(text, pos);
        QVariant value;
        if (pos < text.size() && text.at(pos) == '=') {
            ++pos;
            value = parseMiValue(text, pos);
        }
        if (!name.isEmpty()) {
            if (value.typeId() == QMetaType::QVariantMap) {
                map.insert(name, value);
            } else if (value.typeId() == QMetaType::QVariantList) {
                map.insert(name, value);
            } else {
                map.insert(name, value);
            }
        }
        skipDelimiters(text, pos);
        if (pos < text.size() && text.at(pos) == ',') {
            ++pos;
        }
        skipDelimiters(text, pos);
    }
    return map;
}

QString GdbMiClient::decodeMiString(const QString &mi) const {
    if (mi.size() >= 2 && (mi.startsWith('~') || mi.startsWith('@') || mi.startsWith('&'))) {
        QString inner = mi.mid(2);
        if (inner.endsWith('"')) {
            inner.chop(1);
        }
        inner.replace("\\n", "\n");
        inner.replace("\\t", "\t");
        inner.replace("\\\"", "\"");
        return inner;
    }
    return mi;
}
