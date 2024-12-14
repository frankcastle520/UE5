// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/SceneOutlinerTedsBridge.h"

#include "ActorTreeItem.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Columns/TypedElementTransformColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "ILevelEditor.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerColumn.h"
#include "ISceneOutlinerTreeItem.h"
#include "LevelEditor.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "TedsTableViewerColumn.h"
#include "TedsTableViewerUtils.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiTextCapability.h"
#include "TedsOutlinerItem.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerTedsBridge"

FAutoConsoleCommand BindColumnsToSceneOutlinerConsoleCommand(
	TEXT("TEDS.UI.BindColumnsToSceneOutliner"),
	TEXT("Bind one or more columns to the most recently used Scene Outliner. Several prebuild configurations are offered as well.")
	TEXT("An example input to show a label column is 'TEDS.UI.BindColumnsToSceneOutliner /Script/TypedElementFramework.TypedElementLabelColumn'."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			using namespace UE::Editor::DataStorage::Queries;
			
		    const FName WidgetPurposes[] = {TEXT("SceneOutliner.Cell"), TEXT("General.Cell")};

			if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
			{
				static UE::Editor::DataStorage::QueryHandle Queries[] =
				{
					DataStorage->RegisterQuery(Select().ReadWrite<FTypedElementLabelColumn>().Compile()),
					DataStorage->RegisterQuery(Select().ReadOnly<FTypedElementLocalTransformColumn>().Compile()),
					DataStorage->RegisterQuery(Select().ReadOnly<FTypedElementPackagePathColumn>().Compile()),
					DataStorage->RegisterQuery(
						Select()
							.ReadWrite<FTypedElementLabelColumn>()
							.ReadOnly<FTypedElementLocalTransformColumn>()
						.Compile()),
					DataStorage->RegisterQuery(
						Select()
							.ReadOnly<FTypedElementLabelColumn>()
							.ReadOnly<FTypedElementLabelHashColumn>()
						.Compile())
				};

				FSceneOutlinerTedsQueryBinder& Binder = FSceneOutlinerTedsQueryBinder::GetInstance();
				const TWeakPtr<ILevelEditor> LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetLevelEditorInstance();
				const TSharedPtr<ISceneOutliner> SceneOutliner = LevelEditor.IsValid() ? LevelEditor.Pin()->GetMostRecentlyUsedSceneOutliner() : nullptr;
				if (SceneOutliner.IsValid())
				{
					if (!Args.IsEmpty())
					{
						if (Args[0].IsNumeric())
						{
							int32 QueryIndex = FCString::Atoi(*Args[0]);
							if (QueryIndex < sizeof(Queries) / sizeof(UE::Editor::DataStorage::QueryHandle))
							{
								Binder.AssignQuery(Queries[QueryIndex], SceneOutliner, WidgetPurposes);
								return;
							}
						}
						else
						{
							uint32 AdditionCount = 0;
							Select Query;
							for (const FString& Arg : Args)
							{
								FTopLevelAssetPath Path;
								// TrySetPath has an ensure that checks if the path starts with an '/' and otherwise throws
								// an assert.
								if (!Arg.IsEmpty() && Arg[0] == '/' && Path.TrySetPath(Arg))
								{
									const UScriptStruct* ColumnType = TypeOptional(Path);
									if (ColumnType && ColumnType->IsChildOf(UE::Editor::DataStorage::FColumn::StaticStruct()))
									{
										Query.ReadOnly(ColumnType);
										++AdditionCount;
									}
								}
							}
							if (AdditionCount > 0)
							{
								static UE::Editor::DataStorage::QueryHandle CustomQuery = UE::Editor::DataStorage::InvalidQueryHandle;
								if (CustomQuery != UE::Editor::DataStorage::InvalidQueryHandle)
								{
									DataStorage->UnregisterQuery(CustomQuery);
								}
								CustomQuery = DataStorage->RegisterQuery(Query.Compile());
								Binder.AssignQuery(CustomQuery, SceneOutliner, WidgetPurposes);
								return;
							}
						}
					}
					Binder.AssignQuery(UE::Editor::DataStorage::InvalidQueryHandle, SceneOutliner, WidgetPurposes);
				}
			}
		}));

class FSceneOutlinerTedsBridge
{
public:
	~FSceneOutlinerTedsBridge();

	void Initialize(
		IEditorDataStorageProvider& InStorage,
		IEditorDataStorageUiProvider& InStorageUi,
		IEditorDataStorageCompatibilityProvider& InStorageCompatibility,
		const TSharedPtr<ISceneOutliner>& InOutliner);

	void AssignQuery(UE::Editor::DataStorage::QueryHandle Query, const TConstArrayView<FName> CellWidgetPurposes);
	void RegisterDealiaser(const FTreeItemIDDealiaser& InDealiaser);
	FTreeItemIDDealiaser GetDealiaser();
	
private:
	void ClearColumns(ISceneOutliner& InOutliner);

	TArray<FName> AddedColumns;
	TWeakPtr<ISceneOutliner> Outliner;
	IEditorDataStorageProvider* Storage{ nullptr };
	IEditorDataStorageUiProvider* StorageUi{ nullptr };
	IEditorDataStorageCompatibilityProvider* StorageCompatibility{ nullptr };
	FTreeItemIDDealiaser Dealiaser;
	TArray<FName> CellWidgetPurposes;
};

class FOutlinerColumn : public ISceneOutlinerColumn
{
public:
	FOutlinerColumn(
		UE::Editor::DataStorage::QueryHandle InQuery,
		IEditorDataStorageProvider& InStorage, 
		IEditorDataStorageUiProvider& InStorageUi, 
		IEditorDataStorageCompatibilityProvider& InStorageCompatibility,
		FName InNameId, 
		TArray<TWeakObjectPtr<const UScriptStruct>> InColumnTypes,
		TSharedPtr<FTypedElementWidgetConstructor> InHeaderWidgetConstructor,
		TSharedPtr<FTypedElementWidgetConstructor> InCellWidgetConstructor,
		FName InFallbackColumnName,
		TWeakPtr<ISceneOutliner> InOwningOutliner,
		const FTreeItemIDDealiaser& InDealiaser)
		: Storage(InStorage)
		, StorageUi(InStorageUi)
		, StorageCompatibility(InStorageCompatibility)
		, QueryHandle(InQuery)
		, NameId(InNameId)
		, OwningOutliner(InOwningOutliner)
		, Dealiaser(InDealiaser)
	{
		MetaData.AddOrSetMutableData(TEXT("Name"), NameId.ToString());

		using namespace UE::Editor::DataStorage;

		TableViewerColumnImpl = MakeUnique<FTedsTableViewerColumn>(NameId, InCellWidgetConstructor, InColumnTypes,
			InHeaderWidgetConstructor, FComboMetaDataView(FGenericMetaDataView(MetaData)).Next(FQueryMetaDataView(Storage.GetQueryDescription(QueryHandle))));

		TableViewerColumnImpl->SetIsRowVisibleDelegate(
		FTedsTableViewerColumn::FIsRowVisible::CreateRaw(this, &FOutlinerColumn::IsRowVisible)
		);
		
		// Try to find a fallback column from the regular item, for handling cases like folders which are not in TEDS but want to use TEDS columns
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		FallbackColumn = SceneOutlinerModule.FactoryColumn(InFallbackColumnName, *OwningOutliner.Pin());
	};
	
	FName GetColumnID() override
	{
		return NameId;
	}

	virtual void Tick(double InCurrentTime, float InDeltaTime)
	{
		TableViewerColumnImpl->Tick();
	}

	bool IsRowVisible(const UE::Editor::DataStorage::RowHandle InRowHandle) const
	{
		TSharedPtr<ISceneOutliner> OutlinerPinned = OwningOutliner.Pin();

		if(!OutlinerPinned)
		{
			return false;
		}
		
		// Try to grab the TEDS Outliner item from the row handle
		FSceneOutlinerTreeItemPtr Item = OutlinerPinned->GetTreeItem(InRowHandle);

		// If it doesn't exist, this could be a legacy item that uses something other than the row id as the ID, so check if we have a dealiaser
		if(!Item)
		{
			if(Dealiaser.IsBound())
			{
				Item = OutlinerPinned->GetTreeItem(Dealiaser.Execute(InRowHandle));
			}
		}

		if(!Item)
		{
			return false;
		}

		// Check if the item is visible in the tree
		return OutlinerPinned->GetTree().IsItemVisible(Item);
	}

	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override
	{
		return TableViewerColumnImpl->ConstructHeaderRowColumn();
	}

	// TODO: Sorting is currently handled through the fallback column if it exists because we have no way to sort columns through TEDS
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override
	{
		if(FallbackColumn)
		{
			FallbackColumn->SortItems(RootItems, SortMode);
		}
	}

	virtual bool SupportsSorting() const override
	{
		return FallbackColumn ? FallbackColumn->SupportsSorting() : false;
	}

	void SetHighlightText(SWidget& Widget)
	{
		TSharedPtr<ISceneOutliner> OutlinerPinned = OwningOutliner.Pin();

		if(!OutlinerPinned)
		{
			return;
		}

		if (TSharedPtr<ITypedElementUiTextCapability> TextCapability = Widget.GetMetaData<ITypedElementUiTextCapability>())
		{
			TextCapability->SetHighlightText(OutlinerPinned->GetFilterHighlightText());
		}
	
		if (FChildren* ChildWidgets = Widget.GetChildren())
		{
			ChildWidgets->ForEachWidget([this](SWidget& ChildWidget)
				{
					SetHighlightText(ChildWidget);
				});
		}
	}
	
	const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override
	{
		using namespace UE::Editor::DataStorage;
		using namespace UE::Editor::Outliner;
		
		RowHandle RowHandle = InvalidRowHandle;

		TSharedPtr<SWidget> RowWidget;

		if(const FTedsOutlinerTreeItem* TedsItem = TreeItem->CastTo<FTedsOutlinerTreeItem>())
		{
			RowHandle = TedsItem->GetRowHandle();
			
		}
		else if (const FActorTreeItem* ActorItem = TreeItem->CastTo<FActorTreeItem>())
		{
			if (AActor* Actor = ActorItem->Actor.Get())
			{
				RowHandle = StorageCompatibility.FindRowWithCompatibleObject(Actor);
			}
		}
		else if(FallbackColumn)
		{
			RowWidget = FallbackColumn->ConstructRowWidget(TreeItem, Row);
		}

		if(Storage.IsRowAssigned(RowHandle))
		{
			RowWidget = TableViewerColumnImpl->ConstructRowWidget(RowHandle);
		}

		if(RowWidget)
		{
			SetHighlightText(*RowWidget);
			return RowWidget.ToSharedRef();
		}

		return SNullWidget::NullWidget;
	}

	virtual void PopulateSearchStrings( const ISceneOutlinerTreeItem& Item, TArray< FString >& OutSearchStrings ) const override
	{
		// TODO: We don't currently have a way to convert TEDS widgets into searchable strings, but we can rely on the fallback column if it exists
		if(FallbackColumn)
		{
			FallbackColumn->PopulateSearchStrings(Item, OutSearchStrings);
		}
	}

	// The table viewer implementation that we internally use to create our widgets
	TUniquePtr<UE::Editor::DataStorage::FTedsTableViewerColumn> TableViewerColumnImpl;
	
	IEditorDataStorageProvider& Storage;
	IEditorDataStorageUiProvider& StorageUi;
	IEditorDataStorageCompatibilityProvider& StorageCompatibility;
	UE::Editor::DataStorage::QueryHandle QueryHandle;
	UE::Editor::DataStorage::FMetaData MetaData;
	FName NameId;
	TSharedPtr<ISceneOutlinerColumn> FallbackColumn;
	TWeakPtr<ISceneOutliner> OwningOutliner;
	FTreeItemIDDealiaser Dealiaser;
};




//
// USceneOutlinerTedsBridgeFactory
// 

void USceneOutlinerTedsBridgeFactory::RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const
{
	using PurposeType = IEditorDataStorageUiProvider::EPurposeType;

	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::HeaderWidgetPurpose, PurposeType::UniqueByNameAndColumn,
		LOCTEXT("HeaderWidgetPurpose", "Widgets for headers in any Scene Outliner for specific columns or column combinations."));
	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::DefaultHeaderWidgetPurpose, PurposeType::UniqueByName,
		LOCTEXT("DefaultHeaderWidgetPurpose", "The default widget to use in headers for the Scene Outliner."));
	
	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::CellWidgetPurpose, PurposeType::UniqueByNameAndColumn,
		LOCTEXT("CellWidgetPurpose", "Widgets for cells in any Scene Outliner for specific columns or column combinations."));
	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::DefaultCellWidgetPurpose, PurposeType::UniqueByName,
		LOCTEXT("DefaultCellWidgetPurpose", "The default widget to use in cells for the Scene Outliner."));

	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::ItemLabelCellWidgetPurpose, PurposeType::UniqueByNameAndColumn,
		LOCTEXT("ItemCellWidgetPurpose", "Widgets for cells in any Scene Outliner that are specific to the Item label column."));
	DataStorageUi.RegisterWidgetPurpose(FSceneOutlinerTedsQueryBinder::DefaultItemLabelCellWidgetPurpose, PurposeType::UniqueByName,
		LOCTEXT("DefaultItemCellWidgetPurpose", "The default widget to use in cells for the Scene Outliner specific to the Item label column."));


}



//
// FSceneOutlinerTedsQueryBinder
// 

const FName FSceneOutlinerTedsQueryBinder::CellWidgetTableName(TEXT("Editor_SceneOutlinerCellWidgetTable"));
const FName FSceneOutlinerTedsQueryBinder::HeaderWidgetPurpose(TEXT("SceneOutliner.Header"));
const FName FSceneOutlinerTedsQueryBinder::DefaultHeaderWidgetPurpose(TEXT("SceneOutliner.Header.Default"));
const FName FSceneOutlinerTedsQueryBinder::CellWidgetPurpose(TEXT("SceneOutliner.Cell"));
const FName FSceneOutlinerTedsQueryBinder::DefaultCellWidgetPurpose(TEXT("SceneOutliner.Cell.Default"));
const FName FSceneOutlinerTedsQueryBinder::ItemLabelCellWidgetPurpose(TEXT("SceneOutliner.RowLabel"));
const FName FSceneOutlinerTedsQueryBinder::DefaultItemLabelCellWidgetPurpose(TEXT("SceneOutliner.RowLabel.Default"));

FSceneOutlinerTedsQueryBinder::FSceneOutlinerTedsQueryBinder()
{
	using namespace UE::Editor::DataStorage;
	Storage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	StorageUi = GetMutableDataStorageFeature<IEditorDataStorageUiProvider>(UiFeatureName);
	StorageCompatibility = GetMutableDataStorageFeature<IEditorDataStorageCompatibilityProvider>(CompatibilityFeatureName);

	SetupDefaultColumnMapping();
}

void FSceneOutlinerTedsQueryBinder::SetupDefaultColumnMapping()
{
	// Map the type column from the TEDS to the default Outliner type column, so we can show type info for objects not in TEDS
	TEDSToOutlinerDefaultColumnMapping.Add(FTypedElementClassTypeInfoColumn::StaticStruct(), FSceneOutlinerBuiltInColumnTypes::ActorInfo());
}

FName FSceneOutlinerTedsQueryBinder::FindOutlinerColumnFromTEDSColumns(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> TEDSColumns) const
{
	// Currently, the algorithm naively looks through the mapping and returns the first match
	for(const TWeakObjectPtr<const UScriptStruct>& Column : TEDSColumns)
	{
		if(const FName* FoundDefaultColumn = TEDSToOutlinerDefaultColumnMapping.Find(Column))
		{
			return *FoundDefaultColumn;
		}
	}

	return FName();
}

FSceneOutlinerTedsQueryBinder& FSceneOutlinerTedsQueryBinder::GetInstance()
{
	static FSceneOutlinerTedsQueryBinder Binder;
	return Binder;
}

TSharedPtr<FSceneOutlinerTedsBridge>* FSceneOutlinerTedsQueryBinder::FindOrAddQueryMapping(const TSharedPtr<ISceneOutliner>& Outliner)
{
	TSharedPtr<FSceneOutlinerTedsBridge>* QueryMapping = SceneOutliners.Find(Outliner);
	if (QueryMapping == nullptr)
	{
		QueryMapping = &SceneOutliners.Add(Outliner, MakeShared<FSceneOutlinerTedsBridge>());
		(*QueryMapping)->Initialize(*Storage, *StorageUi, *StorageCompatibility, Outliner);
	}

	return QueryMapping;
}

TSharedPtr<FSceneOutlinerTedsBridge>* FSceneOutlinerTedsQueryBinder::FindQueryMapping(const TSharedPtr<ISceneOutliner>& Outliner)
{
	return SceneOutliners.Find(Outliner);
}

void FSceneOutlinerTedsQueryBinder::AssignQuery(UE::Editor::DataStorage::QueryHandle Query, const TSharedPtr<ISceneOutliner>& Outliner, TConstArrayView<FName> CellWidgetPurposes)
{
	CleanupStaleOutliners();

	TSharedPtr<FSceneOutlinerTedsBridge>* QueryMapping = FindOrAddQueryMapping(Outliner);
	(*QueryMapping)->AssignQuery(Query, CellWidgetPurposes);
}

void FSceneOutlinerTedsQueryBinder::RegisterTreeItemIDDealiaser(const TSharedPtr<ISceneOutliner>& Outliner, const FTreeItemIDDealiaser& InDealiaser)
{
	TSharedPtr<FSceneOutlinerTedsBridge>* QueryMapping = FindOrAddQueryMapping(Outliner);
	(*QueryMapping)->RegisterDealiaser(InDealiaser);
}

FTreeItemIDDealiaser FSceneOutlinerTedsQueryBinder::GetTreeItemIDDealiaser(const TSharedPtr<ISceneOutliner>& Widget)
{
	TSharedPtr<FSceneOutlinerTedsBridge>* QueryMapping = FindQueryMapping(Widget);

	if(QueryMapping)
	{
		return (*QueryMapping)->GetDealiaser();
	}

	return FTreeItemIDDealiaser();
}

void FSceneOutlinerTedsQueryBinder::CleanupStaleOutliners()
{
	for (TMap<TWeakPtr<ISceneOutliner>, TSharedPtr<FSceneOutlinerTedsBridge>>::TIterator It(SceneOutliners); It; ++It)
	{
		// Remove any query mappings where the target Outliner doesn't exist anymore
		if(!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

//
// FSceneOutlinerTedsBridge
//

FSceneOutlinerTedsBridge::~FSceneOutlinerTedsBridge()
{
	TSharedPtr<ISceneOutliner> OutlinerPinned = Outliner.Pin();
	if (OutlinerPinned)
	{
		ClearColumns(*OutlinerPinned);
	}
}

void FSceneOutlinerTedsBridge::Initialize(
	IEditorDataStorageProvider& InStorage,
	IEditorDataStorageUiProvider& InStorageUi,
	IEditorDataStorageCompatibilityProvider& InStorageCompatibility,
	const TSharedPtr<ISceneOutliner>& InOutliner)
{
	Storage = &InStorage;
	StorageUi = &InStorageUi;
	StorageCompatibility = &InStorageCompatibility;
	Outliner = InOutliner;
}

void FSceneOutlinerTedsBridge::RegisterDealiaser(const FTreeItemIDDealiaser& InDealiaser)
{
	Dealiaser = InDealiaser;
}

FTreeItemIDDealiaser FSceneOutlinerTedsBridge::GetDealiaser()
{
	return Dealiaser;
}


void FSceneOutlinerTedsBridge::AssignQuery(UE::Editor::DataStorage::QueryHandle Query, const TConstArrayView<FName> InCellWidgetPurposes)
{
	using MatchApproach = IEditorDataStorageUiProvider::EMatchApproach;
	constexpr int32 DefaultPriorityIndex = 100;
	FSceneOutlinerTedsQueryBinder& Binder = FSceneOutlinerTedsQueryBinder::GetInstance();
	CellWidgetPurposes = InCellWidgetPurposes;


	if (TSharedPtr<ISceneOutliner> OutlinerPinned = Outliner.Pin())
	{
		const IEditorDataStorageProvider::FQueryDescription& Description = Storage->GetQueryDescription(Query);
		UE::Editor::DataStorage::FQueryMetaDataView MetaDataView(Description);

		ClearColumns(*OutlinerPinned);

		if (Description.Action == IEditorDataStorageProvider::FQueryDescription::EActionType::Select)
		{
			int32 SelectionCount = Description.SelectionTypes.Num();
			AddedColumns.Reset(SelectionCount);

			TArray<TWeakObjectPtr<const UScriptStruct>> ColumnTypes = UE::Editor::DataStorage::TableViewerUtils::CreateVerifiedColumnTypeArray(Description.SelectionTypes);

			int32 IndexOffset = 0;
			auto ColumnConstructor = [this, Query, MetaDataView, &IndexOffset, &OutlinerPinned, Binder](
				TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes)
				{
					TSharedPtr<FTypedElementWidgetConstructor> CellConstructor(Constructor.Release());

					/* If we have a fallback column for this query, remove it, take over it's priority and 
					 * replace it with the TEDS column. But also allow the TEDS-Outliner column to fallback to it for
					 * data not in TEDS yet.
				 	 */
					FName FallbackColumn = Binder.FindOutlinerColumnFromTEDSColumns(ColumnTypes);
					const FSceneOutlinerColumnInfo* FallbackColumnInfo = OutlinerPinned->GetSharedData().ColumnMap.Find(FallbackColumn);
					int32 ColumnPriority = FallbackColumnInfo ? FallbackColumnInfo->PriorityIndex : DefaultPriorityIndex + IndexOffset;

					OutlinerPinned->RemoveColumn(FallbackColumn);

					FName NameId = UE::Editor::DataStorage::TableViewerUtils::FindLongestMatchingName(ColumnTypes, IndexOffset);
					AddedColumns.Add(NameId);
					OutlinerPinned->AddColumn(NameId,
						FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, ColumnPriority,
							FCreateSceneOutlinerColumn::CreateLambda(
								[this, Query, MetaDataView, NameId, &ColumnTypes, CellConstructor, &OutlinerPinned, FallbackColumn](ISceneOutliner&)
								{
									TSharedPtr<FTypedElementWidgetConstructor> HeaderConstructor = 
										UE::Editor::DataStorage::TableViewerUtils::CreateHeaderWidgetConstructor(*StorageUi, MetaDataView, ColumnTypes, CellWidgetPurposes);
									return MakeShared<FOutlinerColumn>(
										Query, *Storage, *StorageUi, *StorageCompatibility, NameId,
										TArray<TWeakObjectPtr<const UScriptStruct>>(ColumnTypes.GetData(), ColumnTypes.Num()), 
										MoveTemp(HeaderConstructor), CellConstructor, FallbackColumn, OutlinerPinned, Dealiaser);

								})
						)
					);
					++IndexOffset;
					return true;
				};

			for(const FName& WidgetPurpose : CellWidgetPurposes)
			{
				StorageUi->CreateWidgetConstructors(WidgetPurpose, MatchApproach::LongestMatch, ColumnTypes, 
				MetaDataView, ColumnConstructor);
			}

			for (TWeakObjectPtr<const UScriptStruct>& ColumnType : ColumnTypes)
			{
				FName FallbackColumn = Binder.FindOutlinerColumnFromTEDSColumns({ColumnType});

				auto AssignWidgetToColumn = [this, Query, MetaDataView, ColumnType, &IndexOffset, &OutlinerPinned, FallbackColumn](
					TUniquePtr<FTypedElementWidgetConstructor> Constructor, TConstArrayView<TWeakObjectPtr<const UScriptStruct>>)
				{
					TSharedPtr<FTypedElementWidgetConstructor> CellConstructor(Constructor.Release());
					FName NameId = FName(ColumnType->GetDisplayNameText().ToString());
					AddedColumns.Add(NameId);
					OutlinerPinned->AddColumn(NameId,
						FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, DefaultPriorityIndex + IndexOffset,
							FCreateSceneOutlinerColumn::CreateLambda(
								[this, Query, MetaDataView, NameId, ColumnType, CellConstructor, &OutlinerPinned, FallbackColumn](ISceneOutliner&)
								{
									TArray<TWeakObjectPtr<const UScriptStruct>> ColumnTypesStored;
									ColumnTypesStored.Add(ColumnType);
									TSharedPtr<FTypedElementWidgetConstructor> HeaderConstructor =
										UE::Editor::DataStorage::TableViewerUtils::CreateHeaderWidgetConstructor(*StorageUi, MetaDataView, { ColumnType }, CellWidgetPurposes);
									return MakeShared<FOutlinerColumn>(
										Query, *Storage, *StorageUi, *StorageCompatibility, NameId, MoveTemp(ColumnTypesStored),
										HeaderConstructor, CellConstructor, FallbackColumn, OutlinerPinned, Dealiaser);

								})
						)
					);
					++IndexOffset;
					return false;
				};

				const int32 BeforeIndexOffset = IndexOffset;
				for(const FName& WidgetPurpose : CellWidgetPurposes)
				{
					const FName DefaultWidgetPurpose(WidgetPurpose.ToString() + TEXT(".Default"));

					StorageUi->CreateWidgetConstructors(DefaultWidgetPurpose, MetaDataView, AssignWidgetToColumn);
					
					if (BeforeIndexOffset != IndexOffset)
					{
						break;
					}
				}

				if (BeforeIndexOffset == IndexOffset)
				{
					++IndexOffset;
				}
			}
		}
	}
}

void FSceneOutlinerTedsBridge::ClearColumns(ISceneOutliner& InOutliner)
{
	for (FName ColumnName : AddedColumns)
	{
		InOutliner.RemoveColumn(ColumnName);
	}
}

#undef LOCTEXT_NAMESPACE
