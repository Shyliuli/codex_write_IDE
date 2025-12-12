#pragma once

#include <QObject>
#include <QProcess>

class BuildManager : public QObject {
    Q_OBJECT

public:
    explicit BuildManager(QObject *parent = nullptr);

    struct BuildConfig {
        QString compiler = QStringLiteral("g++");
        QString cxxStandard = QStringLiteral("c++20");
        QStringList sources;
        QStringList includeDirs;
        QStringList extraFlags;
        QString outputPath;
        QString workingDirectory;
    };

    void compile(const BuildConfig &config);
    void runLastBinary(const QStringList &args = {}, const QString &workingDirectory = {});
    bool generateMakefile(const BuildConfig &config, const QString &makefilePath);

    QString lastBinaryPath() const;

signals:
    void outputReady(const QString &text);
    void buildFinished(int exitCode, QProcess::ExitStatus status);

private slots:
    void handleReadyRead();
    void handleFinished(int exitCode, QProcess::ExitStatus status);

private:
    QString lastBinaryPath_;
    QProcess process_;
};
