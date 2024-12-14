// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMBuildData.h"
#include "RigVMBlueprint.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "UObject/UObjectIterator.h"
#include "RigVMModel/RigVMClient.h"
#include "UObject/StrongObjectPtr.h"
#include "RigVMBlueprint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMBuildData)

static const FLazyName FunctionReferenceNodeDataName = TEXT("FunctionReferenceNodeData");

// When the object system has been completely loaded, collect all the references between RigVM graphs 
static FDelayedAutoRegisterHelper GRigVMBuildDataSingletonHelper(EDelayedRegisterRunPhase::EndOfEngineInit, []() -> void
{
	URigVMBuildData::Get()->InitializeIfNeeded();
});

FRigVMReferenceNodeData::FRigVMReferenceNodeData(URigVMFunctionReferenceNode* InReferenceNode)
{
	check(InReferenceNode);
	ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(InReferenceNode);
	ReferenceNodePath = ReferenceNodePtr.ToString();
	ReferencedFunctionIdentifier = InReferenceNode->GetReferencedFunctionHeader().LibraryPointer;
}

TSoftObjectPtr<URigVMFunctionReferenceNode> FRigVMReferenceNodeData::GetReferenceNodeObjectPath()
{
	if(ReferenceNodePtr.IsNull())
	{
		ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(ReferenceNodePath));
	}
	return ReferenceNodePtr;
}

URigVMFunctionReferenceNode* FRigVMReferenceNodeData::GetReferenceNode()
{
	if(ReferenceNodePtr.IsNull())
	{
		ReferenceNodePtr = TSoftObjectPtr<URigVMFunctionReferenceNode>(FSoftObjectPath(ReferenceNodePath));
	}
	if(!ReferenceNodePtr.IsValid())
	{
		ReferenceNodePtr.LoadSynchronous();
	}
	if(ReferenceNodePtr.IsValid())
	{
		return ReferenceNodePtr.Get();
	}
	return nullptr;
}

bool URigVMBuildData::bInitialized = false; 

URigVMBuildData::URigVMBuildData()
: UObject()
, bIsRunningUnitTest(false)
{
}

TArray<UClass*> URigVMBuildData::FindAllRigVMAssetClasses()
{
	// Find all classes which implement IRigVMClientHost
	TArray<UClass*> ImplementedClasses;
	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->ImplementsInterface(URigVMClientHost::StaticClass()))
		{
			ImplementedClasses.AddUnique(*ClassIterator);

			const UObject* CDO = ClassIterator->GetDefaultObject();
			if(const URigVMBlueprint* BlueprintCDO = Cast<URigVMBlueprint>(CDO))
			{
				if(UClass* GeneratedClass = BlueprintCDO->GetRigVMBlueprintGeneratedClass())
				{
					ImplementedClasses.AddUnique(GeneratedClass);
				}
			}
		}
	}
	return ImplementedClasses;
}

void URigVMBuildData::SetupRigVMGraphFunctionPointers()
{
	FRigVMGraphFunctionIdentifier::GetVariantRefsByGuidFunc = [this](const FGuid& InGuid)
	{
		return FindFunctionVariantRefs(InGuid);
	};
	FRigVMGraphFunctionHeader::FindFunctionHeaderFromPathFunc = [this](const FSoftObjectPath& InObjectPath, const FName& InFunctionName, bool *bOutIsPublic)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistryModule.GetRegistry().GetAssetsByPath(*InObjectPath.ToString(), Assets, true);
		if(!Assets.IsEmpty())
		{
			const FString FunctionNameString = InFunctionName.ToString();
			const TArray<FRigVMGraphFunctionHeader> Headers = GetFunctionHeadersForAsset(Assets[0]);
			for(const FRigVMGraphFunctionHeader& Header : Headers)
			{
				if(Header.LibraryPointer.GetFunctionName() == FunctionNameString)
				{
					if(bOutIsPublic)
					{
						// if this isn't loaded - it means that the asset is not loaded thus the function can only be public
						*bOutIsPublic = true;
						
						const FSoftObjectPath LibraryNodeSoftPath = Header.LibraryPointer.GetNodeSoftPath();
						if(const URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(LibraryNodeSoftPath.ResolveObject()))
						{
							if(const URigVMFunctionLibrary* FunctionLibrary = LibraryNode->GetTypedOuter<URigVMFunctionLibrary>())
							{
								*bOutIsPublic = FunctionLibrary->IsFunctionPublic(Header.LibraryPointer.GetFunctionFName());
							}
						}
					}
					return Header;
				}
			}
		}
		if(bOutIsPublic)
		{
			*bOutIsPublic = false;
		}
		return FRigVMGraphFunctionHeader();
	};
	FRigVMGraphFunctionData::GetFunctionHostFromObjectFunc = [this](UObject* InObject) -> IRigVMGraphFunctionHost*
	{
		if(IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(InObject))
		{
			return FunctionHost;
		}
		if(IRigVMGraphFunctionHost* OuterFunctionHost = InObject->GetImplementingOuter<IRigVMGraphFunctionHost>())
		{
			return OuterFunctionHost;
		}
		if(IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(InObject))
		{
			if(IRigVMGraphFunctionHost* FunctionHost = ClientHost->GetRigVMGraphFunctionHost())
			{
				return FunctionHost;
			}
		}
		if(IRigVMClientHost* OuterClientHost = InObject->GetImplementingOuter<IRigVMClientHost>())
		{
			if(IRigVMGraphFunctionHost* FunctionHost = OuterClientHost->GetRigVMGraphFunctionHost())
			{
				return FunctionHost;
			}
		}
		if(URigVMBlueprint* Blueprint = Cast<URigVMBlueprint>(InObject))
		{
			return Blueprint->GetRigVMGraphFunctionHost();
		}
		if(URigVMBlueprint* Blueprint = InObject->GetTypedOuter<URigVMBlueprint>())
		{
			return Blueprint->GetRigVMGraphFunctionHost();
		}
		return nullptr;
	};
}

void URigVMBuildData::TearDownRigVMGraphFunctionPointers()
{
	FRigVMGraphFunctionIdentifier::GetVariantRefsByGuidFunc.Reset();
	FRigVMGraphFunctionHeader::FindFunctionHeaderFromPathFunc.Reset();
	FRigVMGraphFunctionData::GetFunctionHostFromObjectFunc.Reset();
}

TArray<FRigVMGraphFunctionHeader> URigVMBuildData::GetFunctionHeadersForAsset(const FAssetData& InAssetData)
{
	TArray<FRigVMGraphFunctionHeader> Result;
	if (!InAssetData.IsInstanceOf(URigVMBlueprint::StaticClass()) &&
	!InAssetData.IsInstanceOf(URigVMBlueprintGeneratedClass::StaticClass()))
	{
		return Result;
	}

	// If the asset is loaded, gather the function variants from the function store
	// which will include private functions
	if (InAssetData.IsAssetLoaded())
	{
		UObject* AssetObject = InAssetData.GetAsset();
		URigVMBlueprintGeneratedClass* GeneratedClass = Cast<URigVMBlueprintGeneratedClass>(AssetObject);
		if (!GeneratedClass)
		{
			if (URigVMBlueprint* RigVMBlueprint = Cast<URigVMBlueprint>(AssetObject))
			{
				GeneratedClass = RigVMBlueprint->GetRigVMBlueprintGeneratedClass();
			}
		}

		if (GeneratedClass)
		{
			for (int32 Pass=0; Pass<2; Pass++)
			{
				TArray<FRigVMGraphFunctionData>& Functions = (Pass == 0)
					? GeneratedClass->GraphFunctionStore.PrivateFunctions
					: GeneratedClass->GraphFunctionStore.PublicFunctions;
				
				for (FRigVMGraphFunctionData& Data : Functions)
				{
					Result.Add(Data.Header);
				}
			}

			return Result;
		}
	}

	// If asset is not loaded, gather function variants from metadata
	const FString PublicGraphFunctionsString = InAssetData.GetTagValueRef<FString>(TEXT("PublicGraphFunctions"));
	if(!PublicGraphFunctionsString.IsEmpty())
	{
		static const FName FunctionsPropertyName = GET_MEMBER_NAME_CHECKED(URigVMBlueprint, PublicGraphFunctions);
		const FArrayProperty* PublicGraphFunctionsProperty = CastField<FArrayProperty>(URigVMBlueprint::StaticClass()->FindPropertyByName(FunctionsPropertyName));
		PublicGraphFunctionsProperty->ImportText_Direct(*PublicGraphFunctionsString, &Result, nullptr, EPropertyPortFlags::PPF_None);
	}
	return Result;
}

URigVMBuildData* URigVMBuildData::Get()
{
	// static in a function scope ensures that the GC system is initiated before 
	// the build data constructor is called
	static TStrongObjectPtr<URigVMBuildData> sBuildData;
	if(!sBuildData.IsValid() && IsInGameThread())
	{
		sBuildData = TStrongObjectPtr<URigVMBuildData>(
			NewObject<URigVMBuildData>(
				GetTransientPackage(), 
				TEXT("RigVMBuildData"), 
				RF_Transient));
	}
	return sBuildData.Get();

}

void URigVMBuildData::InitializeIfNeeded()
{
	if (bInitialized)
	{
		return;
	}

	TArray<UClass*> ImplementedClasses = FindAllRigVMAssetClasses();

	// Loop the classes
	for (UClass* Class : ImplementedClasses)
	{
		const FArrayProperty* ReferenceNodeDataProperty =
		   CastField<FArrayProperty>(Class->FindPropertyByName(FunctionReferenceNodeDataName));
		if(ReferenceNodeDataProperty)
		{
			const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

			// find all assets of this class in the project
			TArray<FAssetData> AssetDatas;
			FARFilter AssetFilter;
			AssetFilter.ClassPaths.Add(Class->GetClassPathName());
			AssetRegistryModule.Get().GetAssets(AssetFilter, AssetDatas);

			// loop over all found assets
			for(const FAssetData& AssetData : AssetDatas)
			{
				RegisterReferencesFromAsset(AssetData);
			}
		}
	}
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistryModule.Get().OnAssetAdded().AddStatic(&URigVMBuildData::RegisterReferencesFromAsset);
	
	bInitialized = true;
}

void URigVMBuildData::RegisterReferencesFromAsset(const FAssetData& InAssetData)
{
	URigVMBuildData* BuildData = URigVMBuildData::Get();

	// It's faster to check for a key directly then trying to get the class
	FAssetDataTagMapSharedView::FFindTagResult FoundValue = InAssetData.TagsAndValues.FindTag(FunctionReferenceNodeDataName);

	if (FoundValue.IsSet())
	{
		if (UClass* Class = InAssetData.GetClass())
		{
			const FArrayProperty* ReferenceNodeDataProperty =
				  CastField<FArrayProperty>(Class->FindPropertyByName(FunctionReferenceNodeDataName));
			if(ReferenceNodeDataProperty)
			{
				const FString ReferenceNodeDataString = FoundValue.AsString();
				if(ReferenceNodeDataString.IsEmpty())
				{
					return;
				}

				// See if it has reference node data, and register the references
				TArray<FRigVMReferenceNodeData> ReferenceNodeDatas;
				ReferenceNodeDataProperty->ImportText_Direct(*ReferenceNodeDataString, &ReferenceNodeDatas, nullptr, EPropertyPortFlags::PPF_None);	
				for(FRigVMReferenceNodeData& ReferenceNodeData : ReferenceNodeDatas)
				{
					if (ReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData.ReferencedFunctionIdentifier, ReferenceNodeData.GetReferenceNodeObjectPath());
					}
					else if (ReferenceNodeData.ReferencedHeader_DEPRECATED.IsValid())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData.ReferencedHeader_DEPRECATED.LibraryPointer, ReferenceNodeData.GetReferenceNodeObjectPath());
					}
					else if (!ReferenceNodeData.ReferencedFunctionPath_DEPRECATED.IsEmpty())
					{
						BuildData->RegisterFunctionReference(ReferenceNodeData);
					}
				}
			}
		}
	}
}

const FRigVMFunctionReferenceArray* URigVMBuildData::FindFunctionReferences(const FRigVMGraphFunctionIdentifier& InFunction) const
{
	return GraphFunctionReferences.Find(InFunction);
}

void URigVMBuildData::ForEachFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                               TFunction<void(URigVMFunctionReferenceNode*)> PerReferenceFunction,
                                               bool bLoadIfNecessary) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			if (bLoadIfNecessary && !Reference.IsValid())
			{
				Reference.LoadSynchronous();
			}
			if (Reference.IsValid())
			{
				PerReferenceFunction(Reference.Get());
			}
		}
	}
}

void URigVMBuildData::ForEachFunctionReferenceSoftPtr(const FRigVMGraphFunctionIdentifier& InFunction,
                                                      TFunction<void(TSoftObjectPtr<URigVMFunctionReferenceNode>)> PerReferenceFunction) const
{
	if (const FRigVMFunctionReferenceArray* ReferencesEntry = FindFunctionReferences(InFunction))
	{
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferencesEntry->Num(); ReferenceIndex++)
		{
			const TSoftObjectPtr<URigVMFunctionReferenceNode>& Reference = ReferencesEntry->operator [](ReferenceIndex);
			PerReferenceFunction(Reference);
		}
	}
}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction, URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	RegisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::RegisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Add(InReference);
	}
	else
	{
		Modify();
		FRigVMFunctionReferenceArray NewReferenceEntry;
		NewReferenceEntry.FunctionReferences.Add(InReference);
		GraphFunctionReferences.Add(InFunction, NewReferenceEntry);
	}
	
	MarkPackageDirty();
}

void URigVMBuildData::RegisterFunctionReference(FRigVMReferenceNodeData InReferenceNodeData)
{
	if (InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		return RegisterFunctionReference(InReferenceNodeData.ReferencedFunctionIdentifier, InReferenceNodeData.GetReferenceNodeObjectPath());
	}

	if (!InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		InReferenceNodeData.ReferencedFunctionIdentifier = InReferenceNodeData.ReferencedHeader_DEPRECATED.LibraryPointer;
	}

	if (!InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid())
	{
		InReferenceNodeData.ReferencedFunctionIdentifier.SetLibraryNodePath(InReferenceNodeData.ReferencedFunctionPath_DEPRECATED);
	}
	
	check(InReferenceNodeData.ReferencedFunctionIdentifier.GetNodeSoftPath().IsValid());

	FString LibraryNodePath = InReferenceNodeData.ReferencedFunctionIdentifier.GetLibraryNodePath();
	TSoftObjectPtr<URigVMLibraryNode> LibraryNodePtr = TSoftObjectPtr<URigVMLibraryNode>(FSoftObjectPath(LibraryNodePath));

	// Try to find a FunctionIdentifier with the same LibraryNodePath
	bool bFound = false;
	for (TPair< FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray >& Pair : GraphFunctionReferences)
	{
		if (Pair.Key.GetLibraryNodePath() == LibraryNodePath)
		{
			Pair.Value.FunctionReferences.Add(InReferenceNodeData.GetReferenceNodeObjectPath());
			bFound = true;
			break;
		}
	}

	// Otherwise, lets add a new identifier, even if it has no function host
	if (!bFound)
	{
		FRigVMGraphFunctionIdentifier Pointer(nullptr, LibraryNodePath);
		if (LibraryNodePtr.IsValid())
		{
			Pointer.HostObject = Cast<UObject>(LibraryNodePtr.Get()->GetFunctionHeader().GetFunctionHost());
		}
		FRigVMFunctionReferenceArray RefArray;
		RefArray.FunctionReferences.Add(InReferenceNodeData.GetReferenceNodeObjectPath());
		GraphFunctionReferences.Add(Pointer, RefArray);
	}
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  URigVMFunctionReferenceNode* InReference)
{
	if(InReference == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<URigVMFunctionReferenceNode> ReferenceKey(InReference);

	return UnregisterFunctionReference(InFunction, ReferenceKey);
}

void URigVMBuildData::UnregisterFunctionReference(const FRigVMGraphFunctionIdentifier& InFunction,
                                                  TSoftObjectPtr<URigVMFunctionReferenceNode> InReference)
{
	if(InReference.IsNull())
	{
		return;
	}

	if(FRigVMFunctionReferenceArray* ReferenceEntry = GraphFunctionReferences.Find(InFunction))
	{
		if(!ReferenceEntry->FunctionReferences.Contains(InReference))
		{
			return;
		}

		Modify();
		ReferenceEntry->FunctionReferences.Remove(InReference);
		MarkPackageDirty();
	}
}

void URigVMBuildData::ClearInvalidReferences()
{
	if (bIsRunningUnitTest)
	{
		return;
	}
	
	Modify();
	
	// check each function's each reference
	int32 NumRemoved = 0;
	for (TTuple<FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray>& FunctionReferenceInfo : GraphFunctionReferences)
	{
		FRigVMFunctionReferenceArray* ReferencesEntry = &FunctionReferenceInfo.Value;

		static FString sTransientPackagePrefix;
		if(sTransientPackagePrefix.IsEmpty())
		{
			sTransientPackagePrefix = GetTransientPackage()->GetPathName();
		}
		static const FString sTempPrefix = TEXT("/Temp/");

		NumRemoved += ReferencesEntry->FunctionReferences.RemoveAll([](TSoftObjectPtr<URigVMFunctionReferenceNode> Referencer)
		{
			// ignore keys / references within the transient package
			const FString ReferencerString = Referencer.ToString();
			return ReferencerString.StartsWith(sTransientPackagePrefix) || ReferencerString.StartsWith(sTempPrefix);
		});
	}

	if (NumRemoved > 0)
	{
		MarkPackageDirty();
	}
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherAllFunctionVariantRefs()
{
	TArray<FRigVMVariantRef> Result;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	FARFilter AssetFilter;
	AssetFilter.ClassPaths.Add(URigVMBlueprint::StaticClass()->GetClassPathName());
	AssetFilter.ClassPaths.Add(URigVMBlueprintGeneratedClass::StaticClass()->GetClassPathName());
	AssetFilter.bRecursiveClasses = true;
	AssetRegistryModule.Get().GetAssets(AssetFilter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		if(Asset.IsAssetLoaded())
		{
			if(const IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(Asset.GetAsset()))
			{
				if(const URigVMFunctionLibrary* FunctionLibrary = ClientHost->GetLocalFunctionLibrary())
				{
					const TArray<URigVMLibraryNode*> Functions = FunctionLibrary->GetFunctions();
					for(const URigVMLibraryNode* Function : Functions)
					{
						FRigVMVariant FunctionVariant = Function->GetFunctionVariant();
						if(!FunctionVariant.Guid.IsValid())
						{
							FunctionVariant.Guid = FRigVMVariant::GenerateGUID(Function->GetPathName());
						}
						Result.Add(FRigVMVariantRef(Function->GetPathName(), FunctionVariant));
					}
					continue;
				}
			}
		}
		TArray<FRigVMVariantRef> VariantRefs = URigVMBuildData::GatherFunctionVariantRefsForAsset(Asset);
		Result.Append(VariantRefs);
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherFunctionVariantRefsForAsset(const FAssetData& InAssetData)
{
	TArray<FRigVMVariantRef> Result;

	const TArray<FRigVMGraphFunctionHeader> FunctionHeaders = GetFunctionHeadersForAsset(InAssetData);
	for(const FRigVMGraphFunctionHeader& FunctionHeader : FunctionHeaders)
	{
		if (FunctionHeader.LibraryPointer.IsValid())
		{
			FRigVMVariantRef& VariantRef = Result.Add_GetRef(FRigVMVariantRef());
			VariantRef.Variant = FunctionHeader.Variant;
			VariantRef.ObjectPath = FunctionHeader.LibraryPointer.GetNodeSoftPath();
			if (!VariantRef.Variant.Guid.IsValid())
			{
				VariantRef.Variant.Guid = FRigVMVariant::GenerateGUID(VariantRef.ObjectPath.ToString());
			}
		}
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::FindFunctionVariantRefs(const FGuid& InGuid)
{
	TArray<FRigVMVariantRef> Result = GatherAllFunctionVariantRefs();
	Result = Result.FilterByPredicate([InGuid](const FRigVMVariantRef& VariantRef) { return VariantRef.Variant.Guid == InGuid; });
	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::GatherAllAssetVariantRefs()
{
	TArray<FRigVMVariantRef> Result;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	FARFilter AssetFilter;
	AssetFilter.ClassPaths.Add(URigVMBlueprint::StaticClass()->GetClassPathName());
	AssetFilter.bRecursiveClasses = true;
	AssetRegistryModule.Get().GetAssets(AssetFilter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		FRigVMVariant AssetVariant;
		if(Asset.IsAssetLoaded())
		{
			if(const URigVMBlueprint* Blueprint = Cast<URigVMBlueprint>(Asset.GetAsset()))
			{
				AssetVariant = Blueprint->GetAssetVariant();
			}
		}
		
		if (!AssetVariant.Guid.IsValid())
		{
			static const FName AssetVariantPropertyName = GET_MEMBER_NAME_CHECKED(URigVMBlueprint, AssetVariant);
			const FProperty* AssetVariantProperty = CastField<FProperty>(URigVMBlueprint::StaticClass()->FindPropertyByName(AssetVariantPropertyName));
			const FString VariantStr = Asset.GetTagValueRef<FString>(AssetVariantPropertyName);
			if(!VariantStr.IsEmpty())
			{
				AssetVariantProperty->ImportText_Direct(*VariantStr, &AssetVariant, nullptr, EPropertyPortFlags::PPF_None);
			}
		}

		if (!AssetVariant.Guid.IsValid())
		{
			AssetVariant.Guid = FRigVMVariant::GenerateGUID(Asset.PackageName.ToString());
		}
		
		FRigVMVariantRef VariantRef(Asset.ToSoftObjectPath(), AssetVariant);
		Result.Add(VariantRef);
	}

	return Result;
}

TArray<FRigVMVariantRef> URigVMBuildData::FindAssetVariantRefs(const FGuid& InGuid)
{
	TArray<FRigVMVariantRef> Result = GatherAllAssetVariantRefs();
	Result = Result.FilterByPredicate([InGuid](const FRigVMVariantRef& VariantRef) { return VariantRef.Variant.Guid == InGuid; });
	return Result;
}

#if WITH_EDITOR

TArray<FRigVMGraphFunctionIdentifier> URigVMBuildData::GetAllFunctionIdentifiers(bool bOnlyPublic) const
{
	static const FName PublicGraphFunctionsName = TEXT("PublicGraphFunctions");
	TArray<FRigVMGraphFunctionIdentifier> Identifiers;
	TArray<UClass*> ImplementedClasses = FindAllRigVMAssetClasses();

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	// find all assets of this class in the project
	TArray<FAssetData> AssetDatas;
	FARFilter AssetFilter;
	for(const UClass* Class : ImplementedClasses)
	{
		AssetFilter.ClassPaths.AddUnique(Class->GetClassPathName());
	}
	AssetRegistryModule.Get().GetAssets(AssetFilter, AssetDatas);

	// loop over all found assets
	TSet<FName> PackagesProcessed;
	for(const FAssetData& AssetData : AssetDatas)
	{
		// Avoid duplication
		if (PackagesProcessed.Contains(AssetData.PackageName))
		{
			continue;
		}
		PackagesProcessed.Add(AssetData.PackageName);

		if(AssetData.IsAssetLoaded())
		{
			IRigVMGraphFunctionHost* FunctionHost = Cast<IRigVMGraphFunctionHost>(AssetData.GetAsset());
			if(FunctionHost == nullptr)
			{
				if(IRigVMClientHost* ClientHost = Cast<IRigVMClientHost>(AssetData.GetAsset()))
				{
					FunctionHost = ClientHost->GetRigVMGraphFunctionHost();
				}
			}
			if(FunctionHost)
			{
				if(const FRigVMGraphFunctionStore* Store = FunctionHost->GetRigVMGraphFunctionStore())
				{
					for(const FRigVMGraphFunctionData& Data : Store->PublicFunctions)
					{
						Identifiers.Add(Data.Header.LibraryPointer);
					}
					if(!bOnlyPublic)
					{
						for(const FRigVMGraphFunctionData& Data : Store->PrivateFunctions)
						{
							Identifiers.Add(Data.Header.LibraryPointer);
						}
					}
					continue;
				}
			}
		}

		FString PublicGraphFunctionsString = AssetData.GetTagValueRef<FString>(PublicGraphFunctionsName);
		if (PublicGraphFunctionsString.IsEmpty())
		{
			PublicGraphFunctionsString = AssetData.GetTagValueRef<FString>(PublicGraphFunctionsName);
		}
		if (!PublicGraphFunctionsString.IsEmpty())
		{
			const FArrayProperty* Property = CastField<FArrayProperty>(AssetData.GetClass()->FindPropertyByName(PublicGraphFunctionsName));
			if (Property)
			{
				TArray<FRigVMGraphFunctionHeader> PublicFunctions;
				Property->ImportText_Direct(*PublicGraphFunctionsString, &PublicFunctions, nullptr, EPropertyPortFlags::PPF_None);
				for(const FRigVMGraphFunctionHeader& PublicFunction : PublicFunctions)
				{
					Identifiers.Add(PublicFunction.LibraryPointer);
				}
			}
			else
			{
				const FString& HeadersString = PublicGraphFunctionsString;
				const FArrayProperty* HeadersArrayProperty = CastField<FArrayProperty>(FRigVMGraphFunctionHeaderArray::StaticStruct()->FindPropertyByName(TEXT("Headers")));
				TArray<FRigVMGraphFunctionHeader> PublicFunctions;
				HeadersArrayProperty->ImportText_Direct(*HeadersString, &PublicFunctions, nullptr, EPropertyPortFlags::PPF_None);
		
				for(const FRigVMGraphFunctionHeader& PublicFunction : PublicFunctions)
				{
					Identifiers.Add(PublicFunction.LibraryPointer);
				}
			}
		}
	}

	return Identifiers;
}

#endif

TArray<FRigVMGraphFunctionIdentifier> URigVMBuildData::GetUsedFunctionIdentifiers(bool bOnlyPublic) const
{
	TArray<FRigVMGraphFunctionIdentifier> Identifiers;
#if WITH_EDITOR
	Identifiers = GetAllFunctionIdentifiers().FilterByPredicate([this](const FRigVMGraphFunctionIdentifier& Identifier)
	{
		const FRigVMFunctionReferenceArray* References = FindFunctionReferences(Identifier); 
		return References != nullptr && References->Num() > 0;
	});
#else
	// outside of editor we can't do much - just get the keys
	// of the used references array
	GraphFunctionReferences.GenerateKeyArray(Identifiers);
#endif
	return Identifiers;
}

FRigVMFunctionReferenceArray URigVMBuildData::GetAllFunctionReferences() const
{
	FRigVMFunctionReferenceArray FunctionReferences;
	for (const TTuple<FRigVMGraphFunctionIdentifier, FRigVMFunctionReferenceArray>& FunctionReferenceInfo : GraphFunctionReferences)
	{
		FunctionReferences.FunctionReferences.Append(FunctionReferenceInfo.Value.FunctionReferences);
	}
	return FunctionReferences;
}



