// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityManager.h"
#include "MassProcessingTypes.h"
#include "MassEntityTestTypes.h"
#include "MassEntityTypes.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "Algo/Sort.h"
#include "Algo/RandomShuffle.h"

#define LOCTEXT_NAMESPACE "MassTest"

UE_DISABLE_OPTIMIZATION_SHIP

//----------------------------------------------------------------------//
// tests 
//----------------------------------------------------------------------//

namespace FMassCommandsTest
{
#if WITH_MASSENTITY_DEBUG
struct FCommands_FragmentInstanceList : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		const int32 Count = 5;
		TArray<FMassEntityHandle> IntEntities;
		TArray<FMassEntityHandle> FloatEntities;
		EntityManager->BatchCreateEntities(IntsArchetype, Count, IntEntities);
		EntityManager->BatchCreateEntities(FloatsArchetype, Count, FloatEntities);

		for (int i = 0; i < Count; ++i)
		{
			EntityManager->Defer().PushCommand<FMassCommandAddFragmentInstances>(IntEntities[i], FTestFragment_Int(i), FTestFragment_Float((float)i));
			EntityManager->Defer().PushCommand<FMassCommandAddFragmentInstances>(FloatEntities[i], FTestFragment_Int(i), FTestFragment_Float((float)i));
		}

		EntityManager->FlushCommands();

		auto TestEntities = [this](const TArray<FMassEntityHandle>& Entities) -> bool {
			// all entities should have ended up in the same archetype, FloatsIntsArchetype
			for (int i = 0; i < Entities.Num(); ++i)
			{
				AITEST_EQUAL(TEXT("All entities should have ended up in the same archetype"), EntityManager->GetArchetypeForEntity(Entities[i]), FloatsIntsArchetype);

				FMassEntityView View(FloatsIntsArchetype, Entities[i]);
				AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Int>().Value, i);
				AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Float>().Value, float(i));
			}
			return true;
		};
		
		if (!TestEntities(IntEntities) || !TestEntities(FloatEntities))
		{
			return false;
		}
		//AITEST_EQUAL(TEXT("All entities should have ended up in the same archetype"), EntitySubsystem->GetArchetypeForEntity(FloatEntities[i]), FloatsIntsArchetype);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_FragmentInstanceList, "System.Mass.Commands.FragmentInstanceList");


struct FCommands_FragmentMemoryCleanup : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		const UScriptStruct* ArrayFragmentTypes[] = {
			FTestFragment_Array::StaticStruct(), 
			FTestFragment_Int::StaticStruct()
		};
		const FMassArchetypeHandle ArrayArchetype = EntityManager->CreateArchetype(MakeArrayView(ArrayFragmentTypes, 1));
		const FMassArchetypeHandle ArrayIntArchetype = EntityManager->CreateArchetype(MakeArrayView(ArrayFragmentTypes, 2));
		const int32 EntitiesPerChunk = EntityManager->DebugGetArchetypeEntitiesCountPerChunk(ArrayArchetype);
		const int32 Count = static_cast<int32>(static_cast<float>(EntitiesPerChunk) * 2.5f);
		
		TArray<FMassEntityHandle> Entities;
		EntityManager->BatchCreateEntities(ArrayArchetype, Count, Entities);

		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(ArrayArchetype), Entities.Num());

		TArray<int32> EntitiesModified;
		for (int i = 0; i < Count; ++i)
		{
			if (FMath::FRand() < 0.2)
			{	
				FTestFragment_Array A;
				A.Value.Add(i);
				EntityManager->Defer().PushCommand<FMassCommandAddFragmentInstances>(Entities[i], A);
				EntityManager->Defer().AddFragment<FTestFragment_Int>(Entities[i]);
				EntitiesModified.Add(i);
			}
		}

		EntityManager->FlushCommands();

		for (int32 i : EntitiesModified)
		{
			FMassEntityView View(ArrayIntArchetype, Entities[i]);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Array>().Value.Num(), 1);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Array>().Value[0], i);
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_FragmentMemoryCleanup, "System.Mass.Commands.MemoryManagement");

// @todo add "add-then remove some to make holes in chunks-then add again" test
struct FCommands_BuildEntitiesWithFragments : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		const int32 EntitiesPerChunk = EntityManager->DebugGetArchetypeEntitiesCountPerChunk(FloatsIntsArchetype);
		const int32 Count = static_cast<int32>(static_cast<float>(EntitiesPerChunk) * 2.5f);

		TArray<FMassEntityHandle> Entities;
		for (int i = 0; i < Count; ++i)
		{
			Entities.Add(EntityManager->ReserveEntity());
			EntityManager->Defer().PushCommand<FMassCommandAddFragmentInstances>(Entities.Last(), FTestFragment_Int(i), FTestFragment_Float((float)i));
		}

		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), 0);
		EntityManager->FlushCommands();
		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), Entities.Num());

		for (int i = 0; i < Entities.Num(); ++i)
		{
			FMassEntityView View(FloatsIntsArchetype, Entities[i]);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Int>().Value, i);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Float>().Value, (float)i);
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_BuildEntitiesWithFragments, "System.Mass.Commands.BuildEntitiesWithFragments");

struct FCommands_BuildEntitiesInHoles : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		const int32 EntitiesPerChunk = EntityManager->DebugGetArchetypeEntitiesCountPerChunk(FloatsIntsArchetype);
		const int32 Count = static_cast<int32>(static_cast<float>(EntitiesPerChunk) * 1.25f) * 2; // making sure it's even

		TArray<FMassEntityHandle> Entities;
		EntityManager->BatchCreateEntities(FloatsIntsArchetype, Count, Entities);
		FMath::SRandInit(0);
		Algo::RandomShuffle(Entities);
		EntityManager->BatchDestroyEntities(MakeArrayView(Entities.GetData(), Entities.Num()/2));

		Entities.Reset();
		for (int i = 0; i < EntitiesPerChunk; ++i)
		{
			Entities.Add(EntityManager->ReserveEntity());
			EntityManager->Defer().PushCommand<FMassCommandAddFragmentInstances>(Entities.Last(), FTestFragment_Int(i), FTestFragment_Float((float)i));
		}

		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), Count / 2);
		EntityManager->FlushCommands();
		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), Count / 2 + Entities.Num());

		for (int i = 0; i < Entities.Num(); ++i)
		{
			FMassEntityView View(FloatsIntsArchetype, Entities[i]);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Int>().Value, i);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Float>().Value, (float)i);
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_BuildEntitiesInHoles, "System.Mass.Commands.BuildEntitiesInHoles");

struct FCommands_BuildEntitiesWithFragmentInstances : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		const int32 EntitiesPerChunk = EntityManager->DebugGetArchetypeEntitiesCountPerChunk(FloatsIntsArchetype);
		const int32 Count = static_cast<int32>(static_cast<float>(EntitiesPerChunk) * 2.5f);

		TArray<FMassEntityHandle> Entities;
		for (int i = 0; i < Count; ++i)
		{
			Entities.Add(EntityManager->ReserveEntity());
			EntityManager->Defer().PushCommand<FMassCommandBuildEntity>(Entities.Last(), FTestFragment_Int(i), FTestFragment_Float((float)i));
		}

		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), 0);
		EntityManager->FlushCommands();
		AITEST_EQUAL(TEXT("All entities created should be in ArrayArchetype"), EntityManager->DebugGetArchetypeEntitiesCount(FloatsIntsArchetype), Entities.Num());

		for (int i = 0; i < Entities.Num(); ++i)
		{
			FMassEntityView View(FloatsIntsArchetype, Entities[i]);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Int>().Value, i);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Float>().Value, (float)i);
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_BuildEntitiesWithFragmentInstances, "System.Mass.Commands.BuildEntitiesWithFragmentInstances");

struct FCommands_DeferredFunction : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		constexpr int32 Count = 5;
		const int32 Offset = 1000;

		TArray<FMassEntityHandle> Entities;
		EntityManager->BatchCreateEntities(IntsArchetype, Count, Entities);

		int i = 0;
		for (FMassEntityHandle Entity : Entities)
		{
			FMassEntityView View(IntsArchetype, Entity);
			View.GetFragmentData<FTestFragment_Int>().Value = Offset + i++;

			EntityManager->Defer().PushCommand<FMassDeferredSetCommand>([Entity, Archetype = IntsArchetype, Offset](FMassEntityManager& System)
				{
					FMassEntityView View(Archetype, Entity);
					View.GetFragmentData<FTestFragment_Int>().Value -= Offset;
				});
		}

		EntityManager->FlushCommands();

		for (i = 0; i < Entities.Num(); ++i)
		{
			FMassEntityView View(IntsArchetype, Entities[i]);
			AITEST_EQUAL(TEXT("Should have predicted values"), View.GetFragmentData<FTestFragment_Int>().Value, i);
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_DeferredFunction, "System.Mass.Commands.DeferredFunction");

// pushing commands while the main buffer is being flushed
struct FCommands_PushWhileFlushing : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		constexpr int32 Count = 5;

		// here's what we want to do:
		// 1. Create a Count number of Int entities
		// 2. Register TagA observer that will add a float fragment when the tag is added
		//	a. The observer will use EntityManager.Defer() directly for the testing purposes - it should use Context.Defer() in real world scenarios
		// 3. Add TagA to all the created Entities
		// 4. Test if all the affected entities have the float fragment after the flushing

		TArray<FMassEntityHandle> Entities;
		EntityManager->BatchCreateEntities(IntsArchetype, Count, Entities);
		for (const FMassEntityHandle& EntityHandle : Entities)
		{
			AITEST_NULL(TEXT("None of the freshly created entities is expexted to contain a float fragment")
				, EntityManager->GetFragmentDataPtr<FTestFragment_Float>(EntityHandle));
		}


		UMassTestProcessorBase* ObserverProcessor = NewObject<UMassTestProcessorBase>();
		ObserverProcessor->ForEachEntityChunkExecutionFunction = [](FMassExecutionContext& Context)
			{
				for (const FMassEntityHandle& EntityHandle : Context.GetEntities())
				{
					Context.GetEntityManagerChecked().Defer().AddFragment<FTestFragment_Float>(EntityHandle);
				}
			};
		FMassObserverManager& ObserverManager = EntityManager->GetObserverManager();
		ObserverManager.AddObserverInstance(*FTestTag_A::StaticStruct(), EMassObservedOperation::Add, *ObserverProcessor);

		EntityManager->Defer().PushCommand<FMassCommandAddTag<FTestTag_A>>(Entities);
		for (const FMassEntityHandle& EntityHandle : Entities)
		{
			AITEST_NULL(TEXT("Pushing the AddTag command should not result in adding the float fragment")
				, EntityManager->GetFragmentDataPtr<FTestFragment_Float>(EntityHandle));
		}

		EntityManager->FlushCommands();
		
		for (const FMassEntityHandle& EntityHandle : Entities)
		{
			AITEST_NOT_NULL(TEXT("After flushing all the observed entities should have the float fragment")
				, EntityManager->GetFragmentDataPtr<FTestFragment_Float>(EntityHandle));
		}

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCommands_PushWhileFlushing, "System.Mass.Commands.PushWhileFlushing");

#endif // WITH_MASSENTITY_DEBUG
} // FMassCommandsTest

UE_ENABLE_OPTIMIZATION_SHIP

#undef LOCTEXT_NAMESPACE
