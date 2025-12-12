#pragma once

#include <QObject>

#include <QStringList>
#include <QVector>

struct ProjectGroup {
    QString name;
    QStringList files;
};

struct BuildProfile {
    QString outputName;
    QStringList flags;
};

class ProjectManager : public QObject {
    Q_OBJECT

public:
    explicit ProjectManager(QObject *parent = nullptr);

    bool createNewProject(const QString &rootDir, const QString &name);
    bool openProject(const QString &projectFilePath);
    bool saveProject();
    void closeProject();

    bool hasProject() const;
    QString rootDir() const;
    QString projectName() const;
    QString projectFilePath() const;

    QString compiler() const;
    QString cxxStandard() const;
    QString outputName() const;
    QStringList extraFlags() const;
    QString activeBuildProfile() const;
    QString activeOutputName() const;
    QStringList activeExtraFlags() const;

    BuildProfile debugProfile() const;
    BuildProfile releaseProfile() const;
    void setDebugProfile(const BuildProfile &profile);
    void setReleaseProfile(const BuildProfile &profile);
    void setActiveBuildProfile(const QString &profile);
    QStringList runArgs() const;
    QString runWorkingDir() const;

    QStringList sources() const;
    QStringList sourceFilesAbsolute() const;

    QStringList includeDirs() const;
    QStringList includeDirsAbsolute() const;

    QVector<ProjectGroup> groups() const;
    void setGroups(const QVector<ProjectGroup> &groups);
    bool addGroup(const QString &name);
    bool removeGroup(const QString &name);
    bool addFileToGroup(const QString &groupName, const QString &filePath);

    bool addSourceFile(const QString &filePath);
    bool addIncludeDir(const QString &dirPath);
    void setIncludeDirs(const QStringList &dirs);

    void setCompiler(const QString &compiler);
    void setCxxStandard(const QString &standard);
    void setOutputName(const QString &outputName);
    void setExtraFlags(const QStringList &flags);
    void setRunArgs(const QStringList &args);
    void setRunWorkingDir(const QString &dir);

    bool generateCompileCommands(QString *errorMessage = nullptr) const;
    bool downloadRusticLibrary(QString *errorMessage = nullptr);

signals:
    void projectLoaded();
    void projectClosed();
    void projectChanged();

private:
    QString normalizeToProjectRelative(const QString &path) const;
    QString resolveToAbsolute(const QString &path) const;
    bool loadFromJson(const QByteArray &data, QString *errorMessage);
    QByteArray toJson() const;
    QString quoteIfNeeded(const QString &value) const;

    void ensureDefaultProfiles();

    QString rootDir_;
    QString projectFilePath_;
    QString projectName_;
    QString outputName_;
    QString compiler_ = QStringLiteral("g++");
    QString cxxStandard_ = QStringLiteral("c++20");
    QStringList sources_;
    QStringList includeDirs_;
    QStringList extraFlags_;

    QString activeProfile_ = QStringLiteral("Debug");
    BuildProfile debugProfile_;
    BuildProfile releaseProfile_;

    QVector<ProjectGroup> groups_;
    QStringList runArgs_;
    QString runWorkingDir_;
};
