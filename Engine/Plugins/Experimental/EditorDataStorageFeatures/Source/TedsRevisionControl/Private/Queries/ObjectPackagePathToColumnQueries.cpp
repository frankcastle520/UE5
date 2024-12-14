// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/ObjectPackagePathToColumnQueries.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace UE::Editor::RevisionControl::Private
{
	using namespace UE::Editor::DataStorage;

	static bool bAutoPopulateRevisionControlState = false;
	FAutoConsoleVariableRef CVarAutoPopulateState(
		TEXT("TEDS.RevisionControl.AutoPopulateState"),
		bAutoPopulateRevisionControlState,
		TEXT("Automatically query revision control provider and fill information into TEDS")
	);

	static void ResolvePackageReference(IEditorDataStorageProvider::IQueryContext& Context, const UPackage* Package, RowHandle Row, RowHandle PackageRow)
	{
		FTypedElementPackageReference PackageReference;
		PackageReference.Row = PackageRow;
		Context.AddColumn(Row, MoveTemp(PackageReference));

		FTypedElementPackagePathColumn PathColumn;
		FTypedElementPackageLoadedPathColumn LoadedPathColumn;

		Package->GetPathName(nullptr, PathColumn.Path);
		LoadedPathColumn.LoadedPath = Package->GetLoadedPath();

		Context.AddColumn(PackageRow, MoveTemp(PathColumn));
		Context.AddColumn(PackageRow, MoveTemp(LoadedPathColumn));

		FTypedElementPackageReference PackageBackReference;
		PackageBackReference.Row = Row;
		Context.AddColumn(PackageRow, MoveTemp(PackageBackReference));
	};
} // namespace UE::Editor::DataStorage

void UTypedElementUObjectPackagePathFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	UE::Editor::RevisionControl::Private::CVarAutoPopulateState->AsVariable()->OnChangedDelegate().AddLambda(
		[this, &DataStorage](IConsoleVariable* AutoPopulate)
		{
			if (AutoPopulate->GetBool())
			{
				RegisterTryAddPackageRef(DataStorage);
			}
			else
			{
				DataStorage.UnregisterQuery(TryAddPackageRef);
			}
		}
	);
	DataStorage.RegisterQuery(
		Select(
			TEXT("Resolve package references"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage)),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Object, const FTypedElementPackageUnresolvedReference& UnresolvedPackageReference)
			{
				RowHandle PackageRow = Context.FindIndexedRow(UnresolvedPackageReference.Index);
				if (!Context.IsRowAvailable(PackageRow))
				{
					return;
				}
				const UObject* ObjectInstance = Object.Object.Get();
				if (!ObjectInstance)
				{
					return;
				}
				const UPackage* Package = ObjectInstance->GetPackage();
				Context.RemoveColumns(Row, { FTypedElementPackageUnresolvedReference::StaticStruct() });

				UE::Editor::RevisionControl::Private::ResolvePackageReference(Context, Package, Row, PackageRow);
			})
		.Compile());

	if (UE::Editor::RevisionControl::Private::CVarAutoPopulateState->GetBool())
	{
		RegisterTryAddPackageRef(DataStorage);
	}
}

void UTypedElementUObjectPackagePathFactory::RegisterTryAddPackageRef(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	TryAddPackageRef = DataStorage.RegisterQuery(
		Select(
			TEXT("Sync UObject package info to columns"),
			FObserver::OnAdd<FTypedElementUObjectColumn>()
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Object)
			{
				if (const UObject* ObjectInstance = Object.Object.Get(); ObjectInstance != nullptr)
				{
					const UPackage* Target = ObjectInstance->GetPackage();
					FString Path;
					Target->GetPathName(nullptr, Path);
					if (FString PackageFilename; FPackageName::TryConvertLongPackageNameToFilename(Path, PackageFilename))
					{
						FPaths::NormalizeFilename(PackageFilename);
						FString FullPackageFilename = FPaths::ConvertRelativePathToFull(PackageFilename);
						IndexHash Index = GenerateIndexHash(FullPackageFilename);
						RowHandle PackageRow = Context.FindIndexedRow(Index);
						if (Context.IsRowAvailable(PackageRow))
						{
							const UPackage* Package = ObjectInstance->GetPackage();
							UE::Editor::RevisionControl::Private::ResolvePackageReference(Context, Package, Row, PackageRow);
						}
						else
						{
							FTypedElementPackageUnresolvedReference UnresolvedPackageReference;
							UnresolvedPackageReference.Index = Index;
							UnresolvedPackageReference.PathOnDisk = MoveTemp(FullPackageFilename);
							Context.AddColumn(Row, MoveTemp(UnresolvedPackageReference));
						}
					}
				}
			})
		.Compile());
}
