// Copyright Epic Games, Inc. All Rights Reserved.

#include "Processors/RevisionControlProcessors.h"

#include "ISourceControlModule.h"
#include "SourceControlFileStatusMonitor.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementRevisionControlColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Columns/TypedElementViewportColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Common/TypedElementDataStorageLog.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementQueryStorageInterfaces.h"

namespace UE::Editor::RevisionControl::Private
{
	extern FAutoConsoleVariableRef CVarAutoPopulateState;

	// Update the overlay color for all rows with InColumn
	void UpdateSCCOverlayStates(UScriptStruct* InColumn)
	{
		using namespace UE::Editor::DataStorage;
		IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

		if(const URevisionControlDataStorageFactory* Factory = DataStorage->FindFactory<URevisionControlDataStorageFactory>())
		{
			Factory->UpdateOverlaysForSCCState(DataStorage, InColumn);
		}
	}

	// Update all currently existing overlay colors
	void UpdateOverlayColors()
	{
		using namespace UE::Editor::DataStorage;
		IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

		if(const URevisionControlDataStorageFactory* Factory = DataStorage->FindFactory<URevisionControlDataStorageFactory>())
		{
			Factory->UpdateOverlayColors(DataStorage);
		}
	}

	static bool gEnableOverlays = false;
	TAutoConsoleVariable<bool> CVarEnableOverlays(
		TEXT("RevisionControl.Overlays.Enable"),
		gEnableOverlays,
		TEXT("Enables overlays."),
		ECVF_Default);

	static bool gEnableOverlayCheckedOutByOtherUser = true;
	TAutoConsoleVariable<bool> CVarEnableOverlayCheckedOutByOtherUser(
		TEXT("RevisionControl.Overlays.CheckedOutByOtherUser.Enable"),
		gEnableOverlayCheckedOutByOtherUser,
		TEXT("Enables overlays for files that are checked out by another user."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			// StaticStruct fails if the module containing the column isn't loaded yet.
			// There's no SCC rows to update on startup so simply skipping the call is fine
			if (FModuleManager::Get().IsModuleLoaded("TypedElementFramework"))
			{
				UpdateSCCOverlayStates(FSCCExternallyLockedColumn::StaticStruct());
			}
		}),
		ECVF_Default);

	static bool gEnableOverlayNotAtHeadRevision = true;
	TAutoConsoleVariable<bool> CVarEnableOverlayNotAtHeadRevision(
		TEXT("RevisionControl.Overlays.NotAtHeadRevision.Enable"),
		gEnableOverlayNotAtHeadRevision,
		TEXT("Enables overlays for files that are not at the latest revision."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			// StaticStruct fails if the module containing the column isn't loaded yet.
			// There's no SCC rows to update on startup so simply skipping the call is fine
			if (FModuleManager::Get().IsModuleLoaded("TypedElementFramework"))
			{
				UpdateSCCOverlayStates(FSCCNotCurrentTag::StaticStruct());
			}
		}),
		ECVF_Default);

	static bool gEnableOverlayCheckedOut = false;
	TAutoConsoleVariable<bool> CVarEnableOverlayCheckedOut(
		TEXT("RevisionControl.Overlays.CheckedOut.Enable"),
		gEnableOverlayCheckedOut,
		TEXT("Enables overlays for files that are checked out by user."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			// StaticStruct fails if the module containing the column isn't loaded yet.
			// There's no SCC rows to update on startup so simply skipping the call is fine
			if (FModuleManager::Get().IsModuleLoaded("TypedElementFramework"))
			{
				UpdateSCCOverlayStates(FSCCLockedTag::StaticStruct());
			}
		}),
		ECVF_Default);

	static bool gEnableOverlayOpenForAdd = false;
	TAutoConsoleVariable<bool> CVarEnableOverlayOpenForAdd(
		TEXT("RevisionControl.Overlays.OpenForAdd.Enable"),
		gEnableOverlayOpenForAdd,
		TEXT("Enables overlays for files that are newly added."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			// StaticStruct fails if the module containing the column isn't loaded yet.
			// There's no SCC rows to update on startup so simply skipping the call is fine
			if (FModuleManager::Get().IsModuleLoaded("TypedElementFramework"))
			{
				UpdateSCCOverlayStates(FSCCStatusColumn::StaticStruct());
			}
		}),
		ECVF_Default);

	static int32 gOverlayAlpha = 20; // [0..100]
	TAutoConsoleVariable<int32> CVarOverlayAlpha(
		TEXT("RevisionControl.Overlays.Alpha"),
		gOverlayAlpha,
		TEXT("Configures overlay opacity."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			// StaticStruct fails if the module containing the column isn't loaded yet.
			// There's no SCC rows to update on startup so simply skipping the call is fine
			if (FModuleManager::Get().IsModuleLoaded("TypedElementFramework"))
			{
				UpdateOverlayColors();
			}
		}),
		ECVF_Default);

	#if UE_BUILD_SHIPPING
	#define ENABLE_OVERLAY_DEBUG 0
	#else
	#define ENABLE_OVERLAY_DEBUG 1
	#endif

	#if ENABLE_OVERLAY_DEBUG
	static int32 gDefaultDebugForceColorOnAllValue = false; // [0..100]
	TAutoConsoleVariable<int32> CVarDebugForceColorOnAll(
		TEXT("RevisionControl.Overlays.Debug.ForceColorOnAll"),
		gDefaultDebugForceColorOnAllValue,
		TEXT("Debug to force overlay color on everything. 1 = Red, 2 = Green, 3 = Blue, 4 = White. 0 = off  ."),
		ECVF_Default);
	#endif

	using namespace UE::Editor::DataStorage;
	static FColor DetermineOverlayColor(const ICommonQueryContext& SCCContext, const FTypedElementUObjectColumn& Actor, bool bSelected)
	{
		check(IsInGameThread());

	#if ENABLE_OVERLAY_DEBUG
		if (int32 Force = CVarDebugForceColorOnAll.GetValueOnGameThread())
		{
			int32 Alpha = FMath::Lerp<float>(0.f, 255.f, gOverlayAlpha / 100.f);
			switch(Force)
			{
			case 1:
				return FColor(255, 0, 0, Alpha);
			case 2:
				return FColor(0, 255, 0, Alpha);
			case 3:
				return FColor(0, 0, 255, Alpha);
			case 4:
				return FColor(255, 255, 255, Alpha);
			// Do normal determination for higher than 4
			}
		}
	#endif

		bool bExternal = Actor.Object.IsValid() ? Cast<AActor>(Actor.Object)->IsPackageExternal() : false;
		bool bIgnored = !bExternal;
		if (!bIgnored && !bSelected)
		{
			// Convert CVar value from [0..100] to [0..255] range.
			int32 Alpha = FMath::Lerp<float>(0.f, 255.f, CVarOverlayAlpha.GetValueOnGameThread() / 100.f);

			// Check if the package is outdated because there is a newer version available.
			if (SCCContext.HasColumn<FSCCNotCurrentTag>())
			{
				if (CVarEnableOverlayNotAtHeadRevision.GetValueOnGameThread())
				{
					// Yellow.
					return FColor(225, 255, 61, Alpha);
				}
			}

			// Check if the package is locked by someone else.
			if (SCCContext.HasColumn<FSCCExternallyLockedColumn>())
			{
				if (CVarEnableOverlayCheckedOutByOtherUser.GetValueOnGameThread())
				{
					// Red.
					return FColor(239, 53, 53, Alpha);
				}
			}

			// Check if the package is added locally.
			if (SCCContext.HasColumn<FSCCStatusColumn>())
			{
				if (CVarEnableOverlayOpenForAdd.GetValueOnGameThread())
				{
					if (const FSCCStatusColumn* StatusColumn = SCCContext.GetColumn<FSCCStatusColumn>())
					{
						if (StatusColumn->Modification == ESCCModification::Added)
						{
							// Blue.
							return FColor(0, 112, 224, Alpha);
						}
					}
				}
			}

			// Check if the package is locked by self.
			if (SCCContext.HasColumn<FSCCLockedTag>())
			{
				if (CVarEnableOverlayCheckedOut.GetValueOnGameThread())
				{
					// Green.
					return FColor(31, 228, 75, Alpha);
				}
			}
		}

		return FColor(ForceInitToZero);
	}
} // namespace UE::Editor::RevisionControl::Private

void URevisionControlDataStorageFactory::RegisterTables(IEditorDataStorageProvider& DataStorage)
{
	DataStorage.RegisterTable(
		TTypedElementColumnTypeList<
			FTypedElementPackagePathColumn, FTypedElementPackageLoadedPathColumn,
			FSCCRevisionIdColumn, FSCCExternalRevisionIdColumn>(),
		FName("Editor_RevisionControlTable"));
}

void URevisionControlDataStorageFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::RevisionControl::Private;
	
	CVarAutoPopulateState->AsVariable()->OnChangedDelegate().AddLambda(
		[this, &DataStorage](IConsoleVariable* AutoPopulate)
		{
			if (AutoPopulate->GetBool())
			{
				RegisterFetchUpdates(DataStorage);
			}
			else
			{
				DataStorage.UnregisterQuery(FetchUpdates);
				FetchUpdates = InvalidQueryHandle;
			}
		}
	);

	CVarEnableOverlays->AsVariable()->OnChangedDelegate().AddLambda(
		[this, &DataStorage](IConsoleVariable* EnableOverlays)
		{
			if (EnableOverlays->GetBool())
			{
				DataStorage.UnregisterQuery(RemoveOverlays);
				RemoveOverlays = InvalidQueryHandle;

				RegisterApplyOverlays(DataStorage);
			}
			else
			{
				DataStorage.UnregisterQuery(ApplyOverlaysObjectToSCC);
				ApplyOverlaysObjectToSCC = InvalidQueryHandle;

				DataStorage.UnregisterQuery(SelectionAdded);
				SelectionAdded = InvalidQueryHandle;

				DataStorage.UnregisterQuery(SelectionRemoved);
				SelectionRemoved = InvalidQueryHandle;

				DataStorage.UnregisterQuery(PackageReferenceAdded);
				PackageReferenceAdded = InvalidQueryHandle;

				DataStorage.UnregisterQuery(UpdateSCCForActors);
				UpdateSCCForActors = InvalidQueryHandle;
				
				DataStorage.UnregisterQuery(UpdateOverlays);
				UpdateOverlays = InvalidQueryHandle;

				RegisterRemoveOverlays(DataStorage);
			}
		}
	);
	
	if (CVarAutoPopulateState->GetBool())
	{
		RegisterFetchUpdates(DataStorage);
	}

	if (CVarEnableOverlays->GetBool())
	{
		RegisterApplyOverlays(DataStorage);
	}
	else
	{
		RegisterRemoveOverlays(DataStorage);
	}
	
	RegisterGeneralQueries(DataStorage);
}

void URevisionControlDataStorageFactory::UpdateOverlaysForSCCState(IEditorDataStorageProvider* DataStorage, const UScriptStruct* Column) const
{
	DataStorage->ActivateQueries(TEXT("UpdateSCCForActors"));
}

void URevisionControlDataStorageFactory::UpdateOverlayColors(IEditorDataStorageProvider* DataStorage) const
{
	DataStorage->ActivateQueries(TEXT("UpdateOverlayForActors"));
}

void URevisionControlDataStorageFactory::RegisterFetchUpdates(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	FSourceControlFileStatusMonitor& FileStatusMonitor = ISourceControlModule::Get().GetSourceControlFileStatusMonitor();

	if (FetchUpdates == InvalidQueryHandle)
	{
		FetchUpdates = DataStorage.RegisterQuery(
			Select(
				TEXT("Gather source control statuses for objects with unresolved package paths"),
				FObserver::OnAdd<FTypedElementPackageUnresolvedReference>()
					.SetExecutionMode(EExecutionMode::GameThread),
				[this, &FileStatusMonitor](IQueryContext& Context, const FTypedElementPackageUnresolvedReference& UnresolvedReference)
				{
					static FSourceControlFileStatusMonitor::FOnSourceControlFileStatus EmptyDelegate{};
				
					FileStatusMonitor.StartMonitoringFile(
						reinterpret_cast<uintptr_t>(this),
						UnresolvedReference.PathOnDisk,
						EmptyDelegate
						
					);
				})
			.Compile());
	}
}

void URevisionControlDataStorageFactory::RegisterApplyOverlays(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::RevisionControl::Private;
	
	if (ApplyOverlaysObjectToSCC == InvalidQueryHandle)
	{
		ApplyOverlaysObjectToSCC = DataStorage.RegisterQuery(
			Select()
				.ReadOnly<FTypedElementPackagePathColumn>()
				.ReadOnly<FSCCStatusColumn>(EOptional::Yes)
			.Compile());
	}
	
	if (UpdateSCCForActors == InvalidQueryHandle)
	{
		check(ApplyOverlaysObjectToSCC != InvalidQueryHandle);

		// Query:
		// For all actors having a package reference:
		//	Determine if a color should be applied based on SCC status tags
		//		If so, either add the OverlayColorColumn to the actor row or remove and re-add it to update it
		//		If not, remove the OverlayColorColumn from the actor row
		UpdateSCCForActors = DataStorage.RegisterQuery(
			Select(
				TEXT("Update overlay for all actors"),
				FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Update))
					.SetExecutionMode(EExecutionMode::GameThread)
					.MakeActivatable(TEXT("UpdateSCCForActors")),
				[](IQueryContext& ActorQueryContext, RowHandle ObjectRow, const FTypedElementUObjectColumn& Actor, const FTypedElementPackageReference& PackageReference)
				{
					// Run a subquery on the SCC row to determine the overlay color
					ActorQueryContext.RunSubquery(0, PackageReference.Row, CreateSubqueryCallbackBinding(
						[&ActorQueryContext, &ObjectRow, &Actor](const ISubqueryContext& SubQueryContext)
						{
							FColor Color = DetermineOverlayColor(SubQueryContext, Actor, ActorQueryContext.HasColumn<FTypedElementSelectionColumn>());

							// If there is no overlay color, simply remove the column
							if (Color.Bits == 0)
							{
								ActorQueryContext.RemoveColumns<FTypedElementViewportOverlayColorColumn>(ObjectRow);
								return;
							}

							// If we already have the color column, only update it if the color has changed
							if(const FTypedElementViewportOverlayColorColumn* OverlayColorColumn = ActorQueryContext.GetColumn<FTypedElementViewportOverlayColorColumn>())
							{
								if (Color != OverlayColorColumn->OverlayColor)
								{
									// Remove and re-add to trigger the observer
									ActorQueryContext.RemoveColumns<FTypedElementViewportOverlayColorColumn>(ObjectRow);
									ActorQueryContext.AddColumn<FTypedElementViewportOverlayColorColumn>(ObjectRow, { .OverlayColor = Color });
								}
							}
							// If we don't have the color column, add it
							else
							{
								ActorQueryContext.AddColumn<FTypedElementViewportOverlayColorColumn>(ObjectRow, { .OverlayColor = Color });
							}
						})
					);
				})
			.ReadOnly<FTypedElementViewportOverlayColorColumn>(EOptional::Yes)
			.Where()
				.All<FTypedElementActorTag>()
			.DependsOn()
				.SubQuery(ApplyOverlaysObjectToSCC)
			.Compile());
	}

	if (SelectionAdded == InvalidQueryHandle)
	{
		SelectionAdded = DataStorage.RegisterQuery(
					Select(
					TEXT("Update Overlay on Selection"),
					FObserver::OnAdd<FTypedElementSelectionColumn>(),
					[this](IQueryContext& Context, RowHandle RowHandle, const FTypedElementSelectionColumn& SelectionColumn)
					{
						// We only care about the level editor's selection set for now. When the selection column is made dynamic
						// we can directly query for it
						if(SelectionColumn.SelectionSet.IsNone())
						{
							// Since we know DetermineOverlayColor() removes the overlay when the row is selected we can directly
							// remove the column here to skip the need to do all the checks in there. But if that logic ever
							// changes we will have to update it here too
							Context.RemoveColumns<FTypedElementViewportOverlayColorColumn>(RowHandle);
						}
					})
					.Where()
						.All<FTypedElementActorTag, FTypedElementViewportOverlayColorColumn, FTypedElementPackageReference>()
					.Compile());
	}

	if (SelectionRemoved == InvalidQueryHandle)
	{
		SelectionRemoved = DataStorage.RegisterQuery(
					Select(
					TEXT("Update Overlay on Deselection"),
					FObserver::OnRemove<FTypedElementSelectionColumn>(),
					[this](IQueryContext& ActorQueryContext, RowHandle ObjectRow, const FTypedElementSelectionColumn& SelectionColumn,
						const FTypedElementPackageReference& PackageReference, const FTypedElementUObjectColumn& Actor)
					{
						// We only care about the level editor's selection set for now. When the selection column is made dynamic
						// we can directly query for it
						if(SelectionColumn.SelectionSet.IsNone())
						{
							// When an item is deselected, add the viewport overlay color column to it if applicable
							ActorQueryContext.RunSubquery(0, PackageReference.Row, CreateSubqueryCallbackBinding(
								[&ActorQueryContext, &ObjectRow, &Actor](const ISubqueryContext& SubQueryContext)
								{
									// We manually set the selection as false because the item is being deslected, but the row still has the selection column
									// when the observer is fired
									bool bSelected = false;
									FColor Color = DetermineOverlayColor(SubQueryContext, Actor, bSelected);
									if (Color.Bits != 0)
									{
										ActorQueryContext.AddColumn<FTypedElementViewportOverlayColorColumn>(ObjectRow, { .OverlayColor = Color });
									}
								})
							);
						}
					})
					.Where()
						.All<FTypedElementActorTag>()
					.DependsOn()
						.SubQuery(ApplyOverlaysObjectToSCC)
					.Compile());
	}

	if (PackageReferenceAdded == InvalidQueryHandle)
	{
		/**
	 * Usually, when a revision control update is requested for an SCC row it adds a new row with FTypedElementPackageUpdateColumn and a reference to
	 * the actor row and the SCC row to update the overlays. However, if the revision control update happens before the actor row and SCC row have a
	 * chance to link to each other via the FTypedElementPackageReference column, FTypedElementPackageUpdateColumn cannot be added.
	 * So we add an observer to track for FTypedElementPackageReference addition to the actor rows and manually execute an overlay update.
	 */
		PackageReferenceAdded = DataStorage.RegisterQuery(
						Select(
						TEXT("Add overlay on package reference added"),
						FObserver::OnAdd<FTypedElementPackageReference>(),
						[this](IQueryContext& ActorQueryContext, RowHandle ObjectRow, const FTypedElementPackageReference& PackageReference,
							const FTypedElementUObjectColumn& Actor)
						{
							ActorQueryContext.RunSubquery(0, PackageReference.Row, CreateSubqueryCallbackBinding(
								[&ActorQueryContext, &ObjectRow, &Actor](const ISubqueryContext& SubQueryContext)
								{
									FColor Color = DetermineOverlayColor(SubQueryContext, Actor, ActorQueryContext.HasColumn<FTypedElementSelectionColumn>());
									if (Color.Bits != 0)
									{
										ActorQueryContext.AddColumn<FTypedElementViewportOverlayColorColumn>(ObjectRow, { .OverlayColor = Color });
									}
								})
							);
						})
						.Where()
							.All<FTypedElementActorTag>()
							.None<FTypedElementViewportOverlayColorColumn>()
						.DependsOn()
							.SubQuery(ApplyOverlaysObjectToSCC)
						.Compile());
	}

	// Query to update the color for all rows that currently have the overlay color column
	if (UpdateOverlays == InvalidQueryHandle)
	{
		UpdateOverlays = DataStorage.RegisterQuery(
			Select(
				TEXT("Update overlay color for actors with overlays"),
				FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Update))
					.SetExecutionMode(EExecutionMode::GameThread)
					.MakeActivatable(TEXT("UpdateOverlayForActors")),
					[](IQueryContext& ActorQueryContext, RowHandle ObjectRow, const FTypedElementUObjectColumn& Actor,
						const FTypedElementPackageReference& PackageReference, FTypedElementViewportOverlayColorColumn& OverlayColorColumn)
					{
						ActorQueryContext.RunSubquery(0, PackageReference.Row, CreateSubqueryCallbackBinding(
							[&ActorQueryContext, &ObjectRow, &Actor, &OverlayColorColumn](const ISubqueryContext& SubQueryContext)
							{
								FColor Color = DetermineOverlayColor(SubQueryContext, Actor, ActorQueryContext.HasColumn<FTypedElementSelectionColumn>());
							
								if (Color != OverlayColorColumn.OverlayColor)
								{
									// Remove and re-add to trigger the observer
									ActorQueryContext.RemoveColumns<FTypedElementViewportOverlayColorColumn>(ObjectRow);
									ActorQueryContext.AddColumn<FTypedElementViewportOverlayColorColumn>(ObjectRow, { .OverlayColor = Color });
								}
							})
						);
					})
			.Where()
				.All<FTypedElementActorTag>()
			.DependsOn()
				.SubQuery(ApplyOverlaysObjectToSCC)
			.Compile());
	}
}

void URevisionControlDataStorageFactory::RegisterRemoveOverlays(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	if (RemoveOverlays == InvalidQueryHandle)
	{
		// Query:
		// For all actors WITH an overlay color column AND having a package reference:
		//   Remove the overlay color column
		//
		// This query is used to clean up the color columns if the overlay feature is disabled dynamically
		RemoveOverlays = DataStorage.RegisterQuery(
			Select(
				TEXT("Remove selection overlay colors"),
				// This is in PrePhysics because the overlay->actor query is in DuringPhysics and contexts don't flush changes between tick groups
				FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
					.SetExecutionMode(EExecutionMode::GameThread),
				[](IQueryContext& Context, RowHandle ObjectRow, FTypedElementUObjectColumn& Actor, const FTypedElementViewportOverlayColorColumn& ViewportColor)
				{
					Context.RemoveColumns<FTypedElementViewportOverlayColorColumn>(ObjectRow);
				})
			.Where()
				.All<FTypedElementActorTag>()
			.Compile());
	}
}

void URevisionControlDataStorageFactory::RegisterGeneralQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::RevisionControl::Private;

	
}

