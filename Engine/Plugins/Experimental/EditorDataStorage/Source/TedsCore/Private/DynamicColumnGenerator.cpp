// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicColumnGenerator.h"

#include "AssetDefinitionDefault.h"
#include "TypedElementDataStorageSharedColumn.h"
#include "Elements/Common/TypedElementCommonTypes.h"
#include "Kismet2/ReloadUtilities.h"

namespace UE::Editor::DataStorage
{
	FDynamicColumnGeneratorInfo FDynamicColumnGenerator::GenerateColumn(const UScriptStruct& Template, const FName& Identifier)
	{
		const FGeneratedColumnKey Key
		{
			.Template = Template,
			.Identifier = Identifier,
		};
	
		FDynamicColumnGeneratorInfo GeneratedColumnInfo;
		UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
		
		{
			const int32* GeneratedColumnIndex = GeneratedColumnLookup.Find(Key);
			if (GeneratedColumnIndex != nullptr)
			{
				const FGeneratedColumnRecord& GeneratedColumnRecord = GeneratedColumnData[*GeneratedColumnIndex];
				GeneratedColumnInfo.Type = GeneratedColumnRecord.Type;
				GeneratedColumnInfo.bNewlyGenerated = false;
				return GeneratedColumnInfo;
			}		
		}
		
		auto IsValidType = [](const UScriptStruct& Template)
		{
			return
				Template.IsChildOf(UE::Editor::DataStorage::FColumn::StaticStruct()) ||
				Template.IsChildOf(UE::Editor::DataStorage::FTag::StaticStruct()) ||
				Template.IsChildOf(FTedsSharedColumn::StaticStruct());
		};
		if (!ensureMsgf(IsValidType(Template), TEXT("Template struct [%s] must derive from Column, Tag or SharedColumn"), *Template.GetName()))
		{
			return FDynamicColumnGeneratorInfo
			{
				.Type = nullptr,
				.bNewlyGenerated = false
			};
		}
	
		{
			checkf(Template.GetCppStructOps() != nullptr && Template.IsNative(), TEXT("Can only create column from native struct"));

			TStringBuilder<256> ObjectNameBuilder;
			ObjectNameBuilder.Append(Template.GetName());
			ObjectNameBuilder.Append(TEXT("::"));
			ObjectNameBuilder.Append(Identifier.ToString());

			const FName ObjectName = FName(ObjectNameBuilder);
			const FTopLevelAssetPath AssetPath(GetTransientPackage()->GetFName(), ObjectName);
			
			UScriptStruct* NewScriptStruct = NewObject<UScriptStruct>(GetTransientPackage(), ObjectName);
			// Ensure it is not garbage collected
			// FDynamicColumnGenerator is not a UObject and thus does not participate in GC
			NewScriptStruct->AddToRoot();
	
			// New struct subclasses the template to allow for casting back to template and usage of CppStructOps
			// for copy/move.
			NewScriptStruct->SetSuperStruct(&const_cast<UScriptStruct&>(Template));
			
			NewScriptStruct->Bind();
			NewScriptStruct->PrepareCppStructOps();
			NewScriptStruct->StaticLink(true);			
			const int32 Index = GeneratedColumnData.Emplace(FGeneratedColumnRecord
			{
				.Identifier = Identifier,
				.Template = &Template,
				.Type = NewScriptStruct,
				.AssetPath = AssetPath
			});
			
			GeneratedColumnLookup.Add(Key, Index);
						
			GeneratedColumnInfo.Type = NewScriptStruct;
			GeneratedColumnInfo.bNewlyGenerated = true;
			return GeneratedColumnInfo;
		}
	}
	
	const UScriptStruct* FDynamicColumnGenerator::FindColumn(const UScriptStruct& Template, const FName& Identifier) const
	{
		UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	
		const int32* IndexPtr = GeneratedColumnLookup.Find(FGeneratedColumnKey{
			.Template = Template,
			.Identifier = Identifier
			});
		if (IndexPtr)
		{
			return GeneratedColumnData[*IndexPtr].Type;
		}
		return nullptr;
	}
	
	FValueTagManager::FValueTagManager(FDynamicColumnGenerator& InColumnGenerator)
		: ColumnGenerator(InColumnGenerator)
	{
	}
	
	FConstSharedStruct FValueTagManager::GenerateValueTag(const FValueTag& InTag, const FName& InValue)
	{
		TPair<FValueTag, FName> Pair(InTag, InValue);
	
		UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	
		// Common path
		{
			if (FConstSharedStruct* TagStruct = ValueTagLookup.Find(Pair))
			{
				return *TagStruct;
			}
		}
	
		// 
		{
			const UScriptStruct* ColumnType = GenerateColumnType(InTag);
	
			const FTedsValueTagColumn Overlay
			{
				.Value = InValue
			};
	
			FConstSharedStruct SharedStruct = FConstSharedStruct::Make(ColumnType, reinterpret_cast<const uint8*>(&Overlay));
			
			ValueTagLookup.Emplace(Pair, SharedStruct);
	
			return SharedStruct;
		}
	}
	
	const UScriptStruct* FValueTagManager::GenerateColumnType(const FValueTag& Tag)
	{
		const FDynamicColumnGeneratorInfo GeneratedColumnType = ColumnGenerator.GenerateColumn(*FTedsValueTagColumn::StaticStruct(), Tag.GetName());
		
		return GeneratedColumnType.Type;
	}
} // namespace UE::Editor::DataStorage
