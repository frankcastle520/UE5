// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsTableViewerUtils.h"

#include "GameFramework/Actor.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementIconOverrideColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/TypedElementDataStorageLog.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Styling/SlateIconFinder.h"

namespace UE::Editor::DataStorage::TableViewerUtils
{
	static FName TableViewerWidgetTableName("Editor_TableViewerWidgetTable");

	FName GetWidgetTableName()
	{
		return TableViewerWidgetTableName;
	}
	
	// TEDS UI TODO: Maybe the widget can specify a user facing name derived from the matched columns instead of trying to find the longest matching name
	FName FindLongestMatchingName(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes, int32 DefaultNameIndex)
	{
		switch (ColumnTypes.Num())
		{
		case 0:
			return FName(TEXT("Column"), DefaultNameIndex);
		case 1:
			return FName(ColumnTypes[0]->GetDisplayNameText().ToString());
		default:
			{
				FText LongestMatchText = ColumnTypes[0]->GetDisplayNameText();
				FStringView LongestMatch = LongestMatchText.ToString();
				const TWeakObjectPtr<const UScriptStruct>* ItEnd = ColumnTypes.end();
				const TWeakObjectPtr<const UScriptStruct>* It = ColumnTypes.begin();
				++It; // Skip the first entry as that's already set.
				for (; It != ItEnd; ++It)
				{
					FText NextMatchText = (*It)->GetDisplayNameText();
					FStringView NextMatch = NextMatchText.ToString();

					int32 MatchSize = 0;
					auto ItLeft = LongestMatch.begin();
					auto ItLeftEnd = LongestMatch.end();
					auto ItRight = NextMatch.begin();
					auto ItRightEnd = NextMatch.end();
					while (
						ItLeft != ItLeftEnd &&
						ItRight != ItRightEnd &&
						*ItLeft == *ItRight)
					{
						++MatchSize;
						++ItLeft;
						++ItRight;
					}

					// At least 3 letters have to match to avoid single or double letter names which typically mean nothing.
					if (MatchSize > 2)
					{
						LongestMatch.LeftInline(MatchSize);
					}
					else
					{
						// There are not enough characters in the string that match. Just return the name of the first column
						return FName(ColumnTypes[0]->GetDisplayNameText().ToString());
					}
				}
				return FName(LongestMatch);
			}
		};
	}
	
	TArray<TWeakObjectPtr<const UScriptStruct>> CreateVerifiedColumnTypeArray(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes)
	{
		TArray<TWeakObjectPtr<const UScriptStruct>> VerifiedColumnTypes;
		VerifiedColumnTypes.Reserve(ColumnTypes.Num());
		for (const TWeakObjectPtr<const UScriptStruct>& ColumnType : ColumnTypes)
		{
			if (ColumnType.IsValid())
			{
				VerifiedColumnTypes.Add(ColumnType.Get());
			}
			else
			{
				UE_LOG(LogEditorDataStorage, Verbose, TEXT("Invalid column provided to the table viewer"));
			}
		}
		return VerifiedColumnTypes;
	}

	TSharedPtr<FTypedElementWidgetConstructor> CreateHeaderWidgetConstructor(IEditorDataStorageUiProvider& StorageUi, const UE::Editor::DataStorage::FMetaDataView& InMetaData, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes, const TConstArrayView<FName> CellWidgetPurposes)
	{
		using MatchApproach = IEditorDataStorageUiProvider::EMatchApproach;

		TArray<TWeakObjectPtr<const UScriptStruct>> VerifiedColumnTypes = CreateVerifiedColumnTypeArray(ColumnTypes);
		TSharedPtr<FTypedElementWidgetConstructor> Constructor;

		for (const FName& Purpose : CellWidgetPurposes)
		{
			// Extract the header purpose from the cell purpose (e.g "SceneOutliner.ItemLabel.Cell" -> "SceneOutliner.ItemLabel.Header")
			FString HeaderPurpose, CellPurpose;
			Purpose.ToString().Split(TEXT(".Cell"), &HeaderPurpose, &CellPurpose);
			HeaderPurpose.Append(TEXT(".Header"));
			
			StorageUi.CreateWidgetConstructors(FName(HeaderPurpose), MatchApproach::ExactMatch, VerifiedColumnTypes, InMetaData,
				[&Constructor, ColumnTypes](
					TUniquePtr<FTypedElementWidgetConstructor> CreatedConstructor, 
					TConstArrayView<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes)
				{
					if (ColumnTypes.Num() == MatchedColumnTypes.Num())
					{
						Constructor = TSharedPtr<FTypedElementWidgetConstructor>(CreatedConstructor.Release());
					}
					// Either this was the exact match so no need to search further or the longest possible chain didn't match so the next ones will 
					// always be shorter in both cases just return.
					return false;
				});
			if (Constructor)
			{
				return Constructor;
			}
		}
		for (const FName& Purpose : CellWidgetPurposes)
		{
			// Extract the default header purpose from the cell purpose (e.g "SceneOutliner.ItemLabel.Cell" -> "SceneOutliner.ItemLabel.Header.Default")
			FString HeaderPurpose, CellPurpose;
			Purpose.ToString().Split(TEXT(".Cell"), &HeaderPurpose, &CellPurpose);
			HeaderPurpose.Append(TEXT(".Header.Default"));

			StorageUi.CreateWidgetConstructors(FName(HeaderPurpose), InMetaData,
				[&Constructor, ColumnTypes](
					TUniquePtr<FTypedElementWidgetConstructor> CreatedConstructor,
					TConstArrayView<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes)
				{
					Constructor = TSharedPtr<FTypedElementWidgetConstructor>(CreatedConstructor.Release());
					return false;
				});
			if (Constructor)
			{
				return Constructor;
			}
		}
		return nullptr;
	}

	const FSlateBrush* GetIconForRow(IEditorDataStorageProvider* DataStorage, RowHandle Row)
	{
		static TMap<FName, const FSlateBrush*> CachedIconMap;

		auto FindCachedIcon = [](const FName& IconName) -> const FSlateBrush*
		{
			if (const FSlateBrush** CachedBrush = CachedIconMap.Find(IconName))
			{
				if (*CachedBrush)
				{
					return *CachedBrush;
				}
			}
			return nullptr;
		};

		// Look for any icon overrides
		if(const FTypedElementIconOverrideColumn* IconOverrideColumn = DataStorage->GetColumn<FTypedElementIconOverrideColumn>(Row))
		{
			const FName IconName = IconOverrideColumn->IconName;

			if(const FSlateBrush* CachedBrush = FindCachedIcon(IconName))
			{
				return CachedBrush;
			}
			else if(const FSlateBrush* CustomBrush = FSlateIconFinder::FindIcon(IconName).GetOptionalIcon())
			{
				CachedIconMap.Add(IconName, CustomBrush);
				return CustomBrush;
			}
		}
		// Otherwise find the icon from the type information if available
		else if (const FTypedElementClassTypeInfoColumn* TypeInfoColumn = DataStorage->GetColumn<FTypedElementClassTypeInfoColumn>(Row))
		{
			if (const UClass* Type = TypeInfoColumn->TypeInfo.Get())
			{
				const FName IconName = Type->GetFName();

				if(const FSlateBrush* CachedBrush = FindCachedIcon(IconName))
				{
					return CachedBrush;
				}
				else if(const FSlateBrush* TypeBrush = FSlateIconFinder::FindIconBrushForClass(Type))
				{
					CachedIconMap.Add(IconName, TypeBrush);
					return TypeBrush;
				}
			}
		}
		
		// Fallback to the regular actor icon if we haven't found any specific icon
		return FSlateIconFinder::FindIconForClass(AActor::StaticClass()).GetOptionalIcon();;

	}
}

void UTypedElementTableViewerFactory::RegisterTables(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	const TableHandle BaseWidgetTable = DataStorage.FindTable(FName(TEXT("Editor_WidgetTable")));
	if (BaseWidgetTable != InvalidTableHandle)
	{
		DataStorage.RegisterTable(
			BaseWidgetTable,
			{
				FTypedElementRowReferenceColumn::StaticStruct()
			}, TableViewerUtils::TableViewerWidgetTableName);
	}
}
