// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicMutableModule.h"

#include "MuCO/ICustomizableObjectModule.h"
#include "RigLogicMutableExtension.h"

class FRigLogicMutableModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		Extension = TWeakObjectPtr<const URigLogicMutableExtension>(GetDefault<URigLogicMutableExtension>());
		ICustomizableObjectModule::Get().RegisterExtension(Extension.Get());
	}

	virtual void ShutdownModule() override
	{
		if (ICustomizableObjectModule::IsAvailable()
			&& Extension.IsValid())
		{
			ICustomizableObjectModule::Get().UnregisterExtension(Extension.Get());
		}
	}

private:
	TWeakObjectPtr<const URigLogicMutableExtension> Extension;
};

IMPLEMENT_MODULE(FRigLogicMutableModule, RigLogicMutable);
