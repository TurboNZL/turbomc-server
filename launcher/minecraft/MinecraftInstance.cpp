// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "MinecraftInstance.h"
#include "Application.h"
#include "BuildConfig.h"
#include "Json.h"
#include "QObjectPtr.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

#include "FileSystem.h"
#include "MMCTime.h"
#include "java/JavaVersion.h"

#include "launch/LaunchTask.h"
#include "launch/TaskStepWrapper.h"
#include "launch/steps/CheckJava.h"
#include "launch/steps/LaunchCommand.h"
#include "launch/steps/QuitAfterGameStop.h"
#include "launch/steps/TextPrint.h"


#include "minecraft/update/AssetUpdateTask.h"
#include "minecraft/update/FoldersTask.h"
#include "minecraft/update/LegacyFMLLibrariesTask.h"
#include "minecraft/update/LibrariesTask.h"

#include "java/JavaUtils.h"

#include "icons/IconList.h"

#include "mod/Mod.h"
#include "mod/ModFolderModel.h"
#include "mod/ResourcePackFolderModel.h"
#include "mod/ShaderPackFolderModel.h"
#include "mod/TexturePackFolderModel.h"
#include "mod/DataPackFolderModel.h"

#include "WorldList.h"

#include "AssetsUtils.h"
#include "MinecraftLoadAndCheck.h"
#include "PackProfile.h"


#include <QActionGroup>
#include <QMainWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QWindow>

#ifdef Q_OS_LINUX
#include "LibraryUtils.h"
#endif

#ifdef WITH_QTDBUS
#include <QtDBus/QtDBus>
#endif

#define IBUS "@im=ibus"

[[maybe_unused]] static bool switcherooSetupGPU(QProcessEnvironment& env)
{
#ifdef WITH_QTDBUS
    if (!QDBusConnection::systemBus().isConnected())
        return false;

    QDBusInterface switcheroo("net.hadess.SwitcherooControl", "/net/hadess/SwitcherooControl", "org.freedesktop.DBus.Properties",
                              QDBusConnection::systemBus());

    if (!switcheroo.isValid())
        return false;

    QDBusReply<QDBusVariant> reply =
        switcheroo.call(QStringLiteral("Get"), QStringLiteral("net.hadess.SwitcherooControl"), QStringLiteral("GPUs"));
    if (!reply.isValid())
        return false;

    QDBusArgument arg = qvariant_cast<QDBusArgument>(reply.value().variant());
    QList<QVariantMap> gpus;
    arg >> gpus;

    for (const auto& gpu : gpus) {
        QString name = qvariant_cast<QString>(gpu[QStringLiteral("Name")]);
        bool defaultGpu = qvariant_cast<bool>(gpu[QStringLiteral("Default")]);
        bool discrete = qvariant_cast<bool>(gpu.value(QStringLiteral("Discrete"), !defaultGpu));
        if (discrete) {
            QStringList envList = qvariant_cast<QStringList>(gpu[QStringLiteral("Environment")]);
            for (int i = 0; i + 1 < envList.size(); i += 2) {
                env.insert(envList[i], envList[i + 1]);
            }
            break;
        }
    }

    return true;
#else
    return false;
#endif
}

// all of this because keeping things compatible with deprecated old settings
// if either of the settings {a, b} is true, this also resolves to true
class OrSetting : public Setting {
    Q_OBJECT
   public:
    OrSetting(QString id, std::shared_ptr<Setting> a, std::shared_ptr<Setting> b) : Setting({ id }, false), m_a(a), m_b(b) {}
    virtual QVariant get() const
    {
        bool a = m_a->get().toBool();
        bool b = m_b->get().toBool();
        return a || b;
    }
    virtual void reset() {}
    virtual void set(QVariant value) {}

   private:
    std::shared_ptr<Setting> m_a;
    std::shared_ptr<Setting> m_b;
};

MinecraftInstance::MinecraftInstance(SettingsObject* globalSettings, std::unique_ptr<SettingsObject> settings, const QString& rootDir)
    : BaseInstance(globalSettings, std::move(settings), rootDir)
{
    m_components.reset(new PackProfile(this));
}

MinecraftInstance::~MinecraftInstance() {}

void MinecraftInstance::saveNow()
{
    m_components->saveNow();
}

void MinecraftInstance::loadSpecificSettings()
{
    if (isSpecificSettingsLoaded())
        return;

    // Java Settings
    auto locationOverride = m_settings->registerSetting("OverrideJavaLocation", false);
    auto argsOverride = m_settings->registerSetting("OverrideJavaArgs", false);
    m_settings->registerSetting("AutomaticJava", false);
    m_settings->registerSetting("UseLatestMinecraftVersion", false);
    m_settings->registerSetting("UseLatestMinecraftVersionType", "release");

    if (auto global_settings = globalSettings()) {
        m_settings->registerOverride(global_settings->getSetting("JavaPath"), locationOverride);
        m_settings->registerOverride(global_settings->getSetting("JvmArgs"), argsOverride);
        m_settings->registerOverride(global_settings->getSetting("IgnoreJavaCompatibility"), locationOverride);

        // special!
        m_settings->registerPassthrough(global_settings->getSetting("JavaSignature"), locationOverride);
        m_settings->registerPassthrough(global_settings->getSetting("JavaArchitecture"), locationOverride);
        m_settings->registerPassthrough(global_settings->getSetting("JavaRealArchitecture"), locationOverride);
        m_settings->registerPassthrough(global_settings->getSetting("JavaVersion"), locationOverride);
        m_settings->registerPassthrough(global_settings->getSetting("JavaVendor"), locationOverride);

        // Window Size
        auto windowSetting = m_settings->registerSetting("OverrideWindow", false);
        m_settings->registerOverride(global_settings->getSetting("LaunchMaximized"), windowSetting);
        m_settings->registerOverride(global_settings->getSetting("MinecraftWinWidth"), windowSetting);
        m_settings->registerOverride(global_settings->getSetting("MinecraftWinHeight"), windowSetting);

        // Memory
        auto memorySetting = m_settings->registerSetting("OverrideMemory", false);
        m_settings->registerOverride(global_settings->getSetting("MinMemAlloc"), memorySetting);
        m_settings->registerOverride(global_settings->getSetting("MaxMemAlloc"), memorySetting);
        m_settings->registerOverride(global_settings->getSetting("PermGen"), memorySetting);
        m_settings->registerOverride(global_settings->getSetting("LowMemWarning"), memorySetting);

        // Native library workarounds
        auto nativeLibraryWorkaroundsOverride = m_settings->registerSetting("OverrideNativeWorkarounds", false);
        m_settings->registerOverride(global_settings->getSetting("UseNativeOpenAL"), nativeLibraryWorkaroundsOverride);
        m_settings->registerOverride(global_settings->getSetting("CustomOpenALPath"), nativeLibraryWorkaroundsOverride);
        m_settings->registerOverride(global_settings->getSetting("UseNativeGLFW"), nativeLibraryWorkaroundsOverride);
        m_settings->registerOverride(global_settings->getSetting("CustomGLFWPath"), nativeLibraryWorkaroundsOverride);
        m_settings->registerOverride(global_settings->getSetting("UseNativeSDL"), nativeLibraryWorkaroundsOverride);
        m_settings->registerOverride(global_settings->getSetting("CustomSDLPath"), nativeLibraryWorkaroundsOverride);

        // Performance related options
        auto performanceOverride = m_settings->registerSetting("OverridePerformance", false);
        m_settings->registerOverride(global_settings->getSetting("EnableFeralGamemode"), performanceOverride);
        m_settings->registerOverride(global_settings->getSetting("EnableMangoHud"), performanceOverride);
        m_settings->registerOverride(global_settings->getSetting("UseDiscreteGpu"), performanceOverride);
        m_settings->registerOverride(global_settings->getSetting("UseZink"), performanceOverride);

        // Miscellaneous
        auto miscellaneousOverride = m_settings->registerSetting("OverrideMiscellaneous", false);
        m_settings->registerOverride(global_settings->getSetting("CloseAfterLaunch"), miscellaneousOverride);
        m_settings->registerOverride(global_settings->getSetting("QuitAfterGameStop"), miscellaneousOverride);

        // Legacy-related options
        auto legacySettings = m_settings->registerSetting("OverrideLegacySettings", false);
        m_settings->registerOverride(global_settings->getSetting("OnlineFixes"), legacySettings);

        auto envSetting = m_settings->registerSetting("OverrideEnv", false);
        m_settings->registerOverride(global_settings->getSetting("Env"), envSetting);

        if (m_settings->get("InstanceType").toString() != "OneSix") {
            m_settings->set("InstanceType", "OneSix");
        }
    }

    // Join server on launch, this does not have a global override
    m_settings->registerSetting("JoinServerOnLaunch", false);
    m_settings->registerSetting("JoinServerOnLaunchAddress", "");
    m_settings->registerSetting("JoinWorldOnLaunch", "");

    // Use account for instance, this does not have a global override
    m_settings->registerSetting("UseAccountForInstance", false);
    m_settings->registerSetting("InstanceAccountId", "");

    m_settings->registerSetting("ExportName", "");
    m_settings->registerSetting("ExportVersion", "1.0.0");
    m_settings->registerSetting("ExportSummary", "");
    m_settings->registerSetting("ExportAuthor", "");
    m_settings->registerSetting("ExportOptionalFiles", true);
    m_settings->registerSetting("ExportRecommendedRAM");

    auto dataPacksEnabled = m_settings->registerSetting("GlobalDataPacksEnabled", false);
    auto dataPacksPath = m_settings->registerSetting("GlobalDataPacksPath", "");

    connect(dataPacksEnabled.get(), &Setting::SettingChanged, this, [this] { m_data_pack_list.reset(); });
    connect(dataPacksPath.get(), &Setting::SettingChanged, this, [this] { m_data_pack_list.reset(); });

    // Join server on launch, this does not have a global override
    m_settings->registerSetting("OverrideModDownloadLoaders", false);
    m_settings->registerSetting("ModDownloadLoaders", "[]");

    qDebug() << "Instance-type specific settings were loaded!";

    setSpecificSettingsLoaded(true);

    updateRuntimeContext();
}

void MinecraftInstance::updateRuntimeContext()
{
    m_runtimeContext.updateFromInstanceSettings(m_settings.get());
    m_components->invalidateLaunchProfile();
}

PackProfile* MinecraftInstance::getPackProfile() const
{
    return m_components.get();
}

QSet<QString> MinecraftInstance::traits() const
{
    auto components = getPackProfile();
    if (!components) {
        return { "version-incomplete" };
    }
    auto profile = components->getProfile();
    if (!profile) {
        return { "version-incomplete" };
    }
    return profile->getTraits();
}

// FIXME: move UI code out of MinecraftInstance
void MinecraftInstance::populateLaunchMenu(QMenu* menu)
{
    QAction* normalLaunch = menu->addAction(tr("&Launch"));
    normalLaunch->setShortcut(QKeySequence::Open);
    QAction* normalLaunchOffline = menu->addAction(tr("Launch &Offline"));
    normalLaunchOffline->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    QAction* normalLaunchDemo = menu->addAction(tr("Launch &Demo"));
    normalLaunchDemo->setShortcut(QKeySequence(tr("Ctrl+Alt+O")));

    normalLaunchDemo->setEnabled(supportsDemo());

    connect(normalLaunch, &QAction::triggered, this, [this] { APPLICATION->launch(this); });
    connect(normalLaunchOffline, &QAction::triggered, this, [this] { APPLICATION->launch(this, LaunchMode::Offline); });
    connect(normalLaunchDemo, &QAction::triggered, this, [this] { APPLICATION->launch(this, LaunchMode::Demo); });

    auto accounts = APPLICATION->accounts();
    if (accounts->count() > 1) {
        QMenu* launchAsMenu = menu->addMenu(tr("Launch &As"));
        for (int i = 0; i < accounts->count(); i++) {
            MinecraftAccountPtr account = accounts->at(i);
            QAction* action = launchAsMenu->addAction(account->displayName());
            if (auto face = account->getFace(); !face.isNull()) {
                action->setIcon(face);
            } else {
                action->setIcon(QIcon::fromTheme("noaccount"));
            }
            connect(action, &QAction::triggered, this,
                    [this, account] { APPLICATION->launch(this, LaunchMode::Normal, nullptr, account); });
        }
    }

    QString profilersTitle = tr("Profilers");
    menu->addSeparator()->setText(profilersTitle);

    auto profilers = new QActionGroup(menu);
    profilers->setExclusive(true);
    connect(profilers, &QActionGroup::triggered, this, [this](QAction* action) {
        settings()->set("Profiler", action->data());
        emit profilerChanged();
    });

    QAction* noProfilerAction = menu->addAction(tr("&No Profiler"));
    noProfilerAction->setData("");
    noProfilerAction->setCheckable(true);
    noProfilerAction->setChecked(true);
    profilers->addAction(noProfilerAction);

    for (auto profiler = APPLICATION->profilers().begin(); profiler != APPLICATION->profilers().end(); profiler++) {
        QAction* profilerAction = menu->addAction(profiler.value()->name());
        profilers->addAction(profilerAction);
        profilerAction->setData(profiler.key());
        profilerAction->setCheckable(true);
        profilerAction->setChecked(settings()->get("Profiler").toString() == profiler.key());

        QString error;
        profilerAction->setEnabled(profiler.value()->check(&error));
    }
}

QString MinecraftInstance::gameRoot() const
{
    QFileInfo mcDir(FS::PathCombine(instanceRoot(), "minecraft"));
    QFileInfo dotMCDir(FS::PathCombine(instanceRoot(), ".minecraft"));

    if (dotMCDir.exists() && !mcDir.exists())
        return dotMCDir.filePath();
    else
        return mcDir.filePath();
}

QString MinecraftInstance::binRoot() const
{
    return FS::PathCombine(gameRoot(), "bin");
}

QString MinecraftInstance::getNativePath() const
{
    QDir natives_dir(FS::PathCombine(instanceRoot(), "natives/"));
    return natives_dir.absolutePath();
}

QString MinecraftInstance::getLocalLibraryPath() const
{
    QDir libraries_dir(FS::PathCombine(instanceRoot(), "libraries/"));
    return libraries_dir.absolutePath();
}

bool MinecraftInstance::supportsDemo() const
{
    Version instance_ver{ getPackProfile()->getComponentVersion("net.minecraft") };
    // Demo mode was introduced in 1.3.1: https://minecraft.wiki/w/Demo_mode#History
    // FIXME: Due to Version constraints atm, this can't handle well non-release versions
    return instance_ver >= Version("1.3.1");
}

QString MinecraftInstance::jarModsDir() const
{
    QDir jarmods_dir(FS::PathCombine(instanceRoot(), "jarmods/"));
    return jarmods_dir.absolutePath();
}

QString MinecraftInstance::modsRoot() const
{
    return FS::PathCombine(gameRoot(), "mods");
}

QString MinecraftInstance::coreModsDir() const
{
    return FS::PathCombine(gameRoot(), "coremods");
}

QString MinecraftInstance::nilModsDir() const
{
    return FS::PathCombine(gameRoot(), "nilmods");
}

QString MinecraftInstance::dataPacksDir()
{
    QString relativePath = settings()->get("GlobalDataPacksPath").toString();

    if (relativePath.isEmpty())
        relativePath = "datapacks";

    return QDir(gameRoot()).filePath(relativePath);
}

QString MinecraftInstance::resourcePacksDir() const
{
    return FS::PathCombine(gameRoot(), "resourcepacks");
}

QString MinecraftInstance::texturePacksDir() const
{
    return FS::PathCombine(gameRoot(), "texturepacks");
}

QString MinecraftInstance::shaderPacksDir() const
{
    return FS::PathCombine(gameRoot(), "shaderpacks");
}

QString MinecraftInstance::instanceConfigFolder() const
{
    return FS::PathCombine(gameRoot(), "config");
}

QString MinecraftInstance::libDir() const
{
    return FS::PathCombine(gameRoot(), "lib");
}

QString MinecraftInstance::worldDir() const
{
    return FS::PathCombine(gameRoot(), "saves");
}

QString MinecraftInstance::resourcesDir() const
{
    return FS::PathCombine(gameRoot(), "resources");
}

QDir MinecraftInstance::librariesPath() const
{
    return QDir::current().absoluteFilePath("libraries");
}

QDir MinecraftInstance::jarmodsPath() const
{
    return QDir(jarModsDir());
}

QString MinecraftInstance::getMainClass() const
{
    auto profile = m_components->getProfile();
    return profile->getMainClass();
}

static QString replaceTokensIn(const QString& text, const QMap<QString, QString>& with)
{
    // TODO: does this still work??
    QString result;
    static const QRegularExpression s_token_regexp("\\$\\{(.+)\\}", QRegularExpression::InvertedGreedinessOption);
    QStringList list;
    QRegularExpressionMatchIterator i = s_token_regexp.globalMatch(text);
    int lastCapturedEnd = 0;
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        result.append(text.mid(lastCapturedEnd, match.capturedStart()));
        QString key = match.captured(1);
        auto iter = with.find(key);
        if (iter != with.end()) {
            result.append(*iter);
        }
        lastCapturedEnd = match.capturedEnd();
    }
    result.append(text.mid(lastCapturedEnd));
    return result;
}

QStringList MinecraftInstance::extraArguments()
{
    auto list = BaseInstance::extraArguments();
    auto version = getPackProfile();
    if (!version)
        return list;
    auto jarMods = getJarMods();
    if (!jarMods.isEmpty()) {
        list.append({ "-Dfml.ignoreInvalidMinecraftCertificates=true", "-Dfml.ignorePatchDiscrepancies=true" });
    }
    auto addn = m_components->getProfile()->getAddnJvmArguments();
    if (!addn.isEmpty()) {
        QMap<QString, QString> tokenMapping = makeProfileVarMapping(m_components->getProfile());

        for (const QString& item : addn) {
            list.append(replaceTokensIn(item, tokenMapping));
        }
    }
    auto agents = m_components->getProfile()->getAgents();
    for (const auto& agent : agents) {
        QStringList jar, temp1, temp2, temp3;
        agent.library->getApplicableFiles(runtimeContext(), jar, temp1, temp2, temp3, getLocalLibraryPath());
        list.append("-javaagent:" + jar[0] + (agent.argument.isEmpty() ? "" : "=" + agent.argument));
    }

    {
        QString openALPath;
        QString glfwPath;
        QString sdlPath;

        if (settings()->get("UseNativeOpenAL").toBool()) {
            openALPath = APPLICATION->m_detectedOpenALPath;
            auto customPath = settings()->get("CustomOpenALPath").toString();
            if (!customPath.isEmpty())
                openALPath = customPath;
        }
        if (settings()->get("UseNativeGLFW").toBool()) {
            glfwPath = APPLICATION->m_detectedGLFWPath;
            auto customPath = settings()->get("CustomGLFWPath").toString();
            if (!customPath.isEmpty())
                glfwPath = customPath;
        }
        if (settings()->get("UseNativeSDL").toBool()) {
            sdlPath = APPLICATION->m_detectedSDLPath;
            auto customPath = settings()->get("CustomSDLPath").toString();
            if (!customPath.isEmpty())
                sdlPath = customPath;
        }

        QFileInfo openALInfo(openALPath);
        QFileInfo glfwInfo(glfwPath);
        QFileInfo sdlInfo(sdlPath);

        if (!openALPath.isEmpty() && openALInfo.exists())
            list.append("-Dorg.lwjgl.openal.libname=" + openALInfo.absoluteFilePath());
        if (!glfwPath.isEmpty() && glfwInfo.exists())
            list.append("-Dorg.lwjgl.glfw.libname=" + glfwInfo.absoluteFilePath());
        if (!sdlPath.isEmpty() && sdlInfo.exists())
            list.append("-Dorg.lwjgl.sdl.libname=" + sdlInfo.absoluteFilePath());
    }

    return list;
}

QString MinecraftInstance::getLauncher()
{
    // use legacy launcher if the traits are set
    if (isLegacy())
        return "legacy";

    return "standard";
}

bool MinecraftInstance::shouldApplyOnlineFixes()
{
    return traits().contains("legacyServices") && settings()->get("OnlineFixes").toBool();
}

QMap<QString, QString> MinecraftInstance::getVariables()
{
    QMap<QString, QString> out;
    out.insert("INST_NAME", name());
    out.insert("INST_ID", id());
    out.insert("INST_DIR", QDir::toNativeSeparators(QDir(instanceRoot()).absolutePath()));
    out.insert("INST_MC_DIR", QDir::toNativeSeparators(QDir(gameRoot()).absolutePath()));
    out.insert("INST_JAVA", QDir::toNativeSeparators(QDir(settings()->get("JavaPath").toString()).absolutePath()));
    out.insert("INST_JAVA_ARGS", javaArguments().join(' '));
    out.insert("NO_COLOR", "1");
#ifdef Q_OS_MACOS
    // get library for Steam overlay support
    QString steamDyldInsertLibraries = qEnvironmentVariable("STEAM_DYLD_INSERT_LIBRARIES");
    if (!steamDyldInsertLibraries.isEmpty()) {
        out.insert("DYLD_INSERT_LIBRARIES", steamDyldInsertLibraries);
    }
#endif
    return out;
}

QProcessEnvironment MinecraftInstance::createEnvironment()
{
    // prepare the process environment
    QProcessEnvironment env = CleanEnviroment();

    // export some infos
    auto variables = getVariables();
    for (auto it = variables.begin(); it != variables.end(); ++it) {
        env.insert(it.key(), it.value());
    }
    // custom env

    auto insertEnv = [&env](QString value) {
        auto envMap = Json::toMap(value);
        if (envMap.isEmpty())
            return;

        for (auto iter = envMap.begin(); iter != envMap.end(); iter++)
            env.insert(iter.key(), iter.value().toString());
    };

    insertEnv(settings()->get("Env").toString());
    return env;
}

QMap<QString, QString> MinecraftInstance::makeProfileVarMapping(std::shared_ptr<LaunchProfile> profile) const
{
    QMap<QString, QString> result;

    result["profile_name"] = name();
    result["version_name"] = profile->getMinecraftVersion();
    result["version_type"] = profile->getMinecraftVersionType();

    QString absRootDir = QDir(gameRoot()).absolutePath();
    result["game_directory"] = absRootDir;
    QString absAssetsDir = QDir("assets/").absolutePath();
    auto assets = profile->getMinecraftAssets();
    result["game_assets"] = AssetsUtils::getAssetsDir(assets->id, resourcesDir()).absolutePath();

    // 1.7.3+ assets tokens
    result["assets_root"] = absAssetsDir;
    result["assets_index_name"] = assets->id;

    result["library_directory"] = APPLICATION->metacache()->getBasePath("libraries");

    return result;
}

QStringList MinecraftInstance::getLogFileSearchPaths()
{
    return { FS::PathCombine(gameRoot(), "crash-reports"), FS::PathCombine(gameRoot(), "logs"), gameRoot() };
}

QString MinecraftInstance::getStatusbarDescription()
{
    QStringList traits;
    if (hasVersionBroken()) {
        traits.append(tr("broken"));
    }

    QString mcVersion = m_components->getComponentVersion("net.minecraft");
    if (mcVersion.isEmpty()) {
        // Load component info if needed
        if (auto res = m_components->reload(Net::Mode::Offline); !res) {
            qWarning() << "Failed to reload components:" << res.error();
        }
        mcVersion = m_components->getComponentVersion("net.minecraft");
    }

    QString description;
    description.append(tr("Minecraft %1").arg(mcVersion));
    if (m_settings->get("ShowGameTime").toBool()) {
        if (lastTimePlayed() > 0 && lastLaunch() > 0) {
            QDateTime lastLaunchTime = QDateTime::fromMSecsSinceEpoch(lastLaunch());
            description.append(
                tr(", last played on %1 for %2")
                    .arg(QLocale().toString(lastLaunchTime, QLocale::ShortFormat))
                    .arg(Time::prettifyDuration(lastTimePlayed(), APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
        }

        if (totalTimePlayed() > 0) {
            description.append(
                tr(", total played for %1")
                    .arg(Time::prettifyDuration(totalTimePlayed(), APPLICATION->settings()->get("ShowGameTimeWithoutDays").toBool())));
        }
    }
    if (hasCrashed()) {
        description.append(tr(", has crashed."));
    }
    return description;
}

JavaVersion MinecraftInstance::getJavaVersion()
{
    return JavaVersion(settings()->get("JavaVersion").toString());
}

ModFolderModel* MinecraftInstance::loaderModList()
{
    if (!m_loader_mod_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_loader_mod_list.reset(new ModFolderModel(modsRoot(), this, is_indexed, true));
    }
    return m_loader_mod_list.get();
}

ModFolderModel* MinecraftInstance::coreModList()
{
    if (!m_core_mod_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_core_mod_list.reset(new ModFolderModel(coreModsDir(), this, is_indexed, true));
    }
    return m_core_mod_list.get();
}

ModFolderModel* MinecraftInstance::nilModList()
{
    if (!m_nil_mod_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_nil_mod_list.reset(new ModFolderModel(nilModsDir(), this, is_indexed, false));
    }
    return m_nil_mod_list.get();
}

ResourcePackFolderModel* MinecraftInstance::resourcePackList()
{
    if (!m_resource_pack_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_resource_pack_list.reset(new ResourcePackFolderModel(resourcePacksDir(), this, is_indexed, true));
    }
    return m_resource_pack_list.get();
}

TexturePackFolderModel* MinecraftInstance::texturePackList()
{
    if (!m_texture_pack_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_texture_pack_list.reset(new TexturePackFolderModel(texturePacksDir(), this, is_indexed, true));
    }
    return m_texture_pack_list.get();
}

ShaderPackFolderModel* MinecraftInstance::shaderPackList()
{
    if (!m_shader_pack_list) {
        bool is_indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_shader_pack_list.reset(new ShaderPackFolderModel(shaderPacksDir(), this, is_indexed, true));
    }
    return m_shader_pack_list.get();
}

DataPackFolderModel* MinecraftInstance::dataPackList()
{
    if (!m_data_pack_list && settings()->get("GlobalDataPacksEnabled").toBool()) {
        bool isIndexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        m_data_pack_list.reset(new DataPackFolderModel(dataPacksDir(), this, isIndexed, true));
    }
    return m_data_pack_list.get();
}

QList<ResourceFolderModel*> MinecraftInstance::resourceLists()
{
    return { loaderModList(), coreModList(), nilModList(), resourcePackList(), texturePackList(), shaderPackList(), dataPackList() };
}

WorldList* MinecraftInstance::worldList()
{
    if (!m_world_list) {
        m_world_list.reset(new WorldList(worldDir(), this));
    }
    return m_world_list.get();
}

QList<Mod*> MinecraftInstance::getJarMods() const
{
    auto profile = m_components->getProfile();
    QList<Mod*> mods;
    for (auto jarmod : profile->getJarMods()) {
        QStringList jar, temp1, temp2, temp3;
        jarmod->getApplicableFiles(runtimeContext(), jar, temp1, temp2, temp3, jarmodsPath().absolutePath());
        // QString filePath = jarmodsPath().absoluteFilePath(jarmod->filename(currentSystem));
        mods.push_back(new Mod(QFileInfo(jar[0])));
    }
    return mods;
}

#include "MinecraftInstance.moc"
