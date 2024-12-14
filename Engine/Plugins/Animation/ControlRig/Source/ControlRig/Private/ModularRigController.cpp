// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModularRigController.h"

#include "ControlRig.h"
#include "ModularRig.h"
#include "ModularRigModel.h"
#include "ModularRigRuleManager.h"
#include "Misc/DefaultValueHelper.h"
#include "Rigs/RigHierarchyController.h"
#include "RigVMFunctions/Math/RigVMMathLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ModularRigController)

#if WITH_EDITOR
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif

UModularRigController::UModularRigController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Model(nullptr)
	, bSuspendNotifications(false)
	, bAutomaticReparenting(true)
{
}

FString UModularRigController::AddModule(const FName& InModuleName, TSubclassOf<UControlRig> InClass, const FString& InParentModulePath, bool bSetupUndo)
{
	if (!InClass)
	{
		UE_LOG(LogControlRig, Error, TEXT("Invalid InClass"));
		return FString();
	}

	UControlRig* ClassDefaultObject = InClass->GetDefaultObject<UControlRig>();
	if (!ClassDefaultObject->IsRigModule())
	{
		UE_LOG(LogControlRig, Error, TEXT("Class %s is not a rig module"), *InClass->GetClassPathName().ToString());
		return FString();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "AddModuleTransaction", "Add Module"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif 

	FRigModuleReference* NewModule = nullptr;
	if (InParentModulePath.IsEmpty())
	{
		for (FRigModuleReference* Module : Model->RootModules)
		{
			if (Module->Name.ToString() == InModuleName)
			{
				return FString();
			}
		}

		Model->Modules.Add(FRigModuleReference(InModuleName, InClass, FString()));
		NewModule = &Model->Modules.Last();
	}
	else if (FRigModuleReference* ParentModule = FindModule(InParentModulePath))
	{
		for (FRigModuleReference* Module : ParentModule->CachedChildren)
		{
			if (Module->Name.ToString() == InModuleName)
			{
				return FString();
			}
		}

		Model->Modules.Add(FRigModuleReference(InModuleName, InClass, ParentModule->GetPath()));
		NewModule = &Model->Modules.Last();
	}

	Model->UpdateCachedChildren();
	UpdateShortNames();

	if (!NewModule)
	{
		UE_LOG(LogControlRig, Error, TEXT("Error while creating module %s"), *InModuleName.ToString());
		return FString();
	}

	Notify(EModularRigNotification::ModuleAdded, NewModule);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return NewModule->GetPath();
}

FRigModuleReference* UModularRigController::FindModule(const FString& InPath)
{
	return Model->FindModule(InPath);
}

const FRigModuleReference* UModularRigController::FindModule(const FString& InPath) const
{
	return const_cast<UModularRigController*>(this)->FindModule(InPath);
}

bool UModularRigController::CanConnectConnectorToElement(const FRigElementKey& InConnectorKey, const FRigElementKey& InTargetKey, FText& OutErrorMessage)
{
	FString ConnectorModulePath, ConnectorName;
	if (!URigHierarchy::SplitNameSpace(InConnectorKey.Name.ToString(), &ConnectorModulePath, &ConnectorName))
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Connector %s does not contain a namespace"), *InConnectorKey.ToString()));
		return false;
	}
	
	FRigModuleReference* Module = FindModule(ConnectorModulePath);
	if (!Module)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find module %s"), *ConnectorModulePath));
		return false;
	}

	UControlRig* RigCDO = Module->Class->GetDefaultObject<UControlRig>();
	if (!RigCDO)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Invalid rig module class %s"), *Module->Class->GetPathName()));
		return false;
	}

	const FRigModuleConnector* ModuleConnector = RigCDO->GetRigModuleSettings().ExposedConnectors.FindByPredicate(
		[ConnectorName](FRigModuleConnector& Connector)
		{
			return Connector.Name == ConnectorName;
		});
	if (!ModuleConnector)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find connector %s in class %s"), *ConnectorName, *Module->Class->GetPathName()));
		return false;
	}

	if (!InTargetKey.IsValid())
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Invalid target %s in class %s"), *InTargetKey.ToString(), *Module->Class->GetPathName()));
		return false;
	}

	if (InTargetKey == InConnectorKey)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Cannot resolve connector %s to itself in class %s"), *InTargetKey.ToString(), *Module->Class->GetPathName()));
		return false;
	}

	FRigElementKey CurrentTarget = Model->Connections.FindTargetFromConnector(InConnectorKey);
	if (CurrentTarget.IsValid() && InTargetKey == CurrentTarget)
	{
		return true; // Nothing to do
	}

	if (!ModuleConnector->IsPrimary())
	{
		const FRigModuleConnector* PrimaryModuleConnector = RigCDO->GetRigModuleSettings().ExposedConnectors.FindByPredicate(
		[](const FRigModuleConnector& Connector)
		{
			return Connector.IsPrimary();
		});

		const FString PrimaryConnectorPath = FString::Printf(TEXT("%s:%s"), *ConnectorModulePath, *PrimaryModuleConnector->Name);
		const FRigElementKey PrimaryConnectorKey(*PrimaryConnectorPath, ERigElementType::Connector);
		const FRigElementKey PrimaryTarget = Model->Connections.FindTargetFromConnector(PrimaryConnectorKey);
		if (!PrimaryTarget.IsValid())
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Cannot resolve connector %s because primary connector is not resolved"), *InConnectorKey.ToString()));
			return false;
		}
	}

#if WITH_EDITOR
	UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());

	// Make sure the connection is valid
	{
		UModularRig* ModularRig = Cast<UModularRig>(Blueprint->GetObjectBeingDebugged());
		if (!ModularRig)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find debugged modular rig in %s"), *Blueprint->GetPathName()));
			return false;
		}
	
		URigHierarchy* Hierarchy = ModularRig->GetHierarchy();
		if (!Hierarchy)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find hierarchy in %s"), *ModularRig->GetPathName()));
			return false;
		}
	
		const FRigConnectorElement* Connector = Cast<FRigConnectorElement>(Hierarchy->Find(InConnectorKey));
		if (!Connector)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find connector %s"), *InConnectorKey.ToString()));
			return false;
		}

		UModularRigRuleManager* RuleManager = Hierarchy->GetRuleManager();
		if (!RuleManager)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not get rule manager")));
			return false;
		}

		const FRigModuleInstance* ModuleInstance = ModularRig->FindModule(Module->GetPath());
		FModularRigResolveResult RuleResults = RuleManager->FindMatches(Connector, ModuleInstance, ModularRig->GetElementKeyRedirector());
		if (!RuleResults.ContainsMatch(InTargetKey))
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("The target %s is not a valid match for connector %s"), *InTargetKey.ToString(), *InConnectorKey.ToString()));
			return false;
		}
	}
#endif

	return true;
}

bool UModularRigController::ConnectConnectorToElement(const FRigElementKey& InConnectorKey, const FRigElementKey& InTargetKey, bool bSetupUndo, bool bAutoResolveOtherConnectors, bool bCheckValidConnection)
{
	FText ErrorMessage;
	if (bCheckValidConnection && !CanConnectConnectorToElement(InConnectorKey, InTargetKey, ErrorMessage))
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not connect %s to %s: %s"), *InConnectorKey.ToString(), *InTargetKey.ToString(), *ErrorMessage.ToString());
		return false;
	}
	
	FString ConnectorParentPath, ConnectorName;
	(void)URigHierarchy::SplitNameSpace(InConnectorKey.Name.ToString(), &ConnectorParentPath, &ConnectorName);
	FRigModuleReference* Module = FindModule(ConnectorParentPath);

	FRigElementKey CurrentTarget = Model->Connections.FindTargetFromConnector(InConnectorKey);

	UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());

#if WITH_EDITOR
	FName TargetModulePathName = NAME_None;
	UModularRig* ModularRig = Cast<UModularRig>(Blueprint->GetObjectBeingDebugged());
	if(ModularRig)
	{
		if (URigHierarchy* Hierarchy = ModularRig->GetHierarchy())
		{
			TargetModulePathName = Hierarchy->GetModulePathFName(InTargetKey);
		}
	}
	
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "ConnectModuleToElementTransaction", "Connect to Element"), !GIsTransacting);
		Blueprint->Modify();
	}
#endif 

	// First disconnect before connecting to anything else. This might disconnect other secondary/optional connectors.
	TMap<FRigElementKey, FRigElementKey> PreviousConnections;
	if (CurrentTarget.IsValid())
	{
		const TGuardValue<bool> DisableAutomaticReparenting(bAutomaticReparenting, false);
		DisconnectConnector_Internal(InConnectorKey, false, &PreviousConnections, bSetupUndo);
	}
	
	Model->Connections.AddConnection(InConnectorKey, InTargetKey);

	// restore previous connections if possible
	for(const TPair<FRigElementKey, FRigElementKey>& PreviousConnection : PreviousConnections)
	{
		if(!Model->Connections.HasConnection(PreviousConnection.Key))
		{
			FText ErrorMessageForPreviousConnection;
			if(CanConnectConnectorToElement(PreviousConnection.Key, PreviousConnection.Value, ErrorMessageForPreviousConnection))
			{
				(void)ConnectConnectorToElement(PreviousConnection.Key, PreviousConnection.Value, bSetupUndo, false, false);
			}
		}
	}
	
	Notify(EModularRigNotification::ConnectionChanged, Module);

#if WITH_EDITOR
	if (UControlRig* RigCDO = Module->Class->GetDefaultObject<UControlRig>())
	{
		if (ModularRig)
		{
			if (URigHierarchy* Hierarchy = ModularRig->GetHierarchy())
			{
				bool bResolvedPrimaryConnector = false;
				if(const FRigConnectorElement* PrimaryConnector = Module->FindPrimaryConnector(Hierarchy))
				{
					bResolvedPrimaryConnector = PrimaryConnector->GetKey() == InConnectorKey;
				}

				// automatically re-parent the module in the module tree as well
				if(bAutomaticReparenting)
				{
					if(const FRigConnectorElement* Connector = Hierarchy->Find<FRigConnectorElement>(InConnectorKey))
					{
						if(Connector->IsPrimary())
						{
							if(!TargetModulePathName.IsNone())
							{
								const FString NewModulePath = ReparentModule(Module->GetPath(), TargetModulePathName.ToString(), bSetupUndo);
								if(!NewModulePath.IsEmpty())
								{
									Module = FindModule(NewModulePath);
								}
							}
						}
					}
				}

				if (Module && bAutoResolveOtherConnectors && bResolvedPrimaryConnector)
				{
					(void)AutoConnectModules( {Module->GetPath()}, false, bSetupUndo);
				}
			}
		}
	}
#endif

	(void)DisconnectCyclicConnectors();

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	
	return true;
}

bool UModularRigController::DisconnectConnector(const FRigElementKey& InConnectorKey, bool bDisconnectSubModules, bool bSetupUndo)
{
	return DisconnectConnector_Internal(InConnectorKey, bDisconnectSubModules, nullptr, bSetupUndo);
};

bool UModularRigController::DisconnectConnector_Internal(const FRigElementKey& InConnectorKey, bool bDisconnectSubModules,
	TMap<FRigElementKey, FRigElementKey>* OutRemovedConnections, bool bSetupUndo)
{
	FString ConnectorModulePath, ConnectorName;
	if (!URigHierarchy::SplitNameSpace(InConnectorKey.Name.ToString(), &ConnectorModulePath, &ConnectorName))
	{
		UE_LOG(LogControlRig, Error, TEXT("Connector %s does not contain a namespace"), *InConnectorKey.ToString());
		return false;
	}
	
	FRigModuleReference* Module = FindModule(ConnectorModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *ConnectorModulePath);
		return false;
	}

	UControlRig* RigCDO = Module->Class->GetDefaultObject<UControlRig>();
	if (!RigCDO)
	{
		UE_LOG(LogControlRig, Error, TEXT("Invalid rig module class %s"), *Module->Class->GetPathName());
		return false;
	}

	const FRigModuleConnector* ModuleConnector = RigCDO->GetRigModuleSettings().ExposedConnectors.FindByPredicate(
		[ConnectorName](FRigModuleConnector& Connector)
		{
			return Connector.Name == ConnectorName;
		});
	if (!ModuleConnector)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find connector %s in class %s"), *ConnectorName, *Module->Class->GetPathName());
		return false;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());

	if(!Model->Connections.HasConnection(InConnectorKey))
	{
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "ConnectModuleToElementTransaction", "Connect to Element"), !GIsTransacting);
		Blueprint->Modify();
	}
#endif 

	if(OutRemovedConnections)
	{
		OutRemovedConnections->Add(InConnectorKey, Model->Connections.FindTargetFromConnector(InConnectorKey));
	}
	Model->Connections.RemoveConnection(InConnectorKey);

	if (ModuleConnector->IsPrimary())
	{
		// Remove connections from module and child modules
		TArray<FRigElementKey> ConnectionsToRemove;
		for (const FModularRigSingleConnection& Connection : Model->Connections)
		{
			if (Connection.Connector.Name.ToString().StartsWith(ConnectorModulePath, ESearchCase::IgnoreCase))
			{
				ConnectionsToRemove.Add(Connection.Connector);
			}
		}
		for (const FRigElementKey& ToRemove : ConnectionsToRemove)
		{
			if(OutRemovedConnections)
			{
				OutRemovedConnections->Add(ToRemove, Model->Connections.FindTargetFromConnector(ToRemove));
			}
			Model->Connections.RemoveConnection(ToRemove);
		}
	}
	else if (!ModuleConnector->IsOptional() && bDisconnectSubModules)
	{
		// Remove connections from child modules
		TArray<FRigElementKey> ConnectionsToRemove;
		for (const FModularRigSingleConnection& Connection : Model->Connections)
		{
			FString OtherConnectorModulePath, OtherConnectorName;
			(void)URigHierarchy::SplitNameSpace(Connection.Connector.Name.ToString(), &OtherConnectorModulePath, &OtherConnectorName);
			if (OtherConnectorModulePath.StartsWith(ConnectorModulePath, ESearchCase::IgnoreCase) && OtherConnectorModulePath.Len() > ConnectorModulePath.Len())
			{
				ConnectionsToRemove.Add(Connection.Connector);
			}
		}
		for (const FRigElementKey& ToRemove : ConnectionsToRemove)
		{
			if(OutRemovedConnections)
			{
				OutRemovedConnections->Add(ToRemove, Model->Connections.FindTargetFromConnector(ToRemove));
			}
			Model->Connections.RemoveConnection(ToRemove);
		}
	}

	// todo: Make sure all the rest of the connections are still valid

	// un-parent the module if we've disconnected the primary
	if(bAutomaticReparenting)
	{
		if(ModuleConnector->IsPrimary() && !Module->IsRootModule())
		{
			(void)ReparentModule(Module->GetPath(), FString(), bSetupUndo);
		}
	}

	Notify(EModularRigNotification::ConnectionChanged, Module);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	
	return true;
}

TArray<FRigElementKey> UModularRigController::DisconnectCyclicConnectors(bool bSetupUndo)
{
	TArray<FRigElementKey> DisconnectedConnectors;

#if WITH_EDITOR
	const UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());
	check(Blueprint);

	const UModularRig* ModularRig = Cast<UModularRig>(Blueprint->GetObjectBeingDebugged());
	if (!ModularRig)
	{
		return DisconnectedConnectors;
	}
	
	const URigHierarchy* Hierarchy = ModularRig->GetHierarchy();
	if (!Hierarchy)
	{
		return DisconnectedConnectors;
	}

	TArray<FRigElementKey> ConnectorsToDisconnect;
	for (const FModularRigSingleConnection& Connection : Model->Connections)
	{
		const FString ConnectorModulePath = Hierarchy->GetModulePath(Connection.Connector);
		const FString TargetModulePath = Hierarchy->GetModulePath(Connection.Target);

		// targets in the base hierarchy are always allowed
		if(TargetModulePath.IsEmpty())
		{
			continue;
		}

		const FRigModuleReference* ConnectorModule = Model->FindModule(ConnectorModulePath);
		const FRigModuleReference* TargetModule = Model->FindModule(TargetModulePath);
		if(ConnectorModule == nullptr || TargetModule == nullptr)
		{
			continue;
		}

		if(!Model->IsModuleParentedTo(ConnectorModule, TargetModule))
		{
			ConnectorsToDisconnect.Add(Connection.Connector);
		}
	}

	for(const FRigElementKey& ConnectorToDisconnect : ConnectorsToDisconnect)
	{
		if(DisconnectConnector(ConnectorToDisconnect, false, bSetupUndo))
		{
			DisconnectedConnectors.Add(ConnectorToDisconnect);
		}
	}
#endif

	return DisconnectedConnectors;
}

bool UModularRigController::AutoConnectSecondaryConnectors(const TArray<FRigElementKey>& InConnectorKeys, bool bReplaceExistingConnections, bool bSetupUndo)
{
#if WITH_EDITOR

	UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());
	if(Blueprint == nullptr)
	{
		UE_LOG(LogControlRig, Error, TEXT("ModularRigController is not nested under blueprint."));
		return false;
	}

	const UModularRig* ModularRig = Cast<UModularRig>(Blueprint->GetObjectBeingDebugged());
	if (!ModularRig)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find debugged modular rig in %s"), *Blueprint->GetPathName());
		return false;
	}
	
	URigHierarchy* Hierarchy = ModularRig->GetHierarchy();
	if (!Hierarchy)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find hierarchy in %s"), *ModularRig->GetPathName());
		return false;
	}

	for(const FRigElementKey& ConnectorKey : InConnectorKeys)
	{
		if(ConnectorKey.Type != ERigElementType::Connector)
		{
			UE_LOG(LogControlRig, Error, TEXT("Could not find debugged modular rig in %s"), *Blueprint->GetPathName());
			return false;
		}
		const FRigConnectorElement* Connector = Hierarchy->Find<FRigConnectorElement>(ConnectorKey);
		if(Connector == nullptr)
		{
			UE_LOG(LogControlRig, Error, TEXT("Cannot find connector %s in %s"), *ConnectorKey.ToString(), *Blueprint->GetPathName());
			return false;
		}
		if(Connector->IsPrimary())
		{
			UE_LOG(LogControlRig, Warning, TEXT("Provided connector %s in %s is a primary connector. It will be skipped during auto resolval."), *ConnectorKey.ToString(), *Blueprint->GetPathName());
		}
	}

	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "AutoResolveSecondaryConnectors", "Auto-Resolve Connectors"), !GIsTransacting);
	}

	Blueprint->Modify();

	bool bResolvedAllConnectors = true;
	for(const FRigElementKey& ConnectorKey : InConnectorKeys)
	{
		const FString ModulePath = Hierarchy->GetModulePath(ConnectorKey);
		if(ModulePath.IsEmpty())
		{
			UE_LOG(LogControlRig, Error, TEXT("Connector %s has no associated module path"), *ConnectorKey.ToString());
			bResolvedAllConnectors = false;
			continue;
		}

		const FRigModuleReference* Module = Model->FindModule(ModulePath);
		if(Module == nullptr)
		{
			UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *ModulePath);
			bResolvedAllConnectors = false;
			continue;
		}

		const FRigConnectorElement* PrimaryConnector = Module->FindPrimaryConnector(Hierarchy);
		if(PrimaryConnector == nullptr)
		{
			UE_LOG(LogControlRig, Error, TEXT("Module %s has no primary connector"), *ModulePath);
			bResolvedAllConnectors = false;
			continue;
		}
		
		const FRigElementKey PrimaryConnectorKey = PrimaryConnector->GetKey();
		if(ConnectorKey == PrimaryConnectorKey)
		{
			// silently skip primary connectors
			continue;
		}
		
		if(!Model->Connections.HasConnection(PrimaryConnectorKey))
		{
			UE_LOG(LogControlRig, Warning, TEXT("Module %s's primary connector is not resolved"), *ModulePath);
			bResolvedAllConnectors = false;
			continue;
		}
		
		const UControlRig* RigCDO = Module->Class->GetDefaultObject<UControlRig>();
		if(RigCDO == nullptr)
		{
			UE_LOG(LogControlRig, Error, TEXT("Module %s has no default rig assigned"), *ModulePath);
			bResolvedAllConnectors = false;
			continue;
		}

		const UModularRigRuleManager* RuleManager = Hierarchy->GetRuleManager();
		const FRigModuleInstance* ModuleInstance = ModularRig->FindModule(Module->GetPath());
		
		if (bReplaceExistingConnections || !Model->Connections.HasConnection(ConnectorKey))
		{
			if (const FRigConnectorElement* OtherConnectorElement = Cast<FRigConnectorElement>(Hierarchy->Find(ConnectorKey)))
			{
				FModularRigResolveResult RuleResults = RuleManager->FindMatches(OtherConnectorElement, ModuleInstance, ModularRig->GetElementKeyRedirector());

				bool bFoundMatch = false;
				if (RuleResults.GetMatches().Num() == 1)
				{
					Model->Connections.AddConnection(ConnectorKey, RuleResults.GetMatches()[0].GetKey());
					Notify(EModularRigNotification::ConnectionChanged, Module);
					bFoundMatch = true;
				}
				else
				{
					for (const FRigElementResolveResult& Result : RuleResults.GetMatches())
					{
						if (Result.GetState() == ERigElementResolveState::DefaultTarget)
						{
							Model->Connections.AddConnection(ConnectorKey, Result.GetKey());
							Notify(EModularRigNotification::ConnectionChanged, Module);
							bFoundMatch = true;
							break;
						}
					}
				}

				if(!bFoundMatch)
				{
					bResolvedAllConnectors = false;
				}
			}
		}
	}

	TransactionPtr.Reset();

	return bResolvedAllConnectors;

#else
	
	return false;

#endif
}

bool UModularRigController::AutoConnectModules(const TArray<FString>& InModulePaths, bool bReplaceExistingConnections, bool bSetupUndo)
{
#if WITH_EDITOR
	TArray<FRigElementKey> ConnectorKeys;

	const UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter());
	if(Blueprint == nullptr)
	{
		UE_LOG(LogControlRig, Error, TEXT("ModularRigController is not nested under blueprint."));
		return false;
	}

	const UModularRig* ModularRig = Cast<UModularRig>(Blueprint->GetObjectBeingDebugged());
	if (!ModularRig)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find debugged modular rig in %s"), *Blueprint->GetPathName());
		return false;
	}
	
	const URigHierarchy* Hierarchy = ModularRig->GetHierarchy();
	if (!Hierarchy)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find hierarchy in %s"), *ModularRig->GetPathName());
		return false;
	}

	for(const FString& ModulePath : InModulePaths)
	{
		const FRigModuleReference* Module = FindModule(ModulePath);
		if (!Module)
		{
			UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *ModulePath);
			return false;
		}

		const TArray<const FRigConnectorElement*> Connectors = Module->FindConnectors(Hierarchy);
		for(const FRigConnectorElement* Connector : Connectors)
		{
			if(Connector->IsSecondary())
			{
				ConnectorKeys.Add(Connector->GetKey());
			}
		}
	}

	return AutoConnectSecondaryConnectors(ConnectorKeys, bReplaceExistingConnections, bSetupUndo);

#else

	return false;

#endif
}

bool UModularRigController::SetConfigValueInModule(const FString& InModulePath, const FName& InVariableName, const FString& InValue, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return false;
	}

	if (!Module->Class.IsValid())
	{
		UE_LOG(LogControlRig, Error, TEXT("Class defined in module %s is not valid"), *InModulePath);
		return false;
	}

	const FProperty* Property = Module->Class->FindPropertyByName(InVariableName);
	if (!Property)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find variable %s in module %s"), *InVariableName.ToString(), *InModulePath);
		return false;
	}

	if (Property->HasAllPropertyFlags(CPF_BlueprintReadOnly))
	{
		UE_LOG(LogControlRig, Error, TEXT("The target variable %s in module %s is read only"), *InVariableName.ToString(), *InModulePath);
		return false;
	}

#if WITH_EDITOR
	TArray<uint8, TAlignedHeapAllocator<16>> TempStorage;
	TempStorage.AddZeroed(Property->GetSize());
	uint8* TempMemory = TempStorage.GetData();
	Property->InitializeValue(TempMemory);

	if (!FBlueprintEditorUtils::PropertyValueFromString_Direct(Property, InValue, TempMemory))
	{
		UE_LOG(LogControlRig, Error, TEXT("Value %s for variable %s in module %s is not valid"), *InValue, *InVariableName.ToString(), *InModulePath);
		return false;
	}

	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "ConfigureModuleValueTransaction", "Configure Module Value"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif 

	Module->ConfigValues.FindOrAdd(InVariableName) = InValue;

	Notify(EModularRigNotification::ModuleConfigValueChanged, Module);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return true;
}

TArray<FString> UModularRigController::GetPossibleBindings(const FString& InModulePath, const FName& InVariableName)
{
	TArray<FString> PossibleBindings;
	const FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		return PossibleBindings;
	}

	if (!Module->Class.IsValid())
	{
		return PossibleBindings;
	}

	const FProperty* TargetProperty = Module->Class->FindPropertyByName(InVariableName);
	if (!TargetProperty)
	{
		return PossibleBindings;
	}

	if (TargetProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_DisableEditOnInstance))
	{
		return PossibleBindings;
	}

	// Add possible blueprint variables
	if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
	{
		TArray<FRigVMExternalVariable> Variables = Blueprint->GeneratedClass->GetDefaultObject<UControlRig>()->GetExternalVariables();
		for (const FRigVMExternalVariable& Variable : Variables)
		{
			FText ErrorMessage;
			const FString VariableName = Variable.Name.ToString();
			if (CanBindModuleVariable(InModulePath, InVariableName, VariableName, ErrorMessage))
			{
				PossibleBindings.Add(VariableName);
			}
		}
	}

	// Add possible module variables
	const FString InvalidModulePrefix = InModulePath + UModularRig::NamespaceSeparator;
	Model->ForEachModule([this, &PossibleBindings, InModulePath, InVariableName, InvalidModulePrefix](const FRigModuleReference* InModule) -> bool
	{
		const FString CurModulePath = InModule->GetPath();
		if (InModulePath != CurModulePath && !CurModulePath.StartsWith(InvalidModulePrefix, ESearchCase::IgnoreCase))
		{
			if (!InModule->Class.IsValid())
			{
				InModule->Class.LoadSynchronous();
			}
			if (InModule->Class.IsValid())
			{
				TArray<FRigVMExternalVariable> Variables = InModule->Class->GetDefaultObject<UControlRig>()->GetExternalVariables();
			   for (const FRigVMExternalVariable& Variable : Variables)
			   {
				   FText ErrorMessage;
				   const FString SourceVariablePath = URigHierarchy::JoinNameSpace(CurModulePath, Variable.Name.ToString());
				   if (CanBindModuleVariable(InModulePath, InVariableName, SourceVariablePath, ErrorMessage))
				   {
					   PossibleBindings.Add(SourceVariablePath);
				   }
			   }
			}
		}		
		return true;
	});

	return PossibleBindings;
}

bool UModularRigController::CanBindModuleVariable(const FString& InModulePath, const FName& InVariableName, const FString& InSourcePath, FText& OutErrorMessage)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find module %s"), *InModulePath));
		return false;
	}

	if (!Module->Class.IsValid())
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Class defined in module %s is not valid"), *InModulePath));
		return false;
	}

	const FProperty* TargetProperty = Module->Class->FindPropertyByName(InVariableName);
	if (!TargetProperty)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find variable %s in module %s"), *InVariableName.ToString(), *InModulePath));
		return false;
	}

	if (TargetProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_DisableEditOnInstance))
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("The target variable %s in module %s is read only"), *InVariableName.ToString(), *InModulePath));
		return false;
	}

	FString SourceModulePath, SourceVariableName = InSourcePath;
	(void)URigHierarchy::SplitNameSpace(InSourcePath, &SourceModulePath, &SourceVariableName);

	FRigModuleReference* SourceModule = nullptr;
	if (!SourceModulePath.IsEmpty())
	{
		SourceModule = FindModule(SourceModulePath);
		if (!SourceModule)
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find source module %s"), *SourceModulePath));
			return false;
		}

		if (SourceModulePath.StartsWith(InModulePath, ESearchCase::IgnoreCase))
		{
			OutErrorMessage = FText::FromString(FString::Printf(TEXT("Cannot bind variable of module %s to a variable of module %s because the source module is a child of the target module"), *InModulePath, *SourceModulePath));
			return false;
		}
	}

	const FProperty* SourceProperty = nullptr;
	if (SourceModule)
	{
		SourceProperty = SourceModule->Class->FindPropertyByName(*SourceVariableName);
	}
	else
	{
		if(const UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			SourceProperty = Blueprint->GeneratedClass->FindPropertyByName(*SourceVariableName);
		}
	}
	if (!SourceProperty)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Could not find source variable %s"), *InSourcePath));
		return false;
	}

	FString SourcePath = (SourceModulePath.IsEmpty()) ? SourceVariableName : URigHierarchy::JoinNameSpace(SourceModulePath, SourceVariableName);
	if (!RigVMTypeUtils::AreCompatible(SourceProperty, TargetProperty))
	{
		FString TargetPath = FString::Printf(TEXT("%s.%s"), *InModulePath, *InVariableName.ToString());
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Property %s of type %s and %s of type %s are not compatible"), *SourcePath, *SourceProperty->GetCPPType(), *TargetPath, *TargetProperty->GetCPPType()));
		return false;
	}

	return true;
}

bool UModularRigController::BindModuleVariable(const FString& InModulePath, const FName& InVariableName, const FString& InSourcePath, bool bSetupUndo)
{
	FText ErrorMessage;
	if (!CanBindModuleVariable(InModulePath, InVariableName, InSourcePath, ErrorMessage))
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not bind module variable %s : %s"), *URigHierarchy::JoinNameSpace(InModulePath, InVariableName.ToString()), *ErrorMessage.ToString());
		return false;
	}
	
	FRigModuleReference* Module = FindModule(InModulePath);
	const FProperty* TargetProperty = Module->Class->FindPropertyByName(InVariableName);

	FString SourceModulePath, SourceVariableName = InSourcePath;
	(void)URigHierarchy::SplitNameSpace(InSourcePath, &SourceModulePath, &SourceVariableName);

	FRigModuleReference* SourceModule = nullptr;
	if (!SourceModulePath.IsEmpty())
	{
		SourceModule = FindModule(SourceModulePath);
	}

	const FProperty* SourceProperty = nullptr;
	if (SourceModule)
	{
		SourceProperty = SourceModule->Class->FindPropertyByName(*SourceVariableName);
	}
	else
	{
		if(const UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			SourceProperty = Blueprint->GeneratedClass->FindPropertyByName(*SourceVariableName);
		}
	}

	FString SourcePath = (SourceModulePath.IsEmpty()) ? SourceVariableName : URigHierarchy::JoinNameSpace(SourceModulePath, SourceVariableName);

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "BindModuleVariableTransaction", "Bind Module Variable"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	FString& SourceStr = Module->Bindings.FindOrAdd(InVariableName);
	SourceStr = SourcePath;

	Notify(EModularRigNotification::ModuleConfigValueChanged, Module);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return true;
}

bool UModularRigController::UnBindModuleVariable(const FString& InModulePath, const FName& InVariableName, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return false;
	}

	if (!Module->Bindings.Contains(InVariableName))
	{
		UE_LOG(LogControlRig, Error, TEXT("Variable %s in module %s is not bound"), *InVariableName.ToString(), *InModulePath);
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "BindModuleVariableTransaction", "Bind Module Variable"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	Module->Bindings.Remove(InVariableName);

	Notify(EModularRigNotification::ModuleConfigValueChanged, Module);

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return true;
}

bool UModularRigController::DeleteModule(const FString& InModulePath, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return false;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "DeleteModuleTransaction", "Delete Module"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	(void)DeselectModule(Module->GetPath());

	// Delete children
	TArray<FString> ChildrenPaths;
	Algo::Transform(Module->CachedChildren, ChildrenPaths, [](const FRigModuleReference* Child){ return Child->GetPath(); });
	for (const FString& ChildPath : ChildrenPaths)
	{
		DeleteModule(ChildPath, bSetupUndo);
	}

	Model->DeletedModules.Add(*Module);
	Model->Modules.RemoveSingle(*Module);
	Model->UpdateCachedChildren();
	UpdateShortNames();

	// Fix connections
	{
		TArray<FRigElementKey> ToRemove;
		for (FModularRigSingleConnection& Connection : Model->Connections)
		{
			FString ConnectionModulePath, ConnectionName;
			(void)URigHierarchy::SplitNameSpace(Connection.Connector.Name.ToString(), &ConnectionModulePath, &ConnectionName);

			if (ConnectionModulePath == InModulePath)
			{
				ToRemove.Add(Connection.Connector);
			}

			FString TargetModulePath, TargetName;
			(void)URigHierarchy::SplitNameSpace(Connection.Target.Name.ToString(), &TargetModulePath, &TargetName);
			if (TargetModulePath == InModulePath)
			{
				ToRemove.Add(Connection.Connector);
			}
		}
		for (FRigElementKey& KeyToRemove : ToRemove)
		{
			Model->Connections.RemoveConnection(KeyToRemove);
		}
		Model->Connections.UpdateFromConnectionList();
	}

	// Fix bindings
	for (FRigModuleReference& Reference : Model->Modules)
	{
		Reference.Bindings = Reference.Bindings.FilterByPredicate([InModulePath](const TPair<FName, FString>& Binding)
		{
			FString ModulePath, VariableName = Binding.Value;
			(void)URigHierarchy::SplitNameSpace(Binding.Value, &ModulePath, &VariableName);
			if (ModulePath == InModulePath)
			{
				return false;
			}
			return true;
		});
	}

	Notify(EModularRigNotification::ModuleRemoved, &Model->DeletedModules.Last());

	Model->DeletedModules.Reset();

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	return false;
}

FString UModularRigController::RenameModule(const FString& InModulePath, const FName& InNewName, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return FString();
	}

	const FString OldName = Module->Name.ToString();
	const FString NewName = InNewName.ToString();
	if (OldName.Equals(NewName))
	{
		return Module->GetPath();
	}

	FText ErrorMessage;
	if (!CanRenameModule(InModulePath, InNewName, ErrorMessage))
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not rename module %s: %s"), *InModulePath, *ErrorMessage.ToString());
		return FString();
	}
	
#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "RenameModuleTransaction", "Rename Module"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif
	
	const FString OldPath = (Module->ParentPath.IsEmpty()) ? OldName : URigHierarchy::JoinNameSpace(Module->ParentPath, OldName);
	const FString NewPath = (Module->ParentPath.IsEmpty()) ? *NewName :  URigHierarchy::JoinNameSpace(Module->ParentPath, NewName);

	const int32 SelectionIndex = Model->SelectedModulePaths.Find(OldPath);
	if(SelectionIndex != INDEX_NONE)
	{
		Notify(EModularRigNotification::ModuleDeselected, Module);
	}

	Module->PreviousName = Module->Name;
	Module->Name = InNewName;
	TArray<FRigModuleReference*> Children;
	Children.Append(Module->CachedChildren);
	for (int32 i=0; i<Children.Num(); ++i)
	{
		FRigModuleReference* Child = Children[i];
		Child->ParentPath.ReplaceInline(*OldPath, *NewPath);

		Children.Append(Child->CachedChildren);
	}

	// Fix connections
	{
		const FString OldNamespace = OldPath + TEXT(":");
		const FString NewNamespace = NewPath + TEXT(":");
		for (FModularRigSingleConnection& Connection : Model->Connections)
		{
			if (Connection.Connector.Name.ToString().StartsWith(OldNamespace, ESearchCase::IgnoreCase))
			{
				Connection.Connector.Name = *FString::Printf(TEXT("%s%s"), *NewNamespace, *Connection.Connector.Name.ToString().RightChop(OldNamespace.Len()));
			}
			if (Connection.Target.Name.ToString().StartsWith(OldNamespace, ESearchCase::IgnoreCase))
			{
				Connection.Target.Name = *FString::Printf(TEXT("%s%s"), *NewNamespace, *Connection.Target.Name.ToString().RightChop(OldNamespace.Len()));
			}
		}
		Model->Connections.UpdateFromConnectionList();
	}

	// Fix bindings
	for (FRigModuleReference& Reference : Model->Modules)
	{
		for (TPair<FName, FString>& Binding : Reference.Bindings)
		{
			FString ModulePath, VariableName = Binding.Value;
			(void)URigHierarchy::SplitNameSpace(Binding.Value, &ModulePath, &VariableName);
			if (ModulePath == OldPath)
			{
				Binding.Value = URigHierarchy::JoinNameSpace(NewPath, VariableName);
			}
		};
	}

	UpdateShortNames();
	Notify(EModularRigNotification::ModuleRenamed, Module);

	if(SelectionIndex != INDEX_NONE)
	{
		Model->SelectedModulePaths[SelectionIndex] = NewPath;
		Notify(EModularRigNotification::ModuleSelected, Module);
	}

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	
	return NewPath;
}

bool UModularRigController::CanRenameModule(const FString& InModulePath, const FName& InNewName, FText& OutErrorMessage) const
{
	if (InNewName.IsNone() || InNewName.ToString().IsEmpty())
	{
		OutErrorMessage = FText::FromString(TEXT("Name is empty."));
		return false;
	}

	if(InNewName.ToString().Contains(UModularRig::NamespaceSeparator))
	{
		OutErrorMessage = NSLOCTEXT("ModularRigController", "NameContainsNamespaceSeparator", "Name contains namespace separator ':'.");
		return false;
	}

	const FRigModuleReference* Module = const_cast<UModularRigController*>(this)->FindModule(InModulePath);
	if (!Module)
	{
		OutErrorMessage = FText::FromString(FString::Printf(TEXT("Module %s not found."), *InModulePath));
		return false;
	}

	FString ErrorMessage;
	if(!IsNameAvailable(Module->ParentPath, InNewName, &ErrorMessage))
	{
		OutErrorMessage = FText::FromString(ErrorMessage);
		return false;
	}
	return true;
}

FString UModularRigController::ReparentModule(const FString& InModulePath, const FString& InNewParentModulePath, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return FString();
	}

	const FRigModuleReference* NewParentModule = FindModule(InNewParentModulePath);
	const FString PreviousParentPath = Module->ParentPath;
	const FString ParentPath = (NewParentModule) ? NewParentModule->GetPath() : FString();
	if(PreviousParentPath.Equals(ParentPath, ESearchCase::IgnoreCase))
	{
		return Module->GetPath();
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "ReparentModuleTransaction", "Reparent Module"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	// Reparent or unparent children
	const FString OldPath = Module->GetPath();

	const int32 SelectionIndex = Model->SelectedModulePaths.Find(OldPath);
	if(SelectionIndex != INDEX_NONE)
	{
		Notify(EModularRigNotification::ModuleDeselected, Module);
	}
	
	Module->PreviousParentPath = Module->ParentPath;
	Module->PreviousName = Module->Name;
	Module->ParentPath = (NewParentModule) ? NewParentModule->GetPath() : FString();
	Module->Name = GetSafeNewName(Module->ParentPath, FRigName(Module->Name));
	const FString NewPath = Module->GetPath();

	// Fix all the subtree namespaces
	TArray<FRigModuleReference*> SubTree = Module->CachedChildren;
	for (int32 Index=0; Index<SubTree.Num(); ++Index)
	{
		SubTree[Index]->ParentPath.ReplaceInline(*OldPath, *NewPath);
		SubTree.Append(SubTree[Index]->CachedChildren);
	}

	Model->UpdateCachedChildren();
	UpdateShortNames();

	// Fix connections
	{
		for (FModularRigSingleConnection& Connection : Model->Connections)
		{
			if (Connection.Connector.Name.ToString().StartsWith(OldPath, ESearchCase::IgnoreCase))
			{
				Connection.Connector.Name = *FString::Printf(TEXT("%s%s"), *NewPath, *Connection.Connector.Name.ToString().RightChop(OldPath.Len()));
			}
			if (Connection.Target.Name.ToString().StartsWith(OldPath, ESearchCase::IgnoreCase))
			{
				Connection.Target.Name = *FString::Printf(TEXT("%s%s"), *NewPath, *Connection.Target.Name.ToString().RightChop(OldPath.Len()));
			}
		}
		Model->Connections.UpdateFromConnectionList();
	}

	// Fix bindings
	for (FRigModuleReference& Reference : Model->Modules)
	{
		const FString ReferencePath = Reference.GetPath();
		for (TPair<FName, FString>& Binding : Reference.Bindings)
		{
			FString ModulePath, VariableName = Binding.Value;
			(void)URigHierarchy::SplitNameSpace(Binding.Value, &ModulePath, &VariableName);
			if (ModulePath == OldPath)
			{
				Binding.Value = URigHierarchy::JoinNameSpace(NewPath, VariableName);
				ModulePath = NewPath;
			}

			// Remove any child dependency
			if (ModulePath.Contains(ReferencePath))
			{
				UE_LOG(LogControlRig, Warning, TEXT("Binding lost due to source %s contained in child module of %s"), *Binding.Value, *ReferencePath);
				Binding.Value.Reset();
			}
		};

		Reference.Bindings = Reference.Bindings.FilterByPredicate([](const TPair<FName, FString>& Binding)
		{
			return !Binding.Value.IsEmpty();
		});
	}

	// Fix connectors in the hierarchies
	

	// since we've reparented the module now we should clear out all connectors which are cyclic
	(void)DisconnectCyclicConnectors(bSetupUndo);

	Notify(EModularRigNotification::ModuleReparented, Module);

	if(SelectionIndex != INDEX_NONE)
	{
		Model->SelectedModulePaths[SelectionIndex] = NewPath;
		Notify(EModularRigNotification::ModuleSelected, Module);
	}

#if WITH_EDITOR
 	TransactionPtr.Reset();
#endif
	
	return NewPath;
}

FString UModularRigController::MirrorModule(const FString& InModulePath, const FRigVMMirrorSettings& InSettings, bool bSetupUndo)
{
	FRigModuleReference* OriginalModule = FindModule(InModulePath);
	if (!OriginalModule || !OriginalModule->Class.IsValid())
	{
		return FString();
	}

	FString NewModuleName = OriginalModule->Name.ToString();
	if (!InSettings.SearchString.IsEmpty())
	{
		NewModuleName = NewModuleName.Replace(*InSettings.SearchString, *InSettings.ReplaceString, ESearchCase::CaseSensitive);
		NewModuleName = GetSafeNewName(OriginalModule->ParentPath, FRigName(NewModuleName)).ToString();
	}

	// Before any changes, gather all the information we need from the OriginalModule, as the pointer might become invalid afterwards
	const TMap<FRigElementKey, FRigElementKey> OriginalConnectionMap = Model->Connections.GetModuleConnectionMap(InModulePath);
	const TMap<FName, FString> OriginalBindings = OriginalModule->Bindings;
	const TSubclassOf<UControlRig> OriginalClass = OriginalModule->Class.Get();
	const FString OriginalParentPath = OriginalModule->ParentPath;
	const TMap<FName, FString> OriginalConfigValues = OriginalModule->ConfigValues;

	FModularRigControllerCompileBracketScope CompileBracketScope(this);

	FString NewModulePath = AddModule(*NewModuleName, OriginalClass, OriginalParentPath, bSetupUndo);
	FRigModuleReference* NewModule = FindModule(NewModulePath);
	if (!NewModule)
	{
		return FString();
	}

	for (const TPair<FRigElementKey, FRigElementKey>& Pair : OriginalConnectionMap)
	{
		FString OriginalTargetPath = Pair.Value.Name.ToString();
		FString NewTargetPath = OriginalTargetPath.Replace(*InSettings.SearchString, *InSettings.ReplaceString, ESearchCase::CaseSensitive);
		FRigElementKey NewTargetKey(*NewTargetPath, Pair.Value.Type);

		FString NewConnectorPath = URigHierarchy::JoinNameSpace(NewModulePath, Pair.Key.Name.ToString());
		FRigElementKey NewConnectorKey(*NewConnectorPath, ERigElementType::Connector);
		ConnectConnectorToElement(NewConnectorKey, NewTargetKey, bSetupUndo, false, false);

		// Path might change after connecting
		NewModulePath = NewModule->GetPath();
	}

	for (const TPair<FName, FString>& Pair : OriginalBindings)
	{
		FString NewSourcePath = Pair.Value.Replace(*InSettings.SearchString, *InSettings.ReplaceString, ESearchCase::CaseSensitive);
		BindModuleVariable(NewModulePath, Pair.Key, NewSourcePath, bSetupUndo);
	}

	TSet<FName> ConfigValueSet;
#if WITH_EDITOR
	for (TFieldIterator<FProperty> PropertyIt(OriginalClass); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		
		// skip advanced properties for now
		if (Property->HasAnyPropertyFlags(CPF_AdvancedDisplay))
		{
			continue;
		}

		// skip non-public properties for now
		const bool bIsPublic = Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst);
		const bool bIsInstanceEditable = !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
		if(!bIsPublic || !bIsInstanceEditable)
		{
			continue;
		}

		const FString CPPType = Property->GetCPPType();
		bool bIsVector;
		if (CPPType == TEXT("FVector"))
		{
			bIsVector = true;
		}
		else if (CPPType == TEXT("FTransform"))
		{
			bIsVector = false;
		}
		else
		{
			continue;
		}

		FString NewValueStr;
		if (const FString* OriginalValue = OriginalConfigValues.Find(Property->GetFName()))
		{
			if (bIsVector)
			{
				FVector Value;
				FBlueprintEditorUtils::PropertyValueFromString_Direct(Property, *OriginalValue, (uint8*)&Value);
				Value = InSettings.MirrorVector(Value);
				FBlueprintEditorUtils::PropertyValueToString_Direct(Property, (uint8*)&Value, NewValueStr, nullptr);
			}
			else
			{
				FTransform Value;
				FBlueprintEditorUtils::PropertyValueFromString_Direct(Property, *OriginalValue, (uint8*)&Value);
				Value = InSettings.MirrorTransform(Value);
				FBlueprintEditorUtils::PropertyValueToString_Direct(Property, (uint8*)&Value, NewValueStr, nullptr);
			}
		}
		else
		{
			if (UControlRig* CDO = OriginalClass->GetDefaultObject<UControlRig>())
			{
				if (bIsVector)
				{
					FVector NewVector = *Property->ContainerPtrToValuePtr<FVector>(CDO);
					NewVector = InSettings.MirrorVector(NewVector);
					FBlueprintEditorUtils::PropertyValueToString_Direct(Property, (uint8*)&NewVector, NewValueStr, nullptr);
				}
				else
				{
					FTransform NewTransform = *Property->ContainerPtrToValuePtr<FTransform>(CDO);
					NewTransform = InSettings.MirrorTransform(NewTransform);
					FBlueprintEditorUtils::PropertyValueToString_Direct(Property, (uint8*)&NewTransform, NewValueStr, nullptr);
				}
			}
		}

		ConfigValueSet.Add(Property->GetFName());
		SetConfigValueInModule(NewModulePath, Property->GetFName(), NewValueStr, bSetupUndo);
	}
#endif

	// Add any other config value that was set in the original module, but was not mirrored
	for (const TPair<FName, FString>& Pair : OriginalConfigValues)
	{
		if (!ConfigValueSet.Contains(Pair.Key))
		{
			SetConfigValueInModule(NewModulePath, Pair.Key, Pair.Value, bSetupUndo);
		}
	}
	
	return NewModulePath;
}

bool UModularRigController::SetModuleShortName(const FString& InModulePath, const FString& InNewShortName, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return false;
	}

	FText ErrorMessage;
	if (!CanSetModuleShortName(InModulePath, InNewShortName, ErrorMessage))
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not rename module %s: %s"), *InModulePath, *ErrorMessage.ToString());
		return false;
	}

	const FString OldShortName = Module->GetShortName();
	const FString NewShortName = InNewShortName;
	if (OldShortName.Equals(NewShortName))
	{
		return true;
	}
	
#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "SetModuleShortNameTransaction", "Set Module Display Name"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif
	
	Module->ShortName = InNewShortName;
	Module->bShortNameBasedOnPath = false;

	Notify(EModularRigNotification::ModuleShortNameChanged, Module);

	// update all other display named to avoid collision
	UpdateShortNames();

#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	
	return true;
}

bool UModularRigController::CanSetModuleShortName(const FString& InModulePath, const FString& InNewShortName, FText& OutErrorMessage) const
{
	FString ErrorMessage;
	if(!IsShortNameAvailable(FRigName(InNewShortName), &ErrorMessage))
	{
		OutErrorMessage = FText::FromString(ErrorMessage);
		return false;
	}
	return true;
}

bool UModularRigController::SwapModuleClass(const FString& InModulePath, TSubclassOf<UControlRig> InNewClass, bool bSetupUndo)
{
	FRigModuleReference* Module = FindModule(InModulePath);
	if (!Module)
	{
		UE_LOG(LogControlRig, Error, TEXT("Could not find module %s"), *InModulePath);
		return false;
	}

	if (!InNewClass)
	{
		UE_LOG(LogControlRig, Error, TEXT("Invalid InClass"));
		return false;
	}

	UControlRig* ClassDefaultObject = InNewClass->GetDefaultObject<UControlRig>();
	if (!ClassDefaultObject->IsRigModule())
	{
		UE_LOG(LogControlRig, Error, TEXT("Class %s is not a rig module"), *InNewClass->GetClassPathName().ToString());
		return false;
	}

	if (Module->Class.Get() == InNewClass)
	{
		// Nothing to do here
		return true;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "SwapModuleClassTransaction", "Swap Module Class"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	Module->Class = InNewClass;

	// Remove invalid connectors/connections
	{
		const TArray<FModularRigSingleConnection>& Connections = Model->Connections.GetConnectionList();
		const UControlRig* CDO = InNewClass->GetDefaultObject<UControlRig>();
		const TArray<FRigModuleConnector>& ExposedConnectors = CDO->GetRigModuleSettings().ExposedConnectors;

		TArray<FRigElementKey> ConnectionsToRemove;
		for (const FModularRigSingleConnection& Connection : Connections)
		{
			FString Namespace, ConnectorName;
			URigHierarchy::SplitNameSpace(Connection.Connector.Name.ToString(), &Namespace, &ConnectorName);
			if (Namespace.Equals(InModulePath))
			{
				if (!ExposedConnectors.ContainsByPredicate([ConnectorName](const FRigModuleConnector& Exposed)
				{
				   return Exposed.Name == ConnectorName;
				}))
				{
					ConnectionsToRemove.Add(Connection.Connector);
					continue;
				}

				FText ErrorMessage;
				if (!CanConnectConnectorToElement(Connection.Connector, Connection.Target, ErrorMessage))
				{
					ConnectionsToRemove.Add(Connection.Connector);
				}
			}
		}

		for (const FRigElementKey& ToRemove : ConnectionsToRemove)
		{
			DisconnectConnector(ToRemove, false, bSetupUndo);
		}
	}

	// Remove config values and bindings that are not supported anymore
	RefreshModuleVariables();

	Notify(EModularRigNotification::ModuleClassChanged, Module);
	
#if WITH_EDITOR
	TransactionPtr.Reset();
#endif

	return true;
}

bool UModularRigController::SwapModulesOfClass(TSubclassOf<UControlRig> InOldClass, TSubclassOf<UControlRig> InNewClass, bool bSetupUndo)
{
#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "SwapModulesOfClassTransaction", "Swap Modules of Class"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif
	
	Model->ForEachModule([this, InOldClass, InNewClass, bSetupUndo](const FRigModuleReference* Module) -> bool
	{
		if (Module->Class.Get() == InOldClass)
		{
			SwapModuleClass(Module->GetPath(), InNewClass, bSetupUndo);
		}
		return true;
	});
	
#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
	
	return true;
}

bool UModularRigController::SelectModule(const FString& InModulePath, const bool InSelected)
{
	const bool bCurrentlySelected = Model->SelectedModulePaths.Contains(InModulePath);
	if(bCurrentlySelected == InSelected)
	{
		return false;
	}

	const FRigModuleReference* Module = FindModule(InModulePath);
	if(Module == nullptr)
	{
		return false;
	}

	if(InSelected)
	{
		Model->SelectedModulePaths.Add(InModulePath);
	}
	else
	{
		Model->SelectedModulePaths.Remove(InModulePath);
	}

	Notify(InSelected ? EModularRigNotification::ModuleSelected : EModularRigNotification::ModuleDeselected, Module);
	return true;
}

bool UModularRigController::DeselectModule(const FString& InModulePath)
{
	return SelectModule(InModulePath, false);
}

bool UModularRigController::SetModuleSelection(const TArray<FString>& InModulePaths)
{
	bool bResult = false;
	const TArray<FString> OldSelection = GetSelectedModules();

	for(const FString& PreviouslySelectedModule : OldSelection)
	{
		if(!InModulePaths.Contains(PreviouslySelectedModule))
		{
			if(DeselectModule(PreviouslySelectedModule))
			{
				bResult = true;
			}
		}
	}
	for(const FString& NewModuleToSelect : InModulePaths)
	{
		if(!OldSelection.Contains(NewModuleToSelect))
		{
			if(SelectModule(NewModuleToSelect))
			{
				bResult = true;
			}
		}
	}

	return bResult;
}

TArray<FString> UModularRigController::GetSelectedModules() const
{
	return Model->SelectedModulePaths;
}

void UModularRigController::RefreshModuleVariables(bool bSetupUndo)
{
	Model->ForEachModule([this, bSetupUndo](const FRigModuleReference* Element) -> bool
	{
		TGuardValue<bool> NotificationsGuard(bSuspendNotifications, true);
		RefreshModuleVariables(Element, bSetupUndo);
		return true;
	});
}

void UModularRigController::RefreshModuleVariables(const FRigModuleReference* InModule, bool bSetupUndo)
{
	if (!InModule)
	{
		return;
	}
	
	// avoid dead class pointers
	const UClass* ModuleClass = InModule->Class.Get();
	if(ModuleClass == nullptr)
	{
		return;
	}

	// Make sure the provided module belongs to our ModularRigModel
	const FString& ModulePath = InModule->GetPath();
	FRigModuleReference* Module = FindModule(ModulePath);
	if (Module != InModule)
	{
		return;
	}

#if WITH_EDITOR
	TSharedPtr<FScopedTransaction> TransactionPtr;
	if (bSetupUndo)
	{
		TransactionPtr = MakeShared<FScopedTransaction>(NSLOCTEXT("ModularRigController", "RefreshModuleVariablesTransaction", "Refresh Module Variables"), !GIsTransacting);
		if(UBlueprint* Blueprint = Cast<UBlueprint>(GetOuter()))
		{
			Blueprint->Modify();
		}
	}
#endif

	for (TFieldIterator<FProperty> PropertyIt(ModuleClass); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;
		
		// remove advanced, private or not editable properties
		const bool bIsAdvanced = Property->HasAnyPropertyFlags(CPF_AdvancedDisplay);
		const bool bIsPublic = Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst);
		const bool bIsInstanceEditable = !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
		if (bIsAdvanced || !bIsPublic || !bIsInstanceEditable)
		{
			Module->ConfigValues.Remove(Property->GetFName());
			Module->Bindings.Remove(Property->GetFName());
		}
	}

	// Make sure all the types are valid
	const TMap<FName, FString> ConfigValues = Module->ConfigValues;
	const TMap<FName, FString> Bindings = Module->Bindings;
	Module->ConfigValues.Reset();
	Module->Bindings.Reset();
	for (const TPair<FName, FString>& Pair : ConfigValues)
	{
		SetConfigValueInModule(ModulePath, Pair.Key, Pair.Value, false);
	}
	for (const TPair<FName, FString>& Pair : Bindings)
	{
		BindModuleVariable(ModulePath, Pair.Key, Pair.Value, false);
	}

	// If the module is the source of another module's binding, make sure it is still a valid binding
	Model->ForEachModule([this, InModule, ModulePath, ModuleClass](const FRigModuleReference* OtherModule) -> bool
	{
		if (InModule == OtherModule)
		{
			return true;
		}
		TArray<FName> BindingsToRemove;
		for (const TPair<FName, FString>& Binding : OtherModule->Bindings)
		{
			FString BindingModulePath, VariableName = Binding.Value;
			(void)URigHierarchy::SplitNameSpace(Binding.Value, &BindingModulePath, &VariableName);
			if (BindingModulePath == ModulePath)
			{
				if (const FProperty* Property = ModuleClass->FindPropertyByName(*VariableName))
				{
					// remove advanced, private or not editable properties
					const bool bIsAdvanced = Property->HasAnyPropertyFlags(CPF_AdvancedDisplay);
					const bool bIsPublic = Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst);
					const bool bIsInstanceEditable = !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
					if (bIsAdvanced || !bIsPublic || !bIsInstanceEditable)
					{
						BindingsToRemove.Add(Binding.Key);
					}
					else 
					{
						FText ErrorMessage;
						if (!CanBindModuleVariable(OtherModule->GetPath(), Binding.Key, Binding.Value, ErrorMessage))
						{
							BindingsToRemove.Add(Binding.Key);
						}
					}
				}
			}
		}

		for (const FName& ToRemove : BindingsToRemove)
		{
			UnBindModuleVariable(OtherModule->GetPath(), ToRemove);
		}
		return true;
	});
	
#if WITH_EDITOR
	TransactionPtr.Reset();
#endif
}

void UModularRigController::SanitizeName(FRigName& InOutName, bool bAllowNameSpaces)
{
	// Sanitize the name
	FString SanitizedNameString = InOutName.GetName();
	bool bChangedSomething = false;
	for (int32 i = 0; i < SanitizedNameString.Len(); ++i)
	{
		TCHAR& C = SanitizedNameString[i];

		const bool bGoodChar = FChar::IsAlpha(C) ||					 // Any letter
			(C == '_') || (C == '-') || (C == '.') || (C == '|') ||	 // _  - .  | anytime
			(FChar::IsDigit(C)) ||									 // 0-9 anytime
			((i > 0) && (C== ' '));									 // Space after the first character to support virtual bones

		if (!bGoodChar)
		{
			if(bAllowNameSpaces && C == ':')
			{
				continue;
			}
			
			C = '_';
			bChangedSomething = true;
		}
	}

	if (SanitizedNameString.Len() > GetMaxNameLength())
	{
		SanitizedNameString.LeftChopInline(SanitizedNameString.Len() - GetMaxNameLength());
		bChangedSomething = true;
	}

	if(bChangedSomething)
	{
		InOutName.SetName(SanitizedNameString);
	}
}

FRigName UModularRigController::GetSanitizedName(const FRigName& InName, bool bAllowNameSpaces)
{
	FRigName Name = InName;
	SanitizeName(Name, bAllowNameSpaces);
	return Name;
}

bool UModularRigController::IsNameAvailable(const FString& InParentModulePath, const FRigName& InDesiredName, FString* OutErrorMessage) const
{
	const FRigName DesiredName = GetSanitizedName(InDesiredName, false);
	if(DesiredName != InDesiredName)
	{
		if(OutErrorMessage)
		{
			static const FString ContainsInvalidCharactersMessage = TEXT("Name contains invalid characters.");
			*OutErrorMessage = ContainsInvalidCharactersMessage;
		}
		return false;
	}

	TArray<FRigModuleReference*>* Children = &Model->RootModules;
	if (!InParentModulePath.IsEmpty())
	{
		if (FRigModuleReference* Parent = const_cast<UModularRigController*>(this)->FindModule(InParentModulePath))
		{
			Children = &Parent->CachedChildren;
		}
	}

	for (const FRigModuleReference* Child : *Children)
	{
		if (FRigName(Child->Name).Equals(DesiredName, ESearchCase::IgnoreCase))
		{
			if(OutErrorMessage)
			{
				static const FString NameAlreadyInUse = TEXT("This name is already in use.");
				*OutErrorMessage = NameAlreadyInUse;
			}
			return false;
		}
	}
	return true;
}

bool UModularRigController::IsShortNameAvailable(const FRigName& InDesiredShortName, FString* OutErrorMessage) const
{
	const FRigName DesiredShortName = GetSanitizedName(InDesiredShortName, false);
	if(DesiredShortName != InDesiredShortName)
	{
		if(OutErrorMessage)
		{
			static const FString ContainsInvalidCharactersMessage = TEXT("Display Name contains invalid characters.");
			*OutErrorMessage = ContainsInvalidCharactersMessage;
		}
		return false;
	}

	for (const FRigModuleReference& Child : Model->Modules)
	{
		if (InDesiredShortName == FRigName(Child.GetShortName()))
		{
			if(OutErrorMessage)
			{
				static const FString NameAlreadyInUse = TEXT("This name is already in use.");
				*OutErrorMessage = NameAlreadyInUse;
			}
			return false;
		}
	}
	return true;
}

FRigName UModularRigController::GetSafeNewName(const FString& InParentModulePath, const FRigName& InDesiredName) const
{
	bool bSafeToUse = false;

	// create a copy of the desired name so that the string conversion can be cached
	const FRigName DesiredName = GetSanitizedName(InDesiredName, false);
	FRigName NewName = DesiredName;
	int32 Index = 0;
	while (!bSafeToUse)
	{
		bSafeToUse = true;
		if(!IsNameAvailable(InParentModulePath, NewName))
		{
			bSafeToUse = false;
			NewName = FString::Printf(TEXT("%s_%d"), *DesiredName.ToString(), ++Index);
		}
	}
	return NewName;
}

FRigName UModularRigController::GetSafeNewShortName(const FRigName& InDesiredShortName) const
{
	bool bSafeToUse = false;

	// create a copy of the desired name so that the string conversion can be cached
	const FRigName DesiredShortName = GetSanitizedName(InDesiredShortName, true);
	FRigName NewShortName = DesiredShortName;
	int32 Index = 0;
	while (!bSafeToUse)
	{
		bSafeToUse = true;
		if(!IsShortNameAvailable(NewShortName))
		{
			bSafeToUse = false;
			NewShortName = FString::Printf(TEXT("%s_%d"), *DesiredShortName.ToString(), ++Index);
		}
	}
	return NewShortName;
}

void UModularRigController::Notify(const EModularRigNotification& InNotification, const FRigModuleReference* InElement)
{
	if(!bSuspendNotifications)
	{
		ModifiedEvent.Broadcast(InNotification, InElement);
	}
}

void UModularRigController::UpdateShortNames()
{
	TMap<FString, int32> TokenToCount;

	// collect all usages of all paths and their segments
	for(const FRigModuleReference& Module : Model->Modules)
	{
		if(Module.bShortNameBasedOnPath)
		{
			FString RemainingPath = Module.GetPath();
			TokenToCount.FindOrAdd(RemainingPath, 0)++;
			while(URigHierarchy::SplitNameSpace(RemainingPath, nullptr, &RemainingPath, false))
			{
				TokenToCount.FindOrAdd(RemainingPath, 0)++;
			}
		}
		else
		{
			TokenToCount.FindOrAdd(Module.ShortName, 0)++;
		}
	}

	for(FRigModuleReference& Module : Model->Modules)
	{
		if(Module.bShortNameBasedOnPath)
		{
			FString ShortPath = Module.GetPath();
			if(!Module.ParentPath.IsEmpty())
			{
				FString Left, Right, RemainingPath = Module.GetPath();
				ShortPath.Reset();

				while(URigHierarchy::SplitNameSpace(RemainingPath, &Left, &Right))
				{
					ShortPath = ShortPath.IsEmpty() ? Right : URigHierarchy::JoinNameSpace(Right, ShortPath);

					// if the short path only exists once - that's what we use for the display name
					if(TokenToCount.FindChecked(ShortPath) == 1)
					{
						RemainingPath.Reset();
						break;
					}

					RemainingPath = Left;
				}

				if(!RemainingPath.IsEmpty())
				{
					ShortPath = URigHierarchy::JoinNameSpace(RemainingPath, ShortPath);
				}
			}

			if(!Module.ShortName.Equals(ShortPath, ESearchCase::IgnoreCase))
			{
				Module.ShortName = ShortPath;
				Notify(EModularRigNotification::ModuleShortNameChanged, &Module);
			}
		}
		else
		{
			// the display name is user defined so we won't touch it
		}
		
	}
}

FModularRigControllerCompileBracketScope::FModularRigControllerCompileBracketScope(UModularRigController* InController)
	: Controller(InController), bSuspendNotifications(InController->bSuspendNotifications)
{
	check(InController);
	
	if (bSuspendNotifications)
	{
		return;
	}
	InController->Notify(EModularRigNotification::InteractionBracketOpened, nullptr);
}

FModularRigControllerCompileBracketScope::~FModularRigControllerCompileBracketScope()
{
	check(Controller);
	if (bSuspendNotifications)
	{
		return;
	}
	Controller->Notify(EModularRigNotification::InteractionBracketClosed, nullptr);
}
