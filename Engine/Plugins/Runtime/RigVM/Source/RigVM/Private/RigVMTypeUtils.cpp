// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RigVMTypeUtils.h: Module implementation.
=============================================================================*/

#include "RigVMTypeUtils.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/LinkerLoad.h"

namespace RigVMTypeUtils
{
	TRigVMTypeIndex TypeIndex::Execute = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::ExecuteArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::Bool = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::Float = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::Double = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::Int32 = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::UInt32 = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::UInt8 = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::FName = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::FString = INDEX_NONE;
	TRigVMTypeIndex TypeIndex::WildCard = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::BoolArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::FloatArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::DoubleArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::Int32Array = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::UInt32Array = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::UInt8Array = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::FNameArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::FStringArray = INDEX_NONE;	
	TRigVMTypeIndex TypeIndex::WildCardArray = INDEX_NONE;	
}

UObject* RigVMTypeUtils::UserDefinedTypeFromCPPType(FString& InOutCPPType, const FRigVMUserDefinedTypeResolver* InTypeResolver)
{
	const FString OriginalTypeName = InOutCPPType;
	UObject* CPPTypeObject = nullptr;
	InOutCPPType.Reset();

	// try to resolve the type name using a path name potentially
	if(InOutCPPType.IsEmpty() && InTypeResolver != nullptr && InTypeResolver->IsValid())
	{
		FString TypeNameToLookUp = OriginalTypeName;
		while(IsArrayType(TypeNameToLookUp))
		{
			TypeNameToLookUp = BaseTypeFromArrayType(TypeNameToLookUp);
		}

		// Ask the resolver to the name of the user-defined struct/enum to an object.
		// For example FUserDefinedStruct_23E408214EE9E6DA5BFADDA0F9F4F577 -> /Game/Animation/MyUserDefinedStruct.MyUserDefinedStruct
		CPPTypeObject = InTypeResolver->GetTypeObjectByName(TypeNameToLookUp); 
		if(CPPTypeObject)
		{
			InOutCPPType = PostProcessCPPType(OriginalTypeName, CPPTypeObject);
			return CPPTypeObject;
		}
	}

#if WITH_EDITOR
	
	// potentially this type hasn't been loaded yet. Let's try again by visiting relevant assets
	if(InOutCPPType.IsEmpty())
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		InOutCPPType = OriginalTypeName;
		while(IsArrayType(InOutCPPType))
		{
			InOutCPPType = BaseTypeFromArrayType(InOutCPPType);
		}

		if(OriginalTypeName.Contains(TEXT("FUserDefinedStruct_")))
		{
			static const FLazyName GuidTag(GET_MEMBER_NAME_CHECKED(UUserDefinedStruct, Guid));

			TArray<FAssetData> AssetDataList;
			AssetRegistry.GetAssetsByClass(UUserDefinedStruct::StaticClass()->GetClassPathName(), AssetDataList, true);

			// Temporary workaround:
			// User Defined Struct has been moved from Engine to CoreUObject at CL34495787 for UE-216472
			// But currently the AssetRegistry is not applying CoreRedirects to the ClassPath, so searching using the latest class path
			// will not give the complete list of assets.
			// To get the complete list, we need to search again using old names of the class. 
			// This can be removed once the AssetRegistry issue (UE-168245) is fixed
			TArray<FString> OldPathNames = FLinkerLoad::FindPreviousPathNamesForClass(UUserDefinedStruct::StaticClass()->GetClassPathName().ToString(), false);
			for (const FString& OldPathName : OldPathNames)
			{
				AssetRegistry.GetAssetsByClass( FTopLevelAssetPath(OldPathName), AssetDataList, true);
			}

			// first pass - try to find it using the tag
			if(CPPTypeObject == nullptr)
			{
				for(const FAssetData& AssetData : AssetDataList)
				{
					if(AssetData.FindTag(GuidTag))
					{
						const FString GuidBasedName = GetUniqueStructTypeName(AssetData.GetTagValueRef<FGuid>(GuidTag));
						if(GuidBasedName == InOutCPPType)
						{
							if(UUserDefinedStruct* UserDefinedStruct = Cast<UUserDefinedStruct>(AssetData.GetAsset()))
							{
								CPPTypeObject = UserDefinedStruct;
								InOutCPPType = PostProcessCPPType(OriginalTypeName, CPPTypeObject);
								return CPPTypeObject;
							}
						}
					}
				}
			}

			// second pass - deal with the ones that don't have a tag and force load them
			if(CPPTypeObject == nullptr)
			{
				for(const FAssetData& AssetData : AssetDataList)
				{
					if(AssetData.FindTag(GuidTag))
					{
						continue;
					}

					if(UUserDefinedStruct* UserDefinedStruct = Cast<UUserDefinedStruct>(AssetData.GetAsset()))
					{
						const FString GuidBasedName = GetUniqueStructTypeName(UserDefinedStruct);
						if(GuidBasedName == InOutCPPType)
						{
							CPPTypeObject = UserDefinedStruct;
							InOutCPPType = PostProcessCPPType(OriginalTypeName, CPPTypeObject);
							return CPPTypeObject;
						}
					}
				}
			}
		}
		else
		{
			TArray<FAssetData> AssetDataList;
			AssetRegistry.GetAssetsByClass(UUserDefinedEnum::StaticClass()->GetClassPathName(), AssetDataList, true);

			for(const FAssetData& AssetData : AssetDataList)
			{
				if(UUserDefinedEnum* UserDefinedEnum = Cast<UUserDefinedEnum>(AssetData.GetAsset()))
				{
					const FString EnumCPPName = UserDefinedEnum->GetName();
					if(EnumCPPName == InOutCPPType)
					{
						CPPTypeObject = UserDefinedEnum;
						InOutCPPType = PostProcessCPPType(OriginalTypeName, CPPTypeObject);
						return CPPTypeObject;
					}
				}
			}
		}
	}
#endif

	return CPPTypeObject;
}
