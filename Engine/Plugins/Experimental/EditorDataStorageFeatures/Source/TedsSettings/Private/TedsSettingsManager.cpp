// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsSettingsManager.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "ISettingsCategory.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TedsSettingsColumns.h"
#include "TedsSettingsLog.h"
#include "UObject/UnrealType.h"

namespace UE::Editor::Settings::Private
{
	static UE::Editor::DataStorage::IndexHash GenerateIndexHash(const FName& ContainerName, const FName& CategoryName, const FName& SectionName)
	{
		constexpr static const char SeedName[] = "ISettingsSection";
		static uint64 Seed = CityHash64(SeedName, sizeof(SeedName) - 1);

		uint64 Hash = CityHash128to64({ Seed, UE::Editor::DataStorage::GenerateIndexHash(ContainerName) });
		Hash = CityHash128to64({ Hash, UE::Editor::DataStorage::GenerateIndexHash(CategoryName) });
		return CityHash128to64({ Hash, UE::Editor::DataStorage::GenerateIndexHash(SectionName) });
	}
}

FTedsSettingsManager::FTedsSettingsManager()
	: bIsInitialized{ false }
	, SelectAllSettingsQuery{ UE::Editor::DataStorage::InvalidQueryHandle }
	, SettingsContainerTable{ UE::Editor::DataStorage::InvalidTableHandle }
	, SettingsCategoryTable{ UE::Editor::DataStorage::InvalidTableHandle }
{
}

void FTedsSettingsManager::Initialize()
{
	if (!bIsInitialized)
	{
		using namespace UE::Editor::DataStorage;
		auto OnDataStorage = [this]
			{
				IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
				check(DataStorage);

				RegisterTables(*DataStorage);
				RegisterQueries(*DataStorage);
				RegisterSettings();
			};

		if (AreEditorDataStorageFeaturesEnabled())
		{
			OnDataStorage();
		}
		else
		{
			OnEditorDataStorageFeaturesEnabled().AddSPLambda(this, OnDataStorage);
		}

		bIsInitialized = true;
	}
}

void FTedsSettingsManager::Shutdown()
{
	if (bIsInitialized)
	{
		using namespace UE::Editor::DataStorage;
		OnEditorDataStorageFeaturesEnabled().RemoveAll(this);

		if (AreEditorDataStorageFeaturesEnabled())
		{
			IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
			check(DataStorage);

			UnregisterSettings();
			UnregisterQueries(*DataStorage);
		}

		bIsInitialized = false;
	}
}

void FTedsSettingsManager::RegisterTables(IEditorDataStorageProvider& DataStorage)
{
	if (SettingsContainerTable == UE::Editor::DataStorage::InvalidTableHandle)
	{
		SettingsContainerTable = DataStorage.RegisterTable(
			TTypedElementColumnTypeList<FNameColumn, FDisplayNameColumn, FDescriptionColumn, FSettingsContainerTag>(),
			FName(TEXT("Editor_SettingsContainerTable")));
	}

	if (SettingsCategoryTable == UE::Editor::DataStorage::InvalidTableHandle)
	{
		SettingsCategoryTable = DataStorage.RegisterTable(
			TTypedElementColumnTypeList<FSettingsContainerReferenceColumn, FNameColumn, FDisplayNameColumn, FDescriptionColumn, FSettingsCategoryTag>(),
			FName(TEXT("Editor_SettingsCategoryTable")));
	}
}

void FTedsSettingsManager::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	if (SelectAllSettingsQuery == UE::Editor::DataStorage::InvalidQueryHandle)
	{
		SelectAllSettingsQuery = DataStorage.RegisterQuery(
			Select()
				.ReadOnly<FSettingsContainerReferenceColumn, FSettingsCategoryReferenceColumn, FNameColumn>()
			.Where()
				.All<FSettingsSectionTag>()
			.Compile());
	}
}

void FTedsSettingsManager::UnregisterQueries(IEditorDataStorageProvider& DataStorage)
{
	DataStorage.UnregisterQuery(SelectAllSettingsQuery);
	SelectAllSettingsQuery = UE::Editor::DataStorage::InvalidQueryHandle;
}

void FTedsSettingsManager::RegisterSettings()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TedsSettingsManager.RegisterSettings);

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	check(SettingsModule);

	TArray<FName> ContainerNames;
	SettingsModule->GetContainerNames(ContainerNames);

	for (FName ContainerName : ContainerNames)
	{
		RegisterSettingsContainer(ContainerName);
	}

	SettingsModule->OnContainerAdded().AddSP(this, &FTedsSettingsManager::RegisterSettingsContainer);
}

void FTedsSettingsManager::RegisterSettingsContainer(const FName& ContainerName)
{
	using namespace UE::Editor::DataStorage;

	TRACE_CPUPROFILER_EVENT_SCOPE(TedsSettingsManager.RegisterSettingsContainer);

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	check(SettingsModule);

	IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	check(DataStorage);

	UE_LOG(LogTedsSettings, Log, TEXT("Register Settings Container : '%s'"), *ContainerName.ToString());

	ISettingsContainerPtr ContainerPtr = SettingsModule->GetContainer(ContainerName);

	uint64 ContainerIndexHash = GenerateIndexHash(ContainerPtr.Get());
	RowHandle ContainerRow = DataStorage->FindIndexedRow(ContainerIndexHash);
	if (ContainerRow == InvalidRowHandle)
	{
		ContainerRow = DataStorage->AddRow(SettingsContainerTable);
		DataStorage->AddColumn<FNameColumn>(ContainerRow, { .Name = ContainerName });
		DataStorage->AddColumn<FDisplayNameColumn>(ContainerRow, { .DisplayName = ContainerPtr->GetDisplayName() });
		DataStorage->AddColumn<FDescriptionColumn>(ContainerRow, { .Description = ContainerPtr->GetDescription() });
		DataStorage->AddColumn<FSettingsContainerTag>(ContainerRow);

		DataStorage->IndexRow(ContainerIndexHash, ContainerRow);
	}

	TArray<ISettingsCategoryPtr> Categories;
	ContainerPtr->GetCategories(Categories);

	for (ISettingsCategoryPtr CategoryPtr : Categories)
	{
		const bool bQueryExistingRows = false;
		UpdateSettingsCategory(CategoryPtr, ContainerRow, bQueryExistingRows);
	}

	// OnCategoryModified is called at the same time as OnSectionRemoved so we only bind to OnCategoryModified for add / update / remove
	ContainerPtr->OnCategoryModified().AddSPLambda(this, [this, ContainerPtr, ContainerRow](const FName& ModifiedCategoryName)
		{
			UE_LOG(LogTedsSettings, Log, TEXT("Settings Category modified : '%s->%s'"), *ContainerPtr->GetName().ToString(), *ModifiedCategoryName.ToString());

			ISettingsCategoryPtr CategoryPtr = ContainerPtr->GetCategory(ModifiedCategoryName);

			UpdateSettingsCategory(CategoryPtr, ContainerRow);
		});
}

void FTedsSettingsManager::UnregisterSettings()
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::Settings::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(TedsSettingsManager.UnregisterSettings);

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	check(SettingsModule);

	IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	check(DataStorage);

	IEditorDataStorageCompatibilityProvider* DataStorageCompatibility = GetMutableDataStorageFeature<IEditorDataStorageCompatibilityProvider>(CompatibilityFeatureName);
	check(DataStorageCompatibility);

	SettingsModule->OnContainerAdded().RemoveAll(this);

	TArray<FName> ContainerNames;
	SettingsModule->GetContainerNames(ContainerNames);

	for (FName ContainerName : ContainerNames)
	{
		UE_LOG(LogTedsSettings, Log, TEXT("Unregister Settings Container : '%s'"), *ContainerName.ToString());

		ISettingsContainerPtr ContainerPtr = SettingsModule->GetContainer(ContainerName);

		ContainerPtr->OnCategoryModified().RemoveAll(this);

		TArray<ISettingsCategoryPtr> Categories;
		ContainerPtr->GetCategories(Categories);

		for (ISettingsCategoryPtr CategoryPtr : Categories)
		{
			UE_LOG(LogTedsSettings, Log, TEXT("Unregister Settings Category : '%s'"), *CategoryPtr->GetName().ToString());

			TArray<ISettingsSectionPtr> Sections;
			const bool bIgnoreVisibility = true;
			CategoryPtr->GetSections(Sections, bIgnoreVisibility);

			for (ISettingsSectionPtr SectionPtr : Sections)
			{
				if (TStrongObjectPtr<UObject> SettingsObjectPtr = SectionPtr->GetSettingsObject().Pin(); SettingsObjectPtr)
				{
					DataStorageCompatibility->RemoveCompatibleObject(SettingsObjectPtr);

					DataStorage->RemoveIndex(GenerateIndexHash(ContainerName, CategoryPtr->GetName(), SectionPtr->GetName()));

					UE_LOG(LogTedsSettings, Log, TEXT("Removed Settings Section : '%s'"), *SectionPtr->GetName().ToString());
				}
			}

			uint64 CategoryIndexHash = GenerateIndexHash(CategoryPtr.Get());
			RowHandle CategoryRow = DataStorage->FindIndexedRow(CategoryIndexHash);
			if (CategoryRow != InvalidRowHandle)
			{
				DataStorage->RemoveRow(CategoryRow);
				DataStorage->RemoveIndex(CategoryIndexHash);
			}
		}

		uint64 ContainerIndexHash = GenerateIndexHash(ContainerPtr.Get());
		RowHandle ContainerRow = DataStorage->FindIndexedRow(ContainerIndexHash);
		if (ContainerRow != InvalidRowHandle)
		{
			DataStorage->RemoveRow(ContainerRow);
			DataStorage->RemoveIndex(ContainerIndexHash);
		}
	}
}

void FTedsSettingsManager::UpdateSettingsCategory(TSharedPtr<ISettingsCategory> SettingsCategory, UE::Editor::DataStorage::RowHandle ContainerRow, const bool bQueryExistingRows)
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::Settings::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(TedsSettingsManager.UpdateSettingsCategory);

	IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	check(DataStorage);

	IEditorDataStorageCompatibilityProvider* DataStorageCompatibility = GetMutableDataStorageFeature<IEditorDataStorageCompatibilityProvider>(CompatibilityFeatureName);
	check(DataStorageCompatibility);

	const FName& ContainerName = DataStorage->GetColumn<FNameColumn>(ContainerRow)->Name;
	const FName& CategoryName = SettingsCategory->GetName();

	UE_LOG(LogTedsSettings, Log, TEXT("Update Settings Category: '%s->%s'"), *ContainerName.ToString(), *CategoryName.ToString());

	uint64 CategoryIndexHash = GenerateIndexHash(SettingsCategory.Get());

	RowHandle CategoryRow = DataStorage->FindIndexedRow(CategoryIndexHash);
	if (CategoryRow == InvalidRowHandle)
	{
		CategoryRow = DataStorage->AddRow(SettingsCategoryTable);

		DataStorage->AddColumn<FSettingsContainerReferenceColumn>(CategoryRow, { .ContainerName = ContainerName, .ContainerRow = ContainerRow });
		DataStorage->AddColumn<FNameColumn>(CategoryRow, { .Name = CategoryName });
		DataStorage->AddColumn<FDisplayNameColumn>(CategoryRow, { .DisplayName = SettingsCategory->GetDisplayName() });
		DataStorage->AddColumn<FDescriptionColumn>(CategoryRow, { .Description = SettingsCategory->GetDescription() });
		DataStorage->AddColumn<FSettingsCategoryTag>(CategoryRow);

		DataStorage->IndexRow(CategoryIndexHash, CategoryRow);
	}

	TArray<RowHandle> OldRowHandles;
	TArray<FName> OldSectionNames;

	// Gather all existing rows for the given { ContainerName, CategoryName } pair.
	if (bQueryExistingRows)
	{
		using namespace UE::Editor::DataStorage::Queries;

		DataStorage->RunQuery(SelectAllSettingsQuery, CreateDirectQueryCallbackBinding(
			[&OldRowHandles, &OldSectionNames, &ContainerName, &CategoryName](
				IDirectQueryContext& Context,
				const FSettingsContainerReferenceColumn* ContainerColumns,
				const FSettingsCategoryReferenceColumn* CategoryColumns,
				const FNameColumn* SectionNameColumns)
			{
				const uint32 RowCount = Context.GetRowCount();

				for (uint32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
				{
					const FName& TempContainerName = ContainerColumns[RowIndex].ContainerName;
					const FName& TempCategoryName = CategoryColumns[RowIndex].CategoryName;
					if (TempContainerName == ContainerName &&
						TempCategoryName == CategoryName)
					{
						OldRowHandles.Emplace(Context.GetRowHandles()[RowIndex]);
						OldSectionNames.Emplace(SectionNameColumns[RowIndex].Name);
					}
				}
			}));
	}

	TArray<FName> NewSectionNames;
	TArray<ISettingsSectionPtr> NewSections;

	const bool bIgnoreVisibility = true;
	SettingsCategory->GetSections(NewSections, bIgnoreVisibility);

	// Iterate the category and add rows for all sections ( replace any existing row for the section as its object may have changed )
	for (ISettingsSectionPtr SectionPtr : NewSections)
	{
		const FName& SectionName = SectionPtr->GetName();

		if (TStrongObjectPtr<UObject> SettingsObjectPtr = SectionPtr->GetSettingsObject().Pin(); SettingsObjectPtr)
		{
			NewSectionNames.Emplace(SectionName);

			uint64 SectionIndexHash = GenerateIndexHash(ContainerName, CategoryName, SectionName);
			
			RowHandle OldSectionRow = DataStorage->FindIndexedRow(SectionIndexHash);
			if (OldSectionRow != InvalidRowHandle)
			{
				UE_LOG(LogTedsSettings, Verbose, TEXT("Settings Section : '%s' is already in data storage"), *SectionName.ToString());

				// Remove the row, the SettingsObjectPtr may have changed so we need to re-add the row with the new object
				DataStorage->RemoveRow(OldSectionRow);

				UE_LOG(LogTedsSettings, Log, TEXT("Removed Settings Section : '%s'"), *SectionName.ToString());
			}

			RowHandle NewSectionRow = DataStorageCompatibility->AddCompatibleObject(SettingsObjectPtr);

			DataStorage->AddColumn<FSettingsSectionTag>(NewSectionRow);
			DataStorage->AddColumn<FSettingsContainerReferenceColumn>(NewSectionRow, { .ContainerName = ContainerName, .ContainerRow = ContainerRow });
			DataStorage->AddColumn<FSettingsCategoryReferenceColumn>(NewSectionRow, { .CategoryName = CategoryName, .CategoryRow = CategoryRow });
			DataStorage->AddColumn<FNameColumn>(NewSectionRow, { .Name = SectionName });
			DataStorage->AddColumn<FDisplayNameColumn>(NewSectionRow, { .DisplayName = SectionPtr->GetDisplayName() });
			DataStorage->AddColumn<FDescriptionColumn>(NewSectionRow, { .Description = SectionPtr->GetDescription() });
			
			DataStorage->IndexRow(SectionIndexHash, NewSectionRow);

			UE_LOG(LogTedsSettings, Log, TEXT("Added Settings Section : '%s'"), *SectionName.ToString());
		}
	}

	// Iterate the old sections and remove rows not in the new sections list.
	for (int32 RowIndex = 0; RowIndex < OldSectionNames.Num(); ++RowIndex)
	{
		const FName& OldSectionName = OldSectionNames[RowIndex];

		if (NewSectionNames.Contains(OldSectionName))
		{
			continue;
		}

		RowHandle OldRowHandle = OldRowHandles[RowIndex];
		check(OldRowHandle != InvalidRowHandle);

		DataStorage->RemoveRow(OldRowHandle);

		DataStorage->RemoveIndex(GenerateIndexHash(ContainerName, CategoryName, OldSectionName));

		UE_LOG(LogTedsSettings, Log, TEXT("Removed Settings Section : '%s'"), *OldSectionName.ToString());
	}
}
