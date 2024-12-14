// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/UnrealString.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "HAL/IConsoleManager.h"
#include "TedsAssetDataColumns.h"
#include "UObject/NameTypes.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"


DEFINE_LOG_CATEGORY_STATIC(LogTEDSAssetRegistry, Log, All)

namespace UE::Editor::DataStorage::Debug::Private
{
	static FAutoConsoleCommand CCMDTestFolderRowData(
		TEXT("TEDS.Debug.ShowDataOfAssetFolder"),
		TEXT("Print some debug information on the specified path."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
			{
				const IEditorDataStorageProvider* Database = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
				IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

				for (const FString& Path : InArgs)
				{
					FName PathAsName(*Path);
					RowHandle Row = Database->FindIndexedRow(GenerateIndexHash(PathAsName));

					if (Database->IsRowAssigned(Row))
					{
						UE_LOG(LogTEDSAssetRegistry, Display, TEXT("The path isn't indexed."));
						return;
					}


					UE_LOG(LogTEDSAssetRegistry, Display, TEXT("Found some information for the path (%s) in the database."), *Path);

					if (const FAssetPathColumn_Experimental* AssetPath = Database->GetColumn<FAssetPathColumn_Experimental>(Row))
					{
						UE_LOG(LogTEDSAssetRegistry, Display, TEXT("Path stored in the database as (%s)."), *AssetPath->Path.ToString());
					}

					if (const FParentAssetPathColumn_Experimental* ParentAssetPath = Database->GetColumn<FParentAssetPathColumn_Experimental>(Row))
					{
						if (const FAssetPathColumn_Experimental* AssetPath = Database->GetColumn<FAssetPathColumn_Experimental>(ParentAssetPath->ParentRow))
						{
							UE_LOG(LogTEDSAssetRegistry, Display, TEXT("	Parent Path: %s"), *AssetPath->Path.ToString());
						}
					}


					if (const FChildrenAssetPathColumn_Experimental* ChildrenPath = Database->GetColumn<FChildrenAssetPathColumn_Experimental>(Row))
					{
						UE_LOG(LogTEDSAssetRegistry, Display, TEXT("	Path as %i children"), ChildrenPath->ChildrenRows.Num());

						for (RowHandle ChildRow : ChildrenPath->ChildrenRows)
						{
							if (const FAssetPathColumn_Experimental* AssetPath = Database->GetColumn<FAssetPathColumn_Experimental>(Row))
							{
								UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Children Path: %s"), *AssetPath->Path.ToString());
							}
						}
					}


					if (const FAssetsInPathColumn_Experimental* AssetInPath = Database->GetColumn<FAssetsInPathColumn_Experimental>(Row))
					{
						UE_LOG(LogTEDSAssetRegistry, Display, TEXT("	Asset in Paths"));

						for (RowHandle AssetRow : AssetInPath->AssetsRow)
						{
							if (const FAssetDataColumn_Experimental* AssetData = Database->GetColumn<FAssetDataColumn_Experimental>(AssetRow))
							{
								UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Asset Name: %s"), *AssetData->AssetData.AssetName.ToString());
							}
							else
							{
								UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Asset Row pointed to stale asset."));
							}
						}
					}


					// Check for asset that haven't processed their path asset in path column yet
					TArray<FAssetData> Assets;
					AssetRegistry.GetAssetsByPath(PathAsName, Assets);
					for (const FAssetData& Asset : Assets)
					{
						RowHandle AssetRow = Database->FindIndexedRow(GenerateIndexHash(Asset.GetSoftObjectPath()));

						if (const FUnresolvedAssetsInPathColumn_Experimental* UnresolvedAssetsInPathColumn = Database->GetColumn<FUnresolvedAssetsInPathColumn_Experimental>(AssetRow))
						{
								UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Unresolved asset in path asset. Asset Name: %s"), *Asset.AssetName.ToString());
						}
					}
				}
			}));


	static FAutoConsoleCommand CCMDTestFolderAssetRegistryData(
		TEXT("TEDS.Debug.ShowAssetRegistryDataOfFolder"),
		TEXT("Print some debug information on the specified path."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& InArgs)
			{
				IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

				FNameBuilder NameBuilder;
				for (const FString& Path : InArgs)
				{
					UE_LOG(LogTEDSAssetRegistry, Display, TEXT("Displaying asset registry info on path (%s)"), *Path);
					const FName PathAsName(*Path);
					AssetRegistry.EnumerateSubPaths(PathAsName, [&NameBuilder](FName InPath)
						{
							InPath.AppendString(NameBuilder);
							UE_LOG(LogTEDSAssetRegistry, Display, TEXT("	Children Path: %s"), *NameBuilder);
							NameBuilder.Reset();
							return true;
						}, false);


					TArray<FAssetData> Assets;
					AssetRegistry.GetAssetsByPath(PathAsName, Assets);

					if (!Assets.IsEmpty())
					{ 
						UE_LOG(LogTEDSAssetRegistry, Display, TEXT("	Asset in Path"));

						for (const FAssetData& Asset : Assets)
						{
							Asset.AssetName.AppendString(NameBuilder);
							UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Asset Name: %s"), *NameBuilder);
							NameBuilder.Reset();

							Asset.GetFullName(NameBuilder);
							UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Asset Full Name: %s"), *NameBuilder);
							NameBuilder.Reset();

							Asset.PackageName.AppendString(NameBuilder);
							UE_LOG(LogTEDSAssetRegistry, Display, TEXT("		Asset Reported Package Path: %s"), *NameBuilder);
							NameBuilder.Reset();
						}
					}
				}
			}));
	
	// A small set of classes to randomly pick from
	static TArray<UClass*> AssetClasses{ UStaticMesh::StaticClass(), UMaterial::StaticClass(),
		ULevel::StaticClass(), UBlueprint::StaticClass(), UTexture::StaticClass() };

	// Populate an asset row with random information
	void PopulateRowWithRandomInfo(RowHandle Row, IEditorDataStorageProvider* DataStorage)
	{
		// Don't modify any rows that aren't our placeholder assets
		if (!DataStorage->HasColumns<FAssetTag>(Row))
		{
			return;
		}

		// Pick a random asset class from our list
		UClass* AssetClass = AssetClasses[FMath::RandRange(0, AssetClasses.Num() - 1)];

		// Add a label to the row
		if (FTypedElementLabelColumn* LabelColumn = DataStorage->GetColumn<FTypedElementLabelColumn>(Row))
		{
			// We can have duplicate names but it doesn't really matter
			LabelColumn->Label = AssetClass->GetFName().ToString();
			LabelColumn->Label.Append(TEXT("_Placeholder_"));
			LabelColumn->Label.Append(FString::FromInt(FMath::RandRange(0, 1000)));

			if (FVersePathColumn* ClassTypeInfoColumn = DataStorage->GetColumn<FVersePathColumn>(Row))
			{
				FString TestVerseModule = TEXT("/UnrealEngine.com/Temporary/TEDS/");
				TestVerseModule.Append(LabelColumn->Label);
			
				UE::Core::FVersePath::TryMake(ClassTypeInfoColumn->VersePath, TestVerseModule);
			}

			if (FAssetPathColumn_Experimental* AssetPathColumn = DataStorage->GetColumn<FAssetPathColumn_Experimental>(Row))
			{
				FString TestAssetPath = TEXT("TestPath/TestDirectory/");
				TestAssetPath.Append(LabelColumn->Label);
				
				AssetPathColumn->Path = FName(TestAssetPath);
			}
		}
		

		if (FTypedElementClassTypeInfoColumn* ClassTypeInfoColumn = DataStorage->GetColumn<FTypedElementClassTypeInfoColumn>(Row))
		{
			ClassTypeInfoColumn->TypeInfo = AssetClass;
		}
		
		if (FDiskSizeColumn* ClassTypeInfoColumn = DataStorage->GetColumn<FDiskSizeColumn>(Row))
		{
			ClassTypeInfoColumn->DiskSize = FMath::RandRange(1024, 32768);
		}

		// Randomly make this a public or a private asset
		DataStorage->AddColumn(Row, FMath::RandRange(0, 1) == 0 ? FPrivateAssetTag::StaticStruct() : FPublicAssetTag::StaticStruct());
	}
	
	FAutoConsoleCommand CreateDebugAssetRowsCommand(
		TEXT("TEDS.Debug.CreateDebugAssetRows"),
		TEXT("Create random asset rows. Args: (TEDS.Debug.CreateDebugAssetRows NumRows) Default 10 rows"),
		FConsoleCommandWithArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.CreateDebugAssetRows);


				IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

				if (!DataStorage)
				{
					return;
				}

				int RowCount = 10; // Create 10 rows by default

				if (Args.Num() == 1)
				{
					RowCount = FCString::Atoi(*Args[0]);
				}

				TableHandle Table = DataStorage->FindTable(FName("Editor_PlaceholderAssetTable"));

				DataStorage->BatchAddRow(Table, RowCount, [DataStorage](RowHandle Row)
				{
					PopulateRowWithRandomInfo(Row, DataStorage);
				});
				
			}
		));

	FAutoConsoleCommand RemoveAssetRowsCommand(
		TEXT("TEDS.Debug.RemoveAssetRows"),
		TEXT("Remove All Asset Rows"),
		FConsoleCommandWithArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args)
			{
				using namespace UE::Editor::DataStorage::Queries;

				IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

				if (!DataStorage)
				{
					return;
				}

				// Select all rows with FAssetTag
				static QueryHandle AssetQueryHandle = DataStorage->RegisterQuery(Select().Where().All<FAssetTag>().Compile());

				TArray<RowHandle> Rows;
				
				DataStorage->RunQuery(AssetQueryHandle,
					CreateDirectQueryCallbackBinding([&Rows](const IEditorDataStorageProvider::IDirectQueryContext& Context)
					{
						Rows.Append(Context.GetRowHandles());
					}));

				for (const RowHandle Row : Rows)
				{
					DataStorage->RemoveRow(Row);
				}
			}
		));
} // namespace UE::Editor::DataStorage::Debug::Private
