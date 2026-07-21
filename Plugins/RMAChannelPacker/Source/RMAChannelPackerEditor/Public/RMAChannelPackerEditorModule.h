#pragma once

#include "Modules/ModuleManager.h"

class FRMAChannelPackerEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
};
