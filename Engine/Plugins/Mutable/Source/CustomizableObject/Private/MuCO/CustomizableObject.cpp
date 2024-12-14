// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableObject.h"

#include "Algo/Copy.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/AsyncFileHandle.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Engine/AssetUserData.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Input/Reply.h"
#include "Interfaces/ITargetPlatform.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCO/ICustomizableObjectModule.h"
#include "MuCO/MutableProjectorTypeUtils.h"
#include "MuCO/UnrealMutableModelDiskStreamer.h"
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuR/Model.h"
#include "MuR/Operations.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"
#include "MuCO/ICustomizableObjectEditorModule.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"

#if WITH_EDITOR
#include "Editor.h"

#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#endif


#include "MuCO/CustomizableObjectCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObject)

#define LOCTEXT_NAMESPACE "CustomizableObject"

DEFINE_LOG_CATEGORY(LogMutable);

#if WITH_EDITOR

TAutoConsoleVariable<int32> CVarPackagedDataBytesLimitOverride(
	TEXT("mutable.PackagedDataBytesLimitOverride"),
	-1,
	TEXT("Defines the value to be used as 'PackagedDataBytesLimitOverride' for the compilation of all COs.\n")
	TEXT(" <0 : Use value defined in the CO\n")
	TEXT(" >=0  : Use this value instead\n"));


TAutoConsoleVariable<bool> CVarMutableUseBulkData(
	TEXT("Mutable.UseBulkData"),
	true,
	TEXT("Switch between .utoc/.ucas (FBulkData) and .mut files (CookAdditionalFiles).\n")
	TEXT("True - Use FBulkData to store streamable data.\n")
	TEXT("False - Use Mut files to store streamable data\n"));


TAutoConsoleVariable<int32> CVarMutableDerivedDataCacheUsage(
	TEXT("mutable.DerivedDataCacheUsage"),
	0,
	TEXT("Derived data cache access for cooked data.")
	TEXT("0 - None. Disables access to the cache.")
	TEXT("1 - Local. Allow cache requests to query and store records and values in local caches.")
	TEXT("2 - Default. Allow cache requests to query and store records and values in any caches."),
	ECVF_Default);


TAutoConsoleVariable<bool> CVarMutableAsyncCook(
	TEXT("Mutable.CookAsync"),
	false,
	TEXT("True - Customizable Objects will be compiled asynchronously during cook.\n")
	TEXT("False - Sync compilation.\n"));

#endif

#if WITH_EDITORONLY_DATA

namespace UE::Mutable::Private
{

template <typename T>
T* MoveOldObjectAndCreateNew(UClass* Class, UObject* InOuter)
{
	FName ObjectFName = Class->GetFName();
	FString ObjectNameStr = ObjectFName.ToString();
	UObject* Existing = FindObject<UAssetUserData>(InOuter, *ObjectNameStr);
	if (Existing)
	{
		// Move the old object out of the way
		Existing->Rename(nullptr /* Rename will pick a free name*/, GetTransientPackage(), REN_DontCreateRedirectors);
	}
	return NewObject<T>(InOuter, Class, *ObjectNameStr);
}

}

#endif

//-------------------------------------------------------------------------------------------------

UCustomizableObject::UCustomizableObject()
	: UObject()
{
	Private = CreateDefaultSubobject<UCustomizableObjectPrivate>(FName("Private"));

#if WITH_EDITORONLY_DATA
	const FString CVarName = TEXT("r.SkeletalMesh.MinLodQualityLevel");
	const FString ScalabilitySectionName = TEXT("ViewDistanceQuality");
	LODSettings.MinQualityLevelLOD.SetQualityLevelCVarForCooking(*CVarName, *ScalabilitySectionName);
#endif
}


#if WITH_EDITOR
bool UCustomizableObject::IsEditorOnly() const
{
	return bIsChildObject;
}


void UCustomizableObjectPrivate::UpdateVersionId()
{
	GetPublic()->VersionId = FGuid::NewGuid();
}


FGuid UCustomizableObjectPrivate::GetVersionId() const
{
	return GetPublic()->VersionId;
}


void UCustomizableObject::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::GetAssetRegistryTags(OutTags);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}


void UCustomizableObject::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	int32 isRoot = 0;

	if (const ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		isRoot = Module->IsRootObject(*this) ? 1 : 0;
	}
	
	Context.AddTag(FAssetRegistryTag("IsRoot", FString::FromInt(isRoot), FAssetRegistryTag::TT_Numerical));
	Super::GetAssetRegistryTags(Context);
}


void UCustomizableObject::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	// Update the derived child object flag
	if (GetPrivate()->TryUpdateIsChildObject())
	{
		if (bIsChildObject)
		{
			GetPackage()->SetPackageFlags(PKG_EditorOnly);
		}
		else
		{
			GetPackage()->ClearPackageFlags(PKG_EditorOnly);
		}
	}

	if (ObjectSaveContext.IsCooking() && !bIsChildObject)
	{
		const ITargetPlatform* TargetPlatform = ObjectSaveContext.GetTargetPlatform();

		// Load cached data before saving
		if (GetPrivate()->TryLoadCompiledCookDataForPlatform(TargetPlatform))
		{
			const bool bUseBulkData = CVarMutableUseBulkData.GetValueOnAnyThread();
			if (bUseBulkData)
			{
				MutablePrivate::FMutableCachedPlatformData& CachedPlatformData = *GetPrivate()->CachedPlatformsData.Find(TargetPlatform->PlatformName());
				TSharedPtr<FModelStreamableBulkData> ModelStreamableBulkData = GetPrivate()->GetModelStreamableBulkData(true);

				const int32 NumBulkDataFiles = CachedPlatformData.BulkDataFiles.Num();

				ModelStreamableBulkData->StreamableBulkData.SetNum(NumBulkDataFiles);

				const auto WriteBulkData = [ModelStreamableBulkData](MutablePrivate::FFile& File, TArray64<uint8>& FileBulkData, uint32 FileIndex)
					{
						MUTABLE_CPUPROFILER_SCOPE(WriteBulkData);

						FByteBulkData& ByteBulkData = ModelStreamableBulkData->StreamableBulkData[FileIndex];

						// BulkData file to store the file to. CookedIndex 0 is used as a default for backwards compatibility, +1 to skip it.
						ByteBulkData.SetCookedIndex(FBulkDataCookedIndex((File.Id % MAX_uint8) + 1));

						ByteBulkData.Lock(LOCK_READ_WRITE);
						uint8* Ptr = (uint8*)ByteBulkData.Realloc(FileBulkData.Num());
						FMemory::Memcpy(Ptr, FileBulkData.GetData(), FileBulkData.Num());
						ByteBulkData.Unlock();

						uint32 BulkDataFlags = BULKDATA_PayloadInSeperateFile | BULKDATA_Force_NOT_InlinePayload;
						if (File.Flags == uint16(mu::ERomFlags::HighRes))
						{
							BulkDataFlags |= BULKDATA_OptionalPayload;
						}
						ByteBulkData.SetBulkDataFlags(BulkDataFlags);
					};

				bool bDropData = true;
				MutablePrivate::SerializeBulkDataFiles(CachedPlatformData, CachedPlatformData.BulkDataFiles, WriteBulkData, bDropData);
			}
			else 
			{
				// Create an export object to manage the streamable data
				if (!BulkData)
				{
					BulkData = UE::Mutable::Private::MoveOldObjectAndCreateNew<UCustomizableObjectBulk>(UCustomizableObjectBulk::StaticClass(), this);
				}
				BulkData->Mark(OBJECTMARK_TagExp);
			}
		}
		else
		{
			UE_LOG(LogMutable, Warning, TEXT("Cook: Customizable Object [%s] is missing [%s] platform data."), *GetName(),
				*ObjectSaveContext.GetTargetPlatform()->PlatformName());
			
			// Clear model resources
			GetPrivate()->SetModel(nullptr, FGuid());
			GetPrivate()->GetModelResources(true /* bIsCooking */) = FModelResources();
			GetPrivate()->GetModelStreamableBulkData(true).Reset();
		}
	}
}


void UCustomizableObject::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObject::PostSaveRoot);

	Super::PostSaveRoot(ObjectSaveContext);

	if (ObjectSaveContext.IsCooking())
	{
		// Free cached data after saving;
		const ITargetPlatform* TargetPlatform = ObjectSaveContext.GetTargetPlatform();
		GetPrivate()->CachedPlatformsData.Remove(TargetPlatform->PlatformName());
	}
}


bool UCustomizableObjectPrivate::TryUpdateIsChildObject()
{
	if (const ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		GetPublic()->bIsChildObject = !Module->IsRootObject(*GetPublic());
		return true;
	}
	else
	{
		return false;
	}
}


bool UCustomizableObject::IsChildObject() const
{
	return bIsChildObject;
}


void UCustomizableObjectPrivate::SetIsChildObject(bool bIsChildObject)
{
	GetPublic()->bIsChildObject = bIsChildObject;
}


bool UCustomizableObjectPrivate::TryLoadCompiledCookDataForPlatform(const ITargetPlatform* TargetPlatform)
{
	const MutablePrivate::FMutableCachedPlatformData* PlatformData = CachedPlatformsData.Find(TargetPlatform->PlatformName());
	if (!PlatformData)
	{
		return false;
	}

	FMemoryReaderView ModelResourcesReader(PlatformData->ModelResourcesData);
	if (LoadModelResources(ModelResourcesReader, TargetPlatform, true))
	{
		SetModelStreamableBulkData(PlatformData->ModelStreamables, true);

		FMemoryReaderView ModelReader(PlatformData->ModelData);
		LoadModel(ModelReader);
		return GetModel() != nullptr;
	}

	return false;
}

#endif // End WITH_EDITOR


void UCustomizableObject::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Source)
	{
		Source->ConditionalPostLoad();
	}
	
	for (int32 Version = GetLinkerCustomVersion(FCustomizableObjectCustomVersion::GUID) + 1; Version <= FCustomizableObjectCustomVersion::LatestVersion; ++Version)
	{
		GetPrivate()->BackwardsCompatibleFixup(Version);
		
		if (Source)
		{
			if (ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
			{
				// Execute backwards compatible code for all nodes. It requires all nodes to be loaded.
			
				Module->BackwardsCompatibleFixup(*Source, Version);
			}
		}
	}

	if (Source)
	{
		if (ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
		{
			Module->PostBackwardsCompatibleFixup(*Source);
		}
	}
	
	// Register to dirty delegate so we update derived data version ID each time that the package is marked as dirty.
	if (UPackage* Package = GetOutermost())
	{
		Package->PackageMarkedDirtyEvent.AddWeakLambda(this, [this](UPackage* Pkg, bool bWasDirty)
			{
				if (GetPackage() == Pkg)
				{
					GetPrivate()->UpdateVersionId();
				}
			});
	}
	
	if (!IsRunningCookCommandlet())
	{
		GetPrivate()->Status.NextState(FCustomizableObjectStatusTypes::EState::Loading);
	
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		if (AssetRegistryModule.Get().IsLoadingAssets())
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddUObject(GetPrivate(), &UCustomizableObjectPrivate::LoadCompiledDataFromDisk);
		}
		else
		{
			GetPrivate()->LoadCompiledDataFromDisk();
		}
	}
#endif
}


void UCustomizableObjectPrivate::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
#if WITH_EDITOR
	if (GetPublic()->ReferenceSkeletalMesh_DEPRECATED)
	{
		GetPublic()->ReferenceSkeletalMeshes_DEPRECATED.Add(GetPublic()->ReferenceSkeletalMesh_DEPRECATED);
		GetPublic()->ReferenceSkeletalMesh_DEPRECATED = nullptr;
	}

#if WITH_EDITORONLY_DATA
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::CompilationOptions)
	{
		OptimizationLevel = GetPublic()->CompileOptions_DEPRECATED.OptimizationLevel;
		TextureCompression = GetPublic()->CompileOptions_DEPRECATED.TextureCompression;
		bUseDiskCompilation = GetPublic()->CompileOptions_DEPRECATED.bUseDiskCompilation;
		EmbeddedDataBytesLimit = GetPublic()->CompileOptions_DEPRECATED.EmbeddedDataBytesLimit;
		PackagedDataBytesLimit = GetPublic()->CompileOptions_DEPRECATED.PackagedDataBytesLimit;
	}

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::NewComponentOptions)
	{
		if (MutableMeshComponents_DEPRECATED.IsEmpty())
		{
			for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < GetPublic()->ReferenceSkeletalMeshes_DEPRECATED.Num(); ++SkeletalMeshIndex)
			{
				FMutableMeshComponentData NewComponent;
				NewComponent.Name = FName(FString::FromInt(SkeletalMeshIndex));
				NewComponent.ReferenceSkeletalMesh = GetPublic()->ReferenceSkeletalMeshes_DEPRECATED[SkeletalMeshIndex];

				MutableMeshComponents_DEPRECATED.Add(NewComponent);
			}

			GetPublic()->ReferenceSkeletalMeshes_DEPRECATED.Empty();
		}
	}
#endif
#endif
}

bool UCustomizableObjectPrivate::IsLocked() const
{
	return bLocked;
}


void UCustomizableObject::Serialize(FArchive& Ar_Asset)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObject::Serialize)
	
	Super::Serialize(Ar_Asset);

	Ar_Asset.UsingCustomVersion(FCustomizableObjectCustomVersion::GUID);
	
#if WITH_EDITOR
	if (Ar_Asset.IsCooking())
	{
		if (Ar_Asset.IsSaving())
		{
			UE_LOG(LogMutable, Verbose, TEXT("Serializing cooked data for Customizable Object [%s]."), *GetName());
			GetPrivate()->SaveEmbeddedData(Ar_Asset);
		}
	}
	else
	{
		// Can't remove this or saved customizable objects will fail to load
		int64 InternalVersion = UCustomizableObjectPrivate::CurrentSupportedVersion;
		Ar_Asset << InternalVersion;
	}
#else
	if (Ar_Asset.IsLoading())
	{
		GetPrivate()->LoadEmbeddedData(Ar_Asset);
	}
#endif
}


#if WITH_EDITOR
void UCustomizableObject::PostRename(UObject * OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	if (Source)
	{
		Source->PostRename(OldOuter, OldName);
	}
}


void UCustomizableObject::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (!TargetPlatform)
	{
		return;
	}

	const TSharedRef<FCompilationRequest>* CompileRequest = GetPrivate()->CompileRequests.FindByPredicate(
		[&TargetPlatform](const TSharedPtr<FCompilationRequest>& Request) { return Request->GetCompileOptions().TargetPlatform == TargetPlatform; });
	
	if (CompileRequest)
	{
		return;
	}

	// Compile and save in the CachedPlatformsData map
	GetPrivate()->CompileForTargetPlatform(*this, *TargetPlatform);
}


bool UCustomizableObject::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) 
{
	if (!TargetPlatform)
	{
		return true;
	}

	const TSharedRef<FCompilationRequest>* CompileRequest = GetPrivate()->CompileRequests.FindByPredicate(
		[&TargetPlatform](const TSharedRef<FCompilationRequest>& Request) { return Request->GetCompileOptions().TargetPlatform == TargetPlatform; });
	
	if (CompileRequest)
	{
		return CompileRequest->Get().GetCompilationState() == ECompilationStatePrivate::Completed;
	}

	return true;
}


FGuid GenerateIdentifier(const UCustomizableObject& CustomizableObject)
{
	// Generate the Identifier using the path and name of the asset
	uint32 FullPathHash = GetTypeHash(CustomizableObject.GetFullName());
	uint32 OutermostHash = GetTypeHash(GetNameSafe(CustomizableObject.GetOutermost()));
	uint32 OuterHash = GetTypeHash(CustomizableObject.GetName());
	return FGuid(0, FullPathHash, OutermostHash, OuterHash);
}


bool UCustomizableObjectPrivate::LoadModelResources(FArchive& MemoryReader, const ITargetPlatform* InTargetPlatform, bool bIsCooking)
{
	// Make sure mutable has been initialised.
	UCustomizableObjectSystem::GetInstance();

	FModelResources LocalModelResources;
	
	FObjectAndNameAsStringProxyArchive ObjectReader(MemoryReader, true);
	const bool bLoadedSuccessfully = LocalModelResources.Unserialize(ObjectReader, *GetPublic(), InTargetPlatform, bIsCooking);

	GetModelResources(bIsCooking) = MoveTemp(LocalModelResources);

	return bLoadedSuccessfully;
}


void UCustomizableObjectPrivate::LoadModelStreamableBulk(FArchive& MemoryReader, bool bIsCooking)
{
	TSharedPtr<FModelStreamableBulkData> LocalModelStreamablesPtr = MakeShared<FModelStreamableBulkData>();
	FModelStreamableBulkData& LocalModelStreamables = *LocalModelStreamablesPtr.Get();
	MemoryReader << LocalModelStreamables;

	SetModelStreamableBulkData(LocalModelStreamablesPtr, bIsCooking);
}


void UCustomizableObjectPrivate::LoadModel(FArchive& MemoryReader)
{
	TSharedPtr<mu::Model, ESPMode::ThreadSafe> LoadedModel;

	UnrealMutableInputStream Stream(MemoryReader);
	mu::InputArchive Arch(&Stream);
	LoadedModel = mu::Model::StaticUnserialise(Arch);

	SetModel(LoadedModel, GenerateIdentifier(*GetPublic()));
}


void SerializeStreamedResources(FArchive& Ar, TArray<FCustomizableObjectStreamedResourceData>& StreamedResources)
{
	check(Ar.IsSaving());

	int32 NumStreamedResources = StreamedResources.Num();
	Ar << NumStreamedResources;

	for (const FCustomizableObjectStreamedResourceData& ResourceData : StreamedResources)
	{
		const FCustomizableObjectResourceData& Data = ResourceData.GetPath().LoadSynchronous()->Data;
		uint32 ResourceDataType = (uint32)Data.Type;
		Ar << ResourceDataType;

		switch (Data.Type)
		{
		case ECOResourceDataType::AssetUserData:
		{
			const FCustomizableObjectAssetUserData* AssetUserData = Data.Data.GetPtr<FCustomizableObjectAssetUserData>();
			
			FString AssetUserDataPath;
			
			if (AssetUserData && AssetUserData->AssetUserDataEditor)
			{
				AssetUserDataPath = TSoftObjectPtr<UAssetUserData>(AssetUserData->AssetUserDataEditor).ToString();
			}
			else
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to serialize streamed resource of type AssetUserData."));
			}

			Ar << AssetUserDataPath;
			break;
		}
		default:
			check(false);
			break;
		}
	}
}


void UnserializeStreamedResources(FArchive& Ar, UObject* Object, TArray<FCustomizableObjectStreamedResourceData>& StreamedResources, bool bIsCooking)
{
	check(Ar.IsLoading());

	const FString CustomizableObjectName = GetNameSafe(Object) + TEXT("_");

	int32 NumStreamedResources = 0;
	Ar << NumStreamedResources;

	StreamedResources.SetNum(NumStreamedResources);

	for (int32 ResourceIndex = 0; ResourceIndex < NumStreamedResources; ++ResourceIndex)
	{
		// Override existing containers
		UCustomizableObjectResourceDataContainer* Container = StreamedResources[ResourceIndex].GetPath().Get();

		// Create a new container if none.
		if (!Container)
		{
			// Generate a deterministic name to help with deterministic cooking
			const FString ContainerName = CustomizableObjectName + FString::Printf(TEXT("SR_%d"), ResourceIndex);

			UCustomizableObjectResourceDataContainer* ExistingContainer = FindObject<UCustomizableObjectResourceDataContainer>(Object, *ContainerName);
			Container = ExistingContainer ? ExistingContainer : NewObject<UCustomizableObjectResourceDataContainer>(
				Object,
				FName(*ContainerName),
				RF_Public);

			StreamedResources[ResourceIndex] = { Container };
		}

		check(Container);
		uint32 Type = 0;
		Ar << Type;

		Container->Data.Type = (ECOResourceDataType)Type;
		switch (Container->Data.Type)
		{
		case ECOResourceDataType::AssetUserData:
		{
			FString AssetUserDataPath;
			Ar << AssetUserDataPath;

			FCustomizableObjectAssetUserData ResourceData;

			TSoftObjectPtr<UAssetUserData> SoftAssetUserData = TSoftObjectPtr<UAssetUserData>(FSoftObjectPath(AssetUserDataPath));
			ResourceData.AssetUserDataEditor = !SoftAssetUserData.IsNull() ? SoftAssetUserData.LoadSynchronous() : nullptr;

			if (!ResourceData.AssetUserDataEditor)
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to load streamed resource of type AssetUserData. Resource name: [%s]"), *AssetUserDataPath);
			}

			if (bIsCooking)
			{
				// Rename the asset user data for duplicate
				const FString AssetName = CustomizableObjectName + GetNameSafe(ResourceData.AssetUserDataEditor);

				// Find or duplicate the AUD replacing the outer
				ResourceData.AssetUserData = FindObject<UAssetUserData>(Container, *AssetName);
				if (!ResourceData.AssetUserData)
				{
					// AUD may be private objects within meshes. Duplicate changing the outer to avoid including meshes into the builds.
					ResourceData.AssetUserData = DuplicateObject<UAssetUserData>(ResourceData.AssetUserDataEditor, Container, FName(*AssetName));
				}
			}

			Container->Data.Data = FInstancedStruct::Make(ResourceData);
			break;
		}
		default:
			check(false);
			break;
		}
	}
}


void FModelResources::Serialize(FObjectAndNameAsStringProxyArchive& MemoryWriter, bool bIsCooking)
{
	MUTABLE_CPUPROFILER_SCOPE(FModelResources::Serialize);
	check(IsInGameThread());

	int32 SupportedVersion = UCustomizableObjectPrivate::CurrentSupportedVersion;
	MemoryWriter << SupportedVersion;

	MemoryWriter << ReferenceSkeletalMeshesData;

	SerializeStreamedResources(MemoryWriter, StreamedResourceData);

	int32 NumReferencedMaterials = Materials.Num();
	MemoryWriter << NumReferencedMaterials;

	for (const TSoftObjectPtr<UMaterialInterface>& Material : Materials)
	{
		FString StringRef = Material.ToString();
		MemoryWriter << StringRef;
	}

	int32 NumReferencedSkeletons = Skeletons.Num();
	MemoryWriter << NumReferencedSkeletons;

	for (const TSoftObjectPtr<USkeleton>& Skeleton : Skeletons)
	{
		FString StringRef = Skeleton.ToString();
		MemoryWriter << StringRef;
	}

	int32 NumPassthroughTextures = PassThroughTextures.Num();
	MemoryWriter << NumPassthroughTextures;

	for (const TSoftObjectPtr<UTexture>& PassthroughTexture : PassThroughTextures)
	{
		FString StringRef = PassthroughTexture.ToString();
		MemoryWriter << StringRef;
	}

	int32 NumPassthroughMeshes = PassThroughMeshes.Num();
	MemoryWriter << NumPassthroughMeshes;

	for (const TSoftObjectPtr<USkeletalMesh>& PassthroughMesh : PassThroughMeshes)
	{
		FString StringRef = PassthroughMesh.ToString();
		MemoryWriter << StringRef;
	}

#if WITH_EDITORONLY_DATA
	int32 NumRuntimeReferencedTextures = RuntimeReferencedTextures.Num();
	MemoryWriter << NumRuntimeReferencedTextures;

	for (const TSoftObjectPtr<const UTexture>& RuntimeReferencedTexture : RuntimeReferencedTextures)
	{
		FString StringRef = RuntimeReferencedTexture.ToString();
		MemoryWriter << StringRef;
	}
#endif

	int32 NumPhysicsAssets = PhysicsAssets.Num();
	MemoryWriter << NumPhysicsAssets;

	for (const TSoftObjectPtr<UPhysicsAsset>& PhysicsAsset : PhysicsAssets)
	{
		FString StringRef = PhysicsAsset.ToString();
		MemoryWriter << StringRef;
	}

	int32 NumAnimBps = AnimBPs.Num();
	MemoryWriter << NumAnimBps;

	for (const TSoftClassPtr<UAnimInstance>& AnimBp : AnimBPs)
	{
		FString StringRef = AnimBp.ToString();
		MemoryWriter << StringRef;
	}

	MemoryWriter << AnimBpOverridePhysiscAssetsInfo;

	MemoryWriter << MaterialSlotNames;
	MemoryWriter << BoneNamesMap;
	MemoryWriter << SocketArray;

	MemoryWriter << SkinWeightProfilesInfo;

	MemoryWriter << ImageProperties;
	MemoryWriter << MeshMetadata;
	MemoryWriter << SurfaceMetadata;
	MemoryWriter << ParameterUIDataMap;
	MemoryWriter << StateUIDataMap;

#if WITH_EDITORONLY_DATA
	MemoryWriter << IntParameterOptionDataTable;
#endif

	MemoryWriter << ClothingAssetsData;
	MemoryWriter << ClothSharedConfigsData;

	MemoryWriter << NumLODs;
	MemoryWriter << NumLODsToStream;
	MemoryWriter << FirstLODAvailable;

	MemoryWriter << ComponentNames;
	MemoryWriter << ReleaseVersion;

	// Editor Only data
	if (!bIsCooking)
	{
		MemoryWriter << bIsTextureStreamingDisabled;
		MemoryWriter << bIsCompiledWithOptimization;
		MemoryWriter << CustomizableObjectPathMap;
		MemoryWriter << GroupNodeMap;
		MemoryWriter << ParticipatingObjects;
		MemoryWriter << TableToParamNames;

		MemoryWriter << EditorOnlyMorphTargetReconstructionData;
		MemoryWriter << EditorOnlyClothingMeshToMeshVertData;
	}
}


bool FModelResources::Unserialize(FObjectAndNameAsStringProxyArchive& MemoryReader, UCustomizableObject& Outer, const ITargetPlatform* InTargetPlatform, bool bIsCooking)
{
	MUTABLE_CPUPROFILER_SCOPE(FModelResources::Unserialize);
	check(IsInGameThread());

	int32 SupportedVersion = 0;
	MemoryReader << SupportedVersion;

	if (SupportedVersion != UCustomizableObjectPrivate::CurrentSupportedVersion)
	{
		return false;
	}

	MemoryReader << ReferenceSkeletalMeshesData;

	UnserializeStreamedResources(MemoryReader, &Outer, StreamedResourceData, bIsCooking);

	// Initialize resources. 
	for (FMutableRefSkeletalMeshData& ReferenceSkeletalMeshData : ReferenceSkeletalMeshesData)
	{
		ReferenceSkeletalMeshData.InitResources(&Outer, *this, InTargetPlatform);
	}

	int32 NumReferencedMaterials = 0;
	MemoryReader << NumReferencedMaterials;
	Materials.Reset(NumReferencedMaterials);

	for (int32 i = 0; i < NumReferencedMaterials; ++i)
	{
		FString StringRef;
		MemoryReader << StringRef;

		Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(StringRef)));
	}

	int32 NumReferencedSkeletons = 0;
	MemoryReader << NumReferencedSkeletons;
	Skeletons.Reset(NumReferencedMaterials);

	for (int32 SkeletonIndex = 0; SkeletonIndex < NumReferencedSkeletons; ++SkeletonIndex)
	{
		FString StringRef;
		MemoryReader << StringRef;

		Skeletons.Add(TSoftObjectPtr<USkeleton>(FSoftObjectPath(StringRef)));
	}

	int32 NumPassthroughTextures = 0;
	MemoryReader << NumPassthroughTextures;
	PassThroughTextures.Reset(NumPassthroughTextures);

	for (int32 Index = 0; Index < NumPassthroughTextures; ++Index)
	{
		FString StringRef;
		MemoryReader << StringRef;

		PassThroughTextures.Add(TSoftObjectPtr<UTexture>(FSoftObjectPath(StringRef)));
	}

	int32 NumPassthroughMeshes = 0;
	MemoryReader << NumPassthroughMeshes;
	PassThroughMeshes.Reset(NumPassthroughMeshes);

	for (int32 Index = 0; Index < NumPassthroughMeshes; ++Index)
	{
		FString StringRef;
		MemoryReader << StringRef;

		PassThroughMeshes.Add(TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(StringRef)));
	}

#if WITH_EDITORONLY_DATA
	int32 NumRuntimeReferencedTextures = 0;
	MemoryReader << NumRuntimeReferencedTextures;
	RuntimeReferencedTextures.Reset(NumRuntimeReferencedTextures);

	for (int32 Index = 0; Index < NumRuntimeReferencedTextures; ++Index)
	{
		FString StringRef;
		MemoryReader << StringRef;

		RuntimeReferencedTextures.Add(TSoftObjectPtr<const UTexture>(FSoftObjectPath(StringRef)));
	}
#endif

	int32 NumPhysicsAssets = 0;
	MemoryReader << NumPhysicsAssets;
	PhysicsAssets.Reset(NumPhysicsAssets);

	for (int32 i = 0; i < NumPhysicsAssets; ++i)
	{
		FString StringRef;
		MemoryReader << StringRef;

		PhysicsAssets.Add(TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(StringRef)));
	}


	int32 NumAnimBps = 0;
	MemoryReader << NumAnimBps;
	AnimBPs.Reset(NumAnimBps);

	for (int32 Index = 0; Index < NumAnimBps; ++Index)
	{
		FString StringRef;
		MemoryReader << StringRef;

		AnimBPs.Add(TSoftClassPtr<UAnimInstance>(StringRef));
	}

	MemoryReader << AnimBpOverridePhysiscAssetsInfo;

	MemoryReader << MaterialSlotNames;
	MemoryReader << BoneNamesMap;
	MemoryReader << SocketArray;

	MemoryReader << SkinWeightProfilesInfo;

	MemoryReader << ImageProperties;
	MemoryReader << MeshMetadata;
	MemoryReader << SurfaceMetadata;
	MemoryReader << ParameterUIDataMap;
	MemoryReader << StateUIDataMap;

#if WITH_EDITORONLY_DATA
	MemoryReader << IntParameterOptionDataTable;
#endif

	MemoryReader << ClothingAssetsData;
	MemoryReader << ClothSharedConfigsData;

	MemoryReader << NumLODs;
	MemoryReader << NumLODsToStream;
	MemoryReader << FirstLODAvailable;

	MemoryReader << ComponentNames;
	MemoryReader << ReleaseVersion;

	// Editor Only data
	if (!bIsCooking)
	{
		MemoryReader << bIsTextureStreamingDisabled;
		MemoryReader << bIsCompiledWithOptimization;
		MemoryReader << CustomizableObjectPathMap;
		MemoryReader << GroupNodeMap;
		MemoryReader << ParticipatingObjects;
		MemoryReader << TableToParamNames;

		MemoryReader << EditorOnlyMorphTargetReconstructionData;
		MemoryReader << EditorOnlyClothingMeshToMeshVertData;
	}

	return true;
}


void UCustomizableObjectPrivate::LoadCompiledDataFromDisk()
{
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	const ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);

	// Compose Folder Name
	const FString FolderPath = GetCompiledDataFolderPath();

	// Compose File Names
	const FString ModelFileName = FolderPath + GetCompiledDataFileName(true, RunningPlatform);
	const FString StreamableFileName = FolderPath + GetCompiledDataFileName(false, RunningPlatform);

	IFileManager& FileManager = IFileManager::Get();
	if (FileManager.FileExists(*ModelFileName) && FileManager.FileExists(*StreamableFileName))
	{
		// Check CompiledData
		TUniquePtr<IFileHandle> CompiledDataFileHandle( FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ModelFileName) );
		TUniquePtr<IFileHandle> StreamableDataFileHandle( FPlatformFileManager::Get().GetPlatformFile().OpenRead(*StreamableFileName) );

		MutableCompiledDataStreamHeader CompiledDataHeader;
		MutableCompiledDataStreamHeader StreamableDataHeader;

		int32 HeaderSize = sizeof(MutableCompiledDataStreamHeader);
		TArray<uint8> HeaderBytes;
		HeaderBytes.SetNum(HeaderSize);

		{
			CompiledDataFileHandle->Read(HeaderBytes.GetData(), HeaderSize);
			FMemoryReader AuxMemoryReader(HeaderBytes);
			AuxMemoryReader << CompiledDataHeader;
		}
		{
			StreamableDataFileHandle->Read(HeaderBytes.GetData(), HeaderSize);
			FMemoryReader AuxMemoryReader(HeaderBytes);
			AuxMemoryReader << StreamableDataHeader;
		}

		if (CompiledDataHeader.InternalVersion == UCustomizableObjectPrivate::CurrentSupportedVersion
			&&
			CompiledDataHeader.InternalVersion == StreamableDataHeader.InternalVersion 
			&&
			CompiledDataHeader.VersionId == StreamableDataHeader.VersionId)
		{
			if (IsRunningGame() || CompiledDataHeader.VersionId == GetVersionId())
			{ 
				int64 CompiledDataSize = CompiledDataFileHandle->Size() - HeaderSize;
				TArray64<uint8> CompiledDataBytes;
				CompiledDataBytes.SetNumUninitialized(CompiledDataSize);

				CompiledDataFileHandle->Seek(HeaderSize);
				CompiledDataFileHandle->Read(CompiledDataBytes.GetData(), CompiledDataSize);

				FMemoryReaderView MemoryReader(CompiledDataBytes);
				
				if (LoadModelResources(MemoryReader, RunningPlatform))
				{
					TArray<FName> OutOfDatePackages;
					TArray<FName> AddedPackages;
					TArray<FName> RemovedPackages;
					bool bReleaseVersion;
					const bool bOutOfDate = IsCompilationOutOfDate(false, OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersion);
					if (!bOutOfDate)
					{
						LoadModelStreamableBulk(MemoryReader, /* bIsCooking */false);
						LoadModel(MemoryReader);
					}
					else
					{
						if (OutOfDatePackages.Num())
						{
							UE_LOG(LogMutable, Display, TEXT("Invalidating compiled data due to changes in %s."), *OutOfDatePackages[0].ToString());
						}
						
						PrintParticipatingPackagesDiff(OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersion);
					}
				}
			}
		}
	}

	if (!GetModel()) // Failed to load the model
	{
		Status.NextState(FCustomizableObjectStatusTypes::EState::NoModel);
	}
}


void UCustomizableObjectPrivate::CompileForTargetPlatform(UCustomizableObject& CustomizableObject, const ITargetPlatform& TargetPlatform)
{
	ICustomizableObjectEditorModule* EditorModule = ICustomizableObjectEditorModule::Get();
	if (!EditorModule || !EditorModule->IsRootObject(CustomizableObject))
	{
		SetIsChildObject(true);
		return;
	}

	const bool bAsync = CVarMutableAsyncCook.GetValueOnAnyThread();

	TSharedRef<FCompilationRequest> CompileRequest = MakeShared<FCompilationRequest>(CustomizableObject, bAsync);
	FCompilationOptions& Options = CompileRequest->GetCompileOptions();
	Options.OptimizationLevel = UE_MUTABLE_MAX_OPTIMIZATION;	// Force max optimization when packaging.
	Options.TextureCompression = ECustomizableObjectTextureCompression::HighQuality;
	Options.bIsCooking = true;
	Options.bUseBulkData = CVarMutableUseBulkData.GetValueOnAnyThread();
	Options.TargetPlatform = &TargetPlatform;

	const int32 DDCUsage = CVarMutableDerivedDataCacheUsage.GetValueOnAnyThread();
	UE::DerivedData::ECachePolicy DefaultCachePolicy = UE::DerivedData::ECachePolicy::None;
	if (DDCUsage == 1)
	{
		DefaultCachePolicy = UE::DerivedData::ECachePolicy::Local;
	}
	else if (DDCUsage == 2)
	{
		DefaultCachePolicy = UE::DerivedData::ECachePolicy::Default;
	}
	CompileRequest->SetDerivedDataCachePolicy(DefaultCachePolicy);

	CompileRequests.Add(CompileRequest);

	EditorModule->CompileCustomizableObject(CompileRequest, true);
}


bool UCustomizableObject::ConditionalAutoCompile()
{
	check(IsInGameThread());

	// Don't compile objects being compiled
	if (GetPrivate()->IsLocked())
	{
		return false;
	}

	// Don't compile compiled objects
	if (IsCompiled())
	{
		return true;
	}

	// Model has not loaded yet
	if (GetPrivate()->Status.Get() == FCustomizableObjectStatusTypes::EState::Loading)
	{
		return false;
	}

	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	if (!System || !System->IsValidLowLevel() || System->HasAnyFlags(RF_BeginDestroyed))
	{
		return false;
	}

	// Don't re-compile objects if they failed to compile. 
	if (GetPrivate()->CompilationResult == ECompilationResultPrivate::Errors)
	{
		return false;
	}

	// By default, don't compile in a commandlet.
	// Notice that the cook is also a commandlet. Do not add a warning/error, otherwise we could end up invalidating the cook for no reason.
	if (IsRunningCookCommandlet() || (IsRunningCommandlet() && !System->IsAutoCompileCommandletEnabled()))
	{
		return false;
	}

	// Don't compile if we're running game or if Mutable or AutoCompile is disabled.
	if (IsRunningGame() || !System->IsActive() || !System->IsAutoCompileEnabled())
	{
		System->AddUncompiledCOWarning(*this);
		return false;
	}

	ICustomizableObjectEditorModule* EditorModule = ICustomizableObjectEditorModule::Get();
	if (ensure(EditorModule))
	{
		// Sync/Async compilation
		TSharedRef<FCompilationRequest> CompileRequest = MakeShared<FCompilationRequest>(*this, !System->IsAutoCompilationSync());
		CompileRequest->GetCompileOptions().bSilentCompilation = true;
		EditorModule->CompileCustomizableObject(CompileRequest);
	}

	return IsCompiled();
}


FReply UCustomizableObjectPrivate::AddNewParameterProfile(FString Name, UCustomizableObjectInstance& CustomInstance)
{
	if (Name.IsEmpty())
	{
		Name = "Unnamed_Profile";
	}

	FString ProfileName = Name;
	int32 Suffix = 0;

	bool bUniqueNameFound = false;
	while (!bUniqueNameFound)
	{
		FProfileParameterDat* Found = GetPublic()->InstancePropertiesProfiles.FindByPredicate(
			[&ProfileName](const FProfileParameterDat& Profile) { return Profile.ProfileName == ProfileName; });

		bUniqueNameFound = static_cast<bool>(!Found);
		if (Found)
		{
			ProfileName = Name + FString::FromInt(Suffix);
			++Suffix;
		}
	}

	int32 ProfileIndex = GetPublic()->InstancePropertiesProfiles.Emplace();

	GetPublic()->InstancePropertiesProfiles[ProfileIndex].ProfileName = ProfileName;
	CustomInstance.GetPrivate()->SaveParametersToProfile(ProfileIndex);

	Modify();

	return FReply::Handled();
}


FString UCustomizableObjectPrivate::GetCompiledDataFolderPath()
{	
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() + TEXT("MutableStreamedDataEditor/"));
}


FString UCustomizableObjectPrivate::GetCompiledDataFileName(bool bIsModel, const ITargetPlatform* InTargetPlatform, bool bIsDiskStreamer)
{
	const FString PlatformName = InTargetPlatform ? InTargetPlatform->PlatformName() : FPlatformProperties::PlatformName();
	const FString FileIdentifier = bIsDiskStreamer ? Identifier.ToString() : GenerateIdentifier(*GetPublic()).ToString();
	const FString Extension = bIsModel ? TEXT("_M.mut") : TEXT("_S.mut");
	return PlatformName + FileIdentifier + Extension;
}


FString UCustomizableObject::GetDesc()
{
	int32 States = GetStateCount();
	int32 Params = GetParameterCount();
	return FString::Printf(TEXT("%d States, %d Parameters"), States, Params);
}


void UCustomizableObjectPrivate::SaveEmbeddedData(FArchive& Ar)
{
	UE_LOG(LogMutable, Verbose, TEXT("Saving embedded data for Customizable Object [%s] now at position %d."), *GetName(), int(Ar.Tell()));

	TSharedPtr<mu::Model> Model = GetModel();

	int32 InternalVersion = Model ? CurrentSupportedVersion : -1;
	Ar << InternalVersion;

	if (Model)
	{	
		// Serialise the entire model, but unload the streamable data first.
		{
			UnrealMutableOutputStream Stream(Ar);
			mu::OutputArchive Arch(&Stream);
			mu::Model::Serialise(Model.Get(), Arch);
		}

		UE_LOG(LogMutable, Verbose, TEXT("Saved embedded data for Customizable Object [%s] now at position %d."), *GetName(), int(Ar.Tell()));
	}
}

#endif // End WITH_EDITOR 

void UCustomizableObjectPrivate::LoadEmbeddedData(FArchive& Ar)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObject::LoadEmbeddedData)

	int32 InternalVersion;
	Ar << InternalVersion;

	// If this fails, something went wrong with the packaging: we have data that belongs
	// to a different version than the code.
	if (ensure(CurrentSupportedVersion == InternalVersion))
	{		
		// Load model
		UnrealMutableInputStream Stream(Ar);
		mu::InputArchive Arch(&Stream);
		TSharedPtr<mu::Model, ESPMode::ThreadSafe> Model = mu::Model::StaticUnserialise(Arch);

		SetModel(Model, FGuid());
	}
}


const UCustomizableObjectPrivate* UCustomizableObject::GetPrivate() const
{
	check(Private);
	return Private;
}


UCustomizableObjectPrivate* UCustomizableObject::GetPrivate()
{
	check(Private);
	return Private;
}


bool UCustomizableObject::IsCompiled() const
{
#if WITH_EDITOR
	const bool bIsCompiled = Private->GetModel() != nullptr && Private->GetModel()->IsValid();
#else
	const bool bIsCompiled = Private->GetModel() != nullptr;
#endif

	return bIsCompiled;
}


void UCustomizableObjectPrivate::AddUncompiledCOWarning(const FString& AdditionalLoggingInfo)
{
	// Send a warning (on-screen notification, log error, and in-editor notification)
	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();
	if (!System || !System->IsValidLowLevel() || System->HasAnyFlags(RF_BeginDestroyed))
	{
		return;
	}

	System->AddUncompiledCOWarning(*GetPublic(), &AdditionalLoggingInfo);
}


USkeletalMesh* UCustomizableObject::GetComponentMeshReferenceSkeletalMesh(const FName& ComponentName) const
{
#if WITH_EDITORONLY_DATA
	if (const ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		return Module->GetReferenceSkeletalMesh(*this, ComponentName);
	}
#else
	const FModelResources& ModelResources = Private->GetModelResources();
	int32 ObjectComponentIndex = ModelResources.ComponentNames.IndexOfByKey(ComponentName);
	if (ModelResources.ReferenceSkeletalMeshesData.IsValidIndex(ObjectComponentIndex))
	{
		// Can be nullptr if RefSkeletalMeshes are not loaded yet.
		return ModelResources.ReferenceSkeletalMeshesData[ObjectComponentIndex].SkeletalMesh;
	}
#endif
	return nullptr;
}


int32 UCustomizableObject::FindState( const FString& Name ) const
{
	int32 Result = -1;
	if (Private->GetModel())
	{
		Result = Private->GetModel()->FindState(Name);
	}

	return Result;
}


int32 UCustomizableObject::GetStateCount() const
{
	int32 Result = 0;

	if (Private->GetModel())
	{
		Result = Private->GetModel()->GetStateCount();
	}

	return Result;
}


FString UCustomizableObject::GetStateName(int32 StateIndex) const
{
	return GetPrivate()->GetStateName(StateIndex);
}


FString UCustomizableObjectPrivate::GetStateName(int32 StateIndex) const
{
	FString Result;

	if (GetModel())
	{
		Result = GetModel()->GetStateName(StateIndex);
	}

	return Result;
}


int32 UCustomizableObject::GetStateParameterCount( int32 StateIndex ) const
{
	int32 Result = 0;

	if (Private->GetModel())
	{
		Result = Private->GetModel()->GetStateParameterCount(StateIndex);
	}

	return Result;
}

int32 UCustomizableObject::GetStateParameterIndex(int32 StateIndex, int32 ParameterIndex) const
{
	int32 Result = 0;

	if (Private->GetModel())
	{
		Result = Private->GetModel()->GetStateParameterIndex(StateIndex, ParameterIndex);
	}

	return Result;
}


int32 UCustomizableObject::GetStateParameterCount(const FString& StateName) const
{
	int32 StateIndex = FindState(StateName);
	
	return GetStateParameterCount(StateIndex);
}


FString UCustomizableObject::GetStateParameterName(const FString& StateName, int32 ParameterIndex) const
{
	int32 StateIndex = FindState(StateName);
	
	return GetStateParameterName(StateIndex, ParameterIndex);
}

FString UCustomizableObject::GetStateParameterName(int32 StateIndex, int32 ParameterIndex) const
{
	return GetParameterName(GetStateParameterIndex(StateIndex, ParameterIndex));
}


#if WITH_EDITORONLY_DATA
void UCustomizableObjectPrivate::PostCompile()
{
	for (TObjectIterator<UCustomizableObjectInstance> It; It; ++It)
	{
		if (It->GetCustomizableObject() == this->GetPublic())
		{
			// This cannot be bound to the PostCompileDelegate below because the CO Editor binds to it too and the order of broadcast is indeterminate.
			// The Instance's OnPostCompile() must happen before all the other bindings.
			It->GetPrivate()->OnPostCompile();
		}
	}

	PostCompileDelegate.Broadcast();
}
#endif


const UCustomizableObjectBulk* UCustomizableObjectPrivate::GetStreamableBulkData() const
{
	return GetPublic()->BulkData;
}


UCustomizableObject* UCustomizableObjectPrivate::GetPublic() const
{
	UCustomizableObject* Public = StaticCast<UCustomizableObject*>(GetOuter());
	check(Public);

	return Public;
}

#if WITH_EDITORONLY_DATA
FPostCompileDelegate& UCustomizableObject::GetPostCompileDelegate()
{
	return GetPrivate()->PostCompileDelegate;
}
#endif


UCustomizableObjectInstance* UCustomizableObject::CreateInstance()
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObject::CreateInstance)

	UCustomizableObjectInstance* PreviewInstance = NewObject<UCustomizableObjectInstance>(GetTransientPackage(), NAME_None, RF_Transient);
	PreviewInstance->SetObject(this);
	PreviewInstance->GetPrivate()->bShowOnlyRuntimeParameters = false;

	UE_LOG(LogMutable, Verbose, TEXT("Created Customizable Object Instance."));

	return PreviewInstance;
}


int32 UCustomizableObject::GetNumLODs() const
{
	if (IsCompiled())
	{
		return GetPrivate()->GetModelResources().NumLODs;
	}

	return 0;
}

int32 UCustomizableObject::GetComponentCount() const
{
	if (IsCompiled())
	{
		return GetPrivate()->GetModelResources().ComponentNames.Num();
	}

	return 0;
}


FName UCustomizableObject::GetComponentName(int32 ObjectComponentIndex) const
{
	if (IsCompiled())
	{
		const TArray<FName>& ComponentNames = GetPrivate()->GetModelResources().ComponentNames;
		if (ComponentNames.IsValidIndex(ObjectComponentIndex))
		{
			return ComponentNames[ObjectComponentIndex];
		}
	}

	return NAME_None;
}


int32 UCustomizableObject::GetParameterCount() const
{
	return GetPrivate()->ParameterProperties.Num();
}


EMutableParameterType UCustomizableObject::GetParameterType(int32 ParamIndex) const
{
	return GetPrivate()->GetParameterType(ParamIndex);
}


EMutableParameterType UCustomizableObjectPrivate::GetParameterType(int32 ParamIndex) const
{
	if (ParamIndex >= 0 && ParamIndex < ParameterProperties.Num())
	{
		return ParameterProperties[ParamIndex].Type;
	}
	else
	{
		UE_LOG(LogMutable, Error, TEXT("Index [%d] out of ParameterProperties bounds at GetParameterType."), ParamIndex);
	}

	return EMutableParameterType::None;
}


EMutableParameterType UCustomizableObject::GetParameterTypeByName(const FString& Name) const
{
	const int32 Index = FindParameter(Name); 
	if (GetPrivate()->ParameterProperties.IsValidIndex(Index))
	{
		return GetPrivate()->ParameterProperties[Index].Type;
	}

	UE_LOG(LogMutable, Warning, TEXT("Name '%s' does not exist in ParameterProperties lookup table at GetParameterTypeByName at CO %s."), *Name, *GetName());

	for (int32 ParamIndex = 0; ParamIndex < GetPrivate()->ParameterProperties.Num(); ++ParamIndex)
	{
		if (GetPrivate()->ParameterProperties[ParamIndex].Name == Name)
		{
			return GetPrivate()->ParameterProperties[ParamIndex].Type;
		}
	}

	UE_LOG(LogMutable, Warning, TEXT("Name '%s' does not exist in ParameterProperties at GetParameterTypeByName at CO %s."), *Name, *GetName());

	return EMutableParameterType::None;
}


static const FString s_EmptyString;

const FString & UCustomizableObject::GetParameterName(int32 ParamIndex) const
{
	if (ParamIndex >= 0 && ParamIndex < GetPrivate()->ParameterProperties.Num())
	{
		return GetPrivate()->ParameterProperties[ParamIndex].Name;
	}
	else
	{
		UE_LOG(LogMutable, Warning, TEXT("Index [%d] out of ParameterProperties bounds at GetParameterName at CO %s."), ParamIndex, *GetName());
	}

	return s_EmptyString;
}


void UCustomizableObjectPrivate::UpdateParameterPropertiesFromModel(const TSharedPtr<mu::Model>& Model)
{
	if (Model)
	{
		mu::ParametersPtr MutableParameters = mu::Model::NewParameters(Model);
		const int32 NumParameters = MutableParameters->GetCount();

		TArray<int32> TypedParametersCount;
		TypedParametersCount.SetNum(static_cast<int32>(mu::PARAMETER_TYPE::T_COUNT));

		ParameterProperties.Reset(NumParameters);
		ParameterPropertiesLookupTable.Empty(NumParameters);
		for (int32 Index = 0; Index < NumParameters; ++Index)
		{
			FMutableModelParameterProperties Data;

			Data.Name = MutableParameters->GetName(Index);
			Data.Type = EMutableParameterType::None;

			mu::PARAMETER_TYPE ParameterType = MutableParameters->GetType(Index);
			switch (ParameterType)
			{
			case mu::PARAMETER_TYPE::T_BOOL:
			{
				Data.Type = EMutableParameterType::Bool;
				break;
			}

			case mu::PARAMETER_TYPE::T_INT:
			{
				Data.Type = EMutableParameterType::Int;

				const int32 ValueCount = MutableParameters->GetIntPossibleValueCount(Index);
				Data.PossibleValues.Reserve(ValueCount);
				for (int32 ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
				{
					FMutableModelParameterValue& ValueData = Data.PossibleValues.AddDefaulted_GetRef();
					ValueData.Name = MutableParameters->GetIntPossibleValueName(Index, ValueIndex);
					ValueData.Value = MutableParameters->GetIntPossibleValue(Index, ValueIndex);
				}
				break;
			}

			case mu::PARAMETER_TYPE::T_FLOAT:
			{
				Data.Type = EMutableParameterType::Float;
				break;
			}

			case mu::PARAMETER_TYPE::T_COLOUR:
			{
				Data.Type = EMutableParameterType::Color;
				break;
			}

			case mu::PARAMETER_TYPE::T_PROJECTOR:
			{
				Data.Type = EMutableParameterType::Projector;
				break;
			}

			case mu::PARAMETER_TYPE::T_MATRIX:
			{
				Data.Type = EMutableParameterType::Transform;
				break;
			}
				
			case mu::PARAMETER_TYPE::T_IMAGE:
			{
				Data.Type = EMutableParameterType::Texture;
				break;
			}

			default:
				// Unhandled type?
				check(false);
				break;
			}

			ParameterProperties.Add(Data);
			ParameterPropertiesLookupTable.Add(Data.Name, FMutableParameterIndex(Index, TypedParametersCount[static_cast<int32>(ParameterType)]++));
		}
	}
	else
	{
		ParameterProperties.Empty();
		ParameterPropertiesLookupTable.Empty();
	}
}


int32 UCustomizableObject::GetParameterDescriptionCount(const FString& ParamName) const
{
	return 0;
}


int32 UCustomizableObject::GetIntParameterNumOptions(int32 ParamIndex) const
{
	if (ParamIndex >= 0 && ParamIndex < GetPrivate()->ParameterProperties.Num())
	{
		return GetPrivate()->ParameterProperties[ParamIndex].PossibleValues.Num();
	}
	else
	{
		UE_LOG(LogMutable, Warning, TEXT("Index [%d] out of ParameterProperties bounds at GetIntParameterNumOptions at CO %s."), ParamIndex, *GetName());
	}

	return 0;
}


const FString& UCustomizableObject::GetIntParameterAvailableOption(int32 ParamIndex, int32 K) const
{
	if (ParamIndex >= 0 && ParamIndex < GetPrivate()->ParameterProperties.Num())
	{
		if (K >= 0 && K < GetIntParameterNumOptions(ParamIndex))
		{
			return GetPrivate()->ParameterProperties[ParamIndex].PossibleValues[K].Name;
		}
		else
		{
			UE_LOG(LogMutable, Warning, TEXT("Index [%d] out of IntParameterNumOptions bounds at GetIntParameterAvailableOption at CO %s."), K, *GetName());
		}
	}
	else
	{
		UE_LOG(LogMutable, Warning, TEXT("Index [%d] out of ParameterProperties bounds at GetIntParameterAvailableOption at CO %s."), ParamIndex, *GetName());
	}

	return s_EmptyString;
}


int32 UCustomizableObject::FindParameter(const FString& Name) const
{
	return GetPrivate()->FindParameter(Name);
}


int32 UCustomizableObjectPrivate::FindParameter(const FString& Name) const
{
	if (const FMutableParameterIndex* Found = ParameterPropertiesLookupTable.Find(Name))
	{
		return Found->Index;
	}

	return INDEX_NONE;
}


int32 UCustomizableObjectPrivate::FindParameterTyped(const FString& Name, EMutableParameterType Type) const
{
	if (const FMutableParameterIndex* Found = ParameterPropertiesLookupTable.Find(Name))
	{
		if (ParameterProperties[Found->Index].Type == Type)
		{
			return Found->TypedIndex;
		}
	}

	return INDEX_NONE;
}


int32 UCustomizableObject::FindIntParameterValue(int32 ParamIndex, const FString& Value) const
{
	return GetPrivate()->FindIntParameterValue(ParamIndex, Value);
}


int32 UCustomizableObjectPrivate::FindIntParameterValue(int32 ParamIndex, const FString& Value) const
{
	int32 MinValueIndex = INDEX_NONE;
	
	if (ParamIndex >= 0 && ParamIndex < ParameterProperties.Num())
	{
		const TArray<FMutableModelParameterValue>& PossibleValues = ParameterProperties[ParamIndex].PossibleValues;
		if (PossibleValues.Num())
		{
			MinValueIndex = PossibleValues[0].Value;

			for (int32 OrderValue = 0; OrderValue < PossibleValues.Num(); ++OrderValue)
			{
				const FString& Name = PossibleValues[OrderValue].Name;

				if (Name == Value)
				{
					int32 CorrectedValue = OrderValue + MinValueIndex;
					check(PossibleValues[OrderValue].Value == CorrectedValue);
					return CorrectedValue;
				}
			}
		}
	}
	
	return MinValueIndex;
}


FString UCustomizableObject::FindIntParameterValueName(int32 ParamIndex, int32 ParamValue) const
{
	if (ParamIndex >= 0 && ParamIndex < GetPrivate()->ParameterProperties.Num())
	{
		const TArray<FMutableModelParameterValue> & PossibleValues = GetPrivate()->ParameterProperties[ParamIndex].PossibleValues;

		const int32 MinValueIndex = !PossibleValues.IsEmpty() ? PossibleValues[0].Value : 0;
		ParamValue = ParamValue - MinValueIndex;

		if (PossibleValues.IsValidIndex(ParamValue))
		{
			return PossibleValues[ParamValue].Name;
		}
	}
	else
	{
		UE_LOG(LogMutable, Warning, TEXT("Index [%d] out of ParameterProperties bounds at FindIntParameterValueName at CO %s."), ParamIndex, *GetName());
	}

	return FString();
}


USkeletalMesh* UCustomizableObject::GetRefSkeletalMesh(int32 ObjectComponentIndex) const
{
	return GetComponentMeshReferenceSkeletalMesh(FName(FString::FromInt(ObjectComponentIndex)));
}


FMutableParamUIMetadata UCustomizableObject::GetParameterUIMetadata(const FString& ParamName) const
{
	const FMutableParameterData* ParameterData = Private->GetModelResources().ParameterUIDataMap.Find(ParamName);
	return ParameterData ? ParameterData->ParamUIMetadata : FMutableParamUIMetadata();
}


FMutableParamUIMetadata UCustomizableObject::GetIntParameterOptionUIMetadata(const FString& ParamName, const FString& OptionName) const
{
	const int32 ParameterIndex = FindParameter(ParamName);
	if (ParameterIndex == INDEX_NONE)
	{
		return {};
	}
	
	const FMutableParameterData* ParameterData = Private->GetModelResources().ParameterUIDataMap.Find(ParamName);
	if (!ParameterData)
	{
		return {};
	}

	const FIntegerParameterUIData* IntegerParameterUIData = ParameterData->ArrayIntegerParameterOption.Find(OptionName);
	return IntegerParameterUIData ? IntegerParameterUIData->ParamUIMetadata : FMutableParamUIMetadata();
}

ECustomizableObjectGroupType UCustomizableObject::GetIntParameterGroupType(const FString& ParamName) const
{
	const int32 ParameterIndex = FindParameter(ParamName);
	if (ParameterIndex == INDEX_NONE)
	{
		return ECustomizableObjectGroupType::COGT_TOGGLE;
	}		
	
	const FMutableParameterData* ParameterData = Private->GetModelResources().ParameterUIDataMap.Find(ParamName);
	if (!ParameterData)
	{
		return ECustomizableObjectGroupType::COGT_TOGGLE;
	}

	return ParameterData->IntegerParameterGroupType;
}

FMutableStateUIMetadata UCustomizableObject::GetStateUIMetadata(const FString& StateName) const
{
	const FMutableStateData* StateData = Private->GetModelResources().StateUIDataMap.Find(StateName);
	return StateData ? StateData->StateUIMetadata : FMutableStateUIMetadata();
}


#if WITH_EDITOR
TArray<TSoftObjectPtr<UDataTable>> UCustomizableObject::GetIntParameterOptionDataTable(const FString& ParamName, const FString& OptionName)
{
	const FModelResources& ModelResources = GetPrivate()->GetModelResources();
	if (const TSet<TSoftObjectPtr<UDataTable>>* Result = ModelResources.IntParameterOptionDataTable.Find(MakeTuple(ParamName, OptionName)))
	{
		return Result->Array();
	}
	
	return {};
}
#endif


float UCustomizableObject::GetFloatParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent float parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectFloatParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectFloatParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	return Model->GetFloatDefaultValue(ParameterIndex);
}


int32 UCustomizableObject::GetIntParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent integer parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectIntParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectIntParameterValue::DEFAULT_PARAMETER_VALUE;
	}
	
	return Model->GetIntDefaultValue(ParameterIndex);
}


bool UCustomizableObject::GetBoolParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent boolean parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectBoolParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectBoolParameterValue::DEFAULT_PARAMETER_VALUE;
	}
	
	return Model->GetBoolDefaultValue(ParameterIndex);
}


FLinearColor UCustomizableObject::GetColorParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent color parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectVectorParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectVectorParameterValue::DEFAULT_PARAMETER_VALUE;
	}
	
	FLinearColor Value;
	Model->GetColourDefaultValue(ParameterIndex, &Value.R, &Value.G, &Value.B, &Value.A);

	return Value;
}


FTransform UCustomizableObject::GetTransformParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent color parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectTransformParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectTransformParameterValue::DEFAULT_PARAMETER_VALUE;
	}
	
	const FMatrix44f Matrix = Model->GetMatrixDefaultValue(ParameterIndex);

	return FTransform(FMatrix(Matrix));
}


void UCustomizableObject::GetProjectorParameterDefaultValue(const FString& InParameterName, FVector3f& OutPos,
	FVector3f& OutDirection, FVector3f& OutUp, FVector3f& OutScale, float& OutAngle,
	ECustomizableObjectProjectorType& OutType) const
{
	const FCustomizableObjectProjector Projector = GetProjectorParameterDefaultValue(InParameterName);
		
	OutType = Projector.ProjectionType;
	OutPos = Projector.Position;
	OutDirection = Projector.Direction;
	OutUp = Projector.Up;
	OutScale = Projector.Scale;
	OutAngle = Projector.Angle;
}


FCustomizableObjectProjector UCustomizableObject::GetProjectorParameterDefaultValue(const FString& InParameterName) const 
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent projector [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectProjectorParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectProjectorParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	FCustomizableObjectProjector Value;
	mu::PROJECTOR_TYPE Type;
	Model->GetProjectorDefaultValue(ParameterIndex, &Type, &Value.Position, &Value.Direction, &Value.Up, &Value.Scale, &Value.Angle);
	Value.ProjectionType = ProjectorUtils::GetEquivalentProjectorType(Type);
	
	return Value;
}


FName UCustomizableObject::GetTextureParameterDefaultValue(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent texture parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return FCustomizableObjectTextureParameterValue::DEFAULT_PARAMETER_VALUE;
	}

	const TSharedPtr<const mu::Model>& Model = GetPrivate()->GetModel();
	if (!Model)
	{
		checkNoEntry();
		return FCustomizableObjectTextureParameterValue::DEFAULT_PARAMETER_VALUE;
	}
	
	return Model->GetImageDefaultValue(ParameterIndex);
}


bool UCustomizableObject::IsParameterMultidimensional(const FString& InParameterName) const
{
	const int32 ParameterIndex = FindParameter(InParameterName);
	if (ParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMutable, Error, TEXT("Tried to access the default value of the nonexistent parameter [%s] in the CustomizableObject [%s]."), *InParameterName, *GetName());
		return false;
	}

	return IsParameterMultidimensional(ParameterIndex);
}


bool UCustomizableObject::IsParameterMultidimensional(const int32& InParamIndex) const
{
	check(InParamIndex != INDEX_NONE);
	if (Private->GetModel())
	{
		return Private->GetModel()->IsParameterMultidimensional(InParamIndex);
	}

	return false;
}


void UCustomizableObjectPrivate::ApplyStateForcedValuesToParameters(int32 State, mu::Parameters* Parameters)
{
	const FString& StateName = GetPublic()->GetStateName(State);
	const FMutableStateData* StateData = GetModelResources().StateUIDataMap.Find(StateName);
	if (!StateData)
	{
		return;
	}
	
	for (const TPair<FString, FString>& ForcedParameter : StateData->ForcedParameterValues)
	{
		int32 ForcedParameterIndex = FindParameter(ForcedParameter.Key);
		if (ForcedParameterIndex == INDEX_NONE)
		{
			continue;
		}

		bool bIsMultidimensional = Parameters->NewRangeIndex(ForcedParameterIndex).get() != nullptr;
		if (!bIsMultidimensional)
		{
			switch (GetParameterType(ForcedParameterIndex))
			{
			case EMutableParameterType::Int:
			{
				FString StringValue = ForcedParameter.Value;
				if (StringValue.IsNumeric())
				{
					Parameters->SetIntValue(ForcedParameterIndex, FCString::Atoi(*StringValue));
				}
				else
				{
					int32 IntParameterIndex = FindIntParameterValue(ForcedParameterIndex, StringValue);
					Parameters->SetIntValue(ForcedParameterIndex, IntParameterIndex);
				}
				break;
			}
			case EMutableParameterType::Bool:
			{
				Parameters->SetBoolValue(ForcedParameterIndex, ForcedParameter.Value.ToBool());
				break;
			}
			default:
			{
				UE_LOG(LogMutable, Warning, TEXT("Forced parameter type not supported."));
				break;
			}
			}
		}
	}
}


void UCustomizableObjectPrivate::GetLowPriorityTextureNames(TArray<FString>& OutTextureNames)
{
	OutTextureNames.Reset(GetPublic()->LowPriorityTextures.Num());

	if (!GetPublic()->LowPriorityTextures.IsEmpty())
	{
		const FModelResources& LocalModelResources = GetModelResources();
		const int32 ImageCount = LocalModelResources.ImageProperties.Num();
		for (int32 ImageIndex = 0; ImageIndex < ImageCount; ++ImageIndex)
		{
			if (GetPublic()->LowPriorityTextures.Find(FName(LocalModelResources.ImageProperties[ImageIndex].TextureParameterName)) != INDEX_NONE)
			{
				OutTextureNames.Add(FString::FromInt(ImageIndex));
			}
		}
	}
}


int32 UCustomizableObjectPrivate::GetMinLODIndex() const
{
	int32 MinLODIdx = 0;

	if (GEngine && GEngine->UseSkeletalMeshMinLODPerQualityLevels)
	{
		if (UCustomizableObjectSystem::GetInstance() != nullptr)
		{
			MinLODIdx = GetPublic()->LODSettings.MinQualityLevelLOD.GetValue(UCustomizableObjectSystem::GetInstance()->GetSkeletalMeshMinLODQualityLevel());
		}
	}
	else
	{
		MinLODIdx = GetPublic()->LODSettings.MinLOD.GetValue();
	}

	return FMath::Max(MinLODIdx, static_cast<int32>(GetModelResources().FirstLODAvailable));
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

USkeletalMesh* FMeshCache::Get(const TArray<mu::FResourceID>& Key)
{
	const TWeakObjectPtr<USkeletalMesh>* Result = GeneratedMeshes.Find(Key);
	return Result ? Result->Get() : nullptr;
}


void FMeshCache::Add(const TArray<mu::FResourceID>& Key, USkeletalMesh* Value)
{
	if (!Value)
	{
		return;
	}
	
	GeneratedMeshes.Add(Key, Value);

	// Remove invalid SkeletalMeshes from the cache.
	for (auto MeshIterator = GeneratedMeshes.CreateIterator(); MeshIterator; ++MeshIterator)
	{
		if (MeshIterator.Value().IsStale())
		{
			MeshIterator.RemoveCurrent();
		}
	}	
}


USkeleton* FSkeletonCache::Get(const TArray<uint16>& Key)
{
	const TWeakObjectPtr<USkeleton>* Result = MergedSkeletons.Find(Key);
	return Result ? Result->Get() : nullptr;
}


void FSkeletonCache::Add(const TArray<uint16>& Key, USkeleton* Value)
{
	if (!Value)
	{
		return;
	}

	MergedSkeletons.Add(Key, Value);

	// Remove invalid SkeletalMeshes from the cache.
	for (auto SkeletonIterator = MergedSkeletons.CreateIterator(); SkeletonIterator; ++SkeletonIterator)
	{
		if (SkeletonIterator.Value().IsStale())
		{
			SkeletonIterator.RemoveCurrent();
		}
	}
}


FArchive& operator<<(FArchive& Ar, FIntegerParameterUIData& Struct)
{
	Ar << Struct.ParamUIMetadata;
	
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableParameterData& Struct)
{
	Ar << Struct.ParamUIMetadata;
	Ar << Struct.Type;
	Ar << Struct.ArrayIntegerParameterOption;
	Ar << Struct.IntegerParameterGroupType;
	
	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableStateData& Struct)
{
	Ar << Struct.StateUIMetadata;
	Ar << Struct.bLiveUpdateMode;
	Ar << Struct.bDisableTextureStreaming;
	Ar << Struct.bReuseInstanceTextures;
	Ar << Struct.ForcedParameterValues;

	return Ar;
}


void FModelStreamableBulkData::Serialize(FArchive& Ar, UObject* Owner, bool bCooked)
{
	MUTABLE_CPUPROFILER_SCOPE(FModelStreamableBulkData::Serialize);

	Ar << ModelStreamables;
	Ar << ClothingStreamables;
	Ar << RealTimeMorphStreamables;

	if (bCooked)
	{
		int32 NumBulkDatas = StreamableBulkData.Num();
		Ar << NumBulkDatas;

		StreamableBulkData.SetNum(NumBulkDatas);

		for (FByteBulkData& BulkData : StreamableBulkData)
		{
			BulkData.Serialize(Ar, Owner);
		}
	}
}


UModelStreamableData::UModelStreamableData()
{
	StreamingData = MakeShared<FModelStreamableBulkData>();
}


void UModelStreamableData::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (bCooked && !IsTemplate() && !Ar.IsCountingMemory())
	{
		check(StreamingData);
		StreamingData->Serialize(Ar, GetOutermostObject(), bCooked);
	}
}


void UCustomizableObjectPrivate::SetModel(const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& Model, const FGuid Id)
{
	if (MutableModel == Model
#if WITH_EDITOR
		&& Identifier == Id
#endif
		)
	{
		return;
	}
	
#if WITH_EDITOR
	if (MutableModel)
	{
		MutableModel->Invalidate();
	}

	Identifier = Id;
#endif
	
	MutableModel = Model;

	// Create parameter properties
	UpdateParameterPropertiesFromModel(Model);

	using EState = FCustomizableObjectStatus::EState;
	Status.NextState(Model ? EState::ModelLoaded : EState::NoModel);
}


const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& UCustomizableObjectPrivate::GetModel()
{
	return MutableModel;
}


const TSharedPtr<const mu::Model, ESPMode::ThreadSafe> UCustomizableObjectPrivate::GetModel() const
{
	return MutableModel;
}

#if WITH_EDITOR
void UCustomizableObjectPrivate::SetModelStreamableBulkData(const TSharedPtr<FModelStreamableBulkData>& StreamableData, bool bIsCooking)
{
	if (bIsCooking)
	{
		if (!ModelStreamableData)
		{
			ModelStreamableData = NewObject<UModelStreamableData>(GetOuter());
		}

		ModelStreamableData->StreamingData = StreamableData;
	}
	else
	{
		ModelStreamableDataEditor = StreamableData;
	}
}
#endif

TSharedPtr<FModelStreamableBulkData> UCustomizableObjectPrivate::GetModelStreamableBulkData(bool bIsCooking)
{
#if WITH_EDITOR
	if (bIsCooking)
	{
		return ModelStreamableData ? ModelStreamableData->StreamingData : nullptr;
	}

	return ModelStreamableDataEditor;
#else
	return ModelStreamableData ? ModelStreamableData->StreamingData : nullptr;
#endif
}


const FModelResources& UCustomizableObjectPrivate::GetModelResources() const
{
#if WITH_EDITORONLY_DATA
	return ModelResourcesEditor;
#else
	return ModelResources;
#endif
}


#if WITH_EDITORONLY_DATA
FModelResources& UCustomizableObjectPrivate::GetModelResources(bool bIsCooking)
{
	const UCustomizableObjectPrivate* ConstThis = this;
	return *const_cast<FModelResources*>(&ConstThis->GetModelResources(bIsCooking));
}


const FModelResources& UCustomizableObjectPrivate::GetModelResources(bool bIsCooking) const
{
	return bIsCooking ? ModelResources : ModelResourcesEditor;
}
#endif


#if WITH_EDITOR
bool UCustomizableObjectPrivate::IsCompilationOutOfDate(bool bSkipIndirectReferences, TArray<FName>& OutOfDatePackages, TArray<FName>& AddedPackages, TArray<FName>& RemovedPackages, bool& bReleaseVersionDiff) const
{
	if (const ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		return Module->IsCompilationOutOfDate(*GetPublic(), bSkipIndirectReferences, OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersionDiff);
	}

	return false;		
}
#endif


TArray<FString>& UCustomizableObjectPrivate::GetCustomizableObjectClassTags()
{
	return GetPublic()->CustomizableObjectClassTags;
}


TArray<FString>& UCustomizableObjectPrivate::GetPopulationClassTags()
{
	return GetPublic()->PopulationClassTags;
}


TMap<FString, FParameterTags>& UCustomizableObjectPrivate::GetCustomizableObjectParametersTags()
{
	return GetPublic()->CustomizableObjectParametersTags;
}


#if WITH_EDITORONLY_DATA
TArray<FProfileParameterDat>& UCustomizableObjectPrivate::GetInstancePropertiesProfiles()
{
	return GetPublic()->InstancePropertiesProfiles;
}
#endif


TArray<FCustomizableObjectResourceData>& UCustomizableObjectPrivate::GetAlwaysLoadedExtensionData()
{
	return GetPublic()->AlwaysLoadedExtensionData;
}

const TArray<FCustomizableObjectResourceData>& UCustomizableObjectPrivate::GetAlwaysLoadedExtensionData() const
{
	return GetPublic()->AlwaysLoadedExtensionData;
}


TArray<FCustomizableObjectStreamedResourceData>& UCustomizableObjectPrivate::GetStreamedExtensionData()
{
	return GetPublic()->StreamedExtensionData;
}


const TArray<FCustomizableObjectStreamedResourceData>& UCustomizableObjectPrivate::GetStreamedExtensionData() const
{
	return GetPublic()->StreamedExtensionData;
}


const FCustomizableObjectResourceData* UCustomizableObjectPrivate::LoadStreamedResource(int32 ResourceIndex)
{
#if WITH_EDITORONLY_DATA
	FModelResources& LocalModelResources = ModelResourcesEditor;
#else
	FModelResources& LocalModelResources = ModelResources;
#endif

	if (LocalModelResources.StreamedResourceData.IsValidIndex(ResourceIndex))
	{
		FCustomizableObjectStreamedResourceData& Resource = LocalModelResources.StreamedResourceData[ResourceIndex];
		if (!Resource.IsLoaded())
		{
			Resource.NotifyLoaded(Resource.GetPath().Get());
		}

		return &Resource.GetLoadedData();
	}

	return nullptr;
}

void UCustomizableObjectPrivate::UnloadStreamedResource(int32 ResourceIndex)
{
	// Only Unload in cooked builds. Unloading them when in the editor will trigger an assert. 
	if (FPlatformProperties::RequiresCookedData())
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	FModelResources& LocalModelResources = ModelResourcesEditor;
#else
	FModelResources& LocalModelResources = ModelResources;
#endif

	if (LocalModelResources.StreamedResourceData.IsValidIndex(ResourceIndex))
	{
		LocalModelResources.StreamedResourceData[ResourceIndex].Unload();
	}
}


#if WITH_EDITORONLY_DATA
TObjectPtr<UEdGraph>& UCustomizableObjectPrivate::GetSource() const
{
	return GetPublic()->Source;
}


FCompilationOptions UCustomizableObjectPrivate::GetCompileOptions() const
{
	FCompilationOptions Options;
	Options.TextureCompression = TextureCompression;
	Options.OptimizationLevel = OptimizationLevel;
	Options.bUseDiskCompilation = bUseDiskCompilation;

	Options.TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();

	const int32 TargetBulkDataFileBytesOverride = CVarPackagedDataBytesLimitOverride.GetValueOnAnyThread();
	if ( TargetBulkDataFileBytesOverride >= 0)
	{
		Options.PackagedDataBytesLimit = TargetBulkDataFileBytesOverride;
		UE_LOG(LogMutable,Display, TEXT("Ignoring CO PackagedDataBytesLimit value in favour of overriding CVar value : mutable.PackagedDataBytesLimitOverride %llu"), Options.PackagedDataBytesLimit);
	}
	else
	{
		Options.PackagedDataBytesLimit =  PackagedDataBytesLimit;
	}

	Options.EmbeddedDataBytesLimit = EmbeddedDataBytesLimit;
	Options.CustomizableObjectNumBoneInfluences = ICustomizableObjectModule::Get().GetNumBoneInfluences();
	Options.bRealTimeMorphTargetsEnabled = GetPublic()->bEnableRealTimeMorphTargets;
	Options.bClothingEnabled = GetPublic()->bEnableClothing;
	Options.b16BitBoneWeightsEnabled = GetPublic()->bEnable16BitBoneWeights;
	Options.bSkinWeightProfilesEnabled = GetPublic()->bEnableAltSkinWeightProfiles;
	Options.bPhysicsAssetMergeEnabled = GetPublic()->bEnablePhysicsAssetMerge;
	Options.bAnimBpPhysicsManipulationEnabled = GetPublic()->bEnableAnimBpPhysicsAssetsManipualtion;
	Options.ImageTiling = ImageTiling;

	return Options;
}
#endif

#if WITH_EDITOR
namespace MutablePrivate
{
	uint64 FFile::GetSize() const
	{
		uint64 FileSize = 0;
		for (const FBlock& Block : Blocks)
		{
			FileSize += Block.Size;
		}
		return FileSize;
	}


	void FFile::GetFileData(FMutableCachedPlatformData* PlatformData, TArray64<uint8>& DestData, bool bDropData)
	{
		check(PlatformData);

		const uint64 DestSize = DestData.Num();
		const int32 NumBlocks = Blocks.Num();

		const uint8* SourceData = nullptr;
		if (DataType == EDataType::Model)
		{
			for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
			{
				const FBlock& Block = Blocks[BlockIndex];
				check(Block.Offset + Block.Size <= DestSize);
				PlatformData->ModelStreamableData.Get(Block.Id, TArrayView64<uint8>(DestData.GetData() + Block.Offset, Block.Size), bDropData);
			}
			return;
		}
		else if (DataType == EDataType::RealTimeMorph)
		{
			SourceData = PlatformData->MorphData.GetData();
		}
		else if (DataType == EDataType::Clothing)
		{
			SourceData = PlatformData->ClothingData.GetData();
		}
		else
		{
			checkf(false, TEXT("Unknown file DataType found."));
		}

		for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
		{
			const FBlock& Block = Blocks[BlockIndex];
			check(Block.Offset + Block.Size <= DestSize);
			FMemory::Memcpy(DestData.GetData() + Block.Offset, SourceData + Block.Offset, Block.Size);
		}
	}

	FFileCategoryID::FFileCategoryID(EDataType InDataType, uint16 InResourceType, uint16 InFlags)
	{
		DataType = InDataType;
		ResourceType = InResourceType;
		Flags = InFlags;
	}

	uint32 GetTypeHash(const FFileCategoryID& Key)
	{
		uint32 Hash = (uint32)Key.DataType;
		Hash = HashCombine(Hash, Key.ResourceType);
		Hash = HashCombine(Hash, Key.Flags);
		return Hash;
	}


	TPair<FFileBucket&, FFileCategory&> FindOrAddCategory(TArray<FFileBucket>& Buckets, FFileBucket& DefaultBucket, const FFileCategoryID CategoryID)
	{
		// Find the category
		for (FFileBucket& Bucket : Buckets)
		{
			for (FFileCategory& Category : Bucket.Categories)
			{
				if (Category.Id == CategoryID)
				{
					return TPair<FFileBucket&, FFileCategory&>(Bucket, Category);
				}
			}
		}

		// Category not found, add to default bucket
		FFileCategory& Category = DefaultBucket.Categories.AddDefaulted_GetRef();
		Category.Id = CategoryID;
		return TPair<FFileBucket&, FFileCategory&>(DefaultBucket, Category);
	}


	struct FClassifyNode
	{
		TArray<FBlock> Blocks;
	};


	void AddNode(TMap<FFileCategoryID, FClassifyNode>& Nodes, int32 Slack, const FFileCategoryID& CategoryID, const FBlock& Block)
	{
		FClassifyNode& Root = Nodes.FindOrAdd(CategoryID);
		if (Root.Blocks.IsEmpty())
		{
			Root.Blocks.Reserve(Slack);
		}

		Root.Blocks.Add(Block);
	}
	
	void GenerateBulkDataFilesListWithFileLimit(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& ModelStreamableBulkData,
		uint32 NumFilesPerBucket,
		TArray<FFile>& OutBulkDataFiles)
	{
		MUTABLE_CPUPROFILER_SCOPE(GenerateBulkDataFilesListWithFileLimit);

		if (!Model)
		{
			return;
		}

		/* Overview.
		 *	1. Add categories to the different buckets and accumulate the size of its resources 
		 *	   to know the total size of each category and the size of the buckets.
		 *	2. Use the accumulated sizes to distribute the NumFilesPerBucket between the bucket's categories.
		 *	3. Generate the list of BulkData files based on the number of files per category.
		 */

		// Two buckets. One for non-optional data and one for optional data.
		TArray<FFileBucket> FileBuckets;

		// DefaultBucket is for non-optional BulkData
		FFileBucket& DefaultBucket = FileBuckets.AddDefaulted_GetRef();
		FFileBucket& OptionalBucket = FileBuckets.AddDefaulted_GetRef();

		// Model Roms. Iterate all Model roms to distribute them in categories.
		{
			// Add meshes and low-res textures to the Default bucket 
			DefaultBucket.Categories.Add({ FFileCategoryID(EDataType::Model, mu::DATATYPE::DT_MESH, 0), 0, 0, 0 });
			DefaultBucket.Categories.Add({ FFileCategoryID(EDataType::Model, mu::DATATYPE::DT_IMAGE, 0), 0, 0, 0 });

			// Add High-res textures to the Optional bucket
			OptionalBucket.Categories.Add({ FFileCategoryID(EDataType::Model, mu::DATATYPE::DT_IMAGE, (uint16)mu::ERomFlags::HighRes), 0, 0, 0});

			const int32 NumRoms = Model->GetRomCount();
			for (int32 RomIndex = 0; RomIndex < NumRoms; ++RomIndex)
			{
				uint32 BlockId = Model->GetRomId(RomIndex);
				const uint32 BlockSize = Model->GetRomSize(RomIndex);
				const uint16 BlockResourceType = Model->GetRomType(RomIndex);
				const mu::ERomFlags BlockFlags = Model->GetRomFlags(RomIndex);

				FFileCategoryID CategoryID = { EDataType::Model, BlockResourceType, uint16(BlockFlags) };
				TPair<FFileBucket&, FFileCategory&> It = FindOrAddCategory(FileBuckets, DefaultBucket, CategoryID); // Add block to an existing or new category
				It.Key.DataSize += BlockSize;
				It.Value.DataSize += BlockSize;
			}
		}

		// RealTime Morphs. Iterate RealTimeMorph streamables to accumulate their sizes.
		{
			// Add RealTimeMorphs to the Default bucket
			FFileCategory& RealTimeMorphCategory = DefaultBucket.Categories.AddDefaulted_GetRef();
			RealTimeMorphCategory.Id.DataType = EDataType::RealTimeMorph;

			const TMap<uint32, FRealTimeMorphStreamable>& RealTimeMorphStreamables = ModelStreamableBulkData.RealTimeMorphStreamables;
			for (const TPair<uint32, FRealTimeMorphStreamable>& MorphStreamable : RealTimeMorphStreamables)
			{
				RealTimeMorphCategory.DataSize += MorphStreamable.Value.Size;
			}

			DefaultBucket.DataSize += RealTimeMorphCategory.DataSize;
		}

		// Clothing. Iterate clothing streamables to accumulate their sizes.
		{
			// Add Clothing to the Default bucket
			FFileCategory& ClothingCategory = DefaultBucket.Categories.AddDefaulted_GetRef();
			ClothingCategory.Id.DataType = EDataType::Clothing;
			
			const TMap<uint32, FClothingStreamable>& ClothingStreamables = ModelStreamableBulkData.ClothingStreamables;
			for (const TPair<uint32, FClothingStreamable>& ClothStreamable : ClothingStreamables)
			{
				ClothingCategory.DataSize += ClothStreamable.Value.Size;
			}

			DefaultBucket.DataSize += ClothingCategory.DataSize;
		}
		
		// Limited number of files in each bucket. Find the ideal file distribution between categories based on the accumulated size of their resources.
		TArray<FFileCategory> Categories;

		for (FFileBucket& Bucket : FileBuckets)
		{
			uint32 NumFiles = 0;

			for (FFileCategory& Category : Bucket.Categories)
			{
				if (Category.DataSize > 0)
				{
					double DataDistribution = (double)Category.DataSize / Bucket.DataSize;
					Category.NumFiles = FMath::Max(DataDistribution * NumFilesPerBucket, 1);  // At least one file if size > 0
					Category.FirstFile = NumFiles;

					NumFiles += Category.NumFiles;
				}
			}

			Categories.Append(Bucket.Categories);
		}
		
		// Function to create the list of bulk data files. Blocks will be grouped by source Id.
		const auto CreateFileList = [Categories](const FFileCategoryID& CategoryID, const FClassifyNode& Node, TArray<FFile>& OutBulkDataFiles)
			{
				const FFileCategory* Category = Categories.FindByPredicate(
					[CategoryID](const FFileCategory& C) { return C.Id == CategoryID; });
				check(Category);

				int32 NumBulkDataFiles = OutBulkDataFiles.Num();
				OutBulkDataFiles.Reserve(NumBulkDataFiles + Category->NumFiles);

				// FileID (File Index) to BulkData file index.
				TArray<int64> BulkDataFileIndex;
				BulkDataFileIndex.Init(INDEX_NONE, Category->NumFiles);

				const int32 NumBlocks = Node.Blocks.Num();
				for (const FBlock& Block : Node.Blocks)
				{
					// Use the module of the source id to determine the file id (FileIndex)
					const uint32 FileID = Block.SourceId % Category->NumFiles;
					int64& FileIndex = BulkDataFileIndex[FileID];

					// Add new file
					if (FileIndex == INDEX_NONE)
					{
						FFile& NewFile = OutBulkDataFiles.AddDefaulted_GetRef();
						NewFile.DataType = CategoryID.DataType;
						NewFile.ResourceType = CategoryID.ResourceType;
						NewFile.Flags = CategoryID.Flags;
						NewFile.Id = FileID; 

						FileIndex = NumBulkDataFiles;
						++NumBulkDataFiles;
					}

					// Add block to the file 
					OutBulkDataFiles[FileIndex].Blocks.Add(Block);
				}
			};

		// Generate the list of BulkData files.
		GenerateBulkDataFilesList(Model, ModelStreamableBulkData, true /* bUseRomTypeAndFlagsToFilter */, CreateFileList, OutBulkDataFiles);
	}
	

	void GenerateBulkDataFilesListWithSizeLimit(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& ModelStreamableBulkData,
		const ITargetPlatform* TargetPlatform,
		uint64 TargetBulkDataFileBytes,
		TArray<FFile>& OutBulkDataFiles)
	{
		MUTABLE_CPUPROFILER_SCOPE(GenerateBulkDataFilesListWithSizeLimit);

		if (!Model)
		{
			return;
		}

		const uint64 MaxChunkSize = UCustomizableObjectSystem::GetInstance()->GetMaxChunkSizeForPlatform(TargetPlatform);
		TargetBulkDataFileBytes = FMath::Min(TargetBulkDataFileBytes, MaxChunkSize);

		// Unlimited number of files, limited file size. Add blocks to the file if the size limit won't be surpassed. Add at least one block to each file. 
		const auto CreateFileList = [TargetBulkDataFileBytes](const FFileCategoryID& CategoryID, const FClassifyNode& Node, TArray<FFile>& OutBulkDataFiles)
			{
				// Temp: Group by order in the array
				for (int32 BlockIndex = 0; BlockIndex < Node.Blocks.Num(); )
				{
					FFile File;
					File.DataType = CategoryID.DataType;
					File.ResourceType = CategoryID.ResourceType;
					File.Flags = CategoryID.Flags;

					uint64 FileSize = 0;
					uint32 FileId = uint32(CategoryID.DataType);

					while (BlockIndex < Node.Blocks.Num())
					{
						const FBlock& CurrentBlock = Node.Blocks[BlockIndex];

						if (FileSize > 0 &&
							FileSize + CurrentBlock.Size > TargetBulkDataFileBytes &&
							TargetBulkDataFileBytes > 0)
						{
							break;
						}

						// Block added to file. Set offset and increase file size.
						FileSize += CurrentBlock.Size;

						// Generate cumulative id for this file
						FileId = HashCombine(FileId, CurrentBlock.Id);

						// Add the block to the current file
						File.Blocks.Add(CurrentBlock);

						// Next block
						++BlockIndex;
					}

					const int32 NumFiles = OutBulkDataFiles.Num();

					// Ensure the FileId is unique
					bool bUnique = false;
					while (!bUnique)
					{
						bUnique = true;
						for (int32 PreviousFileIndex = 0; PreviousFileIndex < NumFiles; ++PreviousFileIndex)
						{
							if (OutBulkDataFiles[PreviousFileIndex].Id == FileId)
							{
								bUnique = false;
								++FileId;
								break;
							}
						}
					}

					// Set it to the editor-only file descriptor
					File.Id = FileId;

					OutBulkDataFiles.Add(MoveTemp(File));
				}
			};

		// TODO: Temp. Remove after unifying generated output files code between editor an package. UE-222777
		const bool bUseRomTypeAndFlagsToFilter = TargetPlatform->RequiresCookedData();

		GenerateBulkDataFilesList(Model, ModelStreamableBulkData, bUseRomTypeAndFlagsToFilter, CreateFileList, OutBulkDataFiles);
	}


	void GenerateBulkDataFilesList(
		TSharedPtr<const mu::Model, ESPMode::ThreadSafe> Model,
		FModelStreamableBulkData& ModelStreamableBulkData,
		bool bUseRomTypeAndFlagsToFilter,
		TFunctionRef<void(const FFileCategoryID&, const FClassifyNode&, TArray<FFile>&)> CreateFileList,
		TArray<FFile>& OutBulkDataFiles)
	{
		MUTABLE_CPUPROFILER_SCOPE(GenerateBulkDataFilesList);

		OutBulkDataFiles.Empty();

		if (!Model)
		{
			return;
		}

		// TODO: Temp. Remove after unifying generated output files code between editor an package. UE-222777 
		const uint16 IgnoreMask = bUseRomTypeAndFlagsToFilter ? MAX_uint16 : 0;

		// Root nodes by flags.
		const int32 NumRoms = Model->GetRomCount();
		TMap<FFileCategoryID, FClassifyNode> RootNode;

		// Create blocks data.
		{
			for (int32 RomIndex = 0; RomIndex < NumRoms; ++RomIndex)
			{
				uint32 BlockId = Model->GetRomId(RomIndex);
				uint32 SourceBlockId = Model->GetRomSourceId(RomIndex);
				const uint32 BlockSize = Model->GetRomSize(RomIndex);
				const uint16 BlockResourceType = IgnoreMask & Model->GetRomType(RomIndex);
				const mu::ERomFlags BlockFlags = (mu::ERomFlags)(IgnoreMask & (uint16)Model->GetRomFlags(RomIndex));

				FFileCategoryID CurrentCategory = { EDataType::Model, BlockResourceType, uint16(BlockFlags) };
				FBlock CurrentBlock = { BlockId, SourceBlockId, BlockSize, 0 };
				AddNode(RootNode, NumRoms, CurrentCategory, CurrentBlock);
			}
		}

		{
			uint64 SourceOffset = 0;

			const TMap<uint32, FRealTimeMorphStreamable>& RealTimeMorphStreamables = ModelStreamableBulkData.RealTimeMorphStreamables;

			const FFileCategoryID RealTimeMorphCategory = { EDataType::RealTimeMorph, (uint16)mu::DATATYPE::DT_NONE, (uint16)mu::ERomFlags::None };

			for (const TPair<uint32, FRealTimeMorphStreamable>& MorphStreamable : RealTimeMorphStreamables)
			{
				const uint32 BlockSize = MorphStreamable.Value.Size;

				FBlock CurrentBlock = { MorphStreamable.Key, MorphStreamable.Value.SourceId, BlockSize, SourceOffset };
				AddNode(RootNode, NumRoms, RealTimeMorphCategory, CurrentBlock);

				SourceOffset += BlockSize;
			}
		}

		{
			uint64 SourceOffset = 0;

			const TMap<uint32, FClothingStreamable>& ClothingStreamables = ModelStreamableBulkData.ClothingStreamables;

			const FFileCategoryID ClothingCategory = { EDataType::Clothing, (uint16)mu::DATATYPE::DT_NONE, (uint16)mu::ERomFlags::None };

			for (const TPair<uint32, FClothingStreamable>& ClothStreamable : ClothingStreamables)
			{
				const uint32 BlockSize = ClothStreamable.Value.Size;

				FBlock CurrentBlock = { ClothStreamable.Key, ClothStreamable.Value.SourceId, BlockSize, SourceOffset };
				AddNode(RootNode, NumRoms, ClothingCategory, CurrentBlock);

				SourceOffset += BlockSize;
			}
		}

		// Create Files list
		for (TPair<FFileCategoryID, FClassifyNode>& Node : RootNode)
		{
			CreateFileList(Node.Key, Node.Value, OutBulkDataFiles);
		}

		// Update streamable blocks data
		const int32 NumBulkDataFiles = OutBulkDataFiles.Num();
		for (int32 FileIndex = 0; FileIndex < NumBulkDataFiles; ++FileIndex)
		{
			FFile& File = OutBulkDataFiles[FileIndex];

			uint64 SourceOffset = 0;

			switch (File.DataType)
			{
			case EDataType::Model:
			{
				for (FBlock& Block : File.Blocks)
				{
					Block.Offset = SourceOffset;
					SourceOffset += Block.Size;

					FMutableStreamableBlock& StreamableBlock = ModelStreamableBulkData.ModelStreamables[Block.Id];
					StreamableBlock.FileId = FileIndex;
					StreamableBlock.Offset = Block.Offset;
				}
				break;
			}
			case EDataType::RealTimeMorph:
			{
				for (FBlock& Block : File.Blocks)
				{
					Block.Offset = SourceOffset;
					SourceOffset += Block.Size;

					FMutableStreamableBlock& StreamableBlock = ModelStreamableBulkData.RealTimeMorphStreamables[Block.Id].Block;
					StreamableBlock.FileId = FileIndex;
					StreamableBlock.Offset = Block.Offset;
				}
				break;
			}
			case EDataType::Clothing:
			{
				for (FBlock& Block : File.Blocks)
				{
					Block.Offset = SourceOffset;
					SourceOffset += Block.Size;

					FMutableStreamableBlock& StreamableBlock = ModelStreamableBulkData.ClothingStreamables[Block.Id].Block;
					StreamableBlock.FileId = FileIndex;
					StreamableBlock.Offset = Block.Offset;
				}
				break;
			}
			default:
				UE_LOG(LogMutable, Error, TEXT("Unknown DataType found while fixing streaming block files ids."));
				unimplemented();
				break;
			}
		}
	}

	
	void CUSTOMIZABLEOBJECT_API SerializeBulkDataFiles(
		FMutableCachedPlatformData& CachedPlatformData,
		TArray<FFile>& BulkDataFiles,
		TFunctionRef<void(FFile&, TArray64<uint8>&, uint32)> WriteFile,
		bool bDropData)
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeBulkDataFiles);

		TArray64<uint8> FileBulkData;

		const uint32 NumBulkDataFiles = BulkDataFiles.Num();
		for (uint32 FileIndex = 0; FileIndex < NumBulkDataFiles; ++FileIndex)
		{
			MutablePrivate::FFile& CurrentFile = BulkDataFiles[FileIndex];

			const int64 FileSize = CurrentFile.GetSize();
			FileBulkData.SetNumUninitialized(FileSize, EAllowShrinking::No);

			// Get the file data in memory
			CurrentFile.GetFileData(&CachedPlatformData, FileBulkData, bDropData);

			WriteFile(CurrentFile, FileBulkData, FileIndex);
		}
	}
	
	UE::DerivedData::FValueId GetDerivedDataModelId()
	{
		UE::DerivedData::FValueId::ByteArray ValueIdBytes = {};
		FMemory::Memset(&ValueIdBytes[0], 1, sizeof(ValueIdBytes));
		return UE::DerivedData::FValueId(ValueIdBytes);
	}

	UE::DerivedData::FValueId GetDerivedDataModelResourcesId()
	{
		UE::DerivedData::FValueId::ByteArray ValueIdBytes = {};		
		FMemory::Memset(&ValueIdBytes[0], 2, sizeof(ValueIdBytes));
		return UE::DerivedData::FValueId(ValueIdBytes);
	}

	UE::DerivedData::FValueId GetDerivedDataModelStreamableBulkDataId()
	{
		UE::DerivedData::FValueId::ByteArray ValueIdBytes = {};
		FMemory::Memset(&ValueIdBytes[0], 3, sizeof(ValueIdBytes));
		return UE::DerivedData::FValueId(ValueIdBytes);
	}

	UE::DerivedData::FValueId GetDerivedDataBulkDataFilesId()
	{
		UE::DerivedData::FValueId::ByteArray ValueIdBytes = {};
		FMemory::Memset(&ValueIdBytes[0], 4, sizeof(ValueIdBytes));
		return UE::DerivedData::FValueId(ValueIdBytes);
	}
}



void SerializeCompilationOptionsForDDC(FArchive& Ar, FCompilationOptions& Options)
{
	FString PlatformName = Options.TargetPlatform ? Options.TargetPlatform->PlatformName() : FString();
	Ar << PlatformName;
	Ar << Options.TextureCompression;
	Ar << Options.OptimizationLevel;
	Ar << Options.CustomizableObjectNumBoneInfluences;
	Ar << Options.bRealTimeMorphTargetsEnabled;
	Ar << Options.bClothingEnabled;
	Ar << Options.b16BitBoneWeightsEnabled;
	Ar << Options.bSkinWeightProfilesEnabled;
	Ar << Options.bPhysicsAssetMergeEnabled;
	Ar << Options.bAnimBpPhysicsManipulationEnabled;
	Ar << Options.ImageTiling;
	Ar << Options.ParamNamesToSelectedOptions;
}



TArray<uint8> UCustomizableObjectPrivate::BuildDerivedDataKey(FCompilationOptions Options)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObjectPrivate::BuildDerivedDataKey)

	check(IsInGameThread());
	
	UCustomizableObject& CustomizableObject = *GetPublic();

	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent=*/ true);
	
	{
		uint32 Version = DerivedDataVersion;
		Ar << Version;
	}
	
	{
		int32 CurrentVersion = CurrentSupportedVersion;
		Ar << CurrentVersion;	
	}

	// Custom Version
	{
		int32 CustomVersion = GetLinkerCustomVersion(FCustomizableObjectCustomVersion::GUID);
		Ar << CustomVersion;
	}
	

	// Customizable Object Ids
	{
		FGuid Id = GenerateIdentifier(CustomizableObject);
		Ar << Id;
	}

	{
		FGuid Version = CustomizableObject.VersionId;
		Ar << Version;
	}

	// Compile Options
	{
		SerializeCompilationOptionsForDDC(Ar, Options);
	}

	// Release Version
	if (const ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		FString Version = Module->GetCurrentReleaseVersionForObject(CustomizableObject);
		Ar << Version;
	}

	// Participating objects hash
	if (ICustomizableObjectEditorModule* Module = ICustomizableObjectEditorModule::Get())
	{
		TArray<TTuple<FName, FGuid>> ParticipatingObjects = Module->GetParticipatingObjects(GetPublic(), true, &Options).Array();
		ParticipatingObjects.Sort([](const TTuple<FName, FGuid>& A, const TTuple<FName, FGuid>& B)
		{
			return A.Get<0>().LexicalLess(B.Get<0>()) && A.Get<1>() < B.Get<1>();
		});
		
		for (const TTuple<FName, FGuid>& Tuple : ParticipatingObjects)
		{
			FString Key = Tuple.Get<0>().ToString();
			Key.ToLowerInline();
			Ar << Key;

			FGuid Id = Tuple.Get<1>();
			Ar << Id;
		}
	}

	// TODO List of plugins and their custom versions

	return Bytes;
}


UE::DerivedData::FCacheKey UCustomizableObjectPrivate::GetDerivedDataCacheKeyForOptions(FCompilationOptions InOptions)
{
	using namespace UE::DerivedData;

	TArray<uint8> DerivedDataKey = BuildDerivedDataKey(InOptions);

	FCacheKey CacheKey;
	CacheKey.Bucket = FCacheBucket(TEXT("CustomizableObject"));
	CacheKey.Hash = FIoHashBuilder::HashBuffer(MakeMemoryView(DerivedDataKey));
	return CacheKey;
}


void UCustomizableObjectPrivate::LoadCompiledDataFromDDC(FCompilationOptions Options, UE::DerivedData::ECachePolicy DefaultPolicy, UE::DerivedData::FCacheKey* DDCKey)
{
	MUTABLE_CPUPROFILER_SCOPE(UCustomizableObjectPrivate::LoadCompiledDataFromDDC);

	using namespace UE::DerivedData;

	/* Overview.
	*	1. Create an initial pull request to look for the compiled data in the DDC. Skip streamable binary blobs.
	*	2. Try to load the compiled data.
	*	3. (Cooking) Create a second request to pull all streamable blobs and cache the compiled data.
	*/

	FCacheKey CacheKey = DDCKey ? *DDCKey : GetDerivedDataCacheKeyForOptions(Options);
	check(CacheKey.Hash.IsZero() == false);

	// Buffers with the compiled data
	FSharedBuffer ModelBytesDDC;
	FSharedBuffer ModelResourcesBytesDDC;
	FSharedBuffer ModelStreamablesBytesDDC;
	FSharedBuffer BulkDataFilesBytesDDC;

	{	// Create a (sync) request to get the serialized Model, ModelResources, and ModelStreamable files to validate versioning and resources 
		MUTABLE_CPUPROFILER_SCOPE(CheckDDC);

		// Set the request policy to Default + SkipData to avoid pulling the streamable files until we know the compiled data can be used.
		FCacheRecordPolicyBuilder PolicyBuilder(DefaultPolicy | ECachePolicy::SkipData);

		// Overwrite the request policy for the resources we want to pull
		PolicyBuilder.AddValuePolicy(MutablePrivate::GetDerivedDataModelResourcesId(), DefaultPolicy);
		PolicyBuilder.AddValuePolicy(MutablePrivate::GetDerivedDataModelId(), DefaultPolicy);
		PolicyBuilder.AddValuePolicy(MutablePrivate::GetDerivedDataModelStreamableBulkDataId(), DefaultPolicy);
		PolicyBuilder.AddValuePolicy(MutablePrivate::GetDerivedDataBulkDataFilesId(), DefaultPolicy);

		FCacheGetRequest Request;
		Request.Name = GetPathNameSafe(GetPublic());
		Request.Key = CacheKey;
		Request.Policy = PolicyBuilder.Build();

		// Sync request to retrieve the compiled data for validation. Streamable resources are excluded.
		FRequestOwner RequestOwner(EPriority::Blocking);
		GetCache().Get(MakeArrayView(&Request, 1), RequestOwner,
			[&ModelBytesDDC, &ModelResourcesBytesDDC, &ModelStreamablesBytesDDC, &BulkDataFilesBytesDDC](FCacheGetResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					const FCompressedBuffer& ModelCompressedBuffer = Response.Record.GetValue(MutablePrivate::GetDerivedDataModelId()).GetData();
					ModelBytesDDC = ModelCompressedBuffer.Decompress();

					const FCompressedBuffer& ModelResourcesCompressedBuffer = Response.Record.GetValue(MutablePrivate::GetDerivedDataModelResourcesId()).GetData();
					ModelResourcesBytesDDC = ModelResourcesCompressedBuffer.Decompress();
					
					const FCompressedBuffer& ModelStreamablesCompressedBuffer = Response.Record.GetValue(MutablePrivate::GetDerivedDataModelStreamableBulkDataId()).GetData();
					ModelStreamablesBytesDDC = ModelStreamablesCompressedBuffer.Decompress();

					const FCompressedBuffer& BulkDataFilesCompressedBuffer = Response.Record.GetValue(MutablePrivate::GetDerivedDataBulkDataFilesId()).GetData();
					BulkDataFilesBytesDDC = BulkDataFilesCompressedBuffer.Decompress();
				}
			});
		RequestOwner.Wait();
	}

	// Check if all the requested buffers were found.
	if (!ModelBytesDDC.IsNull() && !ModelResourcesBytesDDC.IsNull() && !BulkDataFilesBytesDDC.IsNull()  && !ModelStreamablesBytesDDC.IsNull())
	{
		// Load the compiled data to validate it.
		FMemoryReaderView ModelResourcesReader(ModelResourcesBytesDDC.GetView());
		if (LoadModelResources(ModelResourcesReader, Options.TargetPlatform, Options.bIsCooking))
		{
			FModelResources& LocalModelResources = GetModelResources(Options.bIsCooking);
			LocalModelResources.bIsStoredInDDC = true;
			LocalModelResources.DDCKey = CacheKey;
			LocalModelResources.DDCDefaultPolicy = ECachePolicy::Default;

			FMemoryReaderView ModelStreamablesReader(ModelStreamablesBytesDDC.GetView());
			LoadModelStreamableBulk(ModelStreamablesReader, Options.bIsCooking);

			FMemoryReaderView ModelReader(ModelBytesDDC.GetView());
			LoadModel(ModelReader);
		}

		TSharedPtr<mu::Model> Model = GetModel();
		TSharedPtr<FModelStreamableBulkData> ModelStreamables = GetModelStreamableBulkData(Options.bIsCooking);

		// Cache CookedPlatfomData. 
		if (Options.bIsCooking && Model && ModelStreamables)
		{
			// Sync cache cooked platform data
			// TODO UE-220138: Sync -> Async
			MUTABLE_CPUPROFILER_SCOPE(CacheCookedPlatformData);

			MutablePrivate::FMutableCachedPlatformData CachedData;
			
			// Cache Model, ModelResources and ModelStreamables
			CachedData.ModelData.Append(reinterpret_cast<const uint8*>(ModelBytesDDC.GetData()), ModelBytesDDC.GetSize());
			CachedData.ModelResourcesData.Append(reinterpret_cast<const uint8*>(ModelResourcesBytesDDC.GetData()), ModelResourcesBytesDDC.GetSize());
			CachedData.ModelStreamables = ModelStreamables;

			// Value Id to file mapping to reconstruct the cached data
			TMap<FValueId, MutablePrivate::FFile> ValueIdToFile;

			{
				MUTABLE_CPUPROFILER_SCOPE(BuildValueIdToFile);
				TArray<MutablePrivate::FFile> BulkDataFiles;
				FMemoryReaderView FilesReader(BulkDataFilesBytesDDC.GetView());
				FilesReader << BulkDataFiles;

				ValueIdToFile.Reserve(BulkDataFiles.Num());

				FValueId::ByteArray ValueIdBytes = {};
				for (MutablePrivate::FFile& File : BulkDataFiles)
				{
					int8 ValueIdOffset = 0;
					FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.DataType, sizeof(File.DataType));
					ValueIdOffset += sizeof(File.DataType);
					FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.Id, sizeof(File.Id));
					ValueIdOffset += sizeof(File.Id);
					FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.ResourceType, sizeof(File.ResourceType));
					ValueIdOffset += sizeof(File.ResourceType);
					FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.Flags, sizeof(File.Flags));

					MutablePrivate::FFile& DestFile = ValueIdToFile.Add(FValueId(ValueIdBytes));
					DestFile = MoveTemp(File);
				}
				BulkDataFiles.Empty();
			}

			// Create a new pull request to retrieve all compiled data. Streamable bulk data included
			FCacheGetRequest Request;
			Request.Name = GetPathNameSafe(GetPublic());
			Request.Key = CacheKey;
			Request.Policy = ECachePolicy::Default;

			FRequestOwner RequestOwner(EPriority::Blocking);
			GetCache().Get(MakeArrayView(&Request, 1), RequestOwner,
				[&CachedData, &ValueIdToFile, ModelStreamables](FCacheGetResponse&& Response)
				{
					MUTABLE_CPUPROFILER_SCOPE(CacheBulkDataFromDDC);

					if (ensure(Response.Status == EStatus::Ok))
					{
						// Get all values and convert them to FMutableCachedPlatformData's format
						TConstArrayView<FValueWithId> Values = Response.Record.GetValues();

						TArray64<uint8> TempData;
						for (const FValueWithId& Value : Values)
						{
							check(Value.IsValid());

							const MutablePrivate::FFile* File = ValueIdToFile.Find(Value.GetId());
							if (!File) // Skip value. It is not a streamable binary blob.
							{
								continue;
							}

							const uint64 RawSize = Value.GetRawSize();
							TempData.SetNumUninitialized(RawSize, EAllowShrinking::No);

							// Decompress streamable binary blobs
							const bool bDecompressedSuccessfully = Value.GetData().TryDecompressTo(MakeMemoryView(TempData.GetData(), RawSize));
							check(bDecompressedSuccessfully);

							// Filter and cache the data by DataType
							switch (File->DataType)
							{
							case MutablePrivate::EDataType::Model:
							{
								for (const MutablePrivate::FBlock& Block : File->Blocks)
								{
									CachedData.ModelStreamableData.Set(Block.Id, TempData.GetData() + Block.Offset, Block.Size);
								}
								break;
							}
							case MutablePrivate::EDataType::RealTimeMorph:
							{
								// Store the MorphData in a single array.
								uint64 Offset = CachedData.MorphData.Num();
								CachedData.MorphData.Append(TempData.GetData(), RawSize);

								// Fix offsets
								for (const MutablePrivate::FBlock& Block : File->Blocks)
								{
									ModelStreamables->RealTimeMorphStreamables[Block.Id].Block.Offset = Offset;
									Offset += Block.Size;
								}
								break;
							}
							case MutablePrivate::EDataType::Clothing:
							{
								// Store the ClothingData in a single array.
								uint64 Offset = CachedData.ClothingData.Num();
								CachedData.ClothingData.Append(TempData.GetData(), RawSize);

								// Fix offsets
								for (const MutablePrivate::FBlock& Block : File->Blocks)
								{
									ModelStreamables->ClothingStreamables[Block.Id].Block.Offset = Offset;
									Offset += Block.Size;
								}
								break;
							}

							default:
								unimplemented();
								break;
							}
						}
					}
				});
			RequestOwner.Wait();

			// Generate list of files and update streamable blocks ids and offsets
			if (CVarMutableUseBulkData.GetValueOnAnyThread())
			{
				const uint32 NumBulkDataFilesPerBucket = MAX_uint8;
				MutablePrivate::GenerateBulkDataFilesListWithFileLimit(Model, *ModelStreamables.Get(), NumBulkDataFilesPerBucket, CachedData.BulkDataFiles);
			}
			else
			{
				MutablePrivate::GenerateBulkDataFilesListWithSizeLimit(Model, *ModelStreamables.Get(), Options.TargetPlatform, Options.PackagedDataBytesLimit, CachedData.BulkDataFiles);
			}

			MutablePrivate::FMutableCachedPlatformData& CachedPlatformData = CachedPlatformsData.Add(Options.TargetPlatform->PlatformName(), {});
			CachedPlatformData = MoveTemp(CachedData);
		}
	}

	return;
}


#endif // WITH_EDITOR

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void UCustomizableObjectBulk::PostLoad()
{
	UObject::PostLoad();

	const FString OutermostName = GetOutermost()->GetName();
	FString PackageFilename = FPackageName::LongPackageNameToFilename(OutermostName);
	FPaths::MakeStandardFilename(PackageFilename);
	BulkFilePrefix = PackageFilename;
}

TUniquePtr<IAsyncReadFileHandle> UCustomizableObjectBulk::OpenFileAsyncRead(uint32 FileId, uint32 Flags) const
{
	check(IsInGameThread());

	FString FilePath = FString::Printf(TEXT("%s-%08x.mut"), *BulkFilePrefix, FileId);
	if (Flags == uint32(mu::ERomFlags::HighRes))
	{
		FilePath += TEXT(".high");
	}

	IAsyncReadFileHandle* Result = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*FilePath);
	
	// Result being null does not mean the file does not exist. A request has to be made. Let the callee deal with it.
	//UE_CLOG(!Result, LogMutable, Warning, TEXT("CustomizableObjectBulkData: Failed to open file [%s]."), *FilePath);

	return TUniquePtr<IAsyncReadFileHandle>(Result);
}

#if WITH_EDITOR

void UCustomizableObjectBulk::CookAdditionalFilesOverride(const TCHAR* PackageFilename,
	const ITargetPlatform* TargetPlatform,
	TFunctionRef<void(const TCHAR* Filename, void* Data, int64 Size)> WriteAdditionalFile)
{
	// Don't save streamed data on server builds since it won't be used anyway.
	if (TargetPlatform->IsServerOnly())
	{
		return;
	}

	UCustomizableObject* CustomizableObject = CastChecked<UCustomizableObject>(GetOutermostObject());

	MutablePrivate::FMutableCachedPlatformData* PlatformData = CustomizableObject->GetPrivate()->CachedPlatformsData.Find(TargetPlatform->PlatformName());
	check(PlatformData);

	const FString CookedBulkFileName = FString::Printf(TEXT("%s/%s"), *FPaths::GetPath(PackageFilename), *CustomizableObject->GetName());

	const auto WriteFile = [WriteAdditionalFile, CookedBulkFileName](MutablePrivate::FFile& File, TArray64<uint8>& FileBulkData, uint32 FileIndex)
		{
			FString FileName = CookedBulkFileName + FString::Printf(TEXT("-%08x.mut"), File.Id);

			if (File.Flags == uint32(mu::ERomFlags::HighRes))
			{
				// We can do something different here for high-res data.
				// For example: change the file name. We also need to detect it when generating the file name for loading.
				FileName += TEXT(".high");
			}

			WriteAdditionalFile(*FileName, FileBulkData.GetData(), FileBulkData.Num());
		};

	bool bDropData = true;
	MutablePrivate::SerializeBulkDataFiles(*PlatformData, PlatformData->BulkDataFiles, WriteFile, bDropData);
}
#endif // WITH_EDITOR


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
bool FAnimBpOverridePhysicsAssetsInfo::operator==(const FAnimBpOverridePhysicsAssetsInfo& Rhs) const
{
	return AnimInstanceClass == Rhs.AnimInstanceClass &&
		SourceAsset == Rhs.SourceAsset &&
		PropertyIndex == Rhs.PropertyIndex;
}


bool FMutableModelImageProperties::operator!=(const FMutableModelImageProperties& Other) const
{
	return
		TextureParameterName != Other.TextureParameterName ||
		Filter != Other.Filter ||
		SRGB != Other.SRGB ||
		FlipGreenChannel != Other.FlipGreenChannel ||
		IsPassThrough != Other.IsPassThrough ||
		LODBias != Other.LODBias ||
		MipGenSettings != Other.MipGenSettings ||
		LODGroup != Other.LODGroup ||
		AddressX != Other.AddressX ||
		AddressY != Other.AddressY;
}


bool FMutableRefSocket::operator==(const FMutableRefSocket& Other) const
{
	if (
		SocketName == Other.SocketName &&
		BoneName == Other.BoneName &&
		RelativeLocation == Other.RelativeLocation &&
		RelativeRotation == Other.RelativeRotation &&
		RelativeScale == Other.RelativeScale &&
		bForceAlwaysAnimated == Other.bForceAlwaysAnimated &&
		Priority == Other.Priority)
	{
		return true;
	}

	return false;
}


bool FMutableSkinWeightProfileInfo::operator==(const FMutableSkinWeightProfileInfo& Other) const
{
	return Name == Other.Name;
}


FIntegerParameterUIData::FIntegerParameterUIData(const FMutableParamUIMetadata& InParamUIMetadata)
{
	ParamUIMetadata = InParamUIMetadata;
}


FMutableParameterData::FMutableParameterData(const FMutableParamUIMetadata& InParamUIMetadata, EMutableParameterType InType)
{
	ParamUIMetadata = InParamUIMetadata;
	Type = InType;
}


#if WITH_EDITORONLY_DATA
FArchive& operator<<(FArchive& Ar, FMutableRemappedBone& RemappedBone)
{
	Ar << RemappedBone.Name;
	Ar << RemappedBone.Hash;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FMutableModelImageProperties& ImageProps)
{
	Ar << ImageProps.TextureParameterName;
	Ar << ImageProps.Filter;

	// Bitfields don't serialize automatically with FArchive
	if (Ar.IsLoading())
	{
		int32 Aux = 0;
		Ar << Aux;
		ImageProps.SRGB = Aux;

		Aux = 0;
		Ar << Aux;
		ImageProps.FlipGreenChannel = Aux;

		Aux = 0;
		Ar << Aux;
		ImageProps.IsPassThrough = Aux;
	}
	else
	{
		int32 Aux = ImageProps.SRGB;
		Ar << Aux;

		Aux = ImageProps.FlipGreenChannel;
		Ar << Aux;

		Aux = ImageProps.IsPassThrough;
		Ar << Aux;
	}

	Ar << ImageProps.LODBias;
	Ar << ImageProps.MipGenSettings;
	Ar << ImageProps.LODGroup;

	Ar << ImageProps.AddressX;
	Ar << ImageProps.AddressY;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FAnimBpOverridePhysicsAssetsInfo& Info)
{
	FString AnimInstanceClassPathString;
	FString PhysicsAssetPathString;

	if (Ar.IsLoading())
	{
		Ar << AnimInstanceClassPathString;
		Ar << PhysicsAssetPathString;
		Ar << Info.PropertyIndex;

		Info.AnimInstanceClass = TSoftClassPtr<UAnimInstance>(AnimInstanceClassPathString);
		Info.SourceAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(PhysicsAssetPathString));
	}

	if (Ar.IsSaving())
	{
		AnimInstanceClassPathString = Info.AnimInstanceClass.ToString();
		PhysicsAssetPathString = Info.SourceAsset.ToString();

		Ar << AnimInstanceClassPathString;
		Ar << PhysicsAssetPathString;
		Ar << Info.PropertyIndex;
	}

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefSocket& Data)
{
	Ar << Data.SocketName;
	Ar << Data.BoneName;
	Ar << Data.RelativeLocation;
	Ar << Data.RelativeRotation;
	Ar << Data.RelativeScale;
	Ar << Data.bForceAlwaysAnimated;
	Ar << Data.Priority;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefLODRenderData& Data)
{
	Ar << Data.bIsLODOptional;
	Ar << Data.bStreamedDataInlined;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefLODInfo& Data)
{
	Ar << Data.ScreenSize;
	Ar << Data.LODHysteresis;
	Ar << Data.bSupportUniformlyDistributedSampling;
	Ar << Data.bAllowCPUAccess;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefLODData& Data)
{
	Ar << Data.LODInfo;
	Ar << Data.RenderData;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefSkeletalMeshSettings& Data)
{
	Ar << Data.bEnablePerPolyCollision;
	Ar << Data.DefaultUVChannelDensity;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableRefAssetUserData& Data)
{
	Ar << Data.AssetUserDataIndex;

	return Ar;
}


FArchive& operator<<(FArchive& Ar, FMutableSkinWeightProfileInfo& Info)
{
	Ar << Info.Name;
	Ar << Info.NameId;
	Ar << Info.DefaultProfile;
	Ar << Info.DefaultProfileFromLODIndex;

	return Ar;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void FMutableRefSkeletalMeshData::InitResources(UCustomizableObject* InOuter, FModelResources& InModelResources, const ITargetPlatform* InTargetPlatform)
{
	check(InOuter);

	const bool bHasServer = InTargetPlatform ? !InTargetPlatform->IsClientOnly() : false;
	if (InOuter->bEnableUseRefSkeletalMeshAsPlaceholder || bHasServer)
	{
		SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(SoftSkeletalMesh).LoadSynchronous();
	}

	// Initialize AssetUserData
	for (FMutableRefAssetUserData& Data : AssetUserData)
	{
		if (!InModelResources.StreamedResourceData.IsValidIndex(Data.AssetUserDataIndex))
		{
			check(false);
			continue;
		}

		Data.AssetUserData = InModelResources.StreamedResourceData[Data.AssetUserDataIndex].GetPath().LoadSynchronous();
		check(Data.AssetUserData);
		check(Data.AssetUserData->Data.Type == ECOResourceDataType::AssetUserData);
	}
}


FArchive& operator<<(FArchive& Ar, FMutableRefSkeletalMeshData& Data)
{
	Ar << Data.LODData;
	Ar << Data.Sockets;
	Ar << Data.Bounds;
	Ar << Data.Settings;

	if (Ar.IsSaving())
	{
		FString AssetPath = Data.SoftSkeletalMesh.ToString();
		Ar << AssetPath;

		AssetPath = TSoftObjectPtr<USkeletalMeshLODSettings>(Data.SkeletalMeshLODSettings).ToString();
		Ar << AssetPath;

		AssetPath = TSoftObjectPtr<USkeleton>(Data.Skeleton).ToString();
		Ar << AssetPath;

		AssetPath = TSoftObjectPtr<UPhysicsAsset>(Data.PhysicsAsset).ToString();
		Ar << AssetPath;

		AssetPath = Data.PostProcessAnimInst.ToString();
		Ar << AssetPath;

		AssetPath = TSoftObjectPtr<UPhysicsAsset>(Data.ShadowPhysicsAsset).ToString();
		Ar << AssetPath;

	}
	else
	{
		FString SkeletalMeshAssetPath;
		Ar << SkeletalMeshAssetPath;
		Data.SoftSkeletalMesh = SkeletalMeshAssetPath;

		FString SkeletalMeshLODSettingsAssetPath;
		Ar << SkeletalMeshLODSettingsAssetPath;
		Data.SkeletalMeshLODSettings = TSoftObjectPtr<USkeletalMeshLODSettings>(FSoftObjectPath(SkeletalMeshLODSettingsAssetPath)).LoadSynchronous();

		FString SkeletonAssetPath;
		Ar << SkeletonAssetPath;
		Data.Skeleton = TSoftObjectPtr<USkeleton>(FSoftObjectPath(SkeletonAssetPath)).LoadSynchronous();

		FString PhysicsAssetPath;
		Ar << PhysicsAssetPath;
		Data.PhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(PhysicsAssetPath)).LoadSynchronous();

		FString PostProcessAnimInstAssetPath;
		Ar << PostProcessAnimInstAssetPath;
		Data.PostProcessAnimInst = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(PostProcessAnimInstAssetPath)).LoadSynchronous();

		FString ShadowPhysicsAssetPath;
		Ar << ShadowPhysicsAssetPath;
		Data.ShadowPhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(ShadowPhysicsAssetPath)).LoadSynchronous();
	}

	Ar << Data.AssetUserData;

	return Ar;
}
#endif // WITH_EDITORONLY_DATA


#if WITH_EDITOR
FCompilationRequest::FCompilationRequest(UCustomizableObject& InCustomizableObject, bool bAsyncCompile)
{
	CustomizableObject = &InCustomizableObject;
	Options = InCustomizableObject.GetPrivate()->GetCompileOptions();
	bAsync = bAsyncCompile;
	DDCPolicy = UE::DerivedData::ECachePolicy::None;
}


UCustomizableObject* FCompilationRequest::GetCustomizableObject()
{
	return CustomizableObject.Get();
}


FCompilationOptions& FCompilationRequest::GetCompileOptions()
{
	return Options;
}


bool FCompilationRequest::IsAsyncCompilation() const
{
	return bAsync;
}


void FCompilationRequest::SetDerivedDataCachePolicy(UE::DerivedData::ECachePolicy InCachePolicy)
{
	DDCPolicy = InCachePolicy;
	Options.bQueryCompiledDatafromDDC = EnumHasAnyFlags(InCachePolicy, UE::DerivedData::ECachePolicy::Query);
	Options.bStoreCompiledDataInDDC = EnumHasAnyFlags(InCachePolicy, UE::DerivedData::ECachePolicy::Store);
}


UE::DerivedData::ECachePolicy FCompilationRequest::GetDerivedDataCachePolicy() const
{
	return DDCPolicy;
}

void FCompilationRequest::BuildDerivedDataCacheKey()
{
	if (UCustomizableObject* Object = CustomizableObject.Get())
	{
		DDCKey = Object->GetPrivate()->GetDerivedDataCacheKeyForOptions(Options);
	}
}


UE::DerivedData::FCacheKey FCompilationRequest::GetDerivedDataCacheKey() const
{
	return DDCKey;
}


void FCompilationRequest::SetCompilationState(ECompilationStatePrivate InState, ECompilationResultPrivate InResult)
{
	State = InState;
	Result = InResult;
}


ECompilationStatePrivate FCompilationRequest::GetCompilationState() const
{
	return State;
}


ECompilationResultPrivate FCompilationRequest::GetCompilationResult() const
{
	return Result;
}


TArray<FText>& FCompilationRequest::GetWarnings()
{
	return Warnings;
}


TArray<FText>& FCompilationRequest::GetErrors()
{
	return Errors;
}


bool FCompilationRequest::operator==(const FCompilationRequest& Other) const
{
	return CustomizableObject == Other.CustomizableObject && Options.TargetPlatform == Other.Options.TargetPlatform;
}
#endif


FArchive& operator<<(FArchive& Ar, FMutableParamNameSet& MutableParamNameSet)
{
	Ar << MutableParamNameSet.ParamNames;

	return Ar;
}


#undef LOCTEXT_NAMESPACE
