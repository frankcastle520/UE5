// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/HLODProxy.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "HLOD/HLODBatchingPolicy.h"
#include "ISMPartition/ISMComponentBatcher.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"
#include "WorldPartition/HLOD/HLODInstancedStaticMeshComponent.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HLODBuilder)


DEFINE_LOG_CATEGORY(LogHLODBuilder);


UHLODBuilder::UHLODBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, HLODInstancedStaticMeshComponentClass(UHLODInstancedStaticMeshComponent::StaticClass())
#endif
{
}

UNullHLODBuilder::UNullHLODBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderSettings::UHLODBuilderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

TSubclassOf<UHLODBuilderSettings> UHLODBuilder::GetSettingsClass() const
{
	return UHLODBuilderSettings::StaticClass();
}

void UHLODBuilder::SetHLODBuilderSettings(const UHLODBuilderSettings* InHLODBuilderSettings)
{
	check(InHLODBuilderSettings->IsA(GetSettingsClass()));
	HLODBuilderSettings = InHLODBuilderSettings;
}

bool UHLODBuilder::RequiresWarmup() const
{
	return true;
}

uint32 UHLODBuilder::ComputeHLODHash(const UActorComponent* InSourceComponent) const
{
	uint32 ComponentCRC = 0;

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(const_cast<UActorComponent*>(InSourceComponent)))
	{
		UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - Component \'%s\' from actor \'%s\'"), *StaticMeshComponent->GetName(), *StaticMeshComponent->GetOwner()->GetName());

		// CRC component
		ComponentCRC = UHLODProxy::GetCRC(StaticMeshComponent);
		UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Static Mesh Component (%s) = %x"), *StaticMeshComponent->GetName(), ComponentCRC);

		// CRC static mesh
		if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			uint32 StaticMeshCRC = UHLODProxy::GetCRC(StaticMesh);
			UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Static Mesh (%s) = %x"), *StaticMesh->GetName(), StaticMeshCRC);
			ComponentCRC = HashCombineFast(ComponentCRC, StaticMeshCRC);
		}

		// CRC materials
		const int32 NumMaterials = StaticMeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			UMaterialInterface* MaterialInterface = StaticMeshComponent->GetMaterial(MaterialIndex);
			if (MaterialInterface)
			{
				uint32 MaterialInterfaceCRC = UHLODProxy::GetCRC(MaterialInterface);
				UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Material (%s) = %x"), *MaterialInterface->GetName(), MaterialInterfaceCRC);
				ComponentCRC = HashCombineFast(ComponentCRC, MaterialInterfaceCRC);

				TArray<UTexture*> Textures;
				MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
				for (UTexture* Texture : Textures)
				{
					uint32 TextureCRC = UHLODProxy::GetCRC(Texture);
					UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Texture (%s) = %x"), *Texture->GetName(), TextureCRC);
					ComponentCRC = HashCombineFast(ComponentCRC, TextureCRC);
				}
			}
			UMaterialInterface* NaniteOverride = MaterialInterface ? MaterialInterface->GetNaniteOverride() : nullptr;
			if (NaniteOverride)
			{
				uint32 NaniteOverrideCRC = UHLODProxy::GetCRC(NaniteOverride);
				UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Nanite Override Material (%s) = %x"), *NaniteOverride->GetName(), NaniteOverrideCRC);
				ComponentCRC = HashCombineFast(ComponentCRC, NaniteOverrideCRC);

				TArray<UTexture*> Textures;
				NaniteOverride->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
				for (UTexture* Texture : Textures)
				{
					uint32 TextureCRC = UHLODProxy::GetCRC(Texture);
					UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - Nanite Override Texture (%s) = %x"), *Texture->GetName(), TextureCRC);
					ComponentCRC = HashCombineFast(ComponentCRC, TextureCRC);
				}
			}
		}
	}
	else
	{
		ComponentCRC = FMath::Rand();
		UE_LOG(LogHLODBuilder, Warning, TEXT("Can't compute HLOD hash for component of type %s, assuming it is dirty."), *InSourceComponent->GetClass()->GetName());
		
	}

	return ComponentCRC;
}

uint32 UHLODBuilder::ComputeHLODHash(const TArray<UActorComponent*>& InSourceComponents)
{
	// We get the CRC of each component
	TArray<uint32> ComponentsCRCs;

	for (UActorComponent* SourceComponent : InSourceComponents)
	{
		TSubclassOf<UHLODBuilder> HLODBuilderClass = SourceComponent->GetCustomHLODBuilderClass();
		if (!HLODBuilderClass)
		{
			HLODBuilderClass = UHLODBuilder::StaticClass();
		}

		uint32 ComponentHash = HLODBuilderClass->GetDefaultObject<UHLODBuilder>()->ComputeHLODHash(SourceComponent);
		ComponentsCRCs.Add(ComponentHash);
	}

	// Sort the components CRCs to ensure the order of components won't have an impact on the final CRC
	ComponentsCRCs.Sort();

	return FCrc::MemCrc32(ComponentsCRCs.GetData(), ComponentsCRCs.Num() * ComponentsCRCs.GetTypeSize());
}

TSubclassOf<UHLODInstancedStaticMeshComponent> UHLODBuilder::GetInstancedStaticMeshComponentClass()
{
	TSubclassOf<UHLODInstancedStaticMeshComponent> ISMClass = StaticClass()->GetDefaultObject<UHLODBuilder>()->HLODInstancedStaticMeshComponentClass;
	if (!ISMClass)
	{
		FString ConfigValue;
		GConfig->GetString(TEXT("/Script/Engine.HLODBuilder"), TEXT("HLODInstancedStaticMeshComponentClass"), ConfigValue, GEditorIni);
		UE_LOG(LogHLODBuilder, Error, TEXT("Could not resolve the class specified for HLODInstancedStaticMeshComponentClass. Config value was %s"), *ConfigValue);

		// Fallback to standard HLOD ISMC
		ISMClass = UHLODInstancedStaticMeshComponent::StaticClass();
	}
	return ISMClass;
}

namespace
{
	struct FInstanceBatch
	{
		TUniquePtr<FISMComponentDescriptor>		ISMComponentDescriptor;
		FISMComponentBatcher					ISMComponentBatcher;
	};
}

TArray<UActorComponent*> UHLODBuilder::BatchInstances(const TArray<UActorComponent*>& InSourceComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderInstancing::Build);

	TArray<UStaticMeshComponent*> SourceStaticMeshComponents = FilterComponents<UStaticMeshComponent>(InSourceComponents);

	TSubclassOf<UHLODInstancedStaticMeshComponent> ComponentClass = UHLODBuilder::GetInstancedStaticMeshComponentClass();

	// Prepare instance batches
	TMap<uint32, FInstanceBatch> InstancesData;
	for (UStaticMeshComponent* SMC : SourceStaticMeshComponents)
	{
		const UStaticMesh* StaticMesh = SMC->GetStaticMesh();
		const bool bPrivateStaticMesh = StaticMesh && !StaticMesh->HasAnyFlags(RF_Public);
		const bool bTransientStaticMesh = StaticMesh && StaticMesh->HasAnyFlags(RF_Transient);
		if (!StaticMesh || bPrivateStaticMesh || bTransientStaticMesh)
		{
			UE_LOG(LogHLODBuilder, Warning, TEXT("Instanced HLOD source component %s points to a %s static mesh, ignoring."), *SMC->GetPathName(), !StaticMesh ? TEXT("null") : bPrivateStaticMesh ? TEXT("private") : TEXT("transient"));
			continue;
		}

		TUniquePtr<FISMComponentDescriptor> ISMComponentDescriptor = ComponentClass->GetDefaultObject<UHLODInstancedStaticMeshComponent>()->AllocateISMComponentDescriptor();
		ISMComponentDescriptor->InitFrom(SMC, false);

		FInstanceBatch& InstanceBatch = InstancesData.FindOrAdd(ISMComponentDescriptor->GetTypeHash());
		if (!InstanceBatch.ISMComponentDescriptor.IsValid())
		{
			InstanceBatch.ISMComponentDescriptor = MoveTemp(ISMComponentDescriptor);
		}
		
		InstanceBatch.ISMComponentBatcher.Add(SMC);
	}

	// Create an ISMC for each SM asset we found
	TArray<UActorComponent*> HLODComponents;
	for (auto& Entry : InstancesData)
	{
		const FInstanceBatch& InstanceBatch = Entry.Value;

		UInstancedStaticMeshComponent* ISMComponent = InstanceBatch.ISMComponentDescriptor->CreateComponent(GetTransientPackage());
		InstanceBatch.ISMComponentBatcher.InitComponent(ISMComponent);
				
		HLODComponents.Add(ISMComponent);
	};

	return HLODComponents;
}

static bool ShouldBatchComponent(UActorComponent* ActorComponent)
{
	bool bShouldBatch = false;

	if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(ActorComponent))
	{
		switch (PrimitiveComponent->HLODBatchingPolicy)
		{
		case EHLODBatchingPolicy::None:
			break;
		case EHLODBatchingPolicy::Instancing:
			bShouldBatch = true;
			break;
		case EHLODBatchingPolicy::MeshSection:
		{
			bShouldBatch = true;
			FString LogDetails = FString::Printf(TEXT("%s %s (from actor %s)"), *PrimitiveComponent->GetClass()->GetName(), *ActorComponent->GetName(), *ActorComponent->GetOwner()->GetActorLabel());
			if (UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
			{
				LogDetails += FString::Printf(TEXT(" using static mesh %s"), SMComponent->GetStaticMesh() ? *SMComponent->GetStaticMesh()->GetName() : TEXT("<null>"));
			}
			UE_LOG(LogHLODBuilder, Display, TEXT("EHLODBatchingPolicy::MeshSection is not yet supported by the HLOD builder, falling back to EHLODBatchingPolicy::Instancing for %s."), *LogDetails);
			break;
		}
		default:
			checkNoEntry();
		}
	}

	return bShouldBatch;
}

FHLODBuildResult UHLODBuilder::Build(const FHLODBuildContext& InHLODBuildContext) const
{
	// Handle components using a batching policy separately
	TArray<UActorComponent*> InputComponents;
	TArray<UActorComponent*> ComponentsToBatch;

	if (!ShouldIgnoreBatchingPolicy())
	{				
		for (UActorComponent* SourceComponent : InHLODBuildContext.SourceComponents)
		{
			if (ShouldBatchComponent(SourceComponent))
			{
				ComponentsToBatch.Add(SourceComponent);
			}
			else
			{
				InputComponents.Add(SourceComponent);
			}
		}
	}
	else
	{
		InputComponents = InHLODBuildContext.SourceComponents;
	}

	TMap<TSubclassOf<UHLODBuilder>, TArray<UActorComponent*>> HLODBuildersForComponents;

	// Gather custom HLOD builders, and regroup all components by builders
	for (UActorComponent* SourceComponent : InputComponents)
	{
		TSubclassOf<UHLODBuilder> HLODBuilderClass = SourceComponent->GetCustomHLODBuilderClass();
		HLODBuildersForComponents.FindOrAdd(HLODBuilderClass).Add(SourceComponent);
	}
	
	FHLODBuildResult BuildResult;

	auto AddReferencedAssetsToStats = [&BuildResult](FName HLODBuilderClassName, TArray<UActorComponent*> InSourceComponents)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		const FTopLevelAssetPath StaticMeshAssetClassPath(UStaticMesh::StaticClass());

		FHLODBuildInputReferencedAssets& ReferencedAssetsStats = BuildResult.InputStats.BuildersReferencedAssets.FindOrAdd(HLODBuilderClassName);

		for (UActorComponent* SourceComponent : InSourceComponents)
		{
			// At the moment we only care about static meshes for our stats
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SourceComponent))
			{
				FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(StaticMeshComponent->GetStaticMesh()));
				if (AssetData.IsUAsset())
				{					
					if (AssetData.AssetClassPath == StaticMeshAssetClassPath)
					{
						FTopLevelAssetPath StaticMeshAssetPath(AssetData.PackageName, AssetData.AssetName);
						ReferencedAssetsStats.StaticMeshes.FindOrAdd(StaticMeshAssetPath)++;
					}
				}
			}
		}	
	};
	
	// Build HLOD components by sending source components to the individual builders, in batch
	TArray<UActorComponent*> HLODComponents;
	for (const auto& HLODBuilderPair : HLODBuildersForComponents)
	{
		// If no custom HLOD builder is provided, use this current builder.
		const UHLODBuilder* HLODBuilder = HLODBuilderPair.Key ? HLODBuilderPair.Key->GetDefaultObject<UHLODBuilder>() : this;
		const TArray<UActorComponent*>& SourceComponents = HLODBuilderPair.Value;

		AddReferencedAssetsToStats(HLODBuilder->GetClass()->GetFName(), SourceComponents);

		TArray<UActorComponent*> NewComponents = HLODBuilder->Build(InHLODBuildContext, SourceComponents);
		BuildResult.HLODComponents.Append(NewComponents);
	}

	// Append batched components
	if (!ComponentsToBatch.IsEmpty())
	{
		FName HLODBuilderInstancingClassName("HLODBuilderInstancing");
		AddReferencedAssetsToStats(HLODBuilderInstancingClassName, ComponentsToBatch);
		BuildResult.HLODComponents.Append(BatchInstances(ComponentsToBatch));
	}

	// In case a builder returned null entries, clean the array.
	BuildResult.HLODComponents.RemoveSwap(nullptr);

	return BuildResult;
}

#endif // WITH_EDITOR

