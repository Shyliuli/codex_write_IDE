#pragma once

#include <QObject>
#include <QProcess>
#include <QHash>
#include <QVariant>

#include <QList>

struct GdbBreakpoint {
    int number = 0;
    QString file;
    int line = 0; // 1-based
    bool enabled = true;
    QString condition;
    int ignoreCount = 0;
};
Q_DECLARE_METATYPE(GdbBreakpoint)

struct GdbStackFrame {
    int level = 0;
    QString func;
    QString file;
    int line = 0; // 1-based
};
Q_DECLARE_METATYPE(GdbStackFrame)

struct GdbVariable {
    QString name;
    QString value;
};
Q_DECLARE_METATYPE(GdbVariable)

struct GdbThread {
    int id = 0;
    QString state;
    QString file;
    int line = 0;
    bool current = false;
};
Q_DECLARE_METATYPE(GdbThread)

class GdbMiClient : public QObject {
    Q_OBJECT

public:
    explicit GdbMiClient(QObject *parent = nullptr);

    void start(const QString &binaryPath, const QString &workingDir);
    void stop();
    bool isRunning() const;

    void continueExec();
    void runExec();
    void stepOver();
    void stepInto();
    void stepOut();

    void insertBreakpoint(const QString &file, int line); // line 1-based
    void deleteBreakpoint(int number);
    void refreshBreakpoints();
    void refreshStack();
    void refreshLocals();
    void refreshThreads();

    void selectThread(int id);
    void selectFrame(int level);

    void setBreakpointEnabled(int number, bool enabled);
    void setBreakpointCondition(int number, const QString &condition);
    void setBreakpointIgnoreCount(int number, int count);
    void setBreakpointLogMessage(int number, const QString &message);

    void evaluateExpression(const QString &expr);

    void sendConsoleCommand(const QString &cmd);

    QList<GdbBreakpoint> breakpoints() const;

signals:
    void consoleOutput(const QString &text);
    void stopped(const QString &file, int line);
    void breakpointsUpdated(const QList<GdbBreakpoint> &bps);
    void stackUpdated(const QList<GdbStackFrame> &frames);
    void localsUpdated(const QList<GdbVariable> &vars);
    void threadsUpdated(const QList<GdbThread> &threads);
    void expressionEvaluated(const QString &expr, const QString &value);
    void exited(int code);

private slots:
    void handleReadyRead();
    void handleFinished(int code, QProcess::ExitStatus status);

private:
    void sendMiCommand(const QString &command, const QString &tag);
    void parseLine(const QString &line);
    void handleResultForTag(const QString &tag, const QString &line);
    void parseBreakpointList(const QString &line);
    void parseStackList(const QString &line);
    void parseLocalsList(const QString &line);
    void parseThreadsList(const QString &line);
    QString decodeMiString(const QString &mi) const;

    QVariant parseMiValue(const QString &text, int &pos) const;
    QVariantMap parseMiTuple(const QString &text, int &pos) const;
    QVariantList parseMiList(const QString &text, int &pos) const;
    QString parseMiWord(const QString &text, int &pos) const;
    QVariantMap parseMiResults(const QString &text) const;

    QProcess process_;
    QByteArray buffer_;
    int nextToken_ = 1;
    QHash<int, QString> pendingTags_;
    QList<GdbBreakpoint> breakpoints_;
};
