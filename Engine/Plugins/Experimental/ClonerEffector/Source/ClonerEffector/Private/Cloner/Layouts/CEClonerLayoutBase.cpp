// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloner/Layouts/CEClonerLayoutBase.h"

#include "Async/Async.h"
#include "Cloner/CEClonerActor.h"
#include "Cloner/CEClonerComponent.h"
#include "Cloner/Extensions/CEClonerExtensionBase.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "NiagaraEmitter.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"
#include "Subsystems/CEClonerSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogCEClonerLayoutBase, Log, All);

bool UCEClonerLayoutBase::IsLayoutValid() const
{
	if (LayoutName.IsNone() || LayoutAssetPath.IsEmpty())
	{
		return false;
	}

	// Get the template niagara asset
	const UNiagaraSystem* TemplateNiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, *LayoutAssetPath);

	// Get the base niagara asset
	const UNiagaraSystem* BaseNiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, LayoutBaseAssetPath);

	if (!TemplateNiagaraSystem || !BaseNiagaraSystem)
	{
		UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("Cloner layout %s : Template system (%s) or base system (%s) is invalid"), *LayoutName.ToString(), *LayoutAssetPath, LayoutBaseAssetPath);
		return false;
	}

	// Compare parameters : template should have base parameters
	bool bIsSystemBasedOnBaseAsset = true;

	{
		TArray<FNiagaraVariable> TemplateSystemParameters;
		TemplateNiagaraSystem->GetExposedParameters().GetParameters(TemplateSystemParameters);

		TArray<FNiagaraVariable> BaseSystemParameters;
		BaseNiagaraSystem->GetExposedParameters().GetParameters(BaseSystemParameters);

		for (const FNiagaraVariable& SystemParameter : BaseSystemParameters)
		{
			if (!TemplateSystemParameters.Contains(SystemParameter))
			{
				bIsSystemBasedOnBaseAsset = false;
				UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("Cloner layout %s : Template system (%s) missing parameter (%s) from base system (%s)"), *LayoutName.ToString(), *LayoutAssetPath, *SystemParameter.ToString(), LayoutBaseAssetPath);
				break;
			}
		}
	}

	if (!bIsSystemBasedOnBaseAsset)
	{
		UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("Cloner layout %s : Template system (%s) is not based off base system (%s)"), *LayoutName.ToString(), *LayoutAssetPath, LayoutBaseAssetPath);
	}

	return bIsSystemBasedOnBaseAsset;
}

bool UCEClonerLayoutBase::IsLayoutLoaded() const
{
	return !IsTemplate() && NiagaraSystem && MeshRenderer;
}

void UCEClonerLayoutBase::LoadLayout()
{
	if (IsLayoutLoaded())
	{
		return;
	}

	// Already being loaded
	if (LoadRequestIdentifier != INDEX_NONE)
	{
		return;
	}

	// System already cached and available with matching version
	if (NiagaraSystem)
	{
		if (CachedVersion == ECEClonerLayoutAssetVersion::LatestVersion)
		{
			UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout %s using cached system V%i"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), CachedVersion)

			CacheMeshRenderer();
			OnSystemLoaded();
			return;
		}

		UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("%s : Cloner layout %s skipping cached system V%i due to version mismatch V%i"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), CachedVersion, ECEClonerLayoutAssetVersion::LatestVersion)

		NiagaraSystem->MarkAsGarbage();
		NiagaraSystem = nullptr;
	}
	else
	{
		CleanOwnedSystem();
	}

	const UCEClonerComponent* ClonerComponent = GetClonerComponent();

	if (!IsValid(ClonerComponent))
	{
		return;
	}

	if (LayoutAssetPath.IsEmpty())
	{
		return;
	}

	// Extract package path
	FString MountedPath = LayoutAssetPath;
	{
		int32 FirstQuoteIndex;
		MountedPath.FindChar('\'', FirstQuoteIndex);

		int32 LastQuoteIndex;
		MountedPath.FindLastChar('\'', LastQuoteIndex);

		if (FirstQuoteIndex != LastQuoteIndex)
		{
			MountedPath = MountedPath.Mid(FirstQuoteIndex + 1, LastQuoteIndex - FirstQuoteIndex - 1);
		}

		MountedPath = FPackageName::ObjectPathToPackageName(MountedPath);
	}

	FPackagePath LayoutPackagePath;
	FPackagePath::TryFromMountedName(MountedPath, LayoutPackagePath);

	FPackagePath CustomPackagePath;
	FPackagePath::TryFromPackageName(TEXT("/Game/Temp/") + GetLayoutName().ToString() + TEXT("_") + FGuid::NewGuid().ToString(), CustomPackagePath);
	CustomPackagePath.SetHeaderExtension(EPackageExtension::Asset);

	UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout load requested %s - Template system %s - Package %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), *LayoutAssetPath, *CustomPackagePath.GetPackageFName().ToString())

	FLoadPackageAsyncOptionalParams Params;
	Params.PackagePriority = INT32_MAX;
	Params.LoadFlags = LOAD_Async | LOAD_MemoryReader | LOAD_DisableCompileOnLoad;
	Params.CustomPackageName = CustomPackagePath.GetPackageFName();
	Params.CompletionDelegate = MakeUnique<FLoadPackageAsyncDelegate>(FLoadPackageAsyncDelegate::CreateUObject(this, &UCEClonerLayoutBase::OnSystemPackageLoaded));

	LoadRequestIdentifier = LoadPackageAsync(LayoutPackagePath, MoveTemp(Params));

	BindCleanupDelegates();
}

bool UCEClonerLayoutBase::UnloadLayout()
{
	if (!IsLayoutLoaded())
	{
		return false;
	}

	// Cannot unload while active
	if (IsLayoutActive())
	{
		return false;
	}

	MeshRenderer->Meshes.Empty();
#if WITH_EDITOR
	MeshRenderer->OnMeshChanged();
	MeshRenderer->OnChanged().Broadcast();
	NiagaraSystem->KillAllActiveCompilations();
#endif
	NiagaraSystem->RemoveFromRoot();

	MeshRenderer = nullptr;

	UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout unloaded %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString())

	OnLayoutUnloaded();

	return true;
}

bool UCEClonerLayoutBase::IsLayoutActive() const
{
	const UCEClonerComponent* Component = GetClonerComponent();

	if (!Component)
	{
		return false;
	}

	return IsLayoutLoaded() && Component->GetAsset() == NiagaraSystem;
}

bool UCEClonerLayoutBase::ActivateLayout()
{
	if (IsLayoutActive())
	{
		return false;
	}

	// Load layout first
	if (!IsLayoutLoaded())
	{
		return false;
	}

	UCEClonerComponent* ClonerComponent = GetClonerComponent();

	if (!ClonerComponent)
	{
		return false;
	}

	ClonerComponent->SetAsset(NiagaraSystem);

	UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout activated %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString())

	OnLayoutActive();

	return true;
}

bool UCEClonerLayoutBase::DeactivateLayout()
{
	if (!IsLayoutActive())
	{
		return false;
	}

	UCEClonerComponent* ClonerComponent = GetClonerComponent();

	if (!ClonerComponent)
	{
		return false;
	}

	ClonerComponent->GetOverrideParameters().Empty(/** ClearBindings */true);
	ClonerComponent->SetAsset(nullptr);

#if WITH_EDITOR
	NiagaraSystem->KillAllActiveCompilations();
#endif

	UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout deactivated %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString())

	OnLayoutInactive();

	return true;
}

TSet<TSubclassOf<UCEClonerExtensionBase>> UCEClonerLayoutBase::GetSupportedExtensions() const
{
	TSet<TSubclassOf<UCEClonerExtensionBase>> ExtensionSupported;

	if (const UCEClonerSubsystem* ClonerSubsystem = UCEClonerSubsystem::Get())
	{
		for (const TSubclassOf<UCEClonerExtensionBase>& ExtensionClass : ClonerSubsystem->GetExtensionClasses())
		{
			const UCEClonerExtensionBase* Extension = ExtensionClass.GetDefaultObject();

			if (!Extension)
			{
				continue;
			}

			// Does the layout supports this extension
			if (!IsExtensionSupported(Extension))
			{
				continue;
			}

			// Does the extension supports this layout
			if (!Extension->IsLayoutSupported(this))
			{
				continue;
			}

			ExtensionSupported.Add(ExtensionClass);
		}
	}

	return ExtensionSupported;
}

bool UCEClonerLayoutBase::IsLayoutDirty() const
{
	return EnumHasAnyFlags(LayoutStatus, ECEClonerSystemStatus::ParametersDirty);
}

void UCEClonerLayoutBase::PostEditImport()
{
	Super::PostEditImport();

	// After cloner duplication in editor, niagara system should not be duplicated but still is,
	// so look for it in outer chain otherwise it will trigger a world GC leak when switching level
	CleanOwnedSystem();

	MarkLayoutDirty();
}

void UCEClonerLayoutBase::PostLoad()
{
	Super::PostLoad();

	if (CachedVersion == ECEClonerLayoutAssetVersion::PreVersioning)
	{
		// After cloner layout load, niagara system should not be loaded since property was transient pre versioning,
		// so look for it in outer chain otherwise it will trigger a world GC leak when switching level
		CleanOwnedSystem();
	}
}

#if WITH_EDITOR
void UCEClonerLayoutBase::PostEditUndo()
{
	Super::PostEditUndo();

	MarkLayoutDirty();
}
#endif

void UCEClonerLayoutBase::OnLayoutPropertyChanged()
{
	MarkLayoutDirty();
}

void UCEClonerLayoutBase::OnSystemPackageLoaded(const FName& InName, UPackage* InPackage, EAsyncLoadingResult::Type InResult)
{
	NiagaraSystem = InPackage ? Cast<UNiagaraSystem>(InPackage->FindAssetInPackage()) : nullptr;
	LoadRequestIdentifier = INDEX_NONE;

	if (NiagaraSystem)
	{
		InPackage->SetFlags(RF_Transient);
		NiagaraSystem->RemoveFromRoot();
		NiagaraSystem->ClearFlags(RF_Standalone | RF_Public | RF_Transient | RF_Transactional);

		constexpr ERenameFlags RenameFlags = REN_NonTransactional | REN_DontCreateRedirectors;
		if (NiagaraSystem->Rename(nullptr, this, RenameFlags))
		{
			CachedVersion = ECEClonerLayoutAssetVersion::LatestVersion;
			CacheMeshRenderer();
		}

		InPackage->MarkAsGarbage();
	}

	OnSystemLoaded();
}

void UCEClonerLayoutBase::OnSystemLoaded()
{
	const bool bLayoutLoaded = IsLayoutLoaded();

	if (bLayoutLoaded)
	{
		UE_LOG(LogCEClonerLayoutBase, Verbose, TEXT("%s : Cloner layout loaded %s - Template system %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), *LayoutAssetPath)

		OnLayoutLoaded();
	}
	else
	{
		UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("%s : Cloner layout load failed %s - Template system %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), *LayoutAssetPath)
	}

	OnClonerLayoutLoadedDelegate.Broadcast(this, bLayoutLoaded);
	OnClonerLayoutLoadedDelegate.Clear();
}

void UCEClonerLayoutBase::CacheMeshRenderer()
{
	if (!NiagaraSystem)
	{
		return;
	}

	for (FNiagaraEmitterHandle& SystemEmitterHandle : NiagaraSystem->GetEmitterHandles())
	{
		if (const FVersionedNiagaraEmitterData* EmitterData = SystemEmitterHandle.GetEmitterData())
		{
			for (UNiagaraRendererProperties* EmitterRenderer : EmitterData->GetRenderers())
			{
				if (UNiagaraMeshRendererProperties* EmitterMeshRenderer = Cast<UNiagaraMeshRendererProperties>(EmitterRenderer))
				{
					EmitterMeshRenderer->Meshes.Empty();
#if WITH_EDITOR
					EmitterMeshRenderer->OnMeshChanged();
#endif

					MeshRenderer = EmitterMeshRenderer;

					return;
				}
			}
		}
	}
}

void UCEClonerLayoutBase::BindCleanupDelegates()
{
	UnbindCleanupDelegates();

	if (const UCEClonerComponent* ClonerComponent = GetClonerComponent())
	{
		if (ULevel* ClonerLevel = ClonerComponent->GetComponentLevel())
		{
			ClonerLevel->OnCleanupLevel.AddUObject(this, &UCEClonerLayoutBase::OnLevelCleanup);
		}

		FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCEClonerLayoutBase::OnWorldCleanup);
	}
}

void UCEClonerLayoutBase::UnbindCleanupDelegates() const
{
	if (const UCEClonerComponent* ClonerComponent = GetClonerComponent())
	{
		if (ULevel* ClonerLevel = ClonerComponent->GetComponentLevel())
		{
			ClonerLevel->OnCleanupLevel.RemoveAll(this);
		}

		FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	}
}

void UCEClonerLayoutBase::OnWorldCleanup(UWorld* InWorld, bool bInSessionEnded, bool bInCleanupResources)
{
	const AActor* Actor = GetClonerActor();
	if (bInCleanupResources && Actor && Actor->GetWorld() == InWorld)
	{
		OnLevelCleanup();
	}
}

void UCEClonerLayoutBase::OnLevelCleanup()
{
	if (IsLayoutLoaded())
	{
		UE_LOG(LogCEClonerLayoutBase, Log, TEXT("%s : Cloner layout cleanup %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString())

		DeactivateLayout();

		UnloadLayout();
	}

	UnbindCleanupDelegates();
}

void UCEClonerLayoutBase::CleanOwnedSystem() const
{
	TArray<UObject*> OwnedObjects;
	GetObjectsWithOuter(this, OwnedObjects, false);

	for (UObject* OwnedObject : OwnedObjects)
	{
		if (OwnedObject && OwnedObject->IsA<UNiagaraSystem>())
		{
			UE_LOG(LogCEClonerLayoutBase, Warning, TEXT("%s : Cloner layout %s cleaning owned system %s"), *GetClonerActor()->GetActorNameOrLabel(), *LayoutName.ToString(), *OwnedObject->GetName())
			OwnedObject->MarkAsGarbage();
		}
	}
}

UCEClonerComponent* UCEClonerLayoutBase::GetClonerComponent() const
{
	return GetTypedOuter<UCEClonerComponent>();
}

AActor* UCEClonerLayoutBase::GetClonerActor() const
{
	if (const UCEClonerComponent* ClonerComponent = GetClonerComponent())
	{
		return ClonerComponent->GetOwner();
	}

	return nullptr;
}

void UCEClonerLayoutBase::UpdateLayoutParameters()
{
	if (!IsLayoutActive())
	{
		return;
	}

	if (UCEClonerComponent* ClonerComponent = GetClonerComponent())
	{
		if (!ClonerComponent->GetEnabled())
		{
			return;
		}

		OnLayoutParametersChanged(ClonerComponent);

		if (EnumHasAnyFlags(LayoutStatus, ECEClonerSystemStatus::SimulationDirty))
		{
			ClonerComponent->RequestClonerUpdate(/*Immediate*/false);
		}
	}

	LayoutStatus = ECEClonerSystemStatus::UpToDate;
}

void UCEClonerLayoutBase::MarkLayoutDirty(bool bInUpdateCloner)
{
	EnumAddFlags(LayoutStatus, ECEClonerSystemStatus::ParametersDirty);

	if (bInUpdateCloner)
	{
		EnumAddFlags(LayoutStatus, ECEClonerSystemStatus::SimulationDirty);
	}
}
