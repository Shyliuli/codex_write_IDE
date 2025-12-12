#include "BuildManager.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>

BuildManager::BuildManager(QObject *parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &BuildManager::handleReadyRead);
    connect(&process_, &QProcess::readyReadStandardError, this, &BuildManager::handleReadyRead);
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildManager::handleFinished);
}

void BuildManager::compile(const BuildConfig &config) {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }

    if (config.sources.isEmpty()) {
        emit outputReady(tr("没有需要编译的源文件。"));
        return;
    }

    QStringList absSources;
    for (const QString &src : config.sources) {
        QFileInfo info(src);
        if (!info.exists()) {
            emit outputReady(tr("源文件不存在：%1\n").arg(src));
            return;
        }
        absSources.append(info.absoluteFilePath());
    }

    const bool isStaticLib = config.outputPath.endsWith(".a");
    if (isStaticLib) {
        QStringList objPaths;
        for (const QString &absSrc : absSources) {
            QFileInfo info(absSrc);
            const QString objPath = info.absolutePath() + QDir::separator() + info.completeBaseName() + ".o";
            objPaths.append(objPath);

            QStringList args;
            args << ("-std=" + config.cxxStandard) << "-Wall";
            for (const QString &inc : config.includeDirs) {
                args << ("-I" + QFileInfo(inc).absoluteFilePath());
            }
            args << config.extraFlags;
            args << "-c" << absSrc << "-o" << objPath;

            emit outputReady(tr("%1 %2\n").arg(config.compiler, args.join(' ')));
            QProcess proc;
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.setWorkingDirectory(config.workingDirectory.isEmpty() ? info.absolutePath() : config.workingDirectory);
            proc.start(config.compiler, args);
            proc.waitForFinished(-1);
            const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
            if (!out.isEmpty()) {
                emit outputReady(out);
            }
            if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
                emit buildFinished(proc.exitCode(), proc.exitStatus());
                return;
            }
        }

        lastBinaryPath_ = config.outputPath;
        QStringList arArgs = {"rcs", lastBinaryPath_};
        arArgs += objPaths;
        emit outputReady(tr("ar %1\n").arg(arArgs.join(' ')));
        QProcess arProc;
        arProc.setProcessChannelMode(QProcess::MergedChannels);
        arProc.setWorkingDirectory(config.workingDirectory);
        arProc.start("ar", arArgs);
        arProc.waitForFinished(-1);
        const QString arOut = QString::fromLocal8Bit(arProc.readAllStandardOutput());
        if (!arOut.isEmpty()) {
            emit outputReady(arOut);
        }
        emit buildFinished(arProc.exitCode(), arProc.exitStatus());
        return;
    }

    lastBinaryPath_ = config.outputPath;
    if (lastBinaryPath_.isEmpty()) {
        QFileInfo first(absSources.first());
        lastBinaryPath_ = first.absolutePath() + QDir::separator() + first.completeBaseName();
    }

    QStringList args;
    args << ("-std=" + config.cxxStandard) << "-Wall";
    for (const QString &inc : config.includeDirs) {
        args << ("-I" + QFileInfo(inc).absoluteFilePath());
    }
    args << config.extraFlags;
    args << absSources;
    args << "-o" << lastBinaryPath_;

    emit outputReady(tr("%1 %2\n").arg(config.compiler, args.join(' ')));

    process_.setProgram(config.compiler);
    process_.setArguments(args);
    if (!config.workingDirectory.isEmpty()) {
        process_.setWorkingDirectory(config.workingDirectory);
    } else {
        process_.setWorkingDirectory(QFileInfo(absSources.first()).absolutePath());
    }
    process_.start();
}

void BuildManager::runLastBinary(const QStringList &args, const QString &workingDirectory) {
    if (lastBinaryPath_.isEmpty()) {
        emit outputReady(tr("尚未编译过任何文件。"));
        return;
    }

    QFileInfo binInfo(lastBinaryPath_);
    if (!binInfo.exists()) {
        emit outputReady(tr("可执行文件不存在，请先编译。"));
        return;
    }

    auto *runProcess = new QProcess(this);
    runProcess->setProcessChannelMode(QProcess::MergedChannels);
    runProcess->setProgram(lastBinaryPath_);
    runProcess->setArguments(args);
    runProcess->setWorkingDirectory(workingDirectory.isEmpty() ? binInfo.absolutePath() : workingDirectory);

    connect(runProcess, &QProcess::readyReadStandardOutput, this, [this, runProcess]() {
        emit outputReady(QString::fromLocal8Bit(runProcess->readAllStandardOutput()));
    });
    connect(runProcess, &QProcess::readyReadStandardError, this, [this, runProcess]() {
        emit outputReady(QString::fromLocal8Bit(runProcess->readAllStandardError()));
    });
    connect(runProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            runProcess, &QObject::deleteLater);

    emit outputReady(tr("执行：%1 %2\n").arg(lastBinaryPath_, args.join(' ')));
    runProcess->start();
}

bool BuildManager::generateMakefile(const BuildConfig &config, const QString &makefilePath) {
    if (config.sources.isEmpty() || makefilePath.isEmpty()) {
        return false;
    }

    QFile makefile(makefilePath);
    if (!makefile.open(QFile::WriteOnly | QFile::Text)) {
        return false;
    }

    QFileInfo makeInfo(makefilePath);
    const QString rootDir = makeInfo.absolutePath();
    QDir root(rootDir);

    QStringList srcRel;
    QStringList objRel;
    for (const QString &src : config.sources) {
        const QString abs = QFileInfo(src).absoluteFilePath();
        const QString rel = root.relativeFilePath(abs);
        srcRel.append(rel);
        QString obj = rel;
        int dot = obj.lastIndexOf('.');
        if (dot >= 0) {
            obj = obj.left(dot) + ".o";
        } else {
            obj += ".o";
        }
        objRel.append(obj);
    }

    QTextStream out(&makefile);
    out << "CXX=" << config.compiler << "\n";
    out << "CXXFLAGS=-std=" << config.cxxStandard << " -Wall";
    for (const QString &inc : config.includeDirs) {
        out << " -I" << root.relativeFilePath(QFileInfo(inc).absoluteFilePath());
    }
    for (const QString &flag : config.extraFlags) {
        out << " " << flag;
    }
    out << "\n";

    QFileInfo outInfo(config.outputPath);
    const QString targetName = outInfo.fileName().isEmpty() ? "a.out" : outInfo.fileName();
    out << "TARGET=" << targetName << "\n";
    out << "SRCS=" << srcRel.join(' ') << "\n";
    out << "OBJS=" << objRel.join(' ') << "\n\n";

    out << "all: $(TARGET)\n\n";
    out << "$(TARGET): $(OBJS)\n";
    if (targetName.endsWith(".a")) {
        out << "\tar rcs $@ $^\n\n";
    } else {
        out << "\t$(CXX) $(CXXFLAGS) -o $@ $^\n\n";
    }
    out << "%.o: %.cpp\n";
    out << "\t$(CXX) $(CXXFLAGS) -c $< -o $@\n\n";
    out << "clean:\n";
    out << "\trm -f $(TARGET) $(OBJS)\n";

    emit outputReady(tr("已写入 Makefile：%1\n").arg(makefilePath));
    return true;
}

QString BuildManager::lastBinaryPath() const {
    return lastBinaryPath_;
}

void BuildManager::handleReadyRead() {
    const QByteArray data = process_.readAllStandardOutput();
    if (!data.isEmpty()) {
        emit outputReady(QString::fromLocal8Bit(data));
    }
}

void BuildManager::handleFinished(int exitCode, QProcess::ExitStatus status) {
    emit buildFinished(exitCode, status);
}
