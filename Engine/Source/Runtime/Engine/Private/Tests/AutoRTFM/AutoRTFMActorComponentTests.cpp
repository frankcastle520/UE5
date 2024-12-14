// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AutoRTFM/AutoRTFM.h"
#include "AutoRTFMTestActor.h"
#include "AutoRTFMTestAnotherActor.h"
#include "AutoRTFMTestBodySetup.h"
#include "AutoRTFMTestChildActorComponent.h"
#include "AutoRTFMTestLevel.h"
#include "AutoRTFMTestObject.h"
#include "AutoRTFMTestPrimitiveComponent.h"
#include "Chaos/Core.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{

// Declares a new AutoRTFM actor component test with the given name.
// The test body should follow the call to the macro with braces.
// Prior to calling the test, the test will create the following objects and
// will be within scope to the test body:
// - UWorld* World
// - UAutoRTFMTestLevel* Level
// - AAutoRTFMTestActor* Actor
// - UAutoRTFMTestPrimitiveComponent* Component
//
// Initial state:
// - Level->OwningWorld will be assigned World.
// - Component will *not* be automatically registered.
//
// Example:
// AUTORTFM_ACTOR_COMPONENT_TEST(MyTest)
// {
//     // Test something using World, Level, Actor, Component.
// }
#define AUTORTFM_ACTOR_COMPONENT_TEST(NAME) \
	class FAutoRTFMTest##NAME : public FAutoRTFMActorComponentTestBase \
	{ \
		using FAutoRTFMActorComponentTestBase::FAutoRTFMActorComponentTestBase; \
		void Run(UWorld* World, UAutoRTFMTestLevel* Level, AAutoRTFMTestActor* Actor, UAutoRTFMTestPrimitiveComponent* Component) override; \
	}; \
	FAutoRTFMTest##NAME AutoRTFMTestInstance##NAME(TEXT(#NAME), TEXT(__FILE__), __LINE__); \
	void FAutoRTFMTest##NAME::Run(UWorld* World, UAutoRTFMTestLevel* Level, AAutoRTFMTestActor* Actor, UAutoRTFMTestPrimitiveComponent* Component)

// The base class used by the AUTORTFM_ACTOR_COMPONENT_TEST() tests
class FAutoRTFMActorComponentTestBase : public FAutomationTestBase
{
public:
	FAutoRTFMActorComponentTestBase(const TCHAR* InName, const TCHAR* File, int32 Line)
		: FAutomationTestBase(InName, /* bInComplexTask */ false), TestFile(File), TestLine(Line) {}

	// The AUTORTFM_ACTOR_COMPONENT_TEST() virtual function.
	virtual void Run(UWorld* World,
		UAutoRTFMTestLevel* Level,
		AAutoRTFMTestActor* Actor,
		UAutoRTFMTestPrimitiveComponent* Component) = 0;

	// GetTestFlags() changed return type between branches. Support old and new types.
	using GetTestFlagsReturnType = decltype(std::declval<FAutomationTestBase>().GetTestFlags());

	GetTestFlagsReturnType GetTestFlags() const
	{
		return EAutomationTestFlags::EngineFilter |
			EAutomationTestFlags::ClientContext |
			EAutomationTestFlags::ServerContext |
			EAutomationTestFlags::CommandletContext;
	}

	bool IsStressTest() const { return false; }
	uint32 GetRequiredDeviceNum() const override { return 1; }
	FString GetTestSourceFileName() const override { return TestFile; }
	int32 GetTestSourceFileLine() const override { return TestLine; }

protected:
	void GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const override
	{
		OutBeautifiedNames.Add("AutoRTFM.ActorComponent." + TestName);
		OutTestCommands.Add(FString());
	}

	FString GetBeautifiedTestName() const override { return "AutoRTFM.ActorComponent." + TestName; }

	// Implementation of the pure-virtual FAutomationTestBase::RunTest().
	// Skips the test with a message if IsAutoRTFMRuntimeEnabled() return false,
	// otherwise constructs the test World, Level, Actor and Component objects
	// and passes these to Run().
	bool RunTest(const FString& Parameters) override
	{
		if (AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
		{
			UWorld* World = NewObject<UWorld>();
			World->CreatePhysicsScene(nullptr);

			UAutoRTFMTestLevel* Level = NewObject<UAutoRTFMTestLevel>();
			Level->OwningWorld = World;
			AAutoRTFMTestActor* Actor = NewObject<AAutoRTFMTestActor>(Level);
			UAutoRTFMTestPrimitiveComponent* Component = NewObject<UAutoRTFMTestPrimitiveComponent>(Actor);

			Run(World, Level, Actor, Component);

			if (Component->IsRegistered())
			{
				Component->UnregisterComponent();
			}
		}
		else
		{
			FString Desc = FString::Printf(TEXT("SKIPPED test '%s'. AutoRTFM disabled."), GetData(TestName));
			ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, Desc));
		}
		return true;
	}

	// Adds an error message to the test with the provided What description.
	// File and Line should be the source file and line number that performed
	// the test, respectively.
	void Fail(const TCHAR* What, const TCHAR* File, unsigned int Line)
	{
		AddError(FString::Printf(TEXT("FAILED: %s:%u %s"), File, Line, What), 1);
	}

private:
	const TCHAR* const TestFile;
	const int32 TestLine;
};

// Tests that EXPR evaluates to true. If EXPR evaluates to false, then an error
// is raised and the function returns.
#define TEST_CHECK_TRUE(EXPR) do \
{ \
	if (!(EXPR)) \
	{ \
		Fail(TEXT("'") TEXT(#EXPR) TEXT("' was not true"), TEXT(__FILE__), __LINE__); \
		return; \
	} \
} while(false)

// Tests that EXPR evaluates to false. If EXPR evaluates to true, then an error
// is raised and the function returns.
#define TEST_CHECK_FALSE(EXPR) do \
{ \
	if ((EXPR)) \
	{ \
		Fail(TEXT("'") TEXT(#EXPR) TEXT("' was not false"), TEXT(__FILE__), __LINE__); \
		return; \
	} \
} while(false)

// General tests for calling RegisterComponent() and UnregisterComponent() in transactions.
// See: SOL-6709
AUTORTFM_ACTOR_COMPONENT_TEST(RegisterComponent_UnregisterComponent)
{
	Component->BodyInstance.ActorHandle = Chaos::FSingleParticlePhysicsProxy::Create(Chaos::FGeometryParticle::CreateParticle());
	Component->BodyInstance.ActorHandle->GetParticle_LowLevel()->SetGeometry(MakeImplicitObjectPtr<Chaos::FSphere>(Chaos::FVec3(1, 2, 3), 1));
	World->GetPhysicsScene()->GetSolver()->RegisterObject(Component->BodyInstance.ActorHandle);

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Component->RegisterComponent();

			if (Component->IsRegistered())
			{
				AutoRTFM::AbortTransaction();
			}
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_FALSE(Component->IsRegistered());

	bool bWasRegistered = false;

	AutoRTFM::Commit([&]
		{
			Component->RegisterComponent();
			bWasRegistered = Component->IsRegistered();
		});

	TEST_CHECK_TRUE(bWasRegistered);
	TEST_CHECK_TRUE(Component->IsRegistered());

	Result = AutoRTFM::Transact([&]
		{
			Component->UnregisterComponent();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_TRUE(Component->IsRegistered());

	AutoRTFM::Commit([&]
		{
			Component->UnregisterComponent();
		});

	TEST_CHECK_FALSE(Component->IsRegistered());
}

// Test aborting a call to Component::RegisterComponentWithWorld().
// See: FORT-761015
AUTORTFM_ACTOR_COMPONENT_TEST(RegisterComponentWithWorld)
{
	// Create a valid body setup so that there are shapes created
	Component->BodySetup = NewObject<UAutoRTFMTestBodySetup>();
	Component->BodySetup->AggGeom.SphereElems.Add(FKSphereElem(1.0f));

	AutoRTFM::ETransactionResult Result;
	Result = AutoRTFM::Transact([&]
		{
			Component->RegisterComponentWithWorld(World);
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_FALSE(Component->IsRegistered());

	AutoRTFM::Commit([&]
		{
			Component->RegisterComponentWithWorld(World);
		});

	TEST_CHECK_TRUE(Component->IsRegistered());
}

// Test aborting a call to Component::WeldTo().
// See: SOL-6757
AUTORTFM_ACTOR_COMPONENT_TEST(WeldTo)
{
	Component->RegisterComponent();

	FBodyInstance SomeInstance;

	// This test requires us to have a fresh body instance so that it has to be created during the register.
	Component->BodyInstance = FBodyInstance();
	Component->BodyInstance.bSimulatePhysics = 1;
	Component->BodyInstance.WeldParent = &SomeInstance;
	TEST_CHECK_TRUE(Component->IsWelded());

	UAutoRTFMTestBodySetup* BodySetup = NewObject<UAutoRTFMTestBodySetup>();
	BodySetup->AggGeom.SphereElems.Add(FKSphereElem(1.0f));

	Component->BodyInstance.BodySetup = BodySetup;

	UAutoRTFMTestPrimitiveComponent* Parent0 = NewObject<UAutoRTFMTestPrimitiveComponent>(Actor);
	UAutoRTFMTestPrimitiveComponent* Parent1 = NewObject<UAutoRTFMTestPrimitiveComponent>(Actor);

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Component->WeldTo(Parent0);
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_TRUE(Component->IsWelded());
	TEST_CHECK_TRUE(&SomeInstance == Component->BodyInstance.WeldParent);

	AutoRTFM::Commit([&]
		{
			Component->WeldTo(Parent0);
		});

	TEST_CHECK_FALSE(Component->IsWelded());
	TEST_CHECK_TRUE(nullptr == Component->BodyInstance.WeldParent);

	Result = AutoRTFM::Transact([&]
		{
			Component->WeldTo(Parent1);
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_FALSE(Component->IsWelded());

	AutoRTFM::Commit([&]
		{
			Component->WeldTo(Parent1);
		});

	TEST_CHECK_FALSE(Component->IsWelded());

	Result = AutoRTFM::Transact([&]
		{
			Component->UnWeldFromParent();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_FALSE(Component->IsWelded());

	AutoRTFM::Commit([&]
		{
			Component->UnWeldFromParent();
		});

	TEST_CHECK_FALSE(Component->IsWelded());
}

// Test calling Component->UnregisterComponent() on a Component with an event
// listener for OnComponentPhysicsStateChanged().
// See: SOL-6765
AUTORTFM_ACTOR_COMPONENT_TEST(FSparseDelegate)
{
	UAutoRTFMTestObject* const Object = NewObject<UAutoRTFMTestObject>();

	Component->RegisterComponent();
	Component->OnComponentPhysicsStateChanged.AddDynamic(Object, &UAutoRTFMTestObject::OnComponentPhysicsStateChanged);

	TEST_CHECK_FALSE(Object->bHitOnComponentPhysicsStateChanged);

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Component->UnregisterComponent();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_FALSE(Object->bHitOnComponentPhysicsStateChanged);

	AutoRTFM::Commit([&]
		{
			Component->UnregisterComponent();
		});

	TEST_CHECK_TRUE(Object->bHitOnComponentPhysicsStateChanged);
}

AUTORTFM_ACTOR_COMPONENT_TEST(ChildActor)
{
	UAutoRTFMTestChildActorComponent* const ChildActorComponent = NewObject<UAutoRTFMTestChildActorComponent>(Actor);

	AAutoRTFMTestAnotherActor* const AnotherActor = NewObject<AAutoRTFMTestAnotherActor>();

	ChildActorComponent->RegisterComponentWithWorld(World);

	ChildActorComponent->ForceActorClass(AnotherActor->GetClass());

	if (nullptr != ChildActorComponent->GetChildActor())
	{
		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				ChildActorComponent->DestroyChildActor();
				AutoRTFM::AbortTransaction();
			});

		TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
		TEST_CHECK_TRUE(nullptr != ChildActorComponent->GetChildActor());

		Result = AutoRTFM::Transact([&]
			{
				ChildActorComponent->DestroyChildActor();
			});

		TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
		TEST_CHECK_TRUE(nullptr == ChildActorComponent->GetChildActor());
	}

	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			ChildActorComponent->CreateChildActor();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_TRUE(nullptr == ChildActorComponent->GetChildActor());

	Result = AutoRTFM::Transact([&]
		{
			ChildActorComponent->CreateChildActor();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(nullptr != ChildActorComponent->GetChildActor());

	Result = AutoRTFM::Transact([&]
		{
			ChildActorComponent->DestroyChildActor();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
	TEST_CHECK_TRUE(nullptr != ChildActorComponent->GetChildActor());

	Result = AutoRTFM::Transact([&]
		{
			ChildActorComponent->DestroyChildActor();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::Committed == Result);
	TEST_CHECK_TRUE(nullptr == ChildActorComponent->GetChildActor());
}

// Test aborting a call to USkeletalMeshComponent::RegisterComponent() with an assigned skeletal
// mesh and empty PostProcessAnimInstance.
// See: SOL-6779
AUTORTFM_ACTOR_COMPONENT_TEST(USkeletalMeshComponent)
{
	USkeleton* Skeleton = NewObject<USkeleton>();
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>();
	SkeletalMesh->SetSkeleton(Skeleton);
	SkeletalMesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	TRefCountPtr<FSkeletalMeshLODRenderData> LODRenderData = MakeRefCount<FSkeletalMeshLODRenderData>();
	RenderData->LODRenderData.Add(LODRenderData);
	USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(Actor);
	SkeletalMeshComponent->SetSkeletalMeshAsset(SkeletalMesh);
	SkeletalMeshComponent->PostProcessAnimInstance = NewObject<UAnimInstance>(SkeletalMeshComponent);

	AutoRTFM::ETransactionResult Result;
	Result = AutoRTFM::Transact([&]
		{
			SkeletalMeshComponent->RegisterComponent();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
}


// Test aborting a call to AAutoRTFMTestActor::CreateComponentFromTemplate().
// See: SOL-7002
AUTORTFM_ACTOR_COMPONENT_TEST(CreateComponentFromTemplate)
{
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Actor->CreateComponentFromTemplate(Component);
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
}

// Test aborting a call to UObject::GetArchetype().
// See: SOL-7024
AUTORTFM_ACTOR_COMPONENT_TEST(GetArchetype)
{
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			Actor->GetArchetype();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
}

// Test aborting a call to FUObjectArray::CloseDisregardForGC().
// See: SOL-7027
AUTORTFM_ACTOR_COMPONENT_TEST(CloseDisregardForGC)
{
	FUObjectArray ObjectArray;
	AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			ObjectArray.CloseDisregardForGC();
			AutoRTFM::AbortTransaction();
		});

	TEST_CHECK_TRUE(AutoRTFM::ETransactionResult::AbortedByRequest == Result);
}

}  // anonymous namespace

#undef TEST_CHECK_FALSE
#undef TEST_CHECK_TRUE
#undef AUTORTFM_ACTOR_COMPONENT_TEST

#endif //WITH_DEV_AUTOMATION_TESTS
