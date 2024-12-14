// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModule.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Graph/RigUnit_AnimNextShimRoot.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"
#include "TraitCore/ExecutionContext.h"
#include "Graph/AnimNext_LODPose.h"
#include "Serialization/MemoryReader.h"
#include "Graph/AnimNextGraphEntryPoint.h"
#include "Graph/RigUnit_AnimNextBeginExecution.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectResource.h"

#if WITH_EDITOR	
#include "Engine/ExternalAssetDependencyGatherer.h"
REGISTER_ASSETDEPENDENCY_GATHERER(FExternalAssetDependencyGatherer, UAnimNextModule);
#endif // WITH_EDITOR

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextModule)

#if WITH_EDITORONLY_DATA
static UAnimNextModule::FOnModuleCompiled GOnModuleCompiled; 
#endif

UAnimNextModule::UAnimNextModule(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ExtendedExecuteContext.SetContextPublicDataStruct(FAnimNextExecuteContext::StaticStruct());
}

void UAnimNextModule::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if(Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextModuleRefactor)
		{
			// Skip over shared archive buffer if we are loading from an older version
			if (const FLinkerLoad* Linker = GetLinker())
			{
				const int32 LinkerIndex = GetLinkerIndex();
				const FObjectExport& Export = Linker->ExportMap[LinkerIndex];
				Ar.Seek(Export.SerialOffset + Export.SerialSize);
			}
		}
	}
#endif
}

void UAnimNextModule::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if(GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextGraphAccessSpecifiers)
	{
		DefaultState_DEPRECATED.State = PropertyBag_DEPRECATED;
	}
#endif
}

#if WITH_EDITOR

UAnimNextModule::FOnModuleCompiled& UAnimNextModule::OnModuleCompiled()
{
	return GOnModuleCompiled;
}

#endif