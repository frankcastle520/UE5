// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsTypedElementActorHandleFactory.h"

#include "Compatibility/TedsTypedElementBridge.h"
#include "Compatibility/Columns/TypedElement.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "GameFramework/Actor.h"

void UTypedElementActorHandleDataStorageFactory::PreRegister(IEditorDataStorageProvider& DataStorage)
{
	Super::PreRegister(DataStorage);
	
	BridgeEnableDelegateHandle = UE::Editor::DataStorage::Compatibility::OnTypedElementBridgeEnabled().AddUObject(this, &UTypedElementActorHandleDataStorageFactory::HandleBridgeEnabled);
}

void UTypedElementActorHandleDataStorageFactory::PreShutdown(IEditorDataStorageProvider& DataStorage)
{
	UE::Editor::DataStorage::Compatibility::OnTypedElementBridgeEnabled().Remove(BridgeEnableDelegateHandle);
	BridgeEnableDelegateHandle.Reset();
}

void UTypedElementActorHandleDataStorageFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	Super::RegisterQueries(DataStorage);

	if (UE::Editor::DataStorage::Compatibility::IsTypedElementBridgeEnabled())
	{
		RegisterQuery_ActorHandlePopulate(DataStorage);
	}

	using namespace UE::Editor::DataStorage::Queries;
	GetAllActorsQuery = DataStorage.RegisterQuery(
	Select()
		.ReadOnly<FTypedElementUObjectColumn>()
	.Where()
		.All<FTypedElementActorTag>()
	.Compile());
}

void UTypedElementActorHandleDataStorageFactory::RegisterQuery_ActorHandlePopulate(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	if (!ensureMsgf(ActorHandlePopulateQuery == InvalidQueryHandle, TEXT("Already registered query")))
	{
		return;
	}
	
	ActorHandlePopulateQuery = DataStorage.RegisterQuery(
	Select(TEXT("Populate actor typed element handles"),
		FObserver::OnAdd<FTypedElementUObjectColumn>(),
		[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& ObjectColumn)
		{
			if (UObject* Object = ObjectColumn.Object.Get())
			{
				checkSlow(Cast<AActor>(Object));
				FTypedElementHandle Handle = UEngineElementsLibrary::AcquireEditorActorElementHandle(static_cast<AActor*>(Object));
				Context.AddColumn(Row, Compatibility::FTypedElementColumn
				{
					.Handle = Handle
				});
			}
		})
	.Where()
		.All<FTypedElementActorTag>()
	.Compile());
}

void UTypedElementActorHandleDataStorageFactory::HandleBridgeEnabled(bool bEnabled)
{
	using namespace UE::Editor::DataStorage;

	IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

	if (bEnabled)
	{
		using namespace Queries;
		
		// Populate all the rows
		TArray<RowHandle> CollatedRowHandles;
		TArray<TWeakObjectPtr<const AActor>> Actors;
		
		DataStorage->RunQuery(GetAllActorsQuery, CreateDirectQueryCallbackBinding([&CollatedRowHandles, &Actors](IDirectQueryContext& Context, const FTypedElementUObjectColumn* Fragments)
		{
			TConstArrayView<RowHandle> RowHandles = Context.GetRowHandles();

			CollatedRowHandles.Append(RowHandles);

			TConstArrayView<const FTypedElementUObjectColumn> FragmentView(Fragments, Context.GetRowCount());

			Actors.Reserve(Actors.Num() + FragmentView.Num());
			for (const FTypedElementUObjectColumn& Fragment : FragmentView)
			{
				const AActor* Actor = Cast<AActor>(Fragment.Object);
				Actors.Add(Actor);
			}
		}));

		for (int32 Index = 0, End = CollatedRowHandles.Num(); Index < End; ++Index)
		{
			if (const AActor* Actor = Actors[Index].Get())
			{
				FTypedElementHandle Handle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor);
				DataStorage->AddColumn(CollatedRowHandles[Index], Compatibility::FTypedElementColumn
				{
					.Handle = Handle
				});
			}
		}

		RegisterQuery_ActorHandlePopulate(*DataStorage);
	}
	else
	{
		DataStorage->UnregisterQuery(ActorHandlePopulateQuery);
		ActorHandlePopulateQuery = InvalidQueryHandle;
	}
}

