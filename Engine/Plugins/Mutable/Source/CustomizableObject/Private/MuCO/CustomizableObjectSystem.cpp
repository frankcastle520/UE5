// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableObjectSystem.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "GameFramework/PlayerController.h"
#include "MuCO/CustomizableInstanceLODManagement.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectInstanceUsagePrivate.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCO/DefaultImageProvider.h"
#include "MuCO/CustomizableObjectInstanceUsage.h"
#include "MuCO/ICustomizableObjectModule.h"
#include "MuCO/LogBenchmarkUtil.h"
#include "MuCO/LogInformationUtil.h"
#include "MuCO/UnrealExtensionDataStreamer.h"
#include "MuCO/UnrealMutableImageProvider.h"
#include "MuCO/UnrealMutableModelDiskStreamer.h"
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuR/Model.h"
#include "MuR/Settings.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ContentStreaming.h"
#include "Components/SkeletalMeshComponent.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"
#include "CustomizableObjectSettings.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Logging/MessageLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/World.h"
#include "Interfaces/ITargetPlatform.h"
#include "MuCO/ICustomizableObjectEditorModule.h"
#include "MuCO/EditorImageProvider.h"
#else
#include "Engine/Engine.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectSystem)

class AActor;
class UAnimInstance;


DECLARE_CYCLE_STAT(TEXT("MutablePendingRelease Time"), STAT_MutablePendingRelease, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("MutableTask"), STAT_MutableTask, STATGROUP_Game);

#define UE_MUTABLE_UPDATE_REGION TEXT("Mutable Update")
#define UE_TASK_MUTABLE_GETMESHES_REGION TEXT("Task_Mutable_GetMeshes")
#define UE_TASK_MUTABLE_GETIMAGES_REGION TEXT("Task_Mutable_GetImages")


UCustomizableObjectSystem* UCustomizableObjectSystemPrivate::SSystem = nullptr;

bool bIsMutableEnabled = true;

static FAutoConsoleVariableRef CVarMutableEnabled(
	TEXT("Mutable.Enabled"),
	bIsMutableEnabled,
	TEXT("true/false - Disabling Mutable will turn off CO compilation, mesh generation, and texture streaming and will remove the system ticker. "),
	FConsoleVariableDelegate::CreateStatic(&UCustomizableObjectSystemPrivate::OnMutableEnabledChanged));

int32 WorkingMemoryKB =
#if !PLATFORM_DESKTOP
(10 * 1024);
#else
(50 * 1024);
#endif

static FAutoConsoleVariableRef CVarWorkingMemoryKB(
	TEXT("mutable.WorkingMemory"),
	WorkingMemoryKB,
	TEXT("Limit the amount of memory (in KB) to use as working memory when building characters. More memory reduces the object construction time. 0 means no restriction. Defaults: Desktop = 50,000 KB, Others = 10,000 KB"),
	ECVF_Scalability);

TAutoConsoleVariable<bool> CVarClearWorkingMemoryOnUpdateEnd(
	TEXT("mutable.ClearWorkingMemoryOnUpdateEnd"),
	false,
	TEXT("Clear the working memory and cache after every Mutable operation."),
	ECVF_Scalability);

TAutoConsoleVariable<bool> CVarReuseImagesBetweenInstances(
	TEXT("mutable.ReuseImagesBetweenInstances"),
	true,
	TEXT("Enables or disables the reuse of images between instances."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarGeneratedResourcesCacheSize(
	TEXT("mutable.GeneratedResourcesCacheSize"),
	512,
	TEXT("Limit the number of resources (images and meshes) that will be tracked for reusal. Each tracked resource uses a small amout of memory for its key."),
	ECVF_Scalability);

TAutoConsoleVariable<bool> CVarPreserveUserLODsOnFirstGeneration(
	TEXT("mutable.PreserveUserLODsOnFirstGeneration"),
	true,
	TEXT("If false, force disable UCustomizableObject::bPreserveUserLODsOnFirstGeneration."),
	ECVF_Scalability);

TAutoConsoleVariable<bool> CVarEnableMeshCache(
	TEXT("mutable.EnableMeshCache"),
	true,
	TEXT("Enables or disables the reuse of meshes."),
	ECVF_Scalability);

TAutoConsoleVariable<bool> CVarEnableUpdateOptimization(
	TEXT("mutable.EnableUpdateOptimization"),
	false,
	TEXT("Enable or disable update optimization when no changes are made to the parent component."));

TAutoConsoleVariable<bool> CVarEnableRealTimeMorphTargets(
	TEXT("mutable.EnableRealTimeMorphTargets"),
	true,
	TEXT("Enable or disable generation of realtime morph targets."));

#if WITH_EDITOR
bool bEnableLODManagmentInEditor = false;

static FAutoConsoleVariableRef CVarMutableEnableLODManagmentInEditor(
	TEXT("Mutable.EnableLODManagmentInEditor"),
	bEnableLODManagmentInEditor,
	TEXT("true/false - If true, enables custom LODManagment in the editor. "),
	ECVF_Default);
#endif

TAutoConsoleVariable<bool> CVarEnableReleaseMeshResources(
	TEXT("mutable.EnableReleaseMeshResources"),
	true,
	TEXT("Allow releasing resources when discarding instances."));

int32 UCustomizableObjectSystemPrivate::SkeletalMeshMinLodQualityLevel = -1;


static void CVarMutableSinkFunction()
{
	if (UCustomizableObjectSystem::IsCreated())
	{
		UCustomizableObjectSystemPrivate* PrivateSystem = UCustomizableObjectSystem::GetInstance()->GetPrivate();

		static const IConsoleVariable* CVarSkeletalMeshMinLodQualityLevelCVarName = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMesh.MinLodQualityLevel"));
		PrivateSystem->SkeletalMeshMinLodQualityLevel = CVarSkeletalMeshMinLodQualityLevelCVarName ? CVarSkeletalMeshMinLodQualityLevelCVarName->GetInt() : INDEX_NONE;
	}
}

static FAutoConsoleVariableSink CVarMutableSink(FConsoleCommandDelegate::CreateStatic(&CVarMutableSinkFunction));


FUpdateContextPrivate::FUpdateContextPrivate(UCustomizableObjectInstance& InInstance, const FCustomizableObjectInstanceDescriptor& Descriptor)
{
	check(IsInGameThread());
	check(InInstance.GetPrivate());
	check(InInstance.GetCustomizableObject());

	Instance = &InInstance;
	CapturedDescriptor = Descriptor;
	CapturedDescriptorHash = FDescriptorHash(Descriptor);
	Parameters = Descriptor.GetParameters();
	NumObjectComponents = InInstance.GetCustomizableObject()->GetComponentCount();
	FirstLODAvailable = InInstance.GetCustomizableObject()->GetPrivate()->GetMinLODIndex();
	FirstResidentLOD = InInstance.GetPrivate()->FirstResidentLOD;

	MutableSystem = UCustomizableObjectSystem::GetInstance()->GetPrivate()->MutableSystem;	
	check(MutableSystem);
	
	InInstance.GetCustomizableObject()->GetPrivate()->ApplyStateForcedValuesToParameters(CapturedDescriptor.GetState(), Parameters.get());

	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	System->GetPrivate()->CacheTextureParameters(CapturedDescriptor.GetTextureParameters());
}


FUpdateContextPrivate::FUpdateContextPrivate(UCustomizableObjectInstance& InInstance) :
	FUpdateContextPrivate(InInstance, InInstance.GetPrivate()->GetDescriptor())
{
}


FUpdateContextPrivate::~FUpdateContextPrivate()
{
	check(IsInGameThread());

	if (UCustomizableObjectSystem::IsCreated())
	{
		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
		System->GetPrivate()->UnCacheTextureParameters(CapturedDescriptor.GetTextureParameters());
	}
}


int32 FUpdateContextPrivate::GetMinLOD() const
{
	return CapturedDescriptor.GetMinLod();
}


void FUpdateContextPrivate::SetMinLOD(int32 MinLOD)
{
	CapturedDescriptor.SetMinLod(MinLOD);
	CapturedDescriptorHash.MinLOD = MinLOD;
}


const TArray<uint16>& FUpdateContextPrivate::GetRequestedLODs() const
{
	return CapturedDescriptor.GetRequestedLODLevels();
}


void FUpdateContextPrivate::SetRequestedLODs(TArray<uint16>& RequestedLODs)
{
	CapturedDescriptor.SetRequestedLODLevels(RequestedLODs);
	CapturedDescriptorHash.RequestedLODsPerComponent = RequestedLODs;
}


const FCustomizableObjectInstanceDescriptor& FUpdateContextPrivate::GetCapturedDescriptor() const
{
	return CapturedDescriptor;
}


const FDescriptorHash& FUpdateContextPrivate::GetCapturedDescriptorHash() const
{
	return CapturedDescriptorHash;
}


const FCustomizableObjectInstanceDescriptor&& FUpdateContextPrivate::MoveCommittedDescriptor()
{
	return MoveTemp(CapturedDescriptor);	
}


FMutablePendingInstanceUpdate::FMutablePendingInstanceUpdate(const TSharedRef<FUpdateContextPrivate>& InContext) :
	Context(InContext)
{
}


bool FMutablePendingInstanceUpdate::operator==(const FMutablePendingInstanceUpdate& Other) const
{
	return Context->Instance.HasSameIndexAndSerialNumber(Other.Context->Instance);
}


bool FMutablePendingInstanceUpdate::operator<(const FMutablePendingInstanceUpdate& Other) const
{
	if (Context->PriorityType < Other.Context->PriorityType)
	{
		return true;
	}
	else if (Context->PriorityType > Other.Context->PriorityType)
	{
		return false;
	}
	else
	{
		return Context->StartQueueTime < Other.Context->StartQueueTime;
	}
}


uint32 GetTypeHash(const FMutablePendingInstanceUpdate& Update)
{
	return GetTypeHash(Update.Context->Instance.GetWeakPtrTypeHash());
}


TWeakObjectPtr<const UCustomizableObjectInstance> FPendingInstanceUpdateKeyFuncs::GetSetKey(const FMutablePendingInstanceUpdate& PendingUpdate)
{
	return PendingUpdate.Context->Instance;
}


bool FPendingInstanceUpdateKeyFuncs::Matches(const TWeakObjectPtr<const UCustomizableObjectInstance>& A, const TWeakObjectPtr<const UCustomizableObjectInstance>& B)
{
	return A.HasSameIndexAndSerialNumber(B);
}


uint32 FPendingInstanceUpdateKeyFuncs::GetKeyHash(const TWeakObjectPtr<const UCustomizableObjectInstance>& Identifier)
{
	return GetTypeHash(Identifier.GetWeakPtrTypeHash());
}


int32 FMutablePendingInstanceWork::Num() const
{
	return PendingInstanceUpdates.Num() + PendingInstanceDiscards.Num() + PendingIDsToRelease.Num();
}


void FMutablePendingInstanceWork::AddUpdate(const FMutablePendingInstanceUpdate& UpdateToAdd)
{
	UpdateToAdd.Context->StartQueueTime = FPlatformTime::Seconds();
	
	if (const FMutablePendingInstanceUpdate* ExistingUpdate = PendingInstanceUpdates.Find(UpdateToAdd.Context->Instance))
	{
		ExistingUpdate->Context->UpdateResult = EUpdateResult::ErrorReplaced;
		FinishUpdateGlobal(ExistingUpdate->Context);

		const FMutablePendingInstanceUpdate TaskToEnqueue = UpdateToAdd;
		TaskToEnqueue.Context->PriorityType = FMath::Min(ExistingUpdate->Context->PriorityType, UpdateToAdd.Context->PriorityType);
		TaskToEnqueue.Context->StartQueueTime = FMath::Min(ExistingUpdate->Context->StartQueueTime, UpdateToAdd.Context->StartQueueTime);
		
		RemoveUpdate(ExistingUpdate->Context->Instance);
		PendingInstanceUpdates.Add(TaskToEnqueue);
	}
	else
	{
		PendingInstanceUpdates.Add(UpdateToAdd);
	}

	if (const FMutablePendingInstanceDiscard* ExistingDiscard = PendingInstanceDiscards.Find(UpdateToAdd.Context->Instance))
	{
		UpdateToAdd.Context->UpdateResult = EUpdateResult::ErrorReplaced;
		FinishUpdateGlobal(UpdateToAdd.Context);

		PendingInstanceDiscards.Remove(ExistingDiscard->CustomizableObjectInstance);
	}
}


void FMutablePendingInstanceWork::RemoveUpdate(const TWeakObjectPtr<UCustomizableObjectInstance>& Instance)
{
	if (const FMutablePendingInstanceUpdate* Update = PendingInstanceUpdates.Find(Instance))
	{
		Update->Context->QueueTime = FPlatformTime::Seconds() - Update->Context->StartQueueTime;
		PendingInstanceUpdates.Remove(Instance);
	}	
}

#if WITH_EDITOR
void FMutablePendingInstanceWork::RemoveUpdatesForObject(const UCustomizableObject* InObject)
{
	check(InObject);
	for (auto Iterator = PendingInstanceUpdates.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator->Context->Instance.IsValid() && Iterator->Context->Instance->GetCustomizableObject() == InObject)
		{
			Iterator.RemoveCurrent();
		}
	}
}
#endif

const FMutablePendingInstanceUpdate* FMutablePendingInstanceWork::GetUpdate(const TWeakObjectPtr<const UCustomizableObjectInstance>& Instance) const
{
	return PendingInstanceUpdates.Find(Instance);
}


void FMutablePendingInstanceWork::AddDiscard(const FMutablePendingInstanceDiscard& TaskToEnqueue)
{
	if (const FMutablePendingInstanceUpdate* ExistingUpdate = PendingInstanceUpdates.Find(TaskToEnqueue.CustomizableObjectInstance.Get()))
	{
		ExistingUpdate->Context->UpdateResult = EUpdateResult::ErrorDiscarded;
		FinishUpdateGlobal(ExistingUpdate->Context);
		RemoveUpdate(ExistingUpdate->Context->Instance);
	}

	PendingInstanceDiscards.Add(TaskToEnqueue);
}


void FMutablePendingInstanceWork::AddIDRelease(mu::Instance::ID IDToRelease)
{
	PendingIDsToRelease.Add(IDToRelease);
}


UCustomizableObjectSystem* UCustomizableObjectSystem::GetInstance()
{
	if (!UCustomizableObjectSystemPrivate::SSystem)
	{
		UE_LOG(LogMutable, Log, TEXT("Creating Mutable Customizable Object System."));

		check(IsInGameThread());

		UCustomizableObjectSystemPrivate::SSystem = NewObject<UCustomizableObjectSystem>(UCustomizableObjectSystem::StaticClass());
		check(UCustomizableObjectSystemPrivate::SSystem != nullptr);
		checkf(!GUObjectArray.IsDisregardForGC(UCustomizableObjectSystemPrivate::SSystem), TEXT("Mutable was initialized too early in the UE4 init process, for instance, in the constructor of a default UObject."));
		UCustomizableObjectSystemPrivate::SSystem->AddToRoot();
		checkf(!GUObjectArray.IsDisregardForGC(UCustomizableObjectSystemPrivate::SSystem), TEXT("Mutable was initialized too early in the UE4 init process, for instance, in the constructor of a default UObject."));
		UCustomizableObjectSystemPrivate::SSystem->InitSystem();

		//FCoreUObjectDelegates::PurgePendingReleaseSkeletalMesh.AddUObject(UCustomizableObjectSystemPrivate::SSystem, &UCustomizableObjectSystem::PurgePendingReleaseSkeletalMesh);
	}

	return UCustomizableObjectSystemPrivate::SSystem;
}


UCustomizableObjectSystem* UCustomizableObjectSystem::GetInstanceChecked()
{
	UCustomizableObjectSystem* System = GetInstance();
	check(System);
	
	return System;
}


bool UCustomizableObjectSystem::IsUpdateResultValid(const EUpdateResult UpdateResult)
{
	return UpdateResult == EUpdateResult::Success || UpdateResult == EUpdateResult::Warning;
}


UCustomizableInstanceLODManagementBase* UCustomizableObjectSystem::GetInstanceLODManagement() const
{
	return GetPrivate()->CurrentInstanceLODManagement.Get();
}


void UCustomizableObjectSystem::SetInstanceLODManagement(UCustomizableInstanceLODManagementBase* NewInstanceLODManagement)
{
	GetPrivate()->CurrentInstanceLODManagement = NewInstanceLODManagement ? NewInstanceLODManagement : ToRawPtr(GetPrivate()->DefaultInstanceLODManagement);
}


FString UCustomizableObjectSystem::GetPluginVersion() const
{
	// Bridge the call from the module. This implementation is available from blueprint.
	return ICustomizableObjectModule::Get().GetPluginVersion();
}


void UCustomizableObjectSystem::LogShowData(bool bFullInfo, bool ShowMaterialInfo) const
{
	LogInformationUtil::ResetCounters();

	TArray<UCustomizableObjectInstance*> ArrayData;

	for (TObjectIterator<UCustomizableObjectInstanceUsage> It; It; ++It)
	{
		const UCustomizableObjectInstanceUsage* CustomizableObjectInstanceUsage = *It;

#if WITH_EDITOR
		if (IsValid(CustomizableObjectInstanceUsage) && CustomizableObjectInstanceUsage->GetPrivate()->IsNetMode(NM_DedicatedServer))
		{
			continue;
		}
#endif

		if (IsValid(CustomizableObjectInstanceUsage) && CustomizableObjectInstanceUsage->GetCustomizableObjectInstance()
			&& CustomizableObjectInstanceUsage->GetAttachParent())
		{
			const AActor* ParentActor = CustomizableObjectInstanceUsage->GetAttachParent()->GetAttachmentRootActor();

			if (ParentActor != nullptr)
			{
				ArrayData.AddUnique(CustomizableObjectInstanceUsage->GetCustomizableObjectInstance());
			}
		}
	}

	ArrayData.Sort([](UCustomizableObjectInstance& A, UCustomizableObjectInstance& B)
	{
		check(A.GetPrivate() != nullptr);
		check(B.GetPrivate() != nullptr);
		return (A.GetPrivate()->LastMinSquareDistFromComponentToPlayer < B.GetPrivate()->LastMinSquareDistFromComponentToPlayer);
	});

	int32 i;
	const int32 Max = ArrayData.Num();

	if (bFullInfo)
	{
		for (i = 0; i < Max; ++i)
		{
			LogInformationUtil::LogShowInstanceDataFull(ArrayData[i], ShowMaterialInfo);
		}
	}
	else
	{
		FString LogData = "\n\n";
		for (i = 0; i < Max; ++i)
		{
			LogInformationUtil::LogShowInstanceData(ArrayData[i], LogData);
		}
		UE_LOG(LogMutable, Log, TEXT("%s"), *LogData);

		UWorld* World = GWorld;

		if (World)
		{
			APlayerController* PlayerController = World->GetFirstPlayerController();
			if (PlayerController)
			{
				PlayerController->ClientMessage(LogData);
			}
		}
	}
}


UCustomizableObjectSystemPrivate* UCustomizableObjectSystem::GetPrivate()
{
	check(Private);
	return Private;
}


const UCustomizableObjectSystemPrivate* UCustomizableObjectSystem::GetPrivate() const
{
	check(Private);
	return Private;
}


bool UCustomizableObjectSystem::IsCreated()
{
	return UCustomizableObjectSystemPrivate::SSystem != 0;
}

bool UCustomizableObjectSystem::IsActive()
{
	return IsCreated() && bIsMutableEnabled;
}


void UCustomizableObjectSystem::InitSystem()
{
	// Everything initialized in Init() instead of constructor to prevent the default UCustomizableObjectSystem from registering a tick function
	Private = NewObject<UCustomizableObjectSystemPrivate>(this, FName("Private"));
	check(Private != nullptr);

	Private->bReplaceDiscardedWithReferenceMesh = false;

	Private->CurrentMutableOperation = nullptr;
	Private->CurrentInstanceBeingUpdated = nullptr;

	Private->LastWorkingMemoryBytes = CVarWorkingMemoryKB->GetInt() * 1024;
	Private->LastGeneratedResourceCacheSize = CVarGeneratedResourcesCacheSize.GetValueOnGameThread();

	const mu::Ptr<mu::Settings> pSettings = new mu::Settings;
	check(pSettings);
	pSettings->SetProfile(false);
	pSettings->SetWorkingMemoryBytes(Private->LastWorkingMemoryBytes);
	Private->ExtensionDataStreamer = MakeShared<FUnrealExtensionDataStreamer>(Private);
	Private->MutableSystem = new mu::System(pSettings, Private->ExtensionDataStreamer);
	check(Private->MutableSystem);

	Private->Streamer = MakeShared<FUnrealMutableModelBulkReader>();
	check(Private->Streamer != nullptr);
	Private->MutableSystem->SetStreamingInterface(Private->Streamer);

	// Set up the external image provider, for image parameters.
	TSharedPtr<FUnrealMutableResourceProvider> Provider = MakeShared<FUnrealMutableResourceProvider>();
	check(Provider != nullptr);
	Private->ResourceProvider = Provider;
	Private->MutableSystem->SetExternalResourceProvider(Provider);

#if WITH_EDITORONLY_DATA
	Private->EditorImageProvider = NewObject<UEditorImageProvider>();
	check(Private->EditorImageProvider);
	RegisterImageProvider(Private->EditorImageProvider);
#endif
	
	GetPrivate()->DefaultInstanceLODManagement = NewObject<UCustomizableInstanceLODManagement>();
	check(GetPrivate()->DefaultInstanceLODManagement != nullptr);
	GetPrivate()->CurrentInstanceLODManagement = GetPrivate()->DefaultInstanceLODManagement;

	// This CVar is constant for the lifespan of the program. Read its value once. 
	const IConsoleVariable* CVarSupport16BitBoneIndex = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUSkin.Support16BitBoneIndex"));
	Private->bSupport16BitBoneIndex = CVarSupport16BitBoneIndex ? CVarSupport16BitBoneIndex->GetBool() : false;

	// Read non-constant CVars and do work if required.
	CVarMutableSinkFunction();

	Private->OnMutableEnabledChanged();
}


void UCustomizableObjectSystem::BeginDestroy()
{
	// It could be null, for the default object.
	if (Private)
	{
#if WITH_EDITOR
		if (ICustomizableObjectEditorModule* EditorModule = FModuleManager::GetModulePtr<ICustomizableObjectEditorModule>("CustomizableObjectEditor"))
		{
			EditorModule->CancelCompileRequests();
		}
#endif

#if !UE_SERVER
		if (GetMutableDefault<UCustomizableObjectSettings>()->bEnableStreamingManager)
		{
			FStreamingManagerCollection::Get().RemoveStreamingManager(GetPrivate());
		}
		else
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Private->TickDelegateHandle);			
		}
#endif // !UE_SERVER

		// Discard pending game thread tasks
		Private->PendingTasks.Empty();

		// Complete pending taskgraph tasks
		Private->MutableTaskGraph.AllowLaunchingMutableTaskLowPriority(false, false);
		check(Private->Streamer);
		Private->MutableTaskGraph.AddMutableThreadTask(TEXT("EndStream"), [Streamer = Private->Streamer]()
			{
				Streamer->EndStreaming();
			});
		Private->MutableTaskGraph.WaitForMutableTasks();

		// Clear the ongoing operation
		Private->CurrentMutableOperation = nullptr;
		Private->CurrentInstanceBeingUpdated = nullptr;

		UCustomizableObjectSystemPrivate::SSystem = nullptr;

		Private = nullptr;
	}

	Super::BeginDestroy();
}


FString UCustomizableObjectSystem::GetDesc()
{
	return TEXT("Customizable Object System Singleton");
}


int32 UCustomizableObjectSystemPrivate::EnableMutableAnimInfoDebugging = 0;

static FAutoConsoleVariableRef CVarEnableMutableAnimInfoDebugging(
	TEXT("mutable.EnableMutableAnimInfoDebugging"), UCustomizableObjectSystemPrivate::EnableMutableAnimInfoDebugging,
	TEXT("If set to 1 or greater print on screen the animation info of the pawn's Customizable Object Instance. Anim BPs, slots and tags will be displayed."
	"If the root Customizable Object is recompiled after this command is run, the used skeletal meshes will also be displayed."),
	ECVF_Default);


UCustomizableObjectSystem* UCustomizableObjectSystemPrivate::GetPublic() const
{
	UCustomizableObjectSystem* Public = StaticCast<UCustomizableObjectSystem*>(GetOuter());
	check(Public);

	return Public;
}


void UCustomizableObjectSystemPrivate::AddGameThreadTask(const FMutableTask& Task)
{
	PendingTasks.Enqueue(Task);
}


TAutoConsoleVariable<bool> CVarCleanupTextureCache(
	TEXT("mutable.EnableCleanupCache"),
	true,
	TEXT("If enabled stale textures and meshes in mutable's cache will be removed."),
	ECVF_Scalability);


void UCustomizableObjectSystemPrivate::CleanupCache()
{
	check(IsInGameThread());

	const bool bCleanupEnabled = CVarCleanupTextureCache.GetValueOnGameThread();

	for (int32 ModelIndex = 0; ModelIndex < ModelResourcesCache.Num();)
	{
		if (!ModelResourcesCache[ModelIndex].Object.IsValid(false, true))
		{
			// The whole object has been destroyed. Remove everything.
			ModelResourcesCache.RemoveAtSwap(ModelIndex);
		}
		else
		{
			if (bCleanupEnabled)
			{
				// Remove stale textures
				for (auto Iterator = ModelResourcesCache[ModelIndex].Images.CreateIterator(); Iterator; ++Iterator)
				{
					if (Iterator->Value.IsStale())
					{
						Iterator.RemoveCurrent();
					}
				}

				// Remove stale meshes
				for (auto Iterator = ModelResourcesCache[ModelIndex].Meshes.CreateIterator(); Iterator; ++Iterator)
				{
					if (Iterator->Value.IsStale())
					{
						Iterator.RemoveCurrent();
					}
				}
			}

			++ModelIndex;
		}
	}
}


FMutableResourceCache& UCustomizableObjectSystemPrivate::GetObjectCache(const UCustomizableObject* Object)
{
	check(IsInGameThread());

	// Not mandatory, but a good place for a cleanup
	CleanupCache();

	for (int ModelIndex = 0; ModelIndex < ModelResourcesCache.Num(); ++ModelIndex)
	{
		if (ModelResourcesCache[ModelIndex].Object==Object)
		{
			return ModelResourcesCache[ModelIndex];
		}
	}
		
	// Not found, create and add it.
	ModelResourcesCache.Push(FMutableResourceCache());
	ModelResourcesCache.Last().Object = Object;
	return ModelResourcesCache.Last();
}

bool bForceStreamMeshLODs = false;

static FAutoConsoleVariableRef CVarMutableForceStreamMeshLODs(
	TEXT("Mutable.ForceStreamMeshLODs"),
	bForceStreamMeshLODs,
	TEXT("Experimental - true/false - If true, and bStreamMeshLODs is enabled, all COs will stream mesh LODs. "),
	ECVF_Default);


bool bStreamMeshLODs = false;

static FAutoConsoleVariableRef CVarMutableStreamMeshLODsEnabled(
	TEXT("Mutable.StreamMeshLODsEnabled"),
	bStreamMeshLODs,
	TEXT("Experimental - true/false - If true, enable generated meshes to stream mesh LODs. "),
	ECVF_Default);

int32 UCustomizableObjectSystemPrivate::EnableMutableProgressiveMipStreaming = 1;

// Warning! If this is enabled, do not get references to the textures generated by Mutable! They are owned by Mutable and could become invalid at any moment
static FAutoConsoleVariableRef CVarEnableMutableProgressiveMipStreaming(
	TEXT("mutable.EnableMutableProgressiveMipStreaming"), UCustomizableObjectSystemPrivate::EnableMutableProgressiveMipStreaming,
	TEXT("If set to 1 or greater use progressive Mutable Mip streaming for Mutable textures. If disabled, all mips will always be generated and spending memory. In that case, on Desktop platforms they will be stored in CPU memory, on other platforms textures will be non-streaming."),
	ECVF_Default);


int32 UCustomizableObjectSystemPrivate::EnableMutableLiveUpdate = 1;

static FAutoConsoleVariableRef CVarEnableMutableLiveUpdate(
	TEXT("mutable.EnableMutableLiveUpdate"), UCustomizableObjectSystemPrivate::EnableMutableLiveUpdate,
	TEXT("If set to 1 or greater Mutable can use the live update mode if set in the current Mutable state. If disabled, it will never use live update mode even if set in the current Mutable state."),
	ECVF_Default);


int32 UCustomizableObjectSystemPrivate::EnableReuseInstanceTextures = 1;

static FAutoConsoleVariableRef CVarEnableMutableReuseInstanceTextures(
	TEXT("mutable.EnableReuseInstanceTextures"), UCustomizableObjectSystemPrivate::EnableReuseInstanceTextures,
	TEXT("If set to 1 or greater and set in the corresponding setting in the current Mutable state, Mutable can reuse instance UTextures (only uncompressed and not streaming, so set the options in the state) and their resources between updates when they are modified. If geometry or state is changed they cannot be reused."),
	ECVF_Default);


int32 UCustomizableObjectSystemPrivate::EnableOnlyGenerateRequestedLODs = 1;

static FAutoConsoleVariableRef CVarEnableOnlyGenerateRequestedLODs(
	TEXT("mutable.EnableOnlyGenerateRequestedLODs"), UCustomizableObjectSystemPrivate::EnableOnlyGenerateRequestedLODs,
	TEXT("If 1 or greater, Only the RequestedLODLevels will be generated. If 0, all LODs will be build."),
	ECVF_Default);

int32 UCustomizableObjectSystemPrivate::EnableSkipGenerateResidentMips = 1;

static FAutoConsoleVariableRef CVarSkipGenerateResidentMips(
	TEXT("mutable.EnableSkipGenerateResidentMips"), UCustomizableObjectSystemPrivate::EnableSkipGenerateResidentMips,
	TEXT("If 1 or greater, resident mip generation will be optional. If 0, resident mips will be always generated"),
	ECVF_Default);

int32 UCustomizableObjectSystemPrivate::MaxTextureSizeToGenerate = 0;

FAutoConsoleVariableRef CVarMaxTextureSizeToGenerate(
	TEXT("Mutable.MaxTextureSizeToGenerate"),
	UCustomizableObjectSystemPrivate::MaxTextureSizeToGenerate,
	TEXT("Max texture size on Mutable textures. Mip 0 will be the first mip with max size equal or less than MaxTextureSizeToGenerate."
		"If a texture doesn't have small enough mips, mip 0 will be the last mip available."));

static FAutoConsoleVariable CVarDescriptorDebugPrint(
	TEXT("mutable.DescriptorDebugPrint"),
	false,
	TEXT("If true, each time an update is enqueued, print its captured parameters."),
	ECVF_Default);


void FinishUpdateGlobal(const TSharedRef<FUpdateContextPrivate>& Context)
{
	check(IsInGameThread())

	UCustomizableObjectInstance* Instance = Context->Instance.Get();

	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	UCustomizableObjectSystemPrivate* SystemPrivate = System ? System->GetPrivate() : nullptr;

	if (Instance)
	{
		UCustomizableInstancePrivate* PrivateInstance = Instance->GetPrivate();
		
		switch (Context->UpdateResult)
		{
		case EUpdateResult::Success:
		case EUpdateResult::Warning:
			PrivateInstance->SkeletalMeshStatus = ESkeletalMeshStatus::Success;

			if (SystemPrivate)
			{
				SystemPrivate->UnCacheTextureParameters(PrivateInstance->CommittedDescriptor.GetTextureParameters());				
			}

			PrivateInstance->CommittedDescriptor = Context->MoveCommittedDescriptor();
			PrivateInstance->CommittedDescriptorHash = Context->GetCapturedDescriptorHash();

			if (SystemPrivate)
			{
				// Cache new Texture Parameters
				SystemPrivate->CacheTextureParameters(PrivateInstance->CommittedDescriptor.GetTextureParameters());
			}
			
			// Delegates must be called only after updating the Instance flags.
			Instance->UpdatedDelegate.Broadcast(Instance);
			Instance->UpdatedNativeDelegate.Broadcast(Instance);
			break;

		case EUpdateResult::ErrorOptimized:
			break; // Skeletal Mesh not changed.
			
		case EUpdateResult::ErrorDiscarded:
			break; // Status will be updated once the discard is performed.

		case EUpdateResult::Error: 
		case EUpdateResult::Error16BitBoneIndex:
			PrivateInstance->SkeletalMeshStatus = ESkeletalMeshStatus::Error;
			break;
			
		case EUpdateResult::ErrorReplaced:
			break; // Skeletal Mesh not changed.
			
		default:
			unimplemented();
		}
	}

	if (UCustomizableObjectSystem::IsUpdateResultValid(Context->UpdateResult))
	{
		// Call CustomizableObjectInstanceUsages updated callbacks.
		for (TObjectIterator<UCustomizableObjectInstanceUsage> It; It; ++It) // Since iterating objects is expensive, for now CustomizableObjectInstanceUsage does not have a FinishUpdate function.
		{
			const UCustomizableObjectInstanceUsage* InstanceUsage = *It;
			if (!IsValid(InstanceUsage))
			{
				continue;
			}

#if WITH_EDITOR
			if (It->GetPrivate()->IsNetMode(NM_DedicatedServer))
			{
				continue;
			}
#endif

			if (InstanceUsage->GetCustomizableObjectInstance() == Instance &&
				(!Context->bOptimizedUpdate || Context->AttachedParentUpdated.Find(InstanceUsage)))
			{
				InstanceUsage->GetPrivate()->Callbacks();
			}
		}
	}

	FUpdateContext ContextPublic;
	ContextPublic.UpdateResult = Context->UpdateResult;
		
	Context->UpdateCallback.ExecuteIfBound(ContextPublic);
	Context->UpdateNativeCallback.Broadcast(ContextPublic);


	if (SystemPrivate)
	{
		SystemPrivate->MutableTaskGraph.AllowLaunchingMutableTaskLowPriority(true, false);
	}
	
	if (Context->StartUpdateTime != 0.0) // Update started.
	{
		Context->UpdateTime = FPlatformTime::Seconds() - Context->StartUpdateTime;		
	}
	
	const uint32 InstanceId = Instance ? Instance->GetUniqueID() : 0;
	UE_LOG(LogMutable, Log, TEXT("Finished UpdateSkeletalMesh Async. Instance=%d, Frame=%d, QueueTime=%f, UpdateTime=%f"), InstanceId, GFrameNumber, Context->QueueTime, Context->UpdateTime);

	if (SystemPrivate && FLogBenchmarkUtil::IsBenchmarkingReportingEnabled())
	{
		FFunctionGraphTask::CreateAndDispatchWhenReady( // Calling Benchmark in a task so we make sure we exited all scopes.
		[Context]()
		{
			if (!UCustomizableObjectSystem::IsCreated()) // We are shutting down
			{
				return;	
			}
			
			UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
			if (!System)
			{
				return;
			}

			System->GetPrivate()->LogBenchmarkUtil.FinishUpdateMesh(Context);
		},
		TStatId{},
		nullptr,
		ENamedThreads::GameThread);
	}
	
	if (Context->UpdateStarted)
	{
		TRACE_END_REGION(UE_MUTABLE_UPDATE_REGION);		
	}
}


/** Update the given Instance Skeletal Meshes */
void UpdateSkeletalMesh(const TSharedRef<FUpdateContextPrivate>& Context)
{
	MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh);

	check(IsInGameThread());

	UCustomizableObjectInstance* CustomizableObjectInstance = Context->Instance.Get();
	check(CustomizableObjectInstance);

	UCustomizableInstancePrivate* CustomizableObjectInstancePrivateData = CustomizableObjectInstance->GetPrivate();
	check(CustomizableObjectInstancePrivateData != nullptr);
	for (TObjectIterator<UCustomizableObjectInstanceUsage> It; It; ++It)
	{
		UCustomizableObjectInstanceUsage* CustomizableObjectInstanceUsage = *It;

#if WITH_EDITOR
		if (IsValid(CustomizableObjectInstanceUsage) && CustomizableObjectInstanceUsage->GetPrivate()->IsNetMode(NM_DedicatedServer))
		{
			continue;
		}
#endif

		bool bSkeletalMeshUpdated = false;
		bool bMaterialsUpdated = false;
		bool bPhysicsAssetUpdated = false;	
		
		if (IsValid(CustomizableObjectInstanceUsage) &&
			CustomizableObjectInstanceUsage->GetCustomizableObjectInstance() == CustomizableObjectInstance)
		{
			MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_SetSkeletalMesh);

			USkeletalMesh* SkeletalMesh = CustomizableObjectInstance->GetComponentMeshSkeletalMesh(CustomizableObjectInstanceUsage->GetComponentName());
			CustomizableObjectInstanceUsage->GetPrivate()->SetSkeletalMesh(SkeletalMesh, &bSkeletalMeshUpdated, &bMaterialsUpdated);

			if (CustomizableObjectInstancePrivateData->HasCOInstanceFlags(ReplacePhysicsAssets) &&
				SkeletalMesh)
			{
				CustomizableObjectInstanceUsage->GetPrivate()->SetPhysicsAsset(SkeletalMesh->GetPhysicsAsset(), &bPhysicsAssetUpdated);	
			}
		}

		if (bSkeletalMeshUpdated || bMaterialsUpdated || bPhysicsAssetUpdated)
		{
			Context->AttachedParentUpdated.Add(CustomizableObjectInstanceUsage);
		}
	}
}


void UCustomizableObjectSystemPrivate::GetMipStreamingConfig(const UCustomizableObjectInstance& Instance, bool& bOutNeverStream, int32& OutMipsToSkip) const
{
	bOutNeverStream = false;

	// From user-controlled per-state flag?
	const FString CurrentState = Instance.GetCurrentState();
	if (const FMutableStateData* State = Instance.GetCustomizableObject()->GetPrivate()->GetModelResources().StateUIDataMap.Find(CurrentState))
	{
		bOutNeverStream = State->bDisableTextureStreaming;
	}

#if WITH_EDITORONLY_DATA
	// Was streaming disabled at object-compilation time? 
	if (Instance.GetCustomizableObject()->GetPrivate()->GetModelResources().bIsTextureStreamingDisabled)
	{
		bOutNeverStream = true;
	}
#endif

	OutMipsToSkip = 0; // 0 means generate all mips

	// Streaming disabled from platform settings or from platform CustomizableObjectSystem properties?
#if PLATFORM_SUPPORTS_TEXTURE_STREAMING
	if (!IStreamingManager::Get().IsTextureStreamingEnabled() || !EnableMutableProgressiveMipStreaming)
	{
		bOutNeverStream = true;
	}
#else
	bOutNeverStream = true;
#endif
	
	if (!bOutNeverStream)
	{
		OutMipsToSkip = 255; // This means skip all possible mips until only UTexture::GetStaticMinTextureResidentMipCount() are left
	}
}


bool UCustomizableObjectSystemPrivate::IsReplaceDiscardedWithReferenceMeshEnabled() const
{
	return bReplaceDiscardedWithReferenceMesh;
}


void UCustomizableObjectSystemPrivate::SetReplaceDiscardedWithReferenceMeshEnabled(bool bIsEnabled)
{
	bReplaceDiscardedWithReferenceMesh = bIsEnabled;
}


int32 UCustomizableObjectSystemPrivate::GetNumSkeletalMeshes() const
{
	return NumSkeletalMeshes;
}


void UCustomizableObjectSystemPrivate::AddTextureReference(const FMutableImageCacheKey& TextureId)
{
	uint32& CountRef = TextureReferenceCount.FindOrAdd(TextureId);

	CountRef++;
}


bool UCustomizableObjectSystemPrivate::RemoveTextureReference(const FMutableImageCacheKey& TextureId)
{
	uint32* CountPtr = TextureReferenceCount.Find(TextureId);

	if (CountPtr && *CountPtr > 0)
	{
		(*CountPtr)--;

		if (*CountPtr == 0)
		{
			TextureReferenceCount.Remove(TextureId);

			return true;
		}
	}
	else
	{
		ensure(false); // Mutable texture reference count is incorrect
		TextureReferenceCount.Remove(TextureId);
	}

	return false;
}


bool UCustomizableObjectSystemPrivate::TextureHasReferences(const FMutableImageCacheKey& TextureId) const
{
	const uint32* CountPtr = TextureReferenceCount.Find(TextureId);

	if (CountPtr && *CountPtr > 0)
	{
		return true;
	}

	return false;
}


EUpdateRequired UCustomizableObjectSystemPrivate::IsUpdateRequired(const UCustomizableObjectInstance& Instance, bool bOnlyUpdateIfNotGenerated, bool bOnlyUpdateIfLODs, bool bIgnoreCloseDist) const
{
	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	const UCustomizableInstancePrivate* const Private = Instance.GetPrivate();
	
	if (!Instance.CanUpdateInstance())
	{
		return EUpdateRequired::NoUpdate;
	}

	const bool bIsGenerated = Private->SkeletalMeshStatus != ESkeletalMeshStatus::NotGenerated;
	const int32 NumGeneratedInstancesLimit = System->GetInstanceLODManagement()->GetNumGeneratedInstancesLimitFullLODs();
	const int32 NumGeneratedInstancesLimitLOD1 = System->GetInstanceLODManagement()->GetNumGeneratedInstancesLimitLOD1();
	const int32 NumGeneratedInstancesLimitLOD2 = System->GetInstanceLODManagement()->GetNumGeneratedInstancesLimitLOD2();

	if (!bIsGenerated && // Prevent generating more instances than the limit, but let updates to existing instances run normally
		NumGeneratedInstancesLimit > 0 &&
		System->GetPrivate()->GetNumSkeletalMeshes() > NumGeneratedInstancesLimit + NumGeneratedInstancesLimitLOD1 + NumGeneratedInstancesLimitLOD2)
	{
		return EUpdateRequired::NoUpdate;
	}

	const bool bDiscardByDistance = Private->LastMinSquareDistFromComponentToPlayer > FMath::Square(System->GetInstanceLODManagement()->GetOnlyUpdateCloseCustomizableObjectsDist());
	const bool bLODManagementDiscard = System->GetInstanceLODManagement()->IsOnlyUpdateCloseCustomizableObjectsEnabled() &&
			bDiscardByDistance &&
			!bIgnoreCloseDist;
	
	if (Private->HasCOInstanceFlags(DiscardedByNumInstancesLimit) ||
		bLODManagementDiscard)
	{
		if (bIsGenerated)
		{
			return EUpdateRequired::Discard;		
		}
		else
		{
			return EUpdateRequired::NoUpdate;
		}
	}

	const bool bShouldUpdateLODs = Private->HasCOInstanceFlags(PendingLODsUpdate);

	const bool bNoUpdateLODs = bOnlyUpdateIfLODs && !bShouldUpdateLODs;
	const bool bNoInitialUpdate = bOnlyUpdateIfNotGenerated && bIsGenerated;

	if (bNoUpdateLODs &&
		bNoInitialUpdate)
	{
		return EUpdateRequired::NoUpdate;
	}

	return EUpdateRequired::Update;
}


EQueuePriorityType UCustomizableObjectSystemPrivate::GetUpdatePriority(const UCustomizableObjectInstance& Instance,	bool bForceHighPriority) const
{
	const UCustomizableInstancePrivate* InstancePrivate = Instance.GetPrivate();
		
	const bool bNotGenerated = InstancePrivate->SkeletalMeshStatus == ESkeletalMeshStatus::NotGenerated;
	const bool bShouldUpdateLODs = InstancePrivate->HasCOInstanceFlags(PendingLODsUpdate);
	const bool bIsDowngradeLODUpdate = InstancePrivate->HasCOInstanceFlags(PendingLODsDowngrade);
	const bool bIsPlayerOrNearIt = InstancePrivate->HasCOInstanceFlags(UsedByPlayerOrNearIt);

	EQueuePriorityType Priority = EQueuePriorityType::Low;
	if (bForceHighPriority)
	{
		Priority = EQueuePriorityType::High;
	}
	else if (bNotGenerated || !Instance.HasAnySkeletalMesh())
	{
		Priority = EQueuePriorityType::Med;
	}
	else if (bShouldUpdateLODs && bIsDowngradeLODUpdate)
	{
		Priority = EQueuePriorityType::Med_Low;
	}
	else if (bIsPlayerOrNearIt && bShouldUpdateLODs && !bIsDowngradeLODUpdate)
	{
		Priority = EQueuePriorityType::High;
	}
	else if (bShouldUpdateLODs && !bIsDowngradeLODUpdate)
	{
		Priority = EQueuePriorityType::Med;
	}
	else if (bIsPlayerOrNearIt)
	{
		Priority = EQueuePriorityType::High;
	}

	return Priority;
}


void UCustomizableObjectSystemPrivate::EnqueueUpdateSkeletalMesh(const TSharedRef<FUpdateContextPrivate>& Context)
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectSystemPrivate::EnqueueUpdateSkeletalMesh);
	check(IsInGameThread());

	UCustomizableObjectInstance* Instance = Context->Instance.Get();
	check(Instance);
	
	UCustomizableInstancePrivate* InstancePrivate = Instance->GetPrivate();

	const EQueuePriorityType Priority = GetUpdatePriority(*Instance, Context->bForceHighPriority);
	const uint32 InstanceId = Instance->GetUniqueID();
	const float Distance = FMath::Sqrt(InstancePrivate->LastMinSquareDistFromComponentToPlayer);
	const bool bIsPlayerOrNearIt = InstancePrivate->HasCOInstanceFlags(UsedByPlayerOrNearIt);
	UE_LOG(LogMutable, Log, TEXT("Enqueue UpdateSkeletalMesh Async. Instance=%d, Frame=%d, Priority=%d, dist=%f, bIsPlayerOrNearIt=%d"), InstanceId, GFrameNumber, static_cast<int32>(Priority), Distance, bIsPlayerOrNearIt);				
	
	if (!bIsMutableEnabled)
	{
		// Mutable is disabled. Set the reference SkeletalMesh and finish the update with success to avoid breaking too many things.
		Context->UpdateResult = EUpdateResult::Success;
		InstancePrivate->SetDefaultSkeletalMesh();
		FinishUpdateGlobal(Context);
		return;
	}

	if (!Instance->CanUpdateInstance())
	{
		Context->UpdateResult = EUpdateResult::Error;
		FinishUpdateGlobal(Context);
		return;
	}

	const EUpdateRequired UpdateRequired = IsUpdateRequired(*Instance, Context->bOnlyUpdateIfNotGenerated, false, Context->bIgnoreCloseDist);
	switch (UpdateRequired)
	{
	case EUpdateRequired::NoUpdate:
	{	
		Context->UpdateResult = EUpdateResult::Error;
		FinishUpdateGlobal(Context);
		break;
	}		
	case EUpdateRequired::Update:
	{
		if (InstancePrivate->HasCOInstanceFlags(PendingLODsUpdate))
		{
			UE_LOG(LogMutable, Verbose, TEXT("Min LOD change: %d -> %d"), Instance->GetCurrentMinLOD(), Instance->GetMinLODToLoad());
		}

		if (const FMutablePendingInstanceUpdate* QueueElem = MutablePendingInstanceWork.GetUpdate(Instance))
		{
			if (Context->GetCapturedDescriptorHash().IsSubset(QueueElem->Context->GetCapturedDescriptorHash()))
			{
				Context->bOptimizedUpdate = true;
				Context->UpdateResult = EUpdateResult::ErrorOptimized;
				FinishUpdateGlobal(Context);			
				return; // The the requested update is equal to the last enqueued update.
			}
		}	

		if (CurrentMutableOperation &&
			Instance == CurrentMutableOperation->Instance &&
			Context->GetCapturedDescriptorHash().IsSubset(CurrentMutableOperation->GetCapturedDescriptorHash()))
		{
			Context->bOptimizedUpdate = true;
			Context->UpdateResult = EUpdateResult::ErrorOptimized;
			FinishUpdateGlobal(Context);
			return; // The requested update is equal to the running update.
		}
	
		if (Context->GetCapturedDescriptorHash().IsSubset(InstancePrivate->CommittedDescriptorHash) &&
			!(CurrentMutableOperation &&
			Instance == CurrentMutableOperation->Instance)) // This condition is necessary because even if the descriptor is a subset, it will be replaced by the CurrentMutableOperation
		{
			if (CVarEnableUpdateOptimization.GetValueOnGameThread()) // TODO Remove hotfix: UE-218957 
			{
				Context->bOptimizedUpdate = true;

				// The user may have changed the AttachParent and we need to recustomize it.
				// In case nothing need to be recustomized, the update will be considered ErrorOptimized.
				UpdateSkeletalMesh(Context);
				Context->UpdateResult = Context->AttachedParentUpdated.IsEmpty() ? EUpdateResult::ErrorOptimized : EUpdateResult::Success;

				FinishUpdateGlobal(Context);
			}
			else 
			{
				Context->bOptimizedUpdate = false;

				// The user may have changed the AttachParent and we need to recustomize it.
				// In case nothing need to be recustomized, the update will be considered ErrorOptimized.
				UpdateSkeletalMesh(Context);
				Context->UpdateResult = EUpdateResult::Success;

				FinishUpdateGlobal(Context);
			}
		}
		else
		{
			if (CVarDescriptorDebugPrint->GetBool())
			{
				FString String = TEXT("DESCRIPTOR DEBUG PRINT\n");
				String += "================================\n";				
				String += FString::Printf(TEXT("=== DESCRIPTOR HASH ===\n%s\n"), *Context->GetCapturedDescriptorHash().ToString());
				String += FString::Printf(TEXT("=== DESCRIPTOR ===\n%s"), *Instance->GetPrivate()->GetDescriptor().ToString());
				String += "================================";
				
				UE_LOG(LogMutable, Log, TEXT("%s"), *String);
			}

			const FMutablePendingInstanceUpdate InstanceUpdate(Context);
			MutablePendingInstanceWork.AddUpdate(InstanceUpdate);
		}

		break;
	}

	case EUpdateRequired::Discard:
	{
		InitDiscardResourcesSkeletalMesh(Instance);

		Context->UpdateResult = EUpdateResult::ErrorDiscarded;
		FinishUpdateGlobal(Context);
		break;
	}

	default:
		unimplemented();
	}
}


void UCustomizableObjectSystemPrivate::InitDiscardResourcesSkeletalMesh(UCustomizableObjectInstance* InCustomizableObjectInstance)
{
	check(IsInGameThread());

	if (InCustomizableObjectInstance && InCustomizableObjectInstance->IsValidLowLevel())
	{
		check(InCustomizableObjectInstance->GetPrivate() != nullptr);
		MutablePendingInstanceWork.AddDiscard(FMutablePendingInstanceDiscard(InCustomizableObjectInstance));
	}
}


void UCustomizableObjectSystemPrivate::InitInstanceIDRelease(mu::Instance::ID IDToRelease)
{
	check(IsInGameThread());

	MutablePendingInstanceWork.AddIDRelease(IDToRelease);
}


bool UCustomizableObjectSystem::IsReplaceDiscardedWithReferenceMeshEnabled() const
{
	if (Private)
	{
		return Private->IsReplaceDiscardedWithReferenceMeshEnabled();
	}

	return false;
}


void UCustomizableObjectSystem::SetReplaceDiscardedWithReferenceMeshEnabled(bool bIsEnabled)
{
	if (Private)
	{
		Private->SetReplaceDiscardedWithReferenceMeshEnabled(bIsEnabled);
	}
}


void UCustomizableObjectSystem::ClearResourceCacheProtected()
{
	check(IsInGameThread());

	GetPrivate()->ProtectedCachedTextures.Reset(0);
	check(GetPrivate() != nullptr);
	GetPrivate()->ProtectedObjectCachedImages.Reset(0);
}


#if WITH_EDITOR
bool UCustomizableObjectSystem::LockObject(UCustomizableObject* InObject)
{
	check(InObject != nullptr);
	check(InObject->GetPrivate());
	check(!InObject->GetPrivate()->bLocked);
	check(IsInGameThread() && !IsInParallelGameThread());

	if (Private)
	{
		// If the current instance is for this object, make the lock fail by returning false
		if (Private->CurrentInstanceBeingUpdated &&
			Private->CurrentInstanceBeingUpdated->GetCustomizableObject() == InObject)
		{
			UE_LOG(LogMutable, Warning, TEXT("---- failed to lock object %s"), *InObject->GetName());

			return false;
		}

		FString Message = FString::Printf(TEXT("Customizable Object %s has pending texture streaming operations. Please wait a few seconds and try again."),
			*InObject->GetName());

		// Pre-check pending operations before locking. This check is redundant and incomplete because it's checked again after locking 
		// and some operations may start between here and the actual lock. But in the CO Editor preview it will prevent some 
		// textures getting stuck at low resolution when they try to update mips and are cancelled when the user presses 
		// the compile button but the compilation quits anyway because there are pending operations
		if (CheckIfDiskOrMipUpdateOperationsPending(*InObject))
		{
			UE_LOG(LogMutable, Warning, TEXT("%s"), *Message);

			return false;
		}

		// Lock the object, no new file or mip streaming operations should start from this point
		InObject->GetPrivate()->bLocked = true;

		// Invalidate the current model to avoid further disk or mip updates.
		if (InObject->GetPrivate()->GetModel())
		{
			InObject->GetPrivate()->GetModel()->Invalidate();
		}

		// But some could have started between the first CheckIfDiskOrMipUpdateOperationsPending and the lock a few lines back, so check again
		if (CheckIfDiskOrMipUpdateOperationsPending(*InObject))
		{
			UE_LOG(LogMutable, Warning, TEXT("%s"), *Message);

			// Unlock and return because the pending operations cannot be easily stopped now, the compilation hasn't started and the CO
			// hasn't changed state yet. It's simpler to quit the compilation, unlock and let the user try to compile again
			InObject->GetPrivate()->bLocked = false;

			return false;
		}

		// Ensure that we don't try to handle any further streaming operations for this object
		check(GetPrivate() != nullptr);
		if (GetPrivate()->Streamer)
		{
			UE::Tasks::FTask Task = Private->MutableTaskGraph.AddMutableThreadTask(TEXT("EndStream"), [InObject, Streamer = GetPrivate()->Streamer]()
				{
					Streamer->CancelStreamingForObject(InObject);
				});

			
			Task.Wait();
		}

		Private->MutablePendingInstanceWork.RemoveUpdatesForObject(InObject);

		// Clear the cache for the instance, since we will remake it
		FMutableResourceCache& Cache = GetPrivate()->GetObjectCache(InObject);
		Cache.Clear();

		check(InObject->GetPrivate()->bLocked);

		return true;
	}
	else
	{
		FString ObjectName = InObject ? InObject->GetName() : FString("null");
		UE_LOG(LogMutable, Warning, TEXT("Failed to lock the object [%s] because it was null or the system was null or partially destroyed."), *ObjectName);

		return false;
	}
}


void UCustomizableObjectSystem::UnlockObject(UCustomizableObject* Obj)
{
	check(Obj != nullptr);
	check(Obj->GetPrivate());
	check(Obj->GetPrivate()->bLocked);
	check(IsInGameThread() && !IsInParallelGameThread());
	
	Obj->GetPrivate()->bLocked = false;
}


bool UCustomizableObjectSystem::CheckIfDiskOrMipUpdateOperationsPending(const UCustomizableObject& Object) const
{
	for (TObjectIterator<UCustomizableObjectInstance> CustomizableObjectInstance; CustomizableObjectInstance; ++CustomizableObjectInstance)
	{
		if (IsValid(*CustomizableObjectInstance) && CustomizableObjectInstance->GetCustomizableObject() == &Object)
		{
			for (const FGeneratedTexture& GeneratedTexture : CustomizableObjectInstance->GetPrivate()->GeneratedTextures)
			{
				if (GeneratedTexture.Texture->HasPendingInitOrStreaming())
				{
					return true;
				}
			}
		}
	}

	// Ensure that we don't try to handle any further streaming operations for this object
	check(GetPrivate());
	if (const FUnrealMutableModelBulkReader* Streamer = GetPrivate()->Streamer.Get())
	{
		if (Streamer->AreTherePendingStreamingOperationsForObject(&Object))
		{
			return true;
		}
	}

	return false;
}


void UCustomizableObjectSystem::EditorSettingsChanged(const FEditorCompileSettings& InEditorSettings)
{
	EditorSettings = InEditorSettings;

	CVarMutableEnabled->Set(InEditorSettings.bIsMutableEnabled);
}

bool UCustomizableObjectSystem::IsAutoCompileEnabled() const
{
	return EditorSettings.bEnableAutomaticCompilation;
}


bool UCustomizableObjectSystem::IsAutoCompileCommandletEnabled() const
{
	return GetPrivate()->bAutoCompileCommandletEnabled;
}


void UCustomizableObjectSystem::SetAutoCompileCommandletEnabled(bool bValue)
{
	GetPrivate()->bAutoCompileCommandletEnabled = bValue;
}


bool UCustomizableObjectSystem::IsAutoCompilationSync() const
{
	return EditorSettings.bCompileObjectsSynchronously;
}

#endif


void UCustomizableObjectSystem::ClearCurrentMutableOperation()
{
	check(Private != nullptr);
	Private->CurrentInstanceBeingUpdated = nullptr;
	Private->CurrentMutableOperation = nullptr;
	ClearResourceCacheProtected();
}


void UCustomizableObjectSystemPrivate::UpdateMemoryLimit()
{
	// This must run on game thread, and when the mutable thread is not running
	check(IsInGameThread());

	const uint64 MemoryBytes = uint64(CVarWorkingMemoryKB->GetInt()) * 1024;
	if (MemoryBytes != LastWorkingMemoryBytes)
	{
		LastWorkingMemoryBytes = MemoryBytes;
		check(MutableSystem);
		MutableSystem->SetWorkingMemoryBytes(MemoryBytes);
	}

	const uint32 GeneratedResourceCacheSize = CVarGeneratedResourcesCacheSize.GetValueOnGameThread();
	if (GeneratedResourceCacheSize != LastGeneratedResourceCacheSize)
	{
		LastGeneratedResourceCacheSize = GeneratedResourceCacheSize;
		check(MutableSystem);
		MutableSystem->SetGeneratedCacheSize(GeneratedResourceCacheSize);
	}
}


// Asynchronous tasks performed during the creation or update of a mutable instance. 
// Check the documentation before modifying and keep it up to date.
// https://docs.google.com/drawings/d/109NlsdKVxP59K5TuthJkleVG3AROkLJr6N03U4bNp4s
// When it says "mutable thread" it means any task pool thread, but with the guarantee that no other thread is using the mutable runtime.
// Naming: Task_<thread>_<description>
namespace impl
{
	struct FGetImageData
	{
		int32 ImageIndex;
		mu::FResourceID ImageID;
	};
	

	/** Process the next Image. If there are no more Images, go to the end of the task. */
	void Task_Mutable_GetMeshes_GetImage_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetImageData>>& GetImagesData,
		int32 GetImageIndex);
	

	struct FGetMeshData
	{
		int32 LODIndex;
		mu::FResourceID MeshID;
	};


	/** Process the next Mesh. If there are no more Meshes, go to the process Images loop. */
	void Task_Mutable_GetMeshes_GetMesh_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetMeshData>>& GetMeshesData,
		int32 GetMeshIndex);


	/** Call GetImage.
	  * Once GetImage is called, the task must end. Following code will be in a subsequent TaskGraph task. */
	void Task_Mutable_GetImages_GetImage(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
		int32 ImageIndex,
		UE::Tasks::TTask<mu::FImageDesc> GetImageDescTask);

	
	/** Process the next Image. If there are no more Images, go to the end of the task. */
	void Task_Mutable_GetImages_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
		int32 ImageIndex);

	
	void Subtask_Mutable_UpdateParameterRelevancy(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Subtask_Mutable_UpdateParameterRelevancy)

		check(OperationData->Parameters);
		check(OperationData->InstanceID != 0);

		OperationData->RelevantParametersInProgress.Empty();

		// This must run in the mutable thread.
		check(UCustomizableObjectSystem::GetInstance() != nullptr);
		check(UCustomizableObjectSystem::GetInstance()->GetPrivate() != nullptr);

		// Update the parameter relevancy.
		{
			MUTABLE_CPUPROFILER_SCOPE(ParameterRelevancy)

			const int32 NumParameters = OperationData->Parameters->GetCount();

			TArray<bool> Relevant;
			Relevant.SetNumZeroed(NumParameters);
			OperationData->MutableSystem->GetParameterRelevancy(OperationData->InstanceID, OperationData->Parameters, Relevant.GetData());

			for (int32 ParamIndex = 0; ParamIndex < NumParameters; ++ParamIndex)
			{
				if (Relevant[ParamIndex])
				{
					OperationData->RelevantParametersInProgress.Add(ParamIndex);
				}
			}
		}
	}


	void CreateMutableInstance(const TSharedRef<FUpdateContextPrivate>& Operation)
	{
		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked(); // Save since UCustomizableObjectSystem::BeginDestroy always waits for all tasks to finish
		const UCustomizableObjectSystemPrivate* SystemPrivate = System->GetPrivate();
		
		if (FLogBenchmarkUtil::IsBenchmarkingReportingEnabled())
		{
			// Get the amount of mutable memory in use now
			Operation->UpdateStartBytes = mu::FGlobalMemoryCounter::GetAbsoluteCounter();
			// Reset the counter to later get the peak during the updated
			mu::FGlobalMemoryCounter::Zero();													
		}

		// Prepare streaming for the current customizable object
		check(SystemPrivate->Streamer != nullptr);
		SystemPrivate->Streamer->PrepareStreamingForObject(Operation->Instance->GetCustomizableObject());			

		const mu::Ptr<mu::System> MutableSystem = SystemPrivate->MutableSystem;
		
		if (Operation->bLiveUpdateMode)
		{
			if (Operation->InstanceID == 0)
			{
				// It's the first update since the instance was put in LiveUpdate Mode, this ID will be reused from now on
				Operation->InstanceID = MutableSystem->NewInstance(Operation->Model);
				UE_LOG(LogMutable, Verbose, TEXT("Creating Mutable instance with id [%d] for reuse "), Operation->InstanceID);
			}
			else
			{
				// The instance was already in LiveUpdate Mode, the ID is reused
				check(Operation->InstanceID);
				UE_LOG(LogMutable, Verbose, TEXT("Reusing Mutable instance with id [%d] "), Operation->InstanceID);
			}
		}
		else
		{
			// In non-LiveUpdate mode, we are forcing the recreation of mutable-side instances with every update.
			check(Operation->InstanceID == 0);
			Operation->InstanceID = MutableSystem->NewInstance(Operation->Model);
			UE_LOG(LogMutable, Verbose, TEXT("Creating Mutable instance with id [%d] "), Operation->InstanceID);
		}

		Operation->MutableInstance = MutableSystem->BeginUpdate(Operation->InstanceID, Operation->Parameters, Operation->GetCapturedDescriptor().GetState(), mu::System::AllLODs);
		Operation->NumInstanceComponents = Operation->MutableInstance->GetComponentCount();
	}

	
	void FixLODs(const TSharedRef<FUpdateContextPrivate>& Operation)
	{
		if (!Operation->NumObjectComponents)
		{
			return;
		}

		Operation->NumLODsAvailablePerComponent.SetNumZeroed(Operation->NumObjectComponents);

		for (int32 ComponentIndex = 0; ComponentIndex < Operation->NumInstanceComponents; ++ComponentIndex)
		{
			Operation->NumLODsAvailablePerComponent[Operation->MutableInstance->GetComponentId(ComponentIndex)] = Operation->MutableInstance->GetLODCount(ComponentIndex);
		}
		
		int32 CurrentMinLOD = Operation->bStreamMeshLODs ? 0 : Operation->GetMinLOD();
		CurrentMinLOD = FMath::Clamp(CurrentMinLOD, Operation->FirstLODAvailable, Operation->NumLODsAvailablePerComponent[0] - 1); 
		Operation->SetMinLOD(CurrentMinLOD);

		if (Operation->bStreamMeshLODs)
		{
			Operation->FirstResidentLOD = FMath::Clamp(Operation->FirstResidentLOD, Operation->FirstLODAvailable, Operation->NumLODsAvailablePerComponent[0] - 1); 
		}
		else 
		{
			Operation->FirstResidentLOD = Operation->FirstLODAvailable;
		}
		
		// Initialize RequestedLODs to zero if not set
		TArray<uint16> RequestedLODs = Operation->GetRequestedLODs();
		RequestedLODs.SetNumZeroed(Operation->NumObjectComponents);

		for (int32 ComponentIndex = 0; ComponentIndex < Operation->NumObjectComponents; ++ComponentIndex)
		{
			if (Operation->bStreamMeshLODs)
			{
				RequestedLODs[ComponentIndex] = CurrentMinLOD;
			}
			else
			{
				// Clamp value to the valid LOD range.
				RequestedLODs[ComponentIndex] = FMath::Min(RequestedLODs[ComponentIndex], (uint16)(Operation->NumLODsAvailablePerComponent[0] - 1));
			}
		}

		Operation->SetRequestedLODs(RequestedLODs);
	}
	

	// This runs in a worker thread
	void Subtask_Mutable_PrepareTextures(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Subtask_Mutable_PrepareTextures)

		for (const FInstanceUpdateData::FSurface& Surface : OperationData->InstanceUpdateData.Surfaces)
		{
			for (int32 ImageIndex = 0; ImageIndex<Surface.ImageCount; ++ImageIndex)
			{
				const FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[Surface.FirstImage+ImageIndex];

				const FName KeyName = Image.Name;
				mu::ImagePtrConst MutableImage = Image.Image;

				// If the image is null, it must be in the cache (or repeated in this instance), and we don't need to do anything here.
				if (MutableImage)
				{
					// Image references are just references to texture assets and require no work at all
					if (!MutableImage->IsReference())
					{
						if (!OperationData->ImageToPlatformDataMap.Contains(Image.ImageID))
						{
							FTexturePlatformData* PlatformData = MutableCreateImagePlatformData(MutableImage, -1, Image.FullImageSizeX, Image.FullImageSizeY);
							OperationData->ImageToPlatformDataMap.Add(Image.ImageID, PlatformData);
						}
						else
						{
							// The ImageID already exists in the ImageToPlatformDataMap, that means the equivalent surface in a lower
							// LOD already created the PlatformData for that ImageID and added it to the ImageToPlatformDataMap.
						}
					}
				}
			}
		}
	}
	

	// This runs in a worker thread
	void Subtask_Mutable_PrepareSkeletonData(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Subtask_Mutable_PrepareSkeletonData);

		int32 NumInstanceComponents = OperationData->InstanceUpdateData.Components.Num();
		OperationData->InstanceUpdateData.Skeletons.SetNum(NumInstanceComponents);

		for (int32 ComponentIndex = 0; ComponentIndex< NumInstanceComponents; ++ComponentIndex)
		{
			const FInstanceUpdateData::FComponent& Component = OperationData->InstanceUpdateData.Components[ComponentIndex];

			for (int32 LODIndex = 0; LODIndex < Component.LODCount; ++LODIndex)
			{
				FInstanceUpdateData::FLOD& LOD = OperationData->InstanceUpdateData.LODs[Component.FirstLOD+LODIndex];

				mu::Ptr<const mu::Mesh> Mesh = LOD.Mesh;
				if (!Mesh || Mesh->IsReference())
				{
					continue;
				}

				FInstanceUpdateData::FSkeletonData& SkeletonData = OperationData->InstanceUpdateData.Skeletons[ComponentIndex];

				// Add SkeletonIds 
				const int32 SkeletonIDsCount = Mesh->GetSkeletonIDsCount();
				for (int32 SkeletonIndex = 0; SkeletonIndex < SkeletonIDsCount; ++SkeletonIndex)
				{
					SkeletonData.SkeletonIds.AddUnique(Mesh->GetSkeletonID(SkeletonIndex));
				}

				// Append BoneMap to the array of BoneMaps
				const TArray<mu::FBoneName>& BoneMap = Mesh->GetBoneMap();
				LOD.FirstBoneMap = OperationData->InstanceUpdateData.BoneMaps.Num();
				LOD.BoneMapCount = BoneMap.Num();
				OperationData->InstanceUpdateData.BoneMaps.Append(BoneMap);

				// Add active bone indices and poses
				LOD.FirstActiveBone = OperationData->InstanceUpdateData.ActiveBones.Num();
				LOD.ActiveBoneCount = Mesh->GetBonePoseCount();
				for (uint32 BoneIndex = 0; BoneIndex < LOD.ActiveBoneCount; ++BoneIndex)
				{
					const mu::FBoneName& BoneId = Mesh->GetBonePoseId(BoneIndex);

					OperationData->InstanceUpdateData.ActiveBones.Add(BoneId);

					if (!SkeletonData.BonePose.FindByKey(BoneId))
					{
						FTransform3f Transform;
						Mesh->GetBonePoseTransform(BoneIndex, Transform);
						SkeletonData.BonePose.Add({ BoneId,Transform.Inverse().ToMatrixWithScale() });
					}
				}
			}
		}
	}

	void Subtask_Mutable_PrepareRealTimeMorphData(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(BuildMorphTargetsData);

		FInstanceUpdateData& UpdateData = OperationData->InstanceUpdateData;

		const TMap<uint32, FInstanceUpdateData::FMorphTargetMeshData>& ResourceIdToMeshDataMap =
				UpdateData.RealTimeMorphTargetMeshData;

		if (ResourceIdToMeshDataMap.IsEmpty())
		{
			return;
		}
		
		int32 NumNotFoundLoadedMorphsResources = 0;
		
		for (FInstanceUpdateData::FComponent& Component : OperationData->InstanceUpdateData.Components)
		{
			if (!OperationData->InstanceUpdateData.RealTimeMorphTargets.IsValidIndex(Component.Id))
			{
				OperationData->InstanceUpdateData.RealTimeMorphTargets.SetNum(Component.Id + 1);
			}

			FInstanceUpdateData::FRealTimeMorphsComponentData& ComponentMorphTargetsData = 
					OperationData->InstanceUpdateData.RealTimeMorphTargets[Component.Id];

			ComponentMorphTargetsData.ObjectComponentIndex = Component.Id;
		}

		const int32 NumComponents = OperationData->InstanceUpdateData.RealTimeMorphTargets.Num();
		for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
		{
			FInstanceUpdateData::FRealTimeMorphsComponentData& ComponentMorphTargetsData = 
					OperationData->InstanceUpdateData.RealTimeMorphTargets[ComponentIndex];
			
			if (ComponentMorphTargetsData.ObjectComponentIndex == INDEX_NONE)
			{
				continue;
			}

			struct FMorphTargetMeshData
			{
				TArray<int32> NameResolutionMap;
				TArrayView<const FMorphTargetVertexData> DataView;
			};

			TArray<FName>& MorphTargetNames = ComponentMorphTargetsData.RealTimeMorphTargetNames;
			MorphTargetNames.Empty();

			TMap<uint32, FMorphTargetMeshData> MorphTargetMeshData;
			MorphTargetMeshData.Reserve(ResourceIdToMeshDataMap.Num());
			
			for (const TPair<uint32, FInstanceUpdateData::FMorphTargetMeshData>& MorphTargetResource : ResourceIdToMeshDataMap)
			{
				FMorphTargetMeshData& MeshData = MorphTargetMeshData.FindOrAdd(MorphTargetResource.Key);
				MeshData.DataView = MakeArrayView(MorphTargetResource.Value.Data);

				const int32 NumMorphNames = MorphTargetResource.Value.NameResolutionMap.Num();
				MeshData.NameResolutionMap.SetNumUninitialized(NumMorphNames);

				for (int32 NameIndex = 0; NameIndex < NumMorphNames; ++NameIndex)
				{
					const int32 ResolvedNameIndex = MorphTargetNames.AddUnique(MorphTargetResource.Value.NameResolutionMap[NameIndex]);
					MeshData.NameResolutionMap[NameIndex] = ResolvedNameIndex;
				}
			}

			// Allocate Morph data for used morphs.
			TArray<TArray<FMorphTargetLODModel>>& MorphsData = ComponentMorphTargetsData.RealTimeMorphsLODData;
			const int32 NumMorphs = MorphTargetNames.Num();

			MorphsData.SetNum(MorphTargetNames.Num());
			for (int32 MorphIndex = 0; MorphIndex < NumMorphs; ++MorphIndex)
			{
				MorphsData[MorphIndex].SetNum(OperationData->NumLODsAvailablePerComponent[ComponentIndex]);
			}

			TArray<int32> SectionMorphTargetVerticesCount;
			SectionMorphTargetVerticesCount.SetNumZeroed(ComponentMorphTargetsData.RealTimeMorphTargetNames.Num());

			int32 NumInvalidVertexMorphNamesFound = 0;
			
			const int32 FirstGeneratedLOD = FMath::Max((int32)OperationData->GetRequestedLODs()[ComponentIndex], OperationData->GetMinLOD());
			for (int32 LODIndex = FirstGeneratedLOD; LODIndex < OperationData->NumLODsAvailablePerComponent[ComponentIndex]; ++LODIndex)
			{
				const FInstanceUpdateData::FComponent& Component = UpdateData.Components[ComponentIndex];
				const FInstanceUpdateData::FLOD& LOD = UpdateData.LODs[Component.FirstLOD+LODIndex];
				check(LOD.bGenerated);

				if (!LOD.Mesh)
				{
					continue;
				}
				
				const mu::FMeshBufferSet& MeshSet = LOD.Mesh->GetVertexBuffers();

				int32 VertexMorphsInfoIndexAndCountBufferIndex, VertexMorphsInfoIndexAndCountBufferChannel;
				MeshSet.FindChannel(mu::MBS_OTHER, 0, &VertexMorphsInfoIndexAndCountBufferIndex, &VertexMorphsInfoIndexAndCountBufferChannel);

				int32 VertexMorphsResourceIdBufferIndex, VertexMorphsResourceIdBufferChannel;
				MeshSet.FindChannel(mu::MBS_OTHER, 1, &VertexMorphsResourceIdBufferIndex, &VertexMorphsResourceIdBufferChannel);

				if (VertexMorphsInfoIndexAndCountBufferIndex < 0 || VertexMorphsResourceIdBufferIndex < 0)
				{
					continue;
				}

				const uint32* const VertexMorphsInfoIndexAndCountBuffer = reinterpret_cast<const uint32*>(MeshSet.GetBufferData(VertexMorphsInfoIndexAndCountBufferIndex));
				TArrayView<const uint32> VertexMorphsInfoIndexAndCountView(VertexMorphsInfoIndexAndCountBuffer, MeshSet.GetElementCount());

				const uint32* const VertexMorphsResourceIdBuffer = reinterpret_cast<const uint32*>(MeshSet.GetBufferData(VertexMorphsResourceIdBufferIndex));
				TArrayView<const uint32> VertexMorphsResourceIdView(VertexMorphsResourceIdBuffer, MeshSet.GetElementCount());
					
				const int32 SurfaceCount = LOD.Mesh->GetSurfaceCount();
				for (int32 Section = 0; Section < SurfaceCount; ++Section)
				{
					// Reset SectionMorphTargets.
					for (int32& Elem : SectionMorphTargetVerticesCount)
					{
						Elem = 0;
					}

					int32 FirstVertex, VerticesCount, FirstIndex, IndiciesCount, UnusedBoneIndex, UnusedBoneCount;
					LOD.Mesh->GetSurface(Section, FirstVertex, VerticesCount, FirstIndex, IndiciesCount, UnusedBoneIndex, UnusedBoneCount);

					for (int32 VertexIdx = FirstVertex; VertexIdx < FirstVertex + VerticesCount;)
					{
						// Find a span with the same VertexMorphResourceId to amortise the cost of finding 
						// in the loaded resources map. It is expected to find large consecutive mesh sections pointing to
						// the same loaded resource.

						const int32 SpanStart = VertexIdx++;
						const uint32 CurrentResourceId = VertexMorphsResourceIdView[SpanStart];

						// Vertex with no morphs are marked with 0, skip vertex if the case.
						if (CurrentResourceId == 0)
						{
							continue;
						}

						for (; VertexIdx < FirstVertex + VerticesCount; ++VertexIdx)
						{
							const int32 VertexResourceId = VertexMorphsResourceIdView[VertexIdx];
							// we can skip vertices with no morph without breaking the span.
							if (VertexResourceId == 0)
							{
								continue;
							}

							if (CurrentResourceId != VertexResourceId)
							{
								break;
							}
						}
						const int32 SpanEnd = VertexIdx;

						const FMorphTargetMeshData* MorphTargetReconstructionData = MorphTargetMeshData.Find(CurrentResourceId);

						if (!MorphTargetReconstructionData)
						{
							++NumNotFoundLoadedMorphsResources;
							continue;
						}

						TArrayView<const FMorphTargetVertexData> SpanMorphData = MorphTargetReconstructionData->DataView;
						const int32 NumNamesInResolutionMap = MorphTargetReconstructionData->NameResolutionMap.Num();

						for (int32 SpanVertexIdx = SpanStart; SpanVertexIdx < SpanEnd; ++SpanVertexIdx)
						{
							const uint32 MorphOffsetAndCount = VertexMorphsInfoIndexAndCountView[SpanVertexIdx];
							if (MorphOffsetAndCount == 0)
							{
								continue;
							}

							// See encoding in GenerateMutableSourceMesh.cpp.
							constexpr uint32 Log2MaxNumVerts = 23;
							
							TArrayView<const FMorphTargetVertexData> MorphsVertexDataView = MakeArrayView(
									SpanMorphData.GetData() + (MorphOffsetAndCount & ((1 << Log2MaxNumVerts) - 1)), 
									MorphOffsetAndCount >> Log2MaxNumVerts);

							for (const FMorphTargetVertexData& SourceVertex : MorphsVertexDataView)
							{
								if (SourceVertex.MorphNameIndex >= (uint32)NumNamesInResolutionMap)
								{
									++NumInvalidVertexMorphNamesFound;
									continue;
								}

								const uint32 ResolvedNameIndex =
										MorphTargetReconstructionData->NameResolutionMap[SourceVertex.MorphNameIndex];

								FMorphTargetLODModel& DestMorphLODModel = MorphsData[ResolvedNameIndex][LODIndex];

								DestMorphLODModel.Vertices.Emplace(
										FMorphTargetDelta 
										{ 
											SourceVertex.PositionDelta, 
											SourceVertex.TangentZDelta, 
											static_cast<uint32>(SpanVertexIdx) 
										});

								++SectionMorphTargetVerticesCount[ResolvedNameIndex];
							}
						}
					}

					const int32 SectionMorphTargetsNum = SectionMorphTargetVerticesCount.Num();
					for (int32 MorphIdx = 0; MorphIdx < SectionMorphTargetsNum; ++MorphIdx)
					{
						if (SectionMorphTargetVerticesCount[MorphIdx] > 0)
						{
							FMorphTargetLODModel& MorphTargetLodModel = MorphsData[MorphIdx][LODIndex];

							MorphTargetLodModel.SectionIndices.Add(Section);
							MorphTargetLodModel.NumVertices += SectionMorphTargetVerticesCount[MorphIdx];
						}
					}
				}
			}
	
			if (NumInvalidVertexMorphNamesFound > 0)
			{
				UE_LOG(LogMutable, Warning, TEXT("Invalid real-time morphs names found in instance vertices. Some morph may not work as expected."));
			}
	
			// Remove empty morph targets;
			for (int32 MorphIndex = 0; MorphIndex < NumMorphs; ++MorphIndex)
			{
				const int32 NumLODs = MorphsData[MorphIndex].Num();

				int32 LODIndex = 0;
				for (; LODIndex < NumLODs; ++LODIndex)
				{
					if (!MorphsData[MorphIndex][LODIndex].Vertices.IsEmpty())
					{
						break;
					}
				}

				if (LODIndex >= NumLODs)
				{
					MorphsData[MorphIndex].Empty();
				}
			}

		}

		// Free unneeded data memory.
		UpdateData.RealTimeMorphTargetMeshData.Empty();
		
		if (NumNotFoundLoadedMorphsResources > 0)
		{
			UE_LOG(LogMutable, Warning, TEXT("Needed realtime morph reconstruction data was not loaded properly. Some realtime morphs may not work correctly."));
		}
	}

	/** End of the GetMeshes tasks. */
	void Task_Mutable_GetMeshes_End(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshes_End)

		// TODO: Not strictly mutable: move to another worker thread task to free mutable access?
		Subtask_Mutable_PrepareSkeletonData(OperationData);
		if (OperationData->GetCapturedDescriptor().GetBuildParameterRelevancy())
		{
			Subtask_Mutable_UpdateParameterRelevancy(OperationData);
		}
		else
		{
			OperationData->RelevantParametersInProgress.Reset();
		}

		// Copy ExtensionData Object node input from the Instance to the InstanceUpdateData
		for (int32 ExtensionDataIndex = 0; ExtensionDataIndex < OperationData->MutableInstance->GetExtensionDataCount(); ExtensionDataIndex++)
		{
			mu::Ptr<const mu::ExtensionData> ExtensionData;
			FName Name;
			OperationData->MutableInstance->GetExtensionData(ExtensionDataIndex, ExtensionData, Name);

			check(ExtensionData);

			FInstanceUpdateData::FNamedExtensionData& NewEntry = OperationData->InstanceUpdateData.ExtendedInputPins.AddDefaulted_GetRef();
			NewEntry.Data = ExtensionData;
			NewEntry.Name = Name;
			check(NewEntry.Name != NAME_None);
		}
		
		OperationData->TaskGetMeshTime = FPlatformTime::Seconds() - StartTime;

		TRACE_END_REGION(UE_TASK_MUTABLE_GETMESHES_REGION);
	}


	/** TaskGraph task after GetImage has completed. */
	void Task_Mutable_GetMeshes_GetImage_Post(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetImageData>>& GetImagesData,
		int32 GetImageIndex,
		UE::Tasks::TTask<mu::Ptr<const mu::Image>> GetImageTask)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshes_GetImage_Post)

		const UCustomizableObjectInstance* Instance = OperationData->Instance.Get();
		const UCustomizableObject* CustomizableObject = Instance->GetCustomizableObject();
		const FModelResources& ModelResources = CustomizableObject->GetPrivate()->GetModelResources();

		const int32 ImageIndex = (*GetImagesData)[GetImageIndex].ImageIndex;

		FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[ImageIndex];

		Image.Image = GetImageTask.GetResult();
		check(Image.Image->IsReference());

		const uint32 ReferenceID = Image.Image->GetReferencedTexture();

		if (ModelResources.PassThroughTextures.IsValidIndex(ReferenceID))
		{
			const TSoftObjectPtr<const UTexture> Ref = ModelResources.PassThroughTextures[ReferenceID];
			Instance->GetPrivate()->PassThroughTexturesToLoad.Add(Ref);
		}
		else
		{
			// internal error.
			UE_LOG(LogMutable, Error, TEXT("Referenced image [%d] was not stored in the resource array."), ReferenceID);
		}
			
		Task_Mutable_GetMeshes_GetImage_Loop(OperationData, StartTime, GetImagesData, ++GetImageIndex);
	}

	
	/** See declaration. */
	void Task_Mutable_GetMeshes_GetImage_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetImageData>>& GetImagesData,
		int32 GetImageIndex)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMesh_GetImages_Loop)

		if (GetImageIndex >= GetImagesData->Num()) 
		{
			Task_Mutable_GetMeshes_End(OperationData, StartTime);
			return;
		}

		const FGetImageData& ImageData = (*GetImagesData)[GetImageIndex];
		
		UE::Tasks::TTask<mu::Ptr<const mu::Image>> GetImageTask = OperationData->MutableSystem->GetImage(OperationData->InstanceID, ImageData.ImageID, 0, 0);

		UE::Tasks::AddNested(UE::Tasks::Launch(TEXT("Task_Mutable_GetMeshes_GetImage_Post"), [=]()
		{
			Task_Mutable_GetMeshes_GetImage_Post(OperationData, StartTime, GetImagesData, GetImageIndex, GetImageTask);
		},
		GetImageTask,
		LowLevelTasks::ETaskPriority::Inherit));
	}


	/** Gather all GetImages that have to be called. */
	void Task_Mutable_GetMeshes_GetImages(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshes_GetImages)
		
		const UCustomizableObjectInstance* Instance = OperationData->Instance.Get();
		const UCustomizableObject* CustomizableObject = Instance->GetCustomizableObject();
		const FModelResources& ModelResources = CustomizableObject->GetPrivate()->GetModelResources();

		const mu::Instance* MutableInstance = OperationData->MutableInstance.get();

		TArray<int32> SurfacesSharedId;

		const TSharedRef<TArray<FGetImageData>> GetImagesData = MakeShared<TArray<FGetImageData>>();
		
		for (int32 InstanceComponentIndex = 0; InstanceComponentIndex < OperationData->NumInstanceComponents; ++InstanceComponentIndex)
		{
			FInstanceUpdateData::FComponent& Component = OperationData->InstanceUpdateData.Components[InstanceComponentIndex];
			for (int32 MutableLODIndex = 0; MutableLODIndex < Component.LODCount; ++MutableLODIndex)
			{
				// Skip LODs outside the range we want to generate
				if (MutableLODIndex < OperationData->GetMinLOD())
				{
					continue;
				}

				FInstanceUpdateData::FLOD& LOD = OperationData->InstanceUpdateData.LODs[Component.FirstLOD + MutableLODIndex];

				LOD.FirstSurface = OperationData->InstanceUpdateData.Surfaces.Num();
				LOD.SurfaceCount = 0;

				if (!LOD.Mesh)
				{
					continue;
				}
				

				// This lambda does all the work to fill up the surface data
				auto AddSurface = 
					[&LOD, &SurfacesSharedId, &ModelResources, &GetImagesData, OperationData, MutableInstance, CustomizableObject, InstanceComponentIndex, MutableLODIndex]
					(uint32 SurfaceId, uint32 SurfaceMetadataId, int32 InstanceSurfaceIndex)
					{
						int32 BaseSurfaceIndex = InstanceSurfaceIndex;
						int32 BaseLODIndex = MutableLODIndex;

						OperationData->InstanceUpdateData.Surfaces.Push({});
						FInstanceUpdateData::FSurface& Surface = OperationData->InstanceUpdateData.Surfaces.Last();
						++LOD.SurfaceCount;

						// Now Surface.MaterialIndex is decoded from a parameter at the end of this if()
						Surface.SurfaceId = SurfaceId;
						Surface.SurfaceMetadataId = SurfaceMetadataId;

						const int32 SharedSurfaceId = MutableInstance->GetSharedSurfaceId(InstanceComponentIndex, MutableLODIndex, InstanceSurfaceIndex);
						const int32 SharedSurfaceIndex = SurfacesSharedId.Find(SharedSurfaceId);

						SurfacesSharedId.Add(SharedSurfaceId);

						if (SharedSurfaceId != INDEX_NONE)
						{
							if (SharedSurfaceIndex >= 0)
							{
								Surface = OperationData->InstanceUpdateData.Surfaces[SharedSurfaceIndex];
								return;
							}

							// Find the first LOD where this surface can be found
							MutableInstance->FindBaseSurfaceBySharedId(InstanceComponentIndex, SharedSurfaceId, BaseSurfaceIndex, BaseLODIndex);

							Surface.SurfaceId = MutableInstance->GetSurfaceId(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex);
							Surface.SurfaceMetadataId = MutableInstance->GetSurfaceCustomId(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex);
						}

						// Vectors
						Surface.FirstVector = OperationData->InstanceUpdateData.Vectors.Num();
						Surface.VectorCount = MutableInstance->GetVectorCount(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex);
						for (int32 VectorIndex = 0; VectorIndex < Surface.VectorCount; ++VectorIndex)
						{
							MUTABLE_CPUPROFILER_SCOPE(GetVector);
							OperationData->InstanceUpdateData.Vectors.Push({});
							FInstanceUpdateData::FVector& Vector = OperationData->InstanceUpdateData.Vectors.Last();
							Vector.Name = MutableInstance->GetVectorName(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, VectorIndex);
							Vector.Vector = MutableInstance->GetVector(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, VectorIndex);
						}

						// Scalars
						Surface.FirstScalar = OperationData->InstanceUpdateData.Scalars.Num();
						Surface.ScalarCount = MutableInstance->GetScalarCount(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex);
						for (int32 ScalarIndex = 0; ScalarIndex < Surface.ScalarCount; ++ScalarIndex)
						{
							MUTABLE_CPUPROFILER_SCOPE(GetScalar)

								const FName ScalarName = MutableInstance->GetScalarName(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, ScalarIndex);
							const float ScalarValue = MutableInstance->GetScalar(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, ScalarIndex);

							FString EncodingMaterialIdString = "__MutableMaterialId";

							// Decoding Material Switch from Mutable parameter name
							if (ScalarName.ToString().Equals(EncodingMaterialIdString))
							{
								Surface.MaterialIndex = static_cast<uint32>(ScalarValue);

								// This parameter is not needed in the final material instance
								Surface.ScalarCount -= 1;
							}
							else
							{
								OperationData->InstanceUpdateData.Scalars.Push({ ScalarName, ScalarValue });
							}
						}

						// Images
						Surface.FirstImage = OperationData->InstanceUpdateData.Images.Num();
						Surface.ImageCount = MutableInstance->GetImageCount(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex);
						for (int32 ImageIndex = 0; ImageIndex < Surface.ImageCount; ++ImageIndex)
						{
							MUTABLE_CPUPROFILER_SCOPE(GetImageId);

							const int32 UpdateDataImageIndex = OperationData->InstanceUpdateData.Images.AddDefaulted();
							FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images.Last();
							Image.Name = MutableInstance->GetImageName(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, ImageIndex);
							Image.ImageID = MutableInstance->GetImageId(InstanceComponentIndex, BaseLODIndex, BaseSurfaceIndex, ImageIndex);
							Image.FullImageSizeX = 0;
							Image.FullImageSizeY = 0;
							Image.BaseLOD = BaseLODIndex;
							Image.BaseMip = 0;

							FString KeyName = Image.Name.ToString();
							int32 ImageKey = FCString::Atoi(*KeyName);

							if (ImageKey >= 0 && ImageKey < ModelResources.ImageProperties.Num())
							{
								const FMutableModelImageProperties& Props = ModelResources.ImageProperties[ImageKey];

								Image.bIsNonProgressive = Props.MipGenSettings == TMGS_NoMipmaps;

								if (Props.IsPassThrough)
								{
									Image.bIsPassThrough = true;

									// Since it's known it's a pass-through texture there is no need to cache or convert it so we can generate it here already.
									GetImagesData->Add({ UpdateDataImageIndex, Image.ImageID });
								}
							}
							else
							{
								// This means the compiled model (maybe coming from derived data) has images that the asset doesn't know about.
								UE_LOG(LogMutable, Error, TEXT("CustomizableObject derived data out of sync with asset for [%s]. Try recompiling it."), *CustomizableObject->GetName());
							}
						}

					};

				// Materials and images

				// If the mesh is a reference mesh, it won't have the surface information in the mutable mesh. We need to get it from the instance
				// and all defined surfaces will be present. 
				if (LOD.Mesh->IsReference())
				{
					const int32 SurfaceCount = MutableInstance->GetSurfaceCount(InstanceComponentIndex, MutableLODIndex);
					for (int32 SurfaceIndex = 0; SurfaceIndex < SurfaceCount; ++SurfaceIndex)
					{
						uint32 SurfaceId = MutableInstance->GetSurfaceId(InstanceComponentIndex, MutableLODIndex, SurfaceIndex);
						uint32 SurfaceMetadataId = MutableInstance->GetSurfaceCustomId(InstanceComponentIndex, MutableLODIndex, SurfaceIndex);
						AddSurface(SurfaceId, SurfaceMetadataId, SurfaceIndex);
					}
				}

				// If the mesh is a not a reference mesh, we have to add only the materials of the surfaces that appear in the actual final mesh. 
				else
				{
					const int32 SurfaceCount = LOD.Mesh->GetSurfaceCount();
					for (int32 MeshSurfaceIndex = 0; MeshSurfaceIndex < SurfaceCount; ++MeshSurfaceIndex)
					{
						const uint32 SurfaceId = LOD.Mesh->GetSurfaceId(MeshSurfaceIndex);

						const int32 InstanceSurfaceIndex = MutableInstance->FindSurfaceById(InstanceComponentIndex, MutableLODIndex, SurfaceId);
						check(LOD.Mesh->GetVertexCount() > 0 || InstanceSurfaceIndex >= 0);

						if (InstanceSurfaceIndex >= 0)
						{
							AddSurface(SurfaceId, 0, InstanceSurfaceIndex);
						}
					}
				}

			}	
		}

		Task_Mutable_GetMeshes_GetImage_Loop(OperationData, StartTime, GetImagesData, 0);
	}


	/** TaskGraph task after GetMesh has completed. */
	void Task_MutableGetMeshes_GetMesh_Post(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetMeshData>>& GetMeshesData,
		int32 GetMeshIndex,
		UE::Tasks::TTask<mu::Ptr<const mu::Mesh>> GetMeshTask)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_MutableGetMeshes_GetMesh_Post)

		const int32 LODIndex = (*GetMeshesData)[GetMeshIndex].LODIndex;
		FInstanceUpdateData::FLOD& LOD = OperationData->InstanceUpdateData.LODs[LODIndex];

		LOD.Mesh = GetMeshTask.GetResult();

		if (LOD.Mesh &&
			LOD.Mesh->IsReference())
		{
			const UCustomizableObjectInstance* Instance = OperationData->Instance.Get();
			const UCustomizableObject* CustomizableObject = Instance->GetCustomizableObject();
			const FModelResources& ModelResources = CustomizableObject->GetPrivate()->GetModelResources();

			uint32 ReferenceID = LOD.Mesh->GetReferencedMesh();

			if (ModelResources.PassThroughMeshes.IsValidIndex(ReferenceID))
			{
				TSoftObjectPtr<const USkeletalMesh> Ref = ModelResources.PassThroughMeshes[ReferenceID];
				Instance->GetPrivate()->PassThroughMeshesToLoad.Add(Ref);
			}
			else
			{
				// internal error.
				UE_LOG(LogMutable, Error, TEXT("Referenced mesh [%d] was not stored in the resource array."), ReferenceID);
			}
		}
			
		Task_Mutable_GetMeshes_GetMesh_Loop(OperationData, StartTime, GetMeshesData, ++GetMeshIndex);
	}
	

	/** See declaration. */
	void Task_Mutable_GetMeshes_GetMesh_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedRef<TArray<FGetMeshData>>& GetMeshesData,
		int32 GetMeshIndex)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshes_GetMesh_Loop)

		if (GetMeshIndex >= GetMeshesData->Num())
		{
			Task_Mutable_GetMeshes_GetImages(OperationData, StartTime);
			return;
		}
		
		const FGetMeshData& MeshData = (*GetMeshesData)[GetMeshIndex];
		UE::Tasks::TTask<mu::Ptr<const mu::Mesh>> GetMeshTask = OperationData->MutableSystem->GetMesh(OperationData->InstanceID, MeshData.MeshID);
		
		UE::Tasks::AddNested(UE::Tasks::Launch(TEXT("Task_MutableGetMeshes_GetMesh_Post"), [=]()
		{
			Task_MutableGetMeshes_GetMesh_Post(OperationData, StartTime, GetMeshesData, GetMeshIndex, GetMeshTask);
		},
		GetMeshTask,
		LowLevelTasks::ETaskPriority::Inherit));
	}


	namespace Impl
	{
		/** Start of the GetMeshes tasks.
		  * Gathers all GetMeshes that has to be called. */
		void Task_Mutable_GetMeshes(const TSharedRef<FUpdateContextPrivate>& OperationData)
		{
			MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshes)
			TRACE_BEGIN_REGION(UE_TASK_MUTABLE_GETMESHES_REGION);

			const double StartTime = FPlatformTime::Seconds();

			check(OperationData->Parameters);
			OperationData->InstanceUpdateData.Clear();

			check(UCustomizableObjectSystem::GetInstance() != nullptr);
			check(UCustomizableObjectSystem::GetInstance()->GetPrivate() != nullptr);

			UCustomizableInstancePrivate* CustomizableObjectInstancePrivateData = OperationData->Instance->GetPrivate();

			CustomizableObjectInstancePrivateData->PassThroughTexturesToLoad.Empty();
			CustomizableObjectInstancePrivateData->PassThroughMeshesToLoad.Empty();

			if (OperationData->PixelFormatOverride)
			{
				OperationData->MutableSystem->SetImagePixelConversionOverride( OperationData->PixelFormatOverride );
			}

			if (!OperationData->bUseMeshCache)
			{
				CreateMutableInstance(OperationData);
				FixLODs(OperationData);
			}

			// Main instance generation step
			const mu::Instance* Instance = OperationData->MutableInstance.get(); // TODO GMTFuture remove
			if (!Instance)
			{
				UE_LOG(LogMutable, Warning, TEXT("An Instace update has failed."));
				Task_Mutable_GetMeshes_End(OperationData, StartTime);
				return;
			}

			const TArray<uint16>& RequestedLODs = OperationData->GetRequestedLODs();

			const TSharedRef<TArray<FGetMeshData>> GetMeshesData = MakeShared<TArray<FGetMeshData>>();
		
			OperationData->InstanceUpdateData.Components.SetNum(OperationData->NumInstanceComponents);
			for (int32 InstanceComponentIndex = 0; InstanceComponentIndex < OperationData->NumInstanceComponents; ++InstanceComponentIndex)
			{
				FInstanceUpdateData::FComponent& Component = OperationData->InstanceUpdateData.Components[InstanceComponentIndex];
				Component.FirstLOD = OperationData->InstanceUpdateData.LODs.Num();
				Component.Id = Instance->GetComponentId(InstanceComponentIndex);

				int32 ObjectComponentIndex = Component.Id;

				if (!OperationData->NumLODsAvailablePerComponent.IsValidIndex(ObjectComponentIndex))
				{
					// It happens in degenerated cases with empty components.
					continue;
				}

				Component.LODCount = OperationData->NumLODsAvailablePerComponent[ObjectComponentIndex];

				for (int32 MutableLODIndex = 0; MutableLODIndex < Component.LODCount; ++MutableLODIndex)
				{
					// If the LOD is not generated we still add an empty one to keep indexes aligned.
					const int32 UpdateDataLODIndex = OperationData->InstanceUpdateData.LODs.AddDefaulted();

					// Skip LODs outside the range we want to generate
					if (MutableLODIndex < OperationData->GetMinLOD())
					{
						continue;
					}

					FInstanceUpdateData::FLOD& LOD = OperationData->InstanceUpdateData.LODs.Last();

					const bool bGenerateLOD = RequestedLODs.IsValidIndex(ObjectComponentIndex) ? RequestedLODs[ObjectComponentIndex] <= MutableLODIndex : true;

					// Mesh
					{
						MUTABLE_CPUPROFILER_SCOPE(GetMesh);

						LOD.MeshID = Instance->GetMeshId(InstanceComponentIndex, MutableLODIndex);

						if (bGenerateLOD)
						{
							LOD.bGenerated = true;

							GetMeshesData->Add({ UpdateDataLODIndex, LOD.MeshID});
						}
					}
				}
			}

			Task_Mutable_GetMeshes_GetMesh_Loop(OperationData, StartTime, GetMeshesData, 0);
		}
	}


	void Task_Mutable_GetMeshes(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		Impl::Task_Mutable_GetMeshes(OperationData);
	}

	
		/** End of the GetImages tasks. */
	void Task_Mutable_GetImages_End(const TSharedRef<FUpdateContextPrivate>& OperationData, double StartTime)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages_End)
		
		// TODO: Not strictly mutable: move to another worker thread task to free mutable access?
		Subtask_Mutable_PrepareTextures(OperationData);
		
		OperationData->TaskGetImagesTime = FPlatformTime::Seconds() - StartTime;

		TRACE_END_REGION(UE_TASK_MUTABLE_GETIMAGES_REGION);
	}


	/** Call GetImageDesc.
	  * Once GetImageDesc is called, the task must end. Following code will be in a subsequent TaskGraph task. */
	void Task_Mutable_GetImages_GetImageDesc(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
		int32 ImageIndex)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages_GetImageDesc)

		FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[ImageIndex];
		
		// This should only be done when using progressive images, since GetImageDesc does some actual processing.
		UE::Tasks::TTask<mu::FImageDesc> GetImageDescTask = OperationData->MutableSystem->GetImageDesc(OperationData->InstanceID, Image.ImageID);
		
		UE::Tasks::AddNested(UE::Tasks::Launch(TEXT("Task_Mutable_GetImages_GetImage"), [=]()
		{
			Task_Mutable_GetImages_GetImage(OperationData, StartTime, ImagesInThisInstance, ImageIndex, GetImageDescTask);
		},
		GetImageDescTask,
		LowLevelTasks::ETaskPriority::Inherit));
	}


	/** TaskGraph task after GetImage has completed. */
	void Task_Mutable_GetImages_GetImage_Post(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
    	double StartTime,
    	const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
    	int32 ImageIndex,
    	UE::Tasks::TTask<mu::Ptr<const mu::Image>> GetImageTask,
    	int32 MipSizeX,
		int32 MipSizeY,
		int32 FullLODContent,
		int32 MipsToSkip)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages_GetImage_Post)

		FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[ImageIndex];

		Image.Image = GetImageTask.GetResult();
		
		check(Image.Image);

		// We should have generated exactly this size.
		const bool bSizeMissmatch = Image.Image->GetSizeX() != MipSizeX || Image.Image->GetSizeY() != MipSizeY;
		if (bSizeMissmatch)
		{
			// Generate a correctly-sized but empty image instead, to avoid crashes.
			UE_LOG(LogMutable, Warning, TEXT("Mutable generated a wrongly-sized image %llu."), Image.ImageID);
			Image.Image = new mu::Image(MipSizeX, MipSizeY, FullLODContent - MipsToSkip, Image.Image->GetFormat(), mu::EInitializationType::Black);
		}

		// We need one mip or the complete chain. Otherwise there was a bug.
		const int32 FullMipCount = Image.Image->GetMipmapCount(Image.Image->GetSizeX(), Image.Image->GetSizeY());
		const int32 RealMipCount = Image.Image->GetLODCount();

		bool bForceMipchain = 
			// Did we fail to generate the entire mipchain (if we have mips at all)?
			(RealMipCount != 1) && (RealMipCount != FullMipCount);

		if (bForceMipchain)
		{
			MUTABLE_CPUPROFILER_SCOPE(GetImage_MipFix);

			UE_LOG(LogMutable, Warning, TEXT("Mutable generated an incomplete mip chain for image %llu."), Image.ImageID);

			// Force the right number of mips. The missing data will be black.
			const mu::Ptr<mu::Image> NewImage = new mu::Image(Image.Image->GetSizeX(), Image.Image->GetSizeY(), FullMipCount, Image.Image->GetFormat(), mu::EInitializationType::Black);
			check(NewImage);	
			// Formats with BytesPerBlock == 0 will not allocate memory. This type of images are not expected here.
			check(!NewImage->DataStorage.IsEmpty());

			for (int32 L = 0; L < RealMipCount; ++L)
			{
				TArrayView<uint8> DestView = NewImage->DataStorage.GetLOD(L);
				TArrayView<const uint8> SrcView = Image.Image->DataStorage.GetLOD(L);

				check(DestView.Num() == SrcView.Num());
				FMemory::Memcpy(DestView.GetData(), SrcView.GetData(), DestView.Num());
			}
			Image.Image = NewImage;
		}

		ImagesInThisInstance->Add(Image.ImageID);

		Task_Mutable_GetImages_Loop(OperationData, StartTime, ImagesInThisInstance, ++ImageIndex);
	}


	/** See declaration. */
	void Task_Mutable_GetImages_GetImage(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
		int32 ImageIndex,
		UE::Tasks::TTask<mu::FImageDesc> GetImageDescTask)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages_GetImage)

		const mu::FImageDesc& ImageDesc = GetImageDescTask.GetResult();

		FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[ImageIndex];

		const UCustomizableObjectSystemPrivate* CustomizableObjectSystemPrivateData = UCustomizableObjectSystem::GetInstanceChecked()->GetPrivate();
		
		{
			const uint16 MaxTextureSizeToGenerate = static_cast<uint16>(CustomizableObjectSystemPrivateData->MaxTextureSizeToGenerate);
			const uint16 MaxSize = FMath::Max(ImageDesc.m_size[0], ImageDesc.m_size[1]);
			uint16 Reduction = 1;

			if (MaxTextureSizeToGenerate > 0 && MaxSize > MaxTextureSizeToGenerate)
			{
				// Find the reduction factor, and the BaseMip of the texture.
				const uint32 NextPowerOfTwo = FMath::RoundUpToPowerOfTwo(FMath::DivideAndRoundUp(MaxSize, MaxTextureSizeToGenerate));
				Reduction = FMath::Max(NextPowerOfTwo, 2U); // At least divide the texture by a factor of two
				Image.BaseMip = FMath::FloorLog2(Reduction);
			}

			Image.FullImageSizeX = ImageDesc.m_size[0] / Reduction;
			Image.FullImageSizeY = ImageDesc.m_size[1] / Reduction;
		}

		const bool bCached = ImagesInThisInstance->Contains(Image.ImageID) || // See if it is cached from this same instance (can happen with LODs)
			(UCustomizableObjectSystem::ShouldReuseTexturesBetweenInstances() && CustomizableObjectSystemPrivateData->ProtectedObjectCachedImages.Contains(Image.ImageID)); // See if it is cached from another instance

		if (bCached)
		{
			UE_LOG(LogMutable, VeryVerbose, TEXT("Texture resource with id [%llu] is cached."), Image.ImageID);

			Task_Mutable_GetImages_Loop(OperationData, StartTime, ImagesInThisInstance, ++ImageIndex);
			return;
		}
		
		const int32 MaxSize = FMath::Max(Image.FullImageSizeX, Image.FullImageSizeY);
		const int32 FullLODCount = FMath::CeilLogTwo(MaxSize) + 1;
		const int32 MinMipsInImage = FMath::Min(FullLODCount, UTexture::GetStaticMinTextureResidentMipCount());
		const int32 MaxMipsToSkip = FullLODCount - MinMipsInImage;
		int32 MipsToSkip = FMath::Min(MaxMipsToSkip, OperationData->MipsToSkip);

		if (Image.bIsNonProgressive || !FMath::IsPowerOfTwo(Image.FullImageSizeX) || !FMath::IsPowerOfTwo(Image.FullImageSizeY))
		{
			// It doesn't make sense to skip mips as non-power-of-two size textures cannot be streamed anyway
			MipsToSkip = 0;
		}

		const int32 MipSizeX = FMath::Max(Image.FullImageSizeX >> MipsToSkip, 1);
		const int32 MipSizeY = FMath::Max(Image.FullImageSizeY >> MipsToSkip, 1);
		if (MipsToSkip > 0 && CustomizableObjectSystemPrivateData->EnableSkipGenerateResidentMips != 0 && OperationData->LowPriorityTextures.Find(Image.Name.ToString()) != INDEX_NONE)
		{
			mu::Ptr<const mu::Image> NewImage = new mu::Image(MipSizeX, MipSizeY, FullLODCount - MipsToSkip, ImageDesc.m_format, mu::EInitializationType::Black);

			UE::Tasks::TTask<mu::Ptr<const mu::Image>> DummyTask = UE::Tasks::MakeCompletedTask<mu::Ptr<const mu::Image>>(NewImage);
			Task_Mutable_GetImages_GetImage_Post(OperationData, StartTime, ImagesInThisInstance, ImageIndex, DummyTask, MipSizeX, MipSizeY, FullLODCount, MipsToSkip);
		}
		else
		{
			const UE::Tasks::TTask<mu::Ptr<const mu::Image>> GetImageTask = OperationData->MutableSystem->GetImage(OperationData->InstanceID, Image.ImageID, Image.BaseMip + MipsToSkip, Image.BaseLOD);
			
			UE::Tasks::AddNested(UE::Tasks::Launch(TEXT("Task_Mutable_GetImages_GetImage_Post"), [=]()
			{
				Task_Mutable_GetImages_GetImage_Post(OperationData, StartTime, ImagesInThisInstance, ImageIndex, GetImageTask, MipSizeX, MipSizeY, FullLODCount, MipsToSkip);
			},
			GetImageTask,
			LowLevelTasks::ETaskPriority::Inherit));
		}
	}


	/** See declaration. */
	void Task_Mutable_GetImages_Loop(
		const TSharedRef<FUpdateContextPrivate>& OperationData,
		double StartTime,
		const TSharedPtr<TArray<mu::FResourceID>>& ImagesInThisInstance,
		int32 ImageIndex)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages_Loop)

		// Process next image. Some images are skipped
		for (; ImageIndex < OperationData->InstanceUpdateData.Images.Num(); ++ImageIndex)
		{
			const FInstanceUpdateData::FImage& Image = OperationData->InstanceUpdateData.Images[ImageIndex];
			if (!Image.bIsPassThrough)
			{
				Task_Mutable_GetImages_GetImageDesc(OperationData, StartTime, ImagesInThisInstance, ImageIndex);
				return;
			}
		}

		// If not image needs to be processed, go to end directly
		Task_Mutable_GetImages_End(OperationData, StartTime);
	}


	namespace Impl
	{
		// This runs in a worker thread.
		void Task_Mutable_GetImages(const TSharedRef<FUpdateContextPrivate>& OperationData)
		{
			MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetImages)
			TRACE_BEGIN_REGION(UE_TASK_MUTABLE_GETIMAGES_REGION);

			const double StartTime = FPlatformTime::Seconds();		

			const TSharedPtr<TArray<mu::FResourceID>> ImagesInThisInstance = MakeShared<TArray<mu::FResourceID>>();
			Task_Mutable_GetImages_Loop(OperationData, StartTime, ImagesInThisInstance, 0);
		}
		
	}


	/** Start of the GetImages tasks. */
	void Task_Mutable_GetImages(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		Impl::Task_Mutable_GetImages(OperationData);
	}


	// This runs in a worker thread.
	void Task_Mutable_ReleaseInstance(mu::Instance::ID InstanceID, mu::Ptr<mu::System> MutableSystem, bool bLiveUpdateMode)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_ReleaseInstance)

		check(MutableSystem);

		if (InstanceID > 0)
		{
			MutableSystem->EndUpdate(InstanceID);

			if (!bLiveUpdateMode)
			{
				MutableSystem->ReleaseInstance(InstanceID);
			}
		}

		MutableSystem->SetImagePixelConversionOverride(nullptr);

		if (UCustomizableObjectSystem::ShouldClearWorkingMemoryOnUpdateEnd())
		{
			MutableSystem->ClearWorkingMemory();
		}

		UCustomizableObjectSystem::GetInstance()->GetPrivate()->MutableTaskGraph.AllowLaunchingMutableTaskLowPriority(true, true);
	}


	// This runs in a worker thread.
	void Task_Mutable_ReleaseInstanceID(const mu::Instance::ID InstanceID, const mu::Ptr<mu::System>& MutableSystem)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_ReleaseInstanceID)

		if (InstanceID > 0)
		{
			MutableSystem->ReleaseInstance(InstanceID);
		}

		if (UCustomizableObjectSystem::ShouldClearWorkingMemoryOnUpdateEnd())
		{
			MutableSystem->ClearWorkingMemory();
		}
	}


	void Task_Game_ReleasePlatformData(const TSharedPtr<FMutableReleasePlatformOperationData>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_ReleasePlatformData)

		check(OperationData);
		
		TMap<mu::FResourceID, FTexturePlatformData*>& ImageToPlatformDataMap = OperationData->ImageToPlatformDataMap;
		for (const TPair<mu::FResourceID, FTexturePlatformData*>& Pair : ImageToPlatformDataMap)
		{
			delete Pair.Value; // If this is not null then it must mean it hasn't been used, otherwise they would have taken ownership and nulled it
		}
		ImageToPlatformDataMap.Reset();
	}

	
	void Task_Game_Callbacks(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_Callbacks)
		FMutableScopeTimer Timer(OperationData->TaskCallbacksTime);

		check(IsInGameThread());

		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
		if (!System || !System->IsValidLowLevel() || System->HasAnyFlags(RF_BeginDestroyed))
		{
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
			return;
		}

		UCustomizableObjectInstance* CustomizableObjectInstance = OperationData->Instance.Get();

		// TODO: Review checks.
		if (!CustomizableObjectInstance || !CustomizableObjectInstance->IsValidLowLevel() )
		{
			System->ClearCurrentMutableOperation();
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
			return;
		}

		UCustomizableObjectSystemPrivate * CustomizableObjectSystemPrivateData = System->GetPrivate();

		// Actual work
		// TODO MTBL-391: Review This hotfix
		UpdateSkeletalMesh(OperationData);

		// All work is done, release unused textures.
		if (CustomizableObjectSystemPrivateData->bReleaseTexturesImmediately)
		{
			FMutableResourceCache& Cache = CustomizableObjectSystemPrivateData->GetObjectCache(CustomizableObjectInstance->GetCustomizableObject());

			UCustomizableInstancePrivate* CustomizableObjectInstancePrivateData = CustomizableObjectInstance->GetPrivate();
			for (FGeneratedTexture& GeneratedTexture : CustomizableObjectInstancePrivateData->TexturesToRelease)
			{
				UCustomizableInstancePrivate::ReleaseMutableTexture(GeneratedTexture.Key, Cast<UTexture2D>(GeneratedTexture.Texture), Cache);
			}

			CustomizableObjectInstancePrivateData->TexturesToRelease.Empty();
		}

		// End Update
		System->ClearCurrentMutableOperation();

		FinishUpdateGlobal(OperationData);
	}


	void Task_Game_ConvertResources(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_ConvertResources)
		FMutableScopeTimer Timer(OperationData->TaskConvertResourcesTime);

		check(IsInGameThread());

		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
		if (!System || !System->IsValidLowLevel() || System->HasAnyFlags(RF_BeginDestroyed))
		{
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
			return;
		}

		
		if (CVarEnableRealTimeMorphTargets.GetValueOnAnyThread())
		{
			// TODO: This subtask should execute before Convert resources in a worker thread but after 
			// Loading resources. For now keep it here.
			Subtask_Mutable_PrepareRealTimeMorphData(OperationData);
		}

		UCustomizableObjectInstance* CustomizableObjectInstance = OperationData->Instance.Get();

		// Actual work
		// TODO: Review checks.
		const bool bInstanceInvalid = !CustomizableObjectInstance || !CustomizableObjectInstance->IsValidLowLevel();
		if (!bInstanceInvalid)
		{
			UCustomizableInstancePrivate* CustomizableInstancePrivateData = CustomizableObjectInstance->GetPrivate();

			// Convert Step
			//-------------------------------------------------------------

			// \TODO: Bring that code here instead of keeping it in the UCustomizableObjectInstance
			if (CustomizableInstancePrivateData->UpdateSkeletalMesh_PostBeginUpdate0(CustomizableObjectInstance, OperationData))
			{
				// This used to be CustomizableObjectInstance::UpdateSkeletalMesh_PostBeginUpdate1
				{
					MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_PostBeginUpdate1);

					// \TODO: Bring here
					CustomizableInstancePrivateData->BuildMaterials(OperationData, CustomizableObjectInstance);
				}

				// This used to be CustomizableObjectInstance::UpdateSkeletalMesh_PostBeginUpdate2
				{
					MUTABLE_CPUPROFILER_SCOPE(UpdateSkeletalMesh_PostBeginUpdate2);
					
#if WITH_EDITORONLY_DATA
					CustomizableInstancePrivateData->RegenerateImportedModels();
#endif
					CustomizableInstancePrivateData->PostEditChangePropertyWithoutEditor();
				}
			}
		} // if (!bInstanceValid)

		if (FLogBenchmarkUtil::IsBenchmarkingReportingEnabled())
		{
			// Memory used in the context of this the update of mesh
			OperationData->UpdateEndPeakBytes = mu::FGlobalMemoryCounter::GetPeak();
			// Memory used in the context of the mesh update + the baseline memory used by mutable when starting the update
			OperationData->UpdateEndRealPeakBytes = OperationData->UpdateEndPeakBytes + OperationData->UpdateStartBytes;
		}
		
		UCustomizableObjectSystemPrivate* CustomizableObjectSystemPrivateData = System->GetPrivate();

		// Next Task: Release Mutable. We need this regardless if we cancel or not
		//-------------------------------------------------------------		
		{
			const mu::Ptr<mu::System> MutableSystem = CustomizableObjectSystemPrivateData->MutableSystem;
			CustomizableObjectSystemPrivateData->MutableTaskGraph.AddMutableThreadTask(
				TEXT("Task_Mutable_ReleaseInstance"),
				[InstanceID = OperationData->InstanceID, MutableSystem, bLiveUpdateMode = OperationData->bLiveUpdateMode]()
				{
					Task_Mutable_ReleaseInstance(InstanceID, MutableSystem, bLiveUpdateMode);
				});
		}


		// Next Task: Release Platform Data
		//-------------------------------------------------------------
		if (!bInstanceInvalid)
		{
			TSharedPtr<FMutableReleasePlatformOperationData> ReleaseOperationData = MakeShared<FMutableReleasePlatformOperationData>();
			check(ReleaseOperationData);
			
			static_assert(
					std::is_same_v<
						decltype(std::declval<FMutableReleasePlatformOperationData>().ImageToPlatformDataMap), 
						decltype(std::declval<FUpdateContextPrivate>().ImageToPlatformDataMap)
					>, 
					"Cannot move FMutableReleasePlatformOperationData::ImageToPlatformDataMap to FUpdateContextPrivate::ImageToPlatformDataMap, types do not match.");

			ReleaseOperationData->ImageToPlatformDataMap = MoveTemp(OperationData->ImageToPlatformDataMap);
			CustomizableObjectSystemPrivateData->MutableTaskGraph.AddAnyThreadTask(
				TEXT("Mutable_ReleasePlatformData"),
				[ReleaseOperationData]()
				{
					Task_Game_ReleasePlatformData(ReleaseOperationData);
				}
			);


			// Unlock step
			//-------------------------------------------------------------
			if (CustomizableObjectInstance->GetCustomizableObject())
			{
				// Unlock the resource cache for the object used by this instance to avoid
				// the destruction of resources that we may want to reuse.
				System->ClearResourceCacheProtected();
			}

			// Next Task: Callbacks
			//-------------------------------------------------------------
			CustomizableObjectSystemPrivateData->AddGameThreadTask(
				{
				FMutableTaskDelegate::CreateLambda(
					[OperationData]()
					{
						Task_Game_Callbacks(OperationData);
					}),
					{}
				});
		}
		else
		{
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
		}
	}


	/** Lock Cached Resources. */
	void Task_Game_LockCache(const TSharedRef<FUpdateContextPrivate>& OperationData)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_LockCache)
		FMutableScopeTimer Timer(OperationData->TaskLockCacheTime);

		check(IsInGameThread());

		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
		if (!System)
		{
			return;
		}

		UCustomizableObjectInstance* ObjectInstance = OperationData->Instance.Get();
		if (!ObjectInstance)
		{
			System->ClearCurrentMutableOperation();
			
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
			return;
		}

		UCustomizableInstancePrivate* ObjectInstancePrivateData = ObjectInstance->GetPrivate();
		check(ObjectInstancePrivateData != nullptr);

		if (OperationData->bLiveUpdateMode)
		{
			check(OperationData->InstanceID != 0);

			if (ObjectInstancePrivateData->LiveUpdateModeInstanceID == 0)
			{
				// From now this instance will reuse this InstanceID until it gets out of LiveUpdateMode
				ObjectInstancePrivateData->LiveUpdateModeInstanceID = OperationData->InstanceID;
			}
		}

		const UCustomizableObject* CustomizableObject = ObjectInstance->GetCustomizableObject(); 
		if (!CustomizableObject)
		{
			System->ClearCurrentMutableOperation();
			
			OperationData->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(OperationData);
			return;
		}
		
		if (OperationData->GetCapturedDescriptor().GetBuildParameterRelevancy())
		{
			// Relevancy
			ObjectInstancePrivateData->RelevantParameters = OperationData->RelevantParametersInProgress;
		}

		
		// Selectively lock the resource cache for the object used by this instance to avoid the destruction of resources that we may want to reuse.
		// When protecting textures there mustn't be any left from a previous update
		check(System->GetPrivate()->ProtectedCachedTextures.Num() == 0);

		UCustomizableObjectSystemPrivate* SystemPrivateData = System->GetPrivate();

		// TODO: If this is the first code that runs after the CO program has finished AND if it's
		// guaranteed that the next CO program hasn't started yet, we need to call ClearActiveObject
		// and CancelPendingLoads on SystemPrivateData->ExtensionDataStreamer.
		//
		// ExtensionDataStreamer->AreAnyLoadsPending should return false if the program succeeded.
		//
		// If the program aborted, AreAnyLoadsPending may return true, as the program doesn't cancel
		// its own loads on exit (maybe it should?)

		FMutableResourceCache& Cache = SystemPrivateData->GetObjectCache(CustomizableObject);

		System->GetPrivate()->ProtectedCachedTextures.Reset(Cache.Images.Num());
		SystemPrivateData->ProtectedObjectCachedImages.Reset(Cache.Images.Num());

		for (const FInstanceUpdateData::FImage& Image : OperationData->InstanceUpdateData.Images)
		{
			FMutableImageCacheKey Key(Image.ImageID, OperationData->MipsToSkip);
			const TWeakObjectPtr<UTexture2D>* TexturePtr = Cache.Images.Find(Key);

			if (TexturePtr && TexturePtr->Get() && SystemPrivateData->TextureHasReferences(Key))
			{
				System->GetPrivate()->ProtectedCachedTextures.Add(TexturePtr->Get());
				SystemPrivateData->ProtectedObjectCachedImages.Add(Image.ImageID);
			}
		}

		// Any external texture that may be needed for this update will be requested from Mutable Core's GetImage
		// which will safely access the GlobalExternalImages map, and then just get the cached image or issue a disk read

		// Copy data generated in the mutable thread over to the instance
		ObjectInstancePrivateData->PrepareForUpdate(OperationData);

		// Task: Mutable GetImages
		//-------------------------------------------------------------
		UE::Tasks::FTask Mutable_GetImagesTask;
		{
			// Task inputs
			Mutable_GetImagesTask = SystemPrivateData->MutableTaskGraph.AddMutableThreadTask(
				TEXT("Task_Mutable_GetImages"),
				[OperationData]()
				{
					Task_Mutable_GetImages(OperationData);
				});
		}


		// Next Task: Load Unreal Assets
		//-------------------------------------------------------------
		UE::Tasks::FTask Game_LoadUnrealAssets = ObjectInstancePrivateData->LoadAdditionalAssetsAndData(OperationData, System->GetPrivate()->StreamableManager);

		// Next-next Task: Convert Resources
		//-------------------------------------------------------------
		SystemPrivateData->AddGameThreadTask(
			FMutableTask 
			{
				FMutableTaskDelegate::CreateLambda(
				[OperationData]()
				{
					Task_Game_ConvertResources(OperationData);
				}),
				{ Game_LoadUnrealAssets, Mutable_GetImagesTask }
			});
	}


	/** Enqueue the release ID operation in the Mutable queue */
	void Task_Game_ReleaseInstanceID(const mu::Instance::ID IDToRelease)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_ReleaseInstanceID)

		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked();
		UCustomizableObjectSystemPrivate* SystemPrivateData = System->GetPrivate();

		const mu::Ptr<mu::System> MutableSystem = SystemPrivateData->MutableSystem;

		// Task: Release Instance ID
		//-------------------------------------------------------------
		{
			// Task inputs
			SystemPrivateData->MutableTaskGraph.AddMutableThreadTask(
				TEXT("Task_Mutable_ReleaseInstanceID"),
				[IDToRelease, MutableSystem]()
				{
					impl::Task_Mutable_ReleaseInstanceID(IDToRelease, MutableSystem);
				});
		}
	}


	void Task_Game_LockMeshCache(const TSharedRef<FUpdateContextPrivate>& Operation)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_LockMeshCache);

		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked();
		UCustomizableObjectSystemPrivate* SystemPrivate = System->GetPrivate();

		UCustomizableObject* CustomizableObject = Operation->Instance->GetCustomizableObject();
		UCustomizableObjectPrivate* CustomizableObjectPrivate = CustomizableObject->GetPrivate();
		
		for (const TArray<mu::FResourceID>& MeshId : Operation->MeshDescriptors)
		{
			if (USkeletalMesh* CachedMesh = CustomizableObject->GetPrivate()->MeshCache.Get(MeshId))
			{
				Operation->Objects.Emplace(CachedMesh);
			}
		}

		UE::Tasks::FTask Dependency = SystemPrivate->MutableTaskGraph.AddMutableThreadTask(
			TEXT("Task_Mutable_GetMeshes"),
			[Operation]()
			{
				Task_Mutable_GetMeshes(Operation);
			});

		SystemPrivate->AddGameThreadTask(
			FMutableTask
			{
				FMutableTaskDelegate::CreateLambda(
				[Operation]()
				{
					impl::Task_Game_LockCache(Operation);
				}),
				{ Dependency }
			});
	}
	

	void Task_Mutable_GetMeshID(const TSharedRef<FUpdateContextPrivate>& Operation)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Mutable_GetMeshID);


		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked(); // Save since UCustomizableObjectSystem::BeginDestroy always waits for all tasks to finish
		UCustomizableObjectSystemPrivate* SystemPrivate = System->GetPrivate();
		
		CreateMutableInstance(Operation);
		FixLODs(Operation);

		Operation->MeshDescriptors.SetNum(Operation->NumObjectComponents);

		const TArray<uint16>& RequestedLODs = Operation->GetRequestedLODs();
		for (int32 InstanceComponentIndex = 0; InstanceComponentIndex < Operation->NumInstanceComponents; ++InstanceComponentIndex)
		{
			int32 ObjectComponentIndex = Operation->MutableInstance->GetComponentId(InstanceComponentIndex);
			TArray<mu::FResourceID>& MeshId = Operation->MeshDescriptors[ObjectComponentIndex];
			MeshId.Init(MAX_uint64, MAX_MESH_LOD_COUNT);

			
			for (int32 LODIndex = Operation->GetMinLOD(); LODIndex < Operation->NumLODsAvailablePerComponent[ObjectComponentIndex]; ++LODIndex)
			{
				const bool bGenerateLOD = RequestedLODs.IsValidIndex(ObjectComponentIndex) ? RequestedLODs[ObjectComponentIndex] <= LODIndex : true;
				if (bGenerateLOD)
				{
					MeshId[LODIndex] = Operation->MutableInstance->GetMeshId(InstanceComponentIndex, LODIndex);
				}
			}
		}
	}
	
	/** "Start Update" */
	void Task_Game_StartUpdate(const TSharedRef<FUpdateContextPrivate>& Operation)
	{
		MUTABLE_CPUPROFILER_SCOPE(Task_Game_StartUpdate)

		// Check if a level has been loaded
		if (FLogBenchmarkUtil::IsBenchmarkingReportingEnabled() && GWorld)
		{
			Operation->bLevelBegunPlay = GWorld->GetBegunPlay();
		}

		Operation->StartUpdateTime = FPlatformTime::Seconds();

		UCustomizableObjectSystem::GetInstance()->GetPrivate()->MutableTaskGraph.AllowLaunchingMutableTaskLowPriority(false, false);
		
		UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
		check(System != nullptr);

		if (!Operation->Instance.IsValid() || !Operation->Instance->IsValidLowLevel()) // Only start if it hasn't been already destroyed (i.e. GC after finish PIE)
		{
			System->ClearCurrentMutableOperation();

			Operation->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(Operation);
			return;
		}

		UCustomizableObjectInstance* CandidateInstance = Operation->Instance.Get();
		
		UCustomizableInstancePrivate* CandidateInstancePrivateData = CandidateInstance->GetPrivate();
		if (!CandidateInstancePrivateData)
		{
			System->ClearCurrentMutableOperation();
			
			Operation->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(Operation);
			return;
		}

		if (CandidateInstancePrivateData->HasCOInstanceFlags(PendingLODsUpdate))
		{
			CandidateInstancePrivateData->ClearCOInstanceFlags(PendingLODsUpdate);
			// TODO: Is anything needed for this now?
			//Operation->CustomizableObjectInstance->ReleaseMutableInstanceId(); // To make mutable regenerate the LODs even if the instance parameters have not changed
		}

		// Skip update, the requested update is equal to the running update.
		if (Operation->GetCapturedDescriptorHash().IsSubset(CandidateInstancePrivateData->CommittedDescriptorHash))
		{
			System->ClearCurrentMutableOperation();

			Operation->UpdateResult = EUpdateResult::Success;
			UpdateSkeletalMesh(Operation);
			FinishUpdateGlobal(Operation);
			return;
		}

		bool bCancel = false;

		TObjectPtr<UCustomizableObject> CustomizableObject = CandidateInstance->GetCustomizableObject();

		// If the object is locked (for instance, compiling) we skip any instance update.
		if (!CustomizableObject || CustomizableObject->GetPrivate()->bLocked)
		{
			bCancel = true;
		}

		// Only update resources if the instance is in range (it could have got far from the player since the task was queued)
		check(System->GetPrivate()->CurrentInstanceLODManagement != nullptr);
		if (System->GetPrivate()->CurrentInstanceLODManagement->IsOnlyUpdateCloseCustomizableObjectsEnabled()
			&& CandidateInstancePrivateData
			&& CandidateInstancePrivateData->LastMinSquareDistFromComponentToPlayer > FMath::Square(System->GetPrivate()->CurrentInstanceLODManagement->GetOnlyUpdateCloseCustomizableObjectsDist())
			&& CandidateInstancePrivateData->LastMinSquareDistFromComponentToPlayer != FLT_MAX // This means it is the first frame so it has to be updated
		   )
		{
			bCancel = true;
		}

		mu::Ptr<const mu::Parameters> Parameters = Operation->Parameters;
		if (!Parameters)
		{
			bCancel = true;
		}

		if (bCancel)
		{
			System->ClearCurrentMutableOperation();

			Operation->UpdateResult = EUpdateResult::Error;
			FinishUpdateGlobal(Operation);
			return;
		}

		UCustomizableObjectSystemPrivate* SystemPrivateData = System->GetPrivate();

		SystemPrivateData->CurrentInstanceBeingUpdated = CandidateInstance;

		check(SystemPrivateData->ExtensionDataStreamer != nullptr);
		SystemPrivateData->ExtensionDataStreamer->SetActiveObject(CustomizableObject);

		FString StateName = CandidateInstance->GetCustomizableObject()->GetStateName(CandidateInstance->GetPrivate()->GetState());
		const FMutableStateData* StateData = CandidateInstance->GetCustomizableObject()->GetPrivate()->GetModelResources().StateUIDataMap.Find(StateName);

		Operation->bLiveUpdateMode = false;

		if (SystemPrivateData->EnableMutableLiveUpdate)
		{
			Operation->bLiveUpdateMode = StateData ? StateData->bLiveUpdateMode : false;
		}

		Operation->bNeverStream = false;
		Operation->MipsToSkip = 0;

		SystemPrivateData->GetMipStreamingConfig(*CandidateInstance, Operation->bNeverStream, Operation->MipsToSkip);
		
		if (Operation->bLiveUpdateMode && (!Operation->bNeverStream || Operation->MipsToSkip > 0))
		{
			UE_LOG(LogMutable, Warning, TEXT("Instance LiveUpdateMode does not yet support progressive streaming of Mutable textures. Disabling LiveUpdateMode for this update."));
			Operation->bLiveUpdateMode = false;
		}

		Operation->bReuseInstanceTextures = false;

		if (SystemPrivateData->EnableReuseInstanceTextures)
		{
			Operation->bReuseInstanceTextures = StateData ? StateData->bReuseInstanceTextures : false;
			Operation->bReuseInstanceTextures |= CandidateInstancePrivateData->HasCOInstanceFlags(ReuseTextures);
			
			if (Operation->bReuseInstanceTextures && !Operation->bNeverStream)
			{
				UE_LOG(LogMutable, Warning, TEXT("Instance texture reuse requires that the current Mutable state is in non-streaming mode. Change it in the Mutable graph base node in the state definition."));
				Operation->bReuseInstanceTextures = false;
			}
		}

		if (!Operation->bLiveUpdateMode && CandidateInstancePrivateData->LiveUpdateModeInstanceID != 0)
		{
			// The instance was in live update mode last update, but now it's not. So the Id and resources have to be released.
			// Enqueue a new mutable task to release them
			Task_Game_ReleaseInstanceID(CandidateInstancePrivateData->LiveUpdateModeInstanceID);
			CandidateInstancePrivateData->LiveUpdateModeInstanceID = 0;
		}

		Operation->Model = CustomizableObject->GetPrivate()->GetModel().ToSharedRef();

#if WITH_EDITOR
		SystemPrivateData->GetResourceProviderChecked()->CacheRuntimeReferencedImages(Operation->Model.ToSharedRef(), CustomizableObject->GetPrivate()->GetModelResources().RuntimeReferencedTextures);
#endif
		
		// Task: Mutable Update and GetMesh
		//-------------------------------------------------------------
		Operation->InstanceID = Operation->bLiveUpdateMode ? CandidateInstancePrivateData->LiveUpdateModeInstanceID : 0;
		Operation->bUseMeshCache = CustomizableObject->bEnableMeshCache && !Operation->bLiveUpdateMode && UCustomizableObjectSystem::IsMeshCacheEnabled(true);

		const bool bStreamingEnabled = (CustomizableObject->bEnableMeshStreaming || bForceStreamMeshLODs) && bStreamMeshLODs;
		Operation->bStreamMeshLODs = bStreamingEnabled && IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::SkeletalMesh);
#if WITH_EDITOR
		Operation->PixelFormatOverride = SystemPrivateData->ImageFormatOverrideFunc;
#endif

		if (!CandidateInstancePrivateData->HasCOInstanceFlags(ForceGenerateMipTail))
		{
			CustomizableObject->GetPrivate()->GetLowPriorityTextureNames(Operation->LowPriorityTextures);
		}

		bool bRequestAllLODs = !System->IsOnlyGenerateRequestedLODsEnabled() ||
			!System->GetPrivate()->CurrentInstanceLODManagement->IsOnlyGenerateRequestedLODLevelsEnabled();

#if WITH_EDITOR
		// In the editor LOD Management is disabled by default. Overwrite requested LODs when disabled.
		bRequestAllLODs |= !bEnableLODManagmentInEditor;

		for (TObjectIterator<UCustomizableObjectInstanceUsage> CustomizableObjectInstanceUsage; CustomizableObjectInstanceUsage && !bRequestAllLODs; ++CustomizableObjectInstanceUsage)
		{
			if (IsValid(*CustomizableObjectInstanceUsage) && CustomizableObjectInstanceUsage->GetPrivate()->IsNetMode(NM_DedicatedServer))
			{
				continue;
			}

			if (IsValid(*CustomizableObjectInstanceUsage) &&
				CustomizableObjectInstanceUsage->GetCustomizableObjectInstance() == CandidateInstance)
			{
				EWorldType::Type WorldType = EWorldType::Type::None;

				USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(CustomizableObjectInstanceUsage->GetAttachParent());

				if (Parent && Parent->GetWorld())
				{
					WorldType = Parent->GetWorld()->WorldType;
				}

				switch (WorldType)
				{
					// Editor preview instances
				case EWorldType::EditorPreview:
				case EWorldType::None:
					bRequestAllLODs = true;
				default:;
				}
			}
		}
#endif // WITH_EDITOR

		if (bRequestAllLODs)
		{
			TArray<uint16> RequestedLODs = Operation->GetRequestedLODs();
			RequestedLODs.Init(0, Operation->NumObjectComponents);

			Operation->SetRequestedLODs(RequestedLODs);
		}

		UE::Tasks::FTask Mutable_GetMeshTask;
		
		if (Operation->bUseMeshCache)
		{
			Mutable_GetMeshTask = SystemPrivateData->MutableTaskGraph.AddMutableThreadTask(
				TEXT("Task_Mutable_GetMeshID"),
				[Operation]()
				{
					impl::Task_Mutable_GetMeshID(Operation);
				});

			SystemPrivateData->AddGameThreadTask(
				FMutableTask 
				{
					FMutableTaskDelegate::CreateLambda(
					[Operation]()
					{
						impl::Task_Game_LockMeshCache(Operation);
					}),
					{ Mutable_GetMeshTask }
				});
		}
		else
		{
			Mutable_GetMeshTask = SystemPrivateData->MutableTaskGraph.AddMutableThreadTask(
				TEXT("Task_Mutable_GetMeshes"),
				[Operation]()
				{
					Task_Mutable_GetMeshes(Operation);
				});

			// Task: Lock cache
			//-------------------------------------------------------------
			{
				// Task inputs
				SystemPrivateData->AddGameThreadTask(
					FMutableTask 
					{
						FMutableTaskDelegate::CreateLambda(
						[Operation]()
						{
							impl::Task_Game_LockCache(Operation);
						}),
						{ Mutable_GetMeshTask }
					});
			}
		}
	}
} // namespace impl


void UCustomizableObjectSystem::AdvanceCurrentOperation() 
{
	MUTABLE_CPUPROFILER_SCOPE(AdvanceCurrentOperation);

	check(Private != nullptr);

	// See if we have a game-thread task to process
	FMutableTask* PendingTask = Private->PendingTasks.Peek();
	if (PendingTask)
	{
		if (PendingTask->AreDependenciesComplete())
		{
			PendingTask->ClearDependencies();
			PendingTask->Function.Execute();
			Private->PendingTasks.Pop();
		}

		// Don't do anything else until the pending work is completed.
		return;
	}

	// It is safe to do this now.
	Private->UpdateMemoryLimit();

	// If we don't have an ongoing operation, don't do anything.
	if (!Private->CurrentMutableOperation.IsValid())
	{
		return;
	}

	// If we reach here it means:
	// - we have an ongoing operations
	// - we have no pending work for the ongoing operation
	// - so we are starting it.
	{
		MUTABLE_CPUPROFILER_SCOPE(OperationUpdate);

		// Start the first task of the update process. See namespace impl comments above.
		impl::Task_Game_StartUpdate(Private->CurrentMutableOperation.ToSharedRef());
	}
}


bool UCustomizableObjectSystem::Tick(float DeltaTime)
{
	TickInternal(false);
	return true;
}


int32 UCustomizableObjectSystem::TickInternal(const bool bBlocking)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObjectSystem::TickInternal)

	check(IsInGameThread());
	
	// Building instances is not enabled in servers. If at some point relevant collision or animation data is necessary for server logic this will need to be changed.
#if UE_SERVER
	return 0;
#endif

	if (IsEngineExitRequested())
	{
		return 0;
	}

	if (!Private)
	{
		return 0;
	}

	if (GWorld)
	{
		const EWorldType::Type WorldType = GWorld->WorldType;

		if (WorldType != EWorldType::PIE && WorldType != EWorldType::Game && WorldType != EWorldType::Editor && WorldType != EWorldType::GamePreview)
		{
			return 0;
		}
	}

	// \TODO: Review: We should never compile an object from this tick, so this could be removed
#if WITH_EDITOR
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		return 0; // Assets are still being loaded, so subobjects won't be found, compiled objects incomplete and thus updates wrong
	}

	// Do not tick if the CookCommandlet is running.
	if (IsRunningCookCommandlet())
	{
		return 0;
	}
#endif

	Private->UpdateStats();

	FMutableUpdateCandidate* LODUpdateCandidateFound = nullptr;
	
	bool bPendingCompilation = false;
#if WITH_EDITOR
	ICustomizableObjectEditorModule* EditorModule = ICustomizableObjectEditorModule::Get();
	bPendingCompilation = EditorModule && EditorModule->GetNumCompileRequests() > 0;
#endif

	// Get a new operation if we aren't working on one
	if (!Private->CurrentMutableOperation && bIsMutableEnabled && !bPendingCompilation)
	{
		// Reset the instance relevancy
		// The RequestedUpdates only refer to LOD changes. User Customization and discards are handled separately
		FMutableInstanceUpdateMap RequestedLODUpdates;
		
		GetPrivate()->CurrentInstanceLODManagement->UpdateInstanceDistsAndLODs(RequestedLODUpdates);

		for (TObjectIterator<UCustomizableObjectInstance> CustomizableObjectInstance; CustomizableObjectInstance; ++CustomizableObjectInstance)
		{
			if (IsValid(*CustomizableObjectInstance) && CustomizableObjectInstance->GetPrivate())
			{
				UCustomizableInstancePrivate* ObjectInstancePrivateData = CustomizableObjectInstance->GetPrivate();

				if (ObjectInstancePrivateData->HasCOInstanceFlags(UsedByComponentInPlay))
				{
					ObjectInstancePrivateData->TickUpdateCloseCustomizableObjects(**CustomizableObjectInstance, RequestedLODUpdates);
				}
				else if (ObjectInstancePrivateData->HasCOInstanceFlags(UsedByComponent))
				{
					ensure(!RequestedLODUpdates.Contains(*CustomizableObjectInstance));
					ObjectInstancePrivateData->UpdateInstanceIfNotGenerated(**CustomizableObjectInstance, RequestedLODUpdates);
				}
				else
				{
					ensure(!RequestedLODUpdates.Contains(*CustomizableObjectInstance));
				}

				ObjectInstancePrivateData->ClearCOInstanceFlags((ECOInstanceFlags)(UsedByComponent | UsedByComponentInPlay | PendingLODsUpdate)); // TODO MTBL-391: Makes no sense to clear it here, what if an update is requested before we set it back to true
			}
			else
			{
				ensure(!RequestedLODUpdates.Contains(*CustomizableObjectInstance));
			}
		}

		{
			// Look for the highest priority update between the pending updates and the LOD Requested Updates
			EQueuePriorityType MaxPriorityFound = EQueuePriorityType::Low;
			double MaxSquareDistanceFound = TNumericLimits<double>::Max();
			double MinTimeFound = TNumericLimits<double>::Max();
			const FMutablePendingInstanceUpdate* PendingInstanceUpdateFound = nullptr;

			// Look for the highest priority Pending Update
			for (auto Iterator = Private->MutablePendingInstanceWork.GetUpdateIterator(); Iterator; ++Iterator)
			{
				FMutablePendingInstanceUpdate& PendingUpdate = *Iterator;

				if (PendingUpdate.Context->Instance.IsValid())
				{
					const EQueuePriorityType PriorityType = Private->GetUpdatePriority(*PendingUpdate.Context->Instance, false);
					
					if (PendingUpdate.Context->PriorityType <= MaxPriorityFound)
					{
						const double MinSquareDistFromComponentToPlayer = PendingUpdate.Context->Instance->GetPrivate()->MinSquareDistFromComponentToPlayer;
						
						if (MinSquareDistFromComponentToPlayer < MaxSquareDistanceFound ||
							(MinSquareDistFromComponentToPlayer == MaxSquareDistanceFound && PendingUpdate.Context->StartQueueTime < MinTimeFound))
						{
							MaxPriorityFound = PriorityType;
							MaxSquareDistanceFound = MinSquareDistFromComponentToPlayer;
							MinTimeFound = PendingUpdate.Context->StartQueueTime;
							PendingInstanceUpdateFound = &PendingUpdate;
							LODUpdateCandidateFound = nullptr;
						}
					}
				}
				else
				{
					Iterator.RemoveCurrent();
				}
			}

			// Look for a higher priority LOD update
			for (TPair<const UCustomizableObjectInstance*, FMutableUpdateCandidate>& LODUpdateTuple : RequestedLODUpdates)
			{
				const UCustomizableObjectInstance* Instance = LODUpdateTuple.Key;

				if (Instance)
				{
					FMutableUpdateCandidate& LODUpdateCandidate = LODUpdateTuple.Value;
					ensure(LODUpdateCandidate.HasBeenIssued());

					if (LODUpdateCandidate.Priority <= MaxPriorityFound)
					{
						UCustomizableInstancePrivate* CustomizableInstancePrivate = LODUpdateCandidate.CustomizableObjectInstance->GetPrivate();

						FDescriptorHash LODUpdateDescriptorHash = CustomizableInstancePrivate->CommittedDescriptorHash;
						LODUpdateDescriptorHash.MinLOD = LODUpdateCandidate.MinLOD;
						LODUpdateDescriptorHash.RequestedLODsPerComponent = LODUpdateCandidate.RequestedLODLevels;

						if (CustomizableInstancePrivate->MinSquareDistFromComponentToPlayer < MaxSquareDistanceFound &&
							!LODUpdateDescriptorHash.IsSubset(CustomizableInstancePrivate->CommittedDescriptorHash))
						{
							MaxPriorityFound = LODUpdateCandidate.Priority;
							MaxSquareDistanceFound = CustomizableInstancePrivate->MinSquareDistFromComponentToPlayer;
							PendingInstanceUpdateFound = nullptr;
							LODUpdateCandidateFound = &LODUpdateCandidate;
						}
					}
				}
			}

			Private->NumLODUpdatesLastTick = RequestedLODUpdates.Num();

			// If the chosen LODUpdate has the same instance as a PendingUpdate, choose the PendingUpdate to apply both the LOD update
			// and customization change
			if (LODUpdateCandidateFound)
			{
				if (const FMutablePendingInstanceUpdate *PendingUpdateWithSameInstance = Private->MutablePendingInstanceWork.GetUpdate(LODUpdateCandidateFound->CustomizableObjectInstance))
				{
					PendingInstanceUpdateFound = PendingUpdateWithSameInstance;
					LODUpdateCandidateFound = nullptr;

					// In the processing of the PendingUpdate just below, it will add the LODUpdate's LOD params
				}
			}

			if (PendingInstanceUpdateFound)
			{
				check(!LODUpdateCandidateFound);

				UCustomizableObjectInstance* PendingInstance = PendingInstanceUpdateFound->Context->Instance.Get();
				check(PendingInstance);

				// Maybe there's a LODUpdate that has the same instance, merge both updates as an optimization
				FMutableUpdateCandidate* LODUpdateWithSameInstance = RequestedLODUpdates.Find(PendingInstance);
				
				if (LODUpdateWithSameInstance)
				{
					LODUpdateWithSameInstance->ApplyLODUpdateParamsToInstance(PendingInstanceUpdateFound->Context.Get());
				}

				Private->StartUpdateSkeletalMesh(PendingInstanceUpdateFound->Context);				
				Private->MutablePendingInstanceWork.RemoveUpdate(PendingInstanceUpdateFound->Context->Instance);
			}
			else if (LODUpdateCandidateFound)
			{
				UCustomizableObjectInstance* Instance = LODUpdateCandidateFound->CustomizableObjectInstance;
				const FCustomizableObjectInstanceDescriptor& Descriptor = Instance->GetPrivate()->SkeletalMeshStatus == ESkeletalMeshStatus::NotGenerated ? Instance->GetPrivate()->GetDescriptor() : Instance->GetPrivate()->CommittedDescriptor;

				const TSharedRef<FUpdateContextPrivate> Context = MakeShared<FUpdateContextPrivate>(*Instance, Descriptor);

				// Commit the LOD changes
				LODUpdateCandidateFound->ApplyLODUpdateParamsToInstance(*Context);

				Private->StartUpdateSkeletalMesh(Context);
			}
		}

		{
			for (TObjectIterator<UCustomizableObjectInstance> CustomizableObjectInstance; CustomizableObjectInstance; ++CustomizableObjectInstance)
			{
				if (IsValid(*CustomizableObjectInstance) && CustomizableObjectInstance->GetPrivate())
				{
					CustomizableObjectInstance->GetPrivate()->LastMinSquareDistFromComponentToPlayer = CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer;
					CustomizableObjectInstance->GetPrivate()->MinSquareDistFromComponentToPlayer = FLT_MAX;
				}
			}
		}

		// Update the streaming limit if it has changed. It is safe to do this now.
		Private->UpdateMemoryLimit();

		// Free memory before starting the new update
		DiscardInstances();
		ReleaseInstanceIDs();
	}
	
	// Advance the current operation
	if (Private->CurrentMutableOperation)
	{
		AdvanceCurrentOperation();
	}
	
	const int32 RemainingTasks = Private->MutableTaskGraph.Tick();

	Private->LogBenchmarkUtil.UpdateStats(); // Must to be the last thing to perform

	if (!bIsMutableEnabled && !Private->CurrentMutableOperation)
	{
		if (GetMutableDefault<UCustomizableObjectSettings>()->bEnableStreamingManager)
		{
			FStreamingManagerCollection::Get().RemoveStreamingManager(GetPrivate());
		}
		else
		{
			// Mutable has been disabled. Unregister the ticker if there is no CurrentMutableOperation.
			FTSTicker::GetCoreTicker().RemoveTicker(Private->TickDelegateHandle);
			Private->TickDelegateHandle.Reset();
		}
	}

	int32 RemainingWork = Private->CurrentMutableOperation.IsValid() + 
		Private->MutablePendingInstanceWork.Num() +
		static_cast<int32>(LODUpdateCandidateFound != nullptr) + // Still a pending LOD update. We can not use the size of RequestedLODUpdates since not all requests valid in future ticks.
		RemainingTasks;

	if (bBlocking)
	{
#if WITH_EDITOR
		RemainingWork += EditorModule ? EditorModule->Tick(true) : 0;
#endif
		
		if (GetPrivate()->CurrentMutableOperation)
		{
			UCustomizableInstancePrivate* InstancePrivate = GetPrivate()->CurrentMutableOperation->Instance->GetPrivate();
			
			if (InstancePrivate->StreamingHandle)
			{
				InstancePrivate->StreamingHandle->CancelHandle();
				GetPrivate()->StreamableManager.RequestSyncLoad(InstancePrivate->AssetsToStream);
				InstancePrivate->AdditionalAssetsAsyncLoaded();
			}
		}
	}
	
	return RemainingWork;
}


TAutoConsoleVariable<int32> CVarMaxNumInstancesToDiscardPerTick(
	TEXT("mutable.MaxNumInstancesToDiscardPerTick"),
	30,
	TEXT("The maximum number of stale instances that will be discarded per tick by Mutable."),
	ECVF_Scalability);


void UCustomizableObjectSystem::DiscardInstances()
{
	MUTABLE_CPUPROFILER_SCOPE(DiscardInstances);

	check(IsInGameThread());

	// Handle instance discards
	int32 NumInstancesDiscarded = 0;
	const int32 DiscardLimitPerTick = CVarMaxNumInstancesToDiscardPerTick.GetValueOnGameThread();

	for (TSet<FMutablePendingInstanceDiscard, FPendingInstanceDiscardKeyFuncs>::TIterator Iterator = Private->MutablePendingInstanceWork.GetDiscardIterator();
		Iterator && NumInstancesDiscarded < DiscardLimitPerTick;
		++Iterator)
	{

		UCustomizableObjectInstance* COI = Iterator->CustomizableObjectInstance.Get();
		
		const bool bUpdating = Private->CurrentMutableOperation && Private->CurrentMutableOperation->Instance == Iterator->CustomizableObjectInstance;
		if (COI && COI->GetPrivate() && !bUpdating)
		{
			UCustomizableInstancePrivate* COIPrivateData = COI ? COI->GetPrivate() : nullptr;

			// Only discard resources if the instance is still out range (it could have got closer to the player since the task was queued)
			if (!GetPrivate()->CurrentInstanceLODManagement->IsOnlyUpdateCloseCustomizableObjectsEnabled() ||
				COIPrivateData->LastMinSquareDistFromComponentToPlayer > FMath::Square(GetPrivate()->CurrentInstanceLODManagement->GetOnlyUpdateCloseCustomizableObjectsDist()))
			{
				COIPrivateData->DiscardResources();
				COIPrivateData->SetDefaultSkeletalMesh(!IsReplaceDiscardedWithReferenceMeshEnabled());
			}
		}

		Iterator.RemoveCurrent();
		NumInstancesDiscarded++;
	}
}


TAutoConsoleVariable<int32> CVarMaxNumInstanceIDsToReleasePerTick(
	TEXT("mutable.MaxNumInstanceIDsToReleasePerTick"),
	30,
	TEXT("The maximum number of stale instances IDs that will be released per tick by Mutable."),
	ECVF_Scalability);


void UCustomizableObjectSystem::ReleaseInstanceIDs()
{
	// Handle ID discards
	int32 NumIDsReleased = 0;
	const int32 IDReleaseLimitPerTick = CVarMaxNumInstanceIDsToReleasePerTick.GetValueOnGameThread();

	for (auto Iterator = Private->MutablePendingInstanceWork.GetIDsToReleaseIterator();
		Iterator && NumIDsReleased < IDReleaseLimitPerTick; ++Iterator)
	{
		impl::Task_Game_ReleaseInstanceID(*Iterator);

		Iterator.RemoveCurrent();
		NumIDsReleased++;
	}
}


bool UCustomizableObjectSystem::IsUpdating(const UCustomizableObjectInstance* Instance) const
{
	if (!Instance)
	{
		return false;
	}
	
	return GetPrivate()->IsUpdating(*Instance);
}


TArray<FCustomizableObjectExternalTexture> UCustomizableObjectSystem::GetTextureParameterValues()
{
	TArray<FCustomizableObjectExternalTexture> Result;

	for (const TWeakObjectPtr<UCustomizableSystemImageProvider> Provider : GetPrivate()->GetResourceProviderChecked()->ImageProviders)
	{
		if (Provider.IsValid())
		{
			Provider->GetTextureParameterValues(Result);
		}
	}

	return Result;
}


void UCustomizableObjectSystem::RegisterImageProvider(UCustomizableSystemImageProvider* Provider)
{
	GetPrivate()->GetResourceProviderChecked()->ImageProviders.Add(Provider);
}


void UCustomizableObjectSystem::UnregisterImageProvider(UCustomizableSystemImageProvider* Provider)
{
	GetPrivate()->GetResourceProviderChecked()->ImageProviders.Remove(Provider);
}


void UCustomizableObjectSystemPrivate::CacheTextureParameters(const TArray<FCustomizableObjectTextureParameterValue>& TextureParameters) const
{
	for (const FCustomizableObjectTextureParameterValue& TextureParameter : TextureParameters)
	{
		ResourceProvider->CacheImage(TextureParameter.ParameterValue, false);

		for (const FName& RangeValue : TextureParameter.ParameterRangeValues)
		{
			ResourceProvider->CacheImage(RangeValue, false);
		}
	}
}


void UCustomizableObjectSystemPrivate::UnCacheTextureParameters(const TArray<FCustomizableObjectTextureParameterValue>& TextureParameters) const
{
	for (const FCustomizableObjectTextureParameterValue& TextureParameter : TextureParameters)
	{
		ResourceProvider->UnCacheImage(TextureParameter.ParameterValue, false);

		for (const FName& RangeValue : TextureParameter.ParameterRangeValues)
		{
			ResourceProvider->UnCacheImage(RangeValue, false);
		}
	}
}


bool UCustomizableObjectSystemPrivate::IsUsingBenchmarkingSettings()
{
	return bUseBenchmarkingSettings;
}


void UCustomizableObjectSystemPrivate::SetUsageOfBenchmarkingSettings(bool bUseBenchmarkingOptimizedSettings)
{
	bUseBenchmarkingSettings = bUseBenchmarkingOptimizedSettings;
}



int32 UCustomizableObjectSystem::GetNumInstances() const
{
	int32 NumInstances;
	int32 NumBuiltInstances;
	int32 NumInstancesLOD0;
	int32 NumInstancesLOD1;
	int32 NumInstancesLOD2;
	int32 NumAllocatedSkeletalMeshes;
	GetPrivate()->LogBenchmarkUtil.GetInstancesStats(NumInstances, NumBuiltInstances, NumInstancesLOD0, NumInstancesLOD1, NumInstancesLOD2, NumAllocatedSkeletalMeshes);

	return NumBuiltInstances;
}


int32 UCustomizableObjectSystem::GetNumPendingInstances() const
{
	return GetPrivate()->MutablePendingInstanceWork.Num() + GetPrivate()->NumLODUpdatesLastTick;
}


int32 UCustomizableObjectSystem::GetTotalInstances() const
{
	int32 NumInstances = 0;
	
	for (TObjectIterator<UCustomizableObjectInstance> Instance; Instance; ++Instance)
	{
		if (!IsValid(*Instance) ||
			Instance->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
				
		++NumInstances;
	}
	return NumInstances;
}

int64 UCustomizableObjectSystem::GetTextureMemoryUsed() const
{
	return GetPrivate()->LogBenchmarkUtil.TextureGPUSize.GetValue();
}

int32 UCustomizableObjectSystem::GetAverageBuildTime() const
{
	return GetPrivate()->LogBenchmarkUtil.InstanceBuildTimeAvrg.GetValue() * 1000;
}


int32 UCustomizableObjectSystem::GetSkeletalMeshMinLODQualityLevel() const
{
	return GetPrivate()->SkeletalMeshMinLodQualityLevel;
}


bool UCustomizableObjectSystem::IsSupport16BitBoneIndexEnabled() const
{
	return GetPrivate()->bSupport16BitBoneIndex;
}


bool UCustomizableObjectSystem::IsProgressiveMipStreamingEnabled() const
{
	return GetPrivate()->EnableMutableProgressiveMipStreaming != 0;
}


void UCustomizableObjectSystem::SetProgressiveMipStreamingEnabled(bool bIsEnabled)
{
	GetPrivate()->EnableMutableProgressiveMipStreaming = bIsEnabled ? 1 : 0;
}


bool UCustomizableObjectSystem::IsOnlyGenerateRequestedLODsEnabled() const
{
	return GetPrivate()->EnableOnlyGenerateRequestedLODs != 0;
}


void UCustomizableObjectSystem::SetOnlyGenerateRequestedLODsEnabled(bool bIsEnabled)
{
	GetPrivate()->EnableOnlyGenerateRequestedLODs = bIsEnabled ? 1 : 0;
}


#if WITH_EDITOR
void UCustomizableObjectSystem::SetImagePixelFormatOverride(const mu::FImageOperator::FImagePixelFormatFunc& InFunc)
{
	if (Private != nullptr)
	{
		Private->ImageFormatOverrideFunc = InFunc;
	}
}
#endif


void UCustomizableObjectSystem::AddUncompiledCOWarning(const UCustomizableObject& InObject, FString const* OptionalLogInfo)
{
	FString Msg;
	Msg += FString::Printf(TEXT("Warning: Customizable Object [%s] not compiled."), *InObject.GetName());
	GEngine->AddOnScreenDebugMessage((uint64)((PTRINT)&InObject), 10.0f, FColor::Red, Msg);

#if WITH_EDITOR
	// Mutable will spam these warnings constantly due to the tick and LOD manager checking for instances to update with every tick. Send only one message per CO in the editor.
	if (GetPrivate()->UncompiledCustomizableObjectIds.Find(InObject.GetPrivate()->GetVersionId()) != INDEX_NONE)
	{
		return;
	}
	
	// Add notification
	GetPrivate()->UncompiledCustomizableObjectIds.Add(InObject.GetPrivate()->GetVersionId());

	FMessageLog MessageLog("Mutable");
	MessageLog.Warning(FText::FromString(Msg));

	if (!GetPrivate()->UncompiledCustomizableObjectsNotificationPtr.IsValid())
	{
		FNotificationInfo Info(FText::FromString("Uncompiled Customizable Object/s found. Please, check the Message Log - Mutable for more information."));
		Info.bFireAndForget = true;
		Info.bUseThrobber = true;
		Info.FadeOutDuration = 1.0f;
		Info.ExpireDuration = 5.0f;

		GetPrivate()->UncompiledCustomizableObjectsNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	}

	const FString ErrorString = FString::Printf(
		TEXT("Customizable Object [%s] not compiled.  Compile via the editor or via code before instancing.  %s"),
		*InObject.GetName(), OptionalLogInfo ? **OptionalLogInfo : TEXT(""));

#else // !WITH_EDITOR
	const FString ErrorString = FString::Printf(
		TEXT("Customizable Object [%s] not compiled.  This is not an Editor build, so this is an unrecoverable bad state; could be due to code or a cook failure.  %s"),
		*InObject.GetName(), OptionalLogInfo ? **OptionalLogInfo : TEXT(""));
#endif

	// Also log an error so if this happens as part of a bug report we'll have this info.
	UE_LOG(LogMutable, Error, TEXT("%s"), *ErrorString);
}


void UCustomizableObjectSystem::SetReleaseMutableTexturesImmediately(bool bReleaseTextures)
{
	GetPrivate()->bReleaseTexturesImmediately = bReleaseTextures;
}


void UCustomizableObjectSystem::EnableBenchmark()
{
	// Start reporting benchmarking data (log and .csv file)
	FLogBenchmarkUtil::SetBenchmarkReportingStateOverride(true);
}


void UCustomizableObjectSystem::EndBenchmark()
{
	// Stop the reporting of benchmarking data
	FLogBenchmarkUtil::SetBenchmarkReportingStateOverride(false);
}


bool UCustomizableObjectSystem::IsMeshCacheEnabled(bool bCheckCVarOnGameThread /** = false */)
{
	return UCustomizableObjectSystemPrivate::IsUsingBenchmarkingSettings() ? false : CVarEnableMeshCache.GetValueOnAnyThread(bCheckCVarOnGameThread);
}


bool UCustomizableObjectSystem::ShouldClearWorkingMemoryOnUpdateEnd()
{
	return UCustomizableObjectSystemPrivate::IsUsingBenchmarkingSettings() ? true : CVarClearWorkingMemoryOnUpdateEnd.GetValueOnAnyThread();
}


bool UCustomizableObjectSystem::ShouldReuseTexturesBetweenInstances()
{
	return UCustomizableObjectSystemPrivate::IsUsingBenchmarkingSettings() ? false : CVarReuseImagesBetweenInstances.GetValueOnAnyThread();
}


void UCustomizableObjectSystem::SetWorkingMemory(int32 KBytes)
{
	CVarWorkingMemoryKB->Set( KBytes );
	UE_LOG(LogMutable, Log, TEXT("Working Memory set to %i kilobytes."), KBytes);
}


int32 UCustomizableObjectSystem::GetWorkingMemory() const
{
	return UCustomizableObjectSystemPrivate::IsUsingBenchmarkingSettings() ? 16384 : CVarWorkingMemoryKB->GetInt();
}


#if WITH_EDITOR

uint64 UCustomizableObjectSystem::GetMaxChunkSizeForPlatform(const ITargetPlatform* TargetPlatform)
{
	if (!TargetPlatform || !TargetPlatform->RequiresCookedData())
	{
		return MAX_uint64;
	}

	const FString& PlatformName = TargetPlatform ? TargetPlatform->IniPlatformName() : FPlatformProperties::IniPlatformName();

	if (const int64* CachedMaxChunkSize = GetPrivate()->PlatformMaxChunkSize.Find(PlatformName))
	{
		return *CachedMaxChunkSize;
	}

	int64 MaxChunkSize = -1;

	if (!FParse::Value(FCommandLine::Get(), TEXT("ExtraFlavorChunkSize="), MaxChunkSize) || MaxChunkSize < 0)
	{
		FConfigFile PlatformIniFile;
		FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *PlatformName);
		FString ConfigString;
		if (PlatformIniFile.GetString(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MaxChunkSize"), ConfigString))
		{
			MaxChunkSize = FCString::Atoi64(*ConfigString);
		}
	}

	// If no limit is specified default it to MUTABLE_STREAMED_DATA_MAXCHUNKSIZE 
	if (MaxChunkSize <= 0)
	{
		MaxChunkSize = MUTABLE_STREAMED_DATA_MAXCHUNKSIZE;
	}

	GetPrivate()->PlatformMaxChunkSize.Add(PlatformName, MaxChunkSize);

	return MaxChunkSize;
}

#endif // WITH_EDITOR


void UCustomizableObjectSystem::CacheImage(FName ImageId)
{
	GetPrivate()->GetResourceProviderChecked()->CacheImage(ImageId, true);
}


void UCustomizableObjectSystem::UnCacheImage(FName ImageId)
{
	GetPrivate()->GetResourceProviderChecked()->UnCacheImage(ImageId, true);
}


void UCustomizableObjectSystem::ClearImageCache()
{
	GetPrivate()->GetResourceProviderChecked()->ClearCache(true);
}


bool UCustomizableObjectSystemPrivate::IsMutableAnimInfoDebuggingEnabled() const
{ 
#if WITH_EDITORONLY_DATA
	return EnableMutableAnimInfoDebugging > 0;
#else
	return false;
#endif
}


FUnrealMutableResourceProvider* UCustomizableObjectSystemPrivate::GetResourceProviderChecked() const
{
	check(ResourceProvider)
	return ResourceProvider.Get();
}


void UCustomizableObjectSystemPrivate::OnMutableEnabledChanged(IConsoleVariable* MutableEnabled)
{
	if (!UCustomizableObjectSystem::IsCreated())
	{
		return;
	}

	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	UCustomizableObjectSystemPrivate* SystemPrivate = System->GetPrivate();

	if (bIsMutableEnabled)
	{
#if !UE_SERVER
		if (GetMutableDefault<UCustomizableObjectSettings>()->bEnableStreamingManager)
		{
			FStreamingManagerCollection::Get().RemoveStreamingManager(SystemPrivate); // Avoid being added twice
			FStreamingManagerCollection::Get().AddStreamingManager(SystemPrivate);
		}
		else
		{
			if (!SystemPrivate->TickDelegateHandle.IsValid())
			{
				SystemPrivate->TickDelegate = FTickerDelegate::CreateUObject(System, &UCustomizableObjectSystem::Tick);
				SystemPrivate->TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(SystemPrivate->TickDelegate, 0.f);
			}			
		}
#endif // !UE_SERVER
	}
}


void UCustomizableObjectSystemPrivate::StartUpdateSkeletalMesh(const TSharedRef<FUpdateContextPrivate>& Context)
{
	Context->UpdateStarted = true;
	TRACE_BEGIN_REGION(UE_MUTABLE_UPDATE_REGION);

	check(!CurrentMutableOperation); // Can not start an update if there is already another in progress
	check(Context->Instance.IsValid()) // The instance has to be alive to start the update

	const uint32 InstanceId = Context->Instance->GetUniqueID();
	UE_LOG(LogMutable, Log, TEXT("Started UpdateSkeletalMesh Async. Instance=%d, Frame=%d"), InstanceId, GFrameNumber);				
			
	CurrentMutableOperation = Context;
}


bool UCustomizableObjectSystemPrivate::IsUpdating(const UCustomizableObjectInstance& Instance) const
{
	if (CurrentMutableOperation && CurrentMutableOperation->Instance.Get() == &Instance)
	{
		return true;
	}

	if (MutablePendingInstanceWork.GetUpdate(TWeakObjectPtr<const UCustomizableObjectInstance>(&Instance)))
	{
		return true;
	}
	
	return false;
}


void UCustomizableObjectSystemPrivate::UpdateStats()
{
	NumSkeletalMeshes = 0;
	
	for (TObjectIterator<UCustomizableObjectInstance> Instance; Instance; ++Instance)
	{
		if (!IsValid(*Instance))
		{
			continue;
		}

		NumSkeletalMeshes += Instance->GetPrivate()->SkeletalMeshes.Num();
	}
}


bool UCustomizableObjectSystem::IsMutableAnimInfoDebuggingEnabled() const
{
#if WITH_EDITOR
	return GetPrivate()->IsMutableAnimInfoDebuggingEnabled();
#else
	return false;
#endif
}


void UCustomizableObjectSystemPrivate::UpdateResourceStreaming(float DeltaTime, bool bProcessEverything)
{
	GetPublic()->TickInternal(false);
}


int32 UCustomizableObjectSystemPrivate::BlockTillAllRequestsFinished(float TimeLimit, bool bLogResults)
{
	const double BlockEndTime = FPlatformTime::Seconds() + TimeLimit;

	int32 RemainingWork = TNumericLimits<int32>::Max();
	
	if (TimeLimit == 0.0f)
	{
		while (RemainingWork > 0)
		{
			RemainingWork = GetPublic()->TickInternal(true);
		}
	}
	else
	{
		while (RemainingWork > 0)
		{			
			if (FPlatformTime::Seconds() > BlockEndTime)
			{
				return RemainingWork;
			}
			
			RemainingWork = GetPublic()->TickInternal(true);
		}
	}

	return 0;
}

