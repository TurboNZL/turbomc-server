#pragma once
#include <FileSystem.h>
#include "minecraft/MinecraftInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/pages/instance/InstanceSettingsPage.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/pages/instance/ModFolderPage.h"
#include "ui/pages/instance/NotesPage.h"
#include "ui/pages/instance/OtherLogsPage.h"
#include "ui/pages/instance/ServerBackupsPage.h"
#include "ui/pages/instance/ServerPropertiesPage.h"
#include "ui/pages/instance/ServerVersionPage.h"
#include "ui/pages/instance/WorldListPage.h"

class InstancePageProvider : protected QObject, public BasePageProvider {
    Q_OBJECT
   public:
    explicit InstancePageProvider(MinecraftInstance* parent) { inst = parent; }

    virtual ~InstancePageProvider() = default;
    virtual QList<BasePage*> getPages() override
    {
        QList<BasePage*> values;
        values.append(new LogPage(inst));
        values.append(new ServerVersionPage(inst));
        if (Server::supportsAddons(inst->serverSoftware())) {
            auto modsPage = new ModFolderPage(inst, inst->loaderModList());
            modsPage->setFilter("%1 (*.zip *.jar)");
            values.append(modsPage);
        }
        values.append(new NotesPage(inst));
        values.append(new WorldListPage(inst, inst->worldList()));
        values.append(new ServerPropertiesPage(inst));
        values.append(new ServerBackupsPage(inst));
        values.append(new InstanceSettingsPage(inst));
        values.append(new OtherLogsPage("logs", tr("Other Logs"), "Other-Logs", inst));
        return values;
    }

    virtual QString dialogTitle() override { return tr("Edit Instance (%1)").arg(inst->name()); }

   protected:
    MinecraftInstance* inst;
};
