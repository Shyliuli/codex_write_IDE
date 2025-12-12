#include "ProjectManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>

ProjectManager::ProjectManager(QObject *parent) : QObject(parent) {}

bool ProjectManager::hasProject() const {
    return !projectFilePath_.isEmpty();
}

QString ProjectManager::rootDir() const {
    return rootDir_;
}

QString ProjectManager::projectName() const {
    return projectName_;
}

QString ProjectManager::projectFilePath() const {
    return projectFilePath_;
}

QString ProjectManager::compiler() const {
    return compiler_;
}

QString ProjectManager::cxxStandard() const {
    return cxxStandard_;
}

QString ProjectManager::outputName() const {
    return outputName_;
}

QStringList ProjectManager::extraFlags() const {
    return extraFlags_;
}

QString ProjectManager::activeBuildProfile() const {
    return activeProfile_;
}

QString ProjectManager::activeOutputName() const {
    if (activeProfile_.compare("Release", Qt::CaseInsensitive) == 0) {
        return releaseProfile_.outputName.isEmpty() ? outputName_ : releaseProfile_.outputName;
    }
    return debugProfile_.outputName.isEmpty() ? (outputName_ + "_debug") : debugProfile_.outputName;
}

QStringList ProjectManager::activeExtraFlags() const {
    QStringList flags = extraFlags_;
    if (activeProfile_.compare("Release", Qt::CaseInsensitive) == 0) {
        flags += releaseProfile_.flags;
    } else {
        flags += debugProfile_.flags;
    }
    return flags;
}

BuildProfile ProjectManager::debugProfile() const {
    return debugProfile_;
}

BuildProfile ProjectManager::releaseProfile() const {
    return releaseProfile_;
}

void ProjectManager::setDebugProfile(const BuildProfile &profile) {
    debugProfile_ = profile;
    saveProject();
}

void ProjectManager::setReleaseProfile(const BuildProfile &profile) {
    releaseProfile_ = profile;
    outputName_ = profile.outputName;
    saveProject();
}

void ProjectManager::setActiveBuildProfile(const QString &profile) {
    if (profile.isEmpty()) {
        return;
    }
    activeProfile_ = profile;
    saveProject();
}

QStringList ProjectManager::runArgs() const {
    return runArgs_;
}

QString ProjectManager::runWorkingDir() const {
    return runWorkingDir_;
}

QStringList ProjectManager::sources() const {
    return sources_;
}

QStringList ProjectManager::sourceFilesAbsolute() const {
    QStringList abs;
    abs.reserve(sources_.size());
    for (const QString &src : sources_) {
        abs.append(resolveToAbsolute(src));
    }
    return abs;
}

QStringList ProjectManager::includeDirs() const {
    return includeDirs_;
}

QStringList ProjectManager::includeDirsAbsolute() const {
    QStringList abs;
    abs.reserve(includeDirs_.size());
    for (const QString &dir : includeDirs_) {
        abs.append(resolveToAbsolute(dir));
    }
    return abs;
}

QVector<ProjectGroup> ProjectManager::groups() const {
    return groups_;
}

void ProjectManager::setGroups(const QVector<ProjectGroup> &groups) {
    groups_ = groups;
    saveProject();
}

bool ProjectManager::addGroup(const QString &name) {
    if (!hasProject() || name.trimmed().isEmpty()) {
        return false;
    }
    for (const auto &g : groups_) {
        if (g.name == name) {
            return false;
        }
    }
    groups_.append(ProjectGroup{name.trimmed(), {}});
    saveProject();
    return true;
}

bool ProjectManager::removeGroup(const QString &name) {
    for (int i = 0; i < groups_.size(); ++i) {
        if (groups_[i].name == name) {
            groups_.removeAt(i);
            saveProject();
            return true;
        }
    }
    return false;
}

bool ProjectManager::addFileToGroup(const QString &groupName, const QString &filePath) {
    if (!hasProject() || groupName.isEmpty() || filePath.isEmpty()) {
        return false;
    }
    const QString rel = normalizeToProjectRelative(filePath);
    for (auto &g : groups_) {
        if (g.name == groupName) {
            if (!g.files.contains(rel)) {
                g.files.append(rel);
            }
            if (rel.endsWith(".cpp") || rel.endsWith(".cc") || rel.endsWith(".cxx")) {
                addSourceFile(filePath);
            } else {
                saveProject();
            }
            return true;
        }
    }
    return false;
}

bool ProjectManager::createNewProject(const QString &rootDir, const QString &name) {
    QDir dir(rootDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        return false;
    }

    rootDir_ = dir.absolutePath();
    projectName_ = name;
    outputName_ = name;
    projectFilePath_ = dir.filePath(name + ".rcppide.json");
    sources_.clear();
    includeDirs_.clear();
    extraFlags_.clear();
    groups_.clear();
    runArgs_.clear();
    runWorkingDir_.clear();
    includeDirs_.append(".");

    ensureDefaultProfiles();

    if (!saveProject()) {
        return false;
    }
    emit projectLoaded();
    emit projectChanged();
    return true;
}

bool ProjectManager::openProject(const QString &projectFilePath) {
    QFile file(projectFilePath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return false;
    }
    const QByteArray data = file.readAll();
    QString error;
    if (!loadFromJson(data, &error)) {
        return false;
    }

    projectFilePath_ = QFileInfo(projectFilePath).absoluteFilePath();
    rootDir_ = QFileInfo(projectFilePath_).absolutePath();

    emit projectLoaded();
    emit projectChanged();
    return true;
}

bool ProjectManager::saveProject() {
    if (projectFilePath_.isEmpty()) {
        return false;
    }

    QFile file(projectFilePath_);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        return false;
    }
    file.write(toJson());
    file.close();

    generateCompileCommands();
    emit projectChanged();
    return true;
}

void ProjectManager::closeProject() {
    rootDir_.clear();
    projectFilePath_.clear();
    projectName_.clear();
    outputName_.clear();
    sources_.clear();
    includeDirs_.clear();
    extraFlags_.clear();
    groups_.clear();
    activeProfile_ = QStringLiteral("Debug");
    debugProfile_ = BuildProfile{};
    releaseProfile_ = BuildProfile{};
    runArgs_.clear();
    runWorkingDir_.clear();
    compiler_ = QStringLiteral("g++");
    cxxStandard_ = QStringLiteral("c++20");

    emit projectClosed();
    emit projectChanged();
}

bool ProjectManager::addSourceFile(const QString &filePath) {
    if (!hasProject()) {
        return false;
    }
    const QString normalized = normalizeToProjectRelative(filePath);
    if (sources_.contains(normalized)) {
        return true;
    }
    sources_.append(normalized);
    return saveProject();
}

bool ProjectManager::addIncludeDir(const QString &dirPath) {
    if (!hasProject()) {
        return false;
    }
    const QString normalized = normalizeToProjectRelative(dirPath);
    if (includeDirs_.contains(normalized)) {
        return true;
    }
    includeDirs_.append(normalized);
    return saveProject();
}

void ProjectManager::setIncludeDirs(const QStringList &dirs) {
    includeDirs_.clear();
    for (const QString &dir : dirs) {
        const QString normalized = normalizeToProjectRelative(dir);
        if (!normalized.isEmpty() && !includeDirs_.contains(normalized)) {
            includeDirs_.append(normalized);
        }
    }
    if (includeDirs_.isEmpty()) {
        includeDirs_.append(".");
    }
    saveProject();
}

void ProjectManager::setCompiler(const QString &compiler) {
    compiler_ = compiler.trimmed().isEmpty() ? QStringLiteral("g++") : compiler.trimmed();
    saveProject();
}

void ProjectManager::setCxxStandard(const QString &standard) {
    cxxStandard_ = standard.trimmed().isEmpty() ? QStringLiteral("c++20") : standard.trimmed();
    saveProject();
}

void ProjectManager::setOutputName(const QString &outputName) {
    outputName_ = outputName.trimmed().isEmpty() ? projectName_ : outputName.trimmed();
    releaseProfile_.outputName = outputName_;
    if (debugProfile_.outputName.isEmpty()) {
        debugProfile_.outputName = outputName_ + "_debug";
    }
    saveProject();
}

void ProjectManager::setExtraFlags(const QStringList &flags) {
    extraFlags_ = flags;
    saveProject();
}

void ProjectManager::setRunArgs(const QStringList &args) {
    runArgs_ = args;
    saveProject();
}

void ProjectManager::setRunWorkingDir(const QString &dir) {
    runWorkingDir_ = normalizeToProjectRelative(dir);
    saveProject();
}

bool ProjectManager::generateCompileCommands(QString *errorMessage) const {
    if (!hasProject()) {
        if (errorMessage) {
            *errorMessage = tr("没有打开工程");
        }
        return false;
    }

    const QString outputPath = QDir(rootDir_).filePath("compile_commands.json");
    QFile file(outputPath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (errorMessage) {
            *errorMessage = tr("无法写入 compile_commands.json");
        }
        return false;
    }

    QJsonArray commands;
    const QStringList absIncludes = includeDirsAbsolute();

    for (const QString &src : sources_) {
        const QString absSrc = resolveToAbsolute(src);
        QString cmd = quoteIfNeeded(compiler_);
        cmd += " -std=" + cxxStandard_ + " -Wall";
        for (const QString &inc : absIncludes) {
            cmd += " -I" + quoteIfNeeded(inc);
        }
        const QStringList flags = activeExtraFlags();
        for (const QString &flag : flags) {
            cmd += " " + flag;
        }
        cmd += " -c " + quoteIfNeeded(absSrc);

        QJsonObject entry;
        entry.insert("directory", rootDir_);
        entry.insert("file", absSrc);
        entry.insert("command", cmd);
        commands.append(entry);
    }

    QJsonDocument doc(commands);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectManager::downloadRusticLibrary(QString *errorMessage) {
    if (!hasProject()) {
        if (errorMessage) {
            *errorMessage = tr("请先打开工程");
        }
        return false;
    }

    const QString thirdPartyRoot = QDir(rootDir_).filePath("third_party");
    QDir().mkpath(thirdPartyRoot);

    const QString cloneDir = QDir(thirdPartyRoot).filePath("rustic.hpp");
    if (QDir(cloneDir).exists()) {
        addIncludeDir(cloneDir);
        if (errorMessage) {
            *errorMessage = tr("third_party/rustic.hpp 已存在，已加入 include 目录");
        }
        return true;
    }

    QProcess proc;
    QStringList args = {"clone", "--depth=1", "https://github.com/Shyliuli/rustic.hpp.git", cloneDir};
    proc.start("git", args);
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = tr("git clone rustic.hpp 失败：%1").arg(QString::fromLocal8Bit(proc.readAllStandardError()));
        }
        return false;
    }

    addIncludeDir(cloneDir);
    if (errorMessage) {
        *errorMessage = tr("已克隆 rustic.hpp 到 third_party，并加入 include 目录");
    }
    return true;
}

QString ProjectManager::normalizeToProjectRelative(const QString &path) const {
    if (rootDir_.isEmpty()) {
        return QFileInfo(path).absoluteFilePath();
    }

    QFileInfo info(path);
    const QString absPath = info.absoluteFilePath();
    QDir root(rootDir_);

    if (absPath.startsWith(root.absolutePath() + QDir::separator())) {
        return root.relativeFilePath(absPath);
    }
    return absPath;
}

QString ProjectManager::resolveToAbsolute(const QString &path) const {
    if (QDir::isAbsolutePath(path) || rootDir_.isEmpty()) {
        return QFileInfo(path).absoluteFilePath();
    }
    return QDir(rootDir_).absoluteFilePath(path);
}

bool ProjectManager::loadFromJson(const QByteArray &data, QString *errorMessage) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.errorString();
        }
        return false;
    }

    QJsonObject obj = doc.object();
    projectName_ = obj.value("name").toString();
    outputName_ = obj.value("output").toString(projectName_);
    compiler_ = obj.value("compiler").toString("g++");
    cxxStandard_ = obj.value("cxxStandard").toString("c++20");

    sources_.clear();
    for (const auto &value : obj.value("sources").toArray()) {
        sources_.append(value.toString());
    }

    includeDirs_.clear();
    for (const auto &value : obj.value("includeDirs").toArray()) {
        includeDirs_.append(value.toString());
    }

    extraFlags_.clear();
    for (const auto &value : obj.value("extraFlags").toArray()) {
        extraFlags_.append(value.toString());
    }

    activeProfile_ = obj.value("activeProfile").toString(QStringLiteral("Debug"));
    debugProfile_ = BuildProfile{};
    releaseProfile_ = BuildProfile{};
    const QJsonObject profilesObj = obj.value("profiles").toObject();
    if (!profilesObj.isEmpty()) {
        const QJsonObject dbgObj = profilesObj.value("Debug").toObject();
        debugProfile_.outputName = dbgObj.value("output").toString();
        for (const auto &f : dbgObj.value("flags").toArray()) {
            debugProfile_.flags.append(f.toString());
        }

        const QJsonObject relObj = profilesObj.value("Release").toObject();
        releaseProfile_.outputName = relObj.value("output").toString();
        for (const auto &f : relObj.value("flags").toArray()) {
            releaseProfile_.flags.append(f.toString());
        }
    }

    groups_.clear();
    for (const auto &gVal : obj.value("groups").toArray()) {
        const QJsonObject gObj = gVal.toObject();
        ProjectGroup g;
        g.name = gObj.value("name").toString();
        for (const auto &f : gObj.value("files").toArray()) {
            g.files.append(f.toString());
        }
        if (!g.name.isEmpty()) {
            groups_.append(g);
        }
    }

    runArgs_.clear();
    for (const auto &value : obj.value("runArgs").toArray()) {
        runArgs_.append(value.toString());
    }
    runWorkingDir_ = obj.value("runWorkingDir").toString();
    if (includeDirs_.isEmpty()) {
        includeDirs_.append(".");
    }

    ensureDefaultProfiles();

    return true;
}

QByteArray ProjectManager::toJson() const {
    QJsonObject obj;
    obj.insert("name", projectName_);
    obj.insert("output", outputName_);
    obj.insert("compiler", compiler_);
    obj.insert("cxxStandard", cxxStandard_);

    QJsonArray flags;
    for (const QString &flag : extraFlags_) {
        flags.append(flag);
    }
    obj.insert("extraFlags", flags);

    obj.insert("activeProfile", activeProfile_);
    QJsonObject profilesObj;
    QJsonObject dbgObj;
    dbgObj.insert("output", debugProfile_.outputName);
    QJsonArray dbgFlags;
    for (const QString &f : debugProfile_.flags) {
        dbgFlags.append(f);
    }
    dbgObj.insert("flags", dbgFlags);
    profilesObj.insert("Debug", dbgObj);

    QJsonObject relObj;
    relObj.insert("output", releaseProfile_.outputName);
    QJsonArray relFlags;
    for (const QString &f : releaseProfile_.flags) {
        relFlags.append(f);
    }
    relObj.insert("flags", relFlags);
    profilesObj.insert("Release", relObj);
    obj.insert("profiles", profilesObj);

    QJsonArray runArgs;
    for (const QString &arg : runArgs_) {
        runArgs.append(arg);
    }
    obj.insert("runArgs", runArgs);
    obj.insert("runWorkingDir", runWorkingDir_);

    QJsonArray sources;
    for (const QString &src : sources_) {
        sources.append(src);
    }
    obj.insert("sources", sources);

    QJsonArray includes;
    for (const QString &inc : includeDirs_) {
        includes.append(inc);
    }
    obj.insert("includeDirs", includes);

    QJsonArray groupsArr;
    for (const auto &g : groups_) {
        QJsonObject gObj;
        gObj.insert("name", g.name);
        QJsonArray filesArr;
        for (const QString &f : g.files) {
            filesArr.append(f);
        }
        gObj.insert("files", filesArr);
        groupsArr.append(gObj);
    }
    obj.insert("groups", groupsArr);

    QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Indented);
}

QString ProjectManager::quoteIfNeeded(const QString &value) const {
    if (value.contains(' ')) {
        return QStringLiteral("\"") + value + QStringLiteral("\"");
    }
    return value;
}

void ProjectManager::ensureDefaultProfiles() {
    if (releaseProfile_.outputName.isEmpty()) {
        releaseProfile_.outputName = outputName_.isEmpty() ? projectName_ : outputName_;
    }
    if (debugProfile_.outputName.isEmpty()) {
        debugProfile_.outputName = releaseProfile_.outputName + "_debug";
    }
    if (debugProfile_.flags.isEmpty()) {
        debugProfile_.flags = {QStringLiteral("-g"), QStringLiteral("-O0")};
    }
    if (activeProfile_.isEmpty()) {
        activeProfile_ = QStringLiteral("Debug");
    }
    outputName_ = releaseProfile_.outputName;
}
