// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityManager.h"
#include "MassProcessingTypes.h"
#include "MassEntityTestTypes.h"
#include "MassEntityTypes.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"

#define LOCTEXT_NAMESPACE "MassTest"

UE_DISABLE_OPTIMIZATION_SHIP

//----------------------------------------------------------------------//
// tests 
//----------------------------------------------------------------------//
namespace FMassObserverTest
{

auto EntityIndexSorted = [](const FMassEntityHandle& A, const FMassEntityHandle& B)
{
	return A.Index < B.Index;
};

struct FTagBaseOperation : FEntityTestBase
{
	using FTagStruct = FTestTag_A;

	TArray<FMassEntityHandle> AffectedEntities;
	UMassTestProcessorBase* ObserverProcessor = nullptr;
	EMassObservedOperation OperationObserved = EMassObservedOperation::MAX;
	TArray<FMassEntityHandle> EntitiesInt;
	TArray<FMassEntityHandle> EntitiesIntsFloat;
	TArray<FMassEntityHandle> ExpectedEntities;
	bool bCommandsFlushed = false;

	// @return signifies if the test can continue
	virtual bool PerformOperation() { return false; }

	virtual bool SetUp() override
	{
		if (FEntityTestBase::SetUp())
		{
			ObserverProcessor = NewObject<UMassTestProcessorBase>();
			ObserverProcessor->EntityQuery.AddRequirement<FTestFragment_Int>(EMassFragmentAccess::ReadOnly);
			ObserverProcessor->EntityQuery.AddTagRequirement<FTagStruct>(EMassFragmentPresence::All);
			ObserverProcessor->ForEachEntityChunkExecutionFunction = [bCommandsFlushedPtr = &bCommandsFlushed, AffectedEntitiesPtr = &AffectedEntities](FMassExecutionContext& Context)
			{
				AffectedEntitiesPtr->Append(Context.GetEntities().GetData(), Context.GetEntities().Num());
				Context.Defer().PushCommand<FMassDeferredSetCommand>([&bCommandsFlushedPtr](FMassEntityManager&)
					{
						// dummy command, here just to catch if commands issue by observers got executed at all
						*bCommandsFlushedPtr = true;
					});
			};

			return true;
		}
		return false;
	}

	virtual bool InstantTest() override
	{
		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FTagStruct::StaticStruct(), OperationObserved, *ObserverProcessor);

		EntityManager->BatchCreateEntities(IntsArchetype, 3, EntitiesInt);
		EntityManager->BatchCreateEntities(FloatsIntsArchetype, 3, EntitiesIntsFloat);

		if (PerformOperation())
		{
			EntityManager->FlushCommands();
			AITEST_EQUAL(TEXT("The observer is expected to be run for predicted number of entities"), AffectedEntities.Num(), ExpectedEntities.Num());
			AITEST_TRUE(TEXT("The commands issued by the observer are flushed"), bCommandsFlushed);

			ExpectedEntities.Sort(EntityIndexSorted);
			AffectedEntities.Sort(EntityIndexSorted);

			for (int i = 0; i < ExpectedEntities.Num(); ++i)
			{
				AITEST_EQUAL(TEXT("Expected and affected sets should be the same"), AffectedEntities[i], ExpectedEntities[i]);
			}
		}

		return true;
	}
};

struct FObserverProcessorTest_SingleEntitySingleArchetypeAdd : FTagBaseOperation
{
	FObserverProcessorTest_SingleEntitySingleArchetypeAdd() { OperationObserved = EMassObservedOperation::Add; }
	virtual bool PerformOperation() override 
	{
		ExpectedEntities = { EntitiesInt[1] };
		EntityManager->Defer().AddTag<FTagStruct>(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_SingleEntitySingleArchetypeAdd, "System.Mass.Observer.Tag.SingleEntitySingleArchetypeAdd");

struct FObserverProcessorTest_SingleEntitySingleArchetypeRemove : FTagBaseOperation
{
	FObserverProcessorTest_SingleEntitySingleArchetypeRemove() { OperationObserved = EMassObservedOperation::Remove; }
	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[1] };

		EntityManager->Defer().AddTag<FTagStruct>(EntitiesInt[1]);
		EntityManager->FlushCommands();
		// since we're only observing tag removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Tag addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		EntityManager->Defer().RemoveTag<FTagStruct>(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_SingleEntitySingleArchetypeRemove, "System.Mass.Observer.Tag.SingleEntitySingleArchetypeRemove");

struct FObserverProcessorTest_SingleEntitySingleArchetypeDestroy : FTagBaseOperation
{
	FObserverProcessorTest_SingleEntitySingleArchetypeDestroy() { OperationObserved = EMassObservedOperation::Remove; }
	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[1] };
		EntityManager->Defer().AddTag<FTagStruct>(EntitiesInt[1]);
		EntityManager->FlushCommands();
		// since we're only observing tag removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Tag addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		EntityManager->Defer().DestroyEntity(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_SingleEntitySingleArchetypeDestroy, "System.Mass.Observer.Tag.SingleEntitySingleArchetypeDestroy");

struct FObserverProcessorTest_MultipleArchetypeAdd : FTagBaseOperation
{
	FObserverProcessorTest_MultipleArchetypeAdd() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesIntsFloat[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddTag<FTagStruct>(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_MultipleArchetypeAdd, "System.Mass.Observer.Tag.MultipleArchetypesAdd");

struct FObserverProcessorTest_MultipleArchetypeRemove : FTagBaseOperation
{
	FObserverProcessorTest_MultipleArchetypeRemove() { OperationObserved = EMassObservedOperation::Remove; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesIntsFloat[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddTag<FTagStruct>(ModifiedEntity);
		}
		EntityManager->FlushCommands();
		// since we're only observing tag removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Tag addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().RemoveTag<FTagStruct>(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_MultipleArchetypeRemove, "System.Mass.Observer.Tag.MultipleArchetypesRemove");

struct FObserverProcessorTest_MultipleArchetypeDestroy : FTagBaseOperation
{
	FObserverProcessorTest_MultipleArchetypeDestroy() { OperationObserved = EMassObservedOperation::Remove; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesIntsFloat[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddTag<FTagStruct>(ModifiedEntity);
		}
		EntityManager->FlushCommands();
		// since we're only observing tag removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Tag addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().DestroyEntity(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_MultipleArchetypeDestroy, "System.Mass.Observer.Tag.MultipleArchetypesDestroy");

struct FObserverProcessorTest_MultipleArchetypeSwap : FTagBaseOperation
{
	FObserverProcessorTest_MultipleArchetypeSwap() { OperationObserved = EMassObservedOperation::Remove; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesIntsFloat[1], EntitiesInt[0], EntitiesInt[2] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddTag<FTagStruct>(ModifiedEntity);
		}
		EntityManager->FlushCommands();
		// since we're only observing tag removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Tag addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().SwapTags<FTagStruct, FTestTag_B>(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_MultipleArchetypeSwap, "System.Mass.Observer.Tag.MultipleArchetypesSwap");

struct FObserverProcessorTest_EntityCreation_Individuals : FTagBaseOperation
{
	FObserverProcessorTest_EntityCreation_Individuals() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool InstantTest() override
	{
		constexpr int32 EntitiesToSpawnCount = 6;
		
		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FTagStruct::StaticStruct(), OperationObserved, *ObserverProcessor);

		int32 ArrayMidPoint = 0;
		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager->BatchCreateEntities(IntsArchetype, EntitiesToSpawnCount, EntitiesInt);
			ArrayMidPoint = EntitiesInt.Num() / 2;

			for (int32 Index = 0; Index < ArrayMidPoint; ++Index)
			{
				EntityManager->AddTagToEntity(EntitiesInt[Index], FTagStruct::StaticStruct());
			}
			AITEST_EQUAL(TEXT("The tag observer is not expected to run yet"), AffectedEntities.Num(), 0);
		}
		AITEST_EQUAL(TEXT("The tag observer is expected to run just after FEntityCreationContext's destruction"), AffectedEntities.Num(), ArrayMidPoint);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_EntityCreation_Individuals, "System.Mass.Observer.Create.TagInvididualEntities");

struct FObserverProcessorTest_EntityCreation_Batched : FTagBaseOperation
{
	FObserverProcessorTest_EntityCreation_Batched() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool InstantTest() override
	{
		constexpr int32 EntitiesToSpawnCount = 6;

		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FTagStruct::StaticStruct(), OperationObserved, *ObserverProcessor);

		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager->BatchCreateEntities(IntsArchetype, EntitiesToSpawnCount, EntitiesInt);

			EntityManager->BatchChangeTagsForEntities(CreationContext->GetEntityCollections(), FMassTagBitSet(*FTagStruct::StaticStruct()), FMassTagBitSet());
			AITEST_TRUE(TEXT("The tag observer is not expected to run yet"), AffectedEntities.Num() == 0);
			AITEST_FALSE(TEXT("CreationContext's entity collection should be invalidated at this moment"), CreationContext->DebugAreEntityCollectionsUpToDate());

			EntityManager->BatchChangeTagsForEntities(CreationContext->GetEntityCollections(), FMassTagBitSet(*FTagStruct::StaticStruct()), FMassTagBitSet());
			AITEST_TRUE(TEXT("The tag observer is still not expected to run"), AffectedEntities.Num() == 0);
		}
		AITEST_TRUE(TEXT("The tag observer is expected to run just after FEntityCreationContext's destruction"), AffectedEntities.Num() > 0);
		AITEST_EQUAL(TEXT("The tag observer is expected to process every entity just once"), AffectedEntities.Num(), EntitiesInt.Num());

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FObserverProcessorTest_EntityCreation_Batched, "System.Mass.Observer.Create.TagBatchedEntities");

//-----------------------------------------------------------------------------
// fragments
//-----------------------------------------------------------------------------
struct FFragmentTestBase : FEntityTestBase
{
	using FFragmentStruct = FTestFragment_Float;

	TArray<FMassEntityHandle> AffectedEntities;
	UMassTestProcessorBase* ObserverProcessor = nullptr;
	EMassObservedOperation OperationObserved = EMassObservedOperation::MAX;
	TArray<FMassEntityHandle> EntitiesInt;
	TArray<FMassEntityHandle> EntitiesIntsFloat;
	TArray<FMassEntityHandle> ExpectedEntities;
	bool bCommandsFlushed = false;

	// @return signifies if the test can continue
	virtual bool PerformOperation() { return false; }

	virtual bool SetUp() override
	{
		if (FEntityTestBase::SetUp())
		{
			ObserverProcessor = NewObject<UMassTestProcessorBase>();
			ObserverProcessor->EntityQuery.AddRequirement(FFragmentStruct::StaticStruct(), EMassFragmentAccess::ReadOnly);
			ObserverProcessor->ForEachEntityChunkExecutionFunction = [bCommandsFlushedPtr = &bCommandsFlushed, AffectedEntitiesPtr = &AffectedEntities](FMassExecutionContext& Context)
				{
					AffectedEntitiesPtr->Append(Context.GetEntities().GetData(), Context.GetEntities().Num());
					Context.Defer().PushCommand<FMassDeferredSetCommand>([&bCommandsFlushedPtr](FMassEntityManager&)
						{
							// dummy command, here just to catch if commands issue by observers got executed at all
							*bCommandsFlushedPtr = true;
						});
				};

			return true;
		}
		return false;
	}

	virtual bool InstantTest() override
	{
		EntityManager->BatchCreateEntities(IntsArchetype, 3, EntitiesInt);
		EntityManager->BatchCreateEntities(FloatsIntsArchetype, 3, EntitiesIntsFloat);

		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FFragmentStruct::StaticStruct(), OperationObserved, *ObserverProcessor);
				
		if (PerformOperation())
		{
			EntityManager->FlushCommands();
			AITEST_EQUAL(TEXT("The fragment observer is expected to be run for predicted number of entities"), AffectedEntities.Num(), ExpectedEntities.Num());
			AITEST_TRUE(TEXT("The commands issued by the observer are flushed"), bCommandsFlushed);

			ExpectedEntities.Sort(EntityIndexSorted);
			AffectedEntities.Sort(EntityIndexSorted);

			for (int i = 0; i < ExpectedEntities.Num(); ++i)
			{
				AITEST_EQUAL(TEXT("Expected and affected sets should be the same"), AffectedEntities[i], ExpectedEntities[i]);
			}
		}

		return true;
	}
};

struct FFragmentObserverTest_SingleEntitySingleArchetypeAdd : FFragmentTestBase
{
	FFragmentObserverTest_SingleEntitySingleArchetypeAdd() { OperationObserved = EMassObservedOperation::Add; }
	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[1] };
		EntityManager->Defer().AddFragment<FFragmentStruct>(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_SingleEntitySingleArchetypeAdd, "System.Mass.Observer.Fragment.SingleEntitySingleArchetypeAdd");

struct FFragmentObserverTest_SingleEntitySingleArchetypeRemove : FFragmentTestBase
{
	FFragmentObserverTest_SingleEntitySingleArchetypeRemove() { OperationObserved = EMassObservedOperation::Remove; }
	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[1] };

		EntityManager->Defer().AddFragment<FFragmentStruct>(EntitiesInt[1]);
		EntityManager->FlushCommands();
		// since we're only observing Fragment removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Fragment addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		EntityManager->Defer().RemoveFragment<FFragmentStruct>(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_SingleEntitySingleArchetypeRemove, "System.Mass.Observer.Fragment.SingleEntitySingleArchetypeRemove");

struct FFragmentObserverTest_SingleEntitySingleArchetypeDestroy : FFragmentTestBase
{
	FFragmentObserverTest_SingleEntitySingleArchetypeDestroy() { OperationObserved = EMassObservedOperation::Remove; }
	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[1] };
		EntityManager->Defer().AddFragment<FFragmentStruct>(EntitiesInt[1]);
		EntityManager->FlushCommands();
		// since we're only observing Fragment removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Fragment addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		EntityManager->Defer().DestroyEntity(EntitiesInt[1]);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_SingleEntitySingleArchetypeDestroy, "System.Mass.Observer.Fragment.SingleEntitySingleArchetypeDestroy");

struct FFragmentObserverTest_MultipleArchetypeAdd : FFragmentTestBase
{
	FFragmentObserverTest_MultipleArchetypeAdd() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesInt[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddFragment<FFragmentStruct>(ModifiedEntity);
		}
		// also adding the fragment to the other archetype that already has the fragment. This should not yield any results
		for (const FMassEntityHandle& OtherEntity : EntitiesIntsFloat)
		{
			EntityManager->Defer().AddFragment<FFragmentStruct>(OtherEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_MultipleArchetypeAdd, "System.Mass.Observer.Fragment.MultipleArchetypesAdd");

struct FFragmentObserverTest_MultipleArchetypeRemove : FFragmentTestBase
{
	FFragmentObserverTest_MultipleArchetypeRemove() { OperationObserved = EMassObservedOperation::Remove; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesIntsFloat[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddFragment<FFragmentStruct>(ModifiedEntity);
		}
		EntityManager->FlushCommands();
		// since we're only observing Fragment removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Fragment addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().RemoveFragment<FFragmentStruct>(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_MultipleArchetypeRemove, "System.Mass.Observer.Fragment.MultipleArchetypesRemove");

struct FFragmentObserverTest_MultipleArchetypeDestroy : FFragmentTestBase
{
	FFragmentObserverTest_MultipleArchetypeDestroy() { OperationObserved = EMassObservedOperation::Remove; }

	virtual bool PerformOperation() override
	{
		ExpectedEntities = { EntitiesInt[0], EntitiesInt[2], EntitiesIntsFloat[1] };
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().AddFragment<FFragmentStruct>(ModifiedEntity);
		}
		EntityManager->FlushCommands();
		// since we're only observing Fragment removal we don't expect AffectedEntities to contain any data at this point
		AITEST_EQUAL(TEXT("Fragment addition is not being observed and is not expected to produce results yet"), AffectedEntities.Num(), 0);
		for (const FMassEntityHandle& ModifiedEntity : ExpectedEntities)
		{
			EntityManager->Defer().DestroyEntity(ModifiedEntity);
		}
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_MultipleArchetypeDestroy, "System.Mass.Observer.Fragment.MultipleArchetypesDestroy");

struct FFragmentObserverTest_EntityCreation_Individual : FFragmentTestBase
{
	FFragmentObserverTest_EntityCreation_Individual() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool InstantTest() override
	{
		constexpr float TestValue = 123.456f;
		float ValueOnNotification = 0.f;

		ObserverProcessor->ForEachEntityChunkExecutionFunction = [&ValueOnNotification](FMassExecutionContext& Context) //-V1047 - This lambda is cleared before routine exit
			{
				const TConstArrayView<FFragmentStruct> Fragments = Context.GetFragmentView<FFragmentStruct>();
				for (int32 EntityIndex = 0; EntityIndex < Context.GetNumEntities(); EntityIndex++)
				{
					ValueOnNotification = Fragments[EntityIndex].Value;
				};
			};

		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FFragmentStruct::StaticStruct(), OperationObserved, *ObserverProcessor);

		TArray<FInstancedStruct> FragmentInstanceList = { FInstancedStruct::Make(FFragmentStruct(TestValue)) };

		// BuildEntity
		{
			const FMassEntityHandle Entity= EntityManager->ReserveEntity();
			EntityManager->BuildEntity(Entity, FragmentInstanceList);	
			AITEST_EQUAL(TEXT("The fragment observer notified by BuildEntity is expected to be able to fetch the initial value"), ValueOnNotification, TestValue);
			EntityManager->DestroyEntity(Entity);
		}

		// CreateEntity
		{
			ValueOnNotification = 0.f;
			const FMassEntityHandle Entity = EntityManager->CreateEntity(FragmentInstanceList);
			AITEST_EQUAL(TEXT("The fragment observer notified by CreateEntity is expected to be able to fetch the initial value"), ValueOnNotification, TestValue);
			EntityManager->DestroyEntity(Entity);
		}

		ObserverProcessor->ForEachEntityChunkExecutionFunction = nullptr;

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_EntityCreation_Individual, "System.Mass.Observer.Create.FragmentSingleEntity");

struct FFragmentObserverTest_EntityCreation_Individuals : FFragmentTestBase
{
	FFragmentObserverTest_EntityCreation_Individuals() { OperationObserved = EMassObservedOperation::Add; }

	virtual bool InstantTest() override
	{
		constexpr int32 EntitiesToSpawnCount = 6;

		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FFragmentStruct::StaticStruct(), OperationObserved, *ObserverProcessor);

		int32 ArrayMidPoint = 0;
		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager->BatchCreateEntities(IntsArchetype, EntitiesToSpawnCount, EntitiesInt);
			ArrayMidPoint = EntitiesInt.Num() / 2;

			for (int32 Index = 0; Index < ArrayMidPoint; ++Index)
			{
				EntityManager->AddFragmentToEntity(EntitiesInt[Index], FFragmentStruct::StaticStruct());
			}
			AITEST_EQUAL(TEXT("The fragment observer is not expected to run yet"), AffectedEntities.Num(), 0);
		}
		AITEST_EQUAL(TEXT("The fragment observer is expected to run just after FEntityCreationContext's destruction"), AffectedEntities.Num(), ArrayMidPoint);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FFragmentObserverTest_EntityCreation_Individuals, "System.Mass.Observer.Create.FragmentInvididualEntities");

//-----------------------------------------------------------------------------
// creation context 
//-----------------------------------------------------------------------------
struct FCreationContextTest : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		constexpr int32 IntEntitiesToSpawnCount = 6;
		constexpr int32 FloatEntitiesToSpawnCount = 7;

		TArray<FMassEntityHandle> Entities;
		TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContextInt = EntityManager->BatchCreateEntities(IntsArchetype, IntEntitiesToSpawnCount, Entities);
		TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContextFloat = EntityManager->BatchCreateEntities(FloatsArchetype, FloatEntitiesToSpawnCount, Entities);
		const int32 NumDifferentArchetypesUsed = 2;

		AITEST_EQUAL(TEXT("Two back to back entity creation operations should result in the same creation context"), CreationContextInt, CreationContextFloat);
		AITEST_FALSE(TEXT("CreationContext's entity collection should be invalidated at this moment"), CreationContextInt->DebugAreEntityCollectionsUpToDate());

		TConstArrayView<FMassArchetypeEntityCollection> EntityCollections = CreationContextInt->GetEntityCollections();
		AITEST_EQUAL(TEXT("We expect the number of resulting collections to match expectations"), EntityCollections.Num(), NumDifferentArchetypesUsed);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCreationContextTest, "System.Mass.CreationContext.Append");

struct FCreationContextTest_ManualCreate : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		constexpr int32 IntEntitiesToSpawnCount = 6;
		constexpr int32 FloatEntitiesToSpawnCount = 7;
		int NumDifferentArchetypesUsed = 0;

		TArray<FMassEntityHandle> Entities;
		TSharedRef<FMassEntityManager::FEntityCreationContext> ObtainedContext = EntityManager->GetOrMakeCreationContext();
		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> ObtainedContextCopy = EntityManager->GetOrMakeCreationContext();
			AITEST_EQUAL(TEXT("Two back to back creation context fetching should result in the same instance"), ObtainedContext, ObtainedContextCopy);
		}

		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContextInt = EntityManager->BatchCreateEntities(IntsArchetype, IntEntitiesToSpawnCount, Entities);
			AITEST_EQUAL(TEXT("Creating entities should return the original context"), ObtainedContext, CreationContextInt);
			++NumDifferentArchetypesUsed;
		}
		
		AITEST_TRUE(TEXT("CreationContext's entity collection should be still valid at this moment since we only added one entity collection/array")
			, ObtainedContext->DebugAreEntityCollectionsUpToDate());

		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> TempContext = EntityManager->BatchCreateEntities(IntsArchetype, IntEntitiesToSpawnCount, Entities);
			AITEST_EQUAL(TEXT("Creating entities should return the original context"), ObtainedContext, TempContext);

			AITEST_FALSE(TEXT("CreationContext's entity collection should be invalidated at this moment")
				, TempContext->DebugAreEntityCollectionsUpToDate());
		}

		TConstArrayView<FMassArchetypeEntityCollection> EntityCollections = ObtainedContext->GetEntityCollections();
		AITEST_EQUAL(TEXT("We expect the number of resulting collections to match expectations"), EntityCollections.Num(), NumDifferentArchetypesUsed);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCreationContextTest_ManualCreate, "System.Mass.CreationContext.ManualCreate");

struct FCreationContextTest_ManualBuild : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		constexpr int32 FloatEntitiesToSpawnCount = 7;
		int NumDifferentArchetypesUsed = 0;

		TArray<FTestFragment_Float> Payload;
		for (int Index = 0; Index < FloatEntitiesToSpawnCount; ++Index)
		{ 
			Payload.Add(FTestFragment_Float(float(Index)));
		}

		TSharedRef<FMassEntityManager::FEntityCreationContext> ObtainedContext = EntityManager->GetOrMakeCreationContext();
		
		TArray<FMassEntityHandle> Entities;
		EntityManager->BatchReserveEntities(FloatEntitiesToSpawnCount, Entities);

		FStructArrayView PaloadView(Payload);
		TArray<FMassArchetypeEntityCollectionWithPayload> EntityCollections;
		FMassArchetypeEntityCollectionWithPayload::CreateEntityRangesWithPayload(*EntityManager, Entities, FMassArchetypeEntityCollection::NoDuplicates
			, FMassGenericPayloadView(MakeArrayView(&PaloadView, 1)), EntityCollections);

		checkf(EntityCollections.Num() <= 1, TEXT("We expect TargetEntities to only contain archetype-less entities, ones that need to be \'build\'"));

		{
			TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext = EntityManager->BatchBuildEntities(EntityCollections[0], FMassFragmentBitSet(*FTestFragment_Float::StaticStruct()));
			AITEST_EQUAL(TEXT("Creating entities should return the original context"), ObtainedContext, CreationContext);
			++NumDifferentArchetypesUsed;
		}

		AITEST_TRUE(TEXT("CreationContext's entity collection should be still valid at this moment since we only added one entity collection/array")
			, ObtainedContext->DebugAreEntityCollectionsUpToDate());

		TConstArrayView<FMassArchetypeEntityCollection> ContextEntityCollections = ObtainedContext->GetEntityCollections();
		AITEST_EQUAL(TEXT("We expect the number of resulting collections to match expectations"), ContextEntityCollections.Num(), NumDifferentArchetypesUsed);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCreationContextTest_ManualBuild, "System.Mass.CreationContext.ManualBuild");

} // FMassObserverTest

UE_ENABLE_OPTIMIZATION_SHIP

#undef LOCTEXT_NAMESPACE
