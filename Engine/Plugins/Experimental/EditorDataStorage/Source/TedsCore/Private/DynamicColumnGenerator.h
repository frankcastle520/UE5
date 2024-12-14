// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TypedElementDataStorageSharedColumn.h"
#include "Containers/Map.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Misc/MTAccessDetector.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/NameTypes.h"
#include "UObject/Class.h"

#include "DynamicColumnGenerator.generated.h"

class FName;
class UScriptStruct;

// The template struct that is used to generate the ValueTag column
USTRUCT()
struct FTedsValueTagColumn : public FTedsSharedColumn
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName Value;
};

namespace UE::Editor::DataStorage
{
	using FValueTagColumn = FTedsValueTagColumn;

	struct FDynamicColumnInfo
	{
		const UScriptStruct* Type;
	};
	
	struct FDynamicColumnGeneratorInfo
	{
		const UScriptStruct* Type;
		bool bNewlyGenerated;
	};

	/**
	 * Utility class that TEDS can use to dynamically generate column types on the fly
	 */
	class FDynamicColumnGenerator
	{
	public:
		/**
		 * Generates a dynamic TEDS column type based on a Template type (if it hasn't been generated before)
		 */
		FDynamicColumnGeneratorInfo GenerateColumn(const UScriptStruct& Template, const FName& Identifier);

		const UScriptStruct* FindColumn(const UScriptStruct& Template, const FName& Identifier) const;
	private:

		struct FGeneratedColumnRecord
		{
			FName Identifier;
			const UScriptStruct* Template;
			const UScriptStruct* Type;
			FTopLevelAssetPath AssetPath;
		};

		struct FGeneratedColumnKey
		{
			const UScriptStruct& Template;
			FName Identifier;

			friend bool operator==(const FGeneratedColumnKey& Lhs, const FGeneratedColumnKey& Rhs)
			{
				return Lhs.Identifier == Rhs.Identifier && &Lhs.Template == &Rhs.Template;
			}

			friend uint32 GetTypeHash(const FGeneratedColumnKey& Key)
			{
				return HashCombineFast(GetTypeHash(Key.Identifier), PointerHash(&Key.Template));
			}
		};

		UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);

		TArray<FGeneratedColumnRecord> GeneratedColumnData;
		
		// Looks up generated column index by the parameters used to generate it
		// Used to de-duplicate
		TMap<FGeneratedColumnKey, int32> GeneratedColumnLookup;
	};

	class FValueTagManager
	{
	public:
		explicit FValueTagManager(FDynamicColumnGenerator& InColumnGenerator);
		FConstSharedStruct GenerateValueTag(const FValueTag& InTag, const FName& InValue);
		const UScriptStruct* GenerateColumnType(const FValueTag& InTag);
	private:

		UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);

		TMap<TPair<FValueTag, FName>, FConstSharedStruct> ValueTagLookup;

		FDynamicColumnGenerator& ColumnGenerator;
	};
} // namespace UE::Editor::DataStorage
