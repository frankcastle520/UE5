// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "StateTreePropertyRef.h"
#include "StateTreePropertyFunctionBase.h"
#include "StateTreeTestTypes.generated.h"

class UStateTree;
struct FStateTreeInstanceData;

// Test log that can be passed as external data.
USTRUCT()
struct FStateTreeTestLog
{
	GENERATED_BODY()

	struct FLogItem
	{
		FLogItem() = default;
		FLogItem(const FName& InName, const FString& InMessage) : Name(InName), Message(InMessage) {}
		FName Name;
		FString Message; 
	};
	TArray<FLogItem> Items;
	
	void Log(const FName& Name, const FString& Message)
	{
		Items.Emplace(Name, Message);
	}
};

struct FTestStateTreeExecutionContext : public FStateTreeExecutionContext
{
	
	FTestStateTreeExecutionContext(UObject& InOwner, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData)
		: FStateTreeExecutionContext(InOwner, InStateTree, InInstanceData)
	{
		// Handle getting pointer to the test log.
		FStateTreeDataView TestLogView(TBaseStructure<FStateTreeTestLog>::Get(), (uint8*)&Log);
		
		CollectExternalDataDelegate = FOnCollectStateTreeExternalData::CreateLambda(
			[TestLogView](const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDataDescs, TArrayView<FStateTreeDataView> OutDataViews)
			{
				for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
				{
					const FStateTreeExternalDataDesc& ItemDesc = ExternalDataDescs[Index];
					if (ItemDesc.Struct == TBaseStructure<FStateTreeTestLog>::Get())
					{
						OutDataViews[Index] = TestLogView;
						break;
					}
				}
				return true;
			});
	}

	FStateTreeTestLog Log;

	void LogClear()
	{
		Log.Items.Empty();
	}

	struct FLogOrder
	{
		FLogOrder(const FTestStateTreeExecutionContext& InContext, const int32 InIndex) : Context(InContext), Index(InIndex) {}

		FLogOrder Then(const FName& Name, const FString& Message) const
		{
			int32 NextIndex = Index;
			while (NextIndex < Context.Log.Items.Num())
			{
				const FStateTreeTestLog::FLogItem& Item = Context.Log.Items[NextIndex];
				if (Item.Name == Name && Item.Message == Message)
				{
					break;
				}
				NextIndex++;
			}
			return FLogOrder(Context, NextIndex);
		}

		operator bool() const { return Index < Context.Log.Items.Num(); }
		
		const FTestStateTreeExecutionContext& Context;
		int32 Index = 0;
	};

	FLogOrder Expect(const FName& Name, const FString& Message) const
	{
		return FLogOrder(*this, 0).Then(Name, Message);
	}

	template <class ...Args>
	bool ExpectInActiveStates(const Args&... States)
	{
		FName ExpectedStateNames[] = { States... };
		const int32 NumExpectedStateNames = sizeof...(States);

		TArray<FName> ActiveStateNames = GetActiveStateNames();

		if (ActiveStateNames.Num() != NumExpectedStateNames)
		{
			return false;
		}

		for (int32 Index = 0; Index != NumExpectedStateNames; Index++)
		{
			if (ExpectedStateNames[Index] != ActiveStateNames[Index])
			{
				return false;
			}
		}

		return true;
	}
	
};

USTRUCT()
struct FTestEval_AInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	float FloatA = 0.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 IntA = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bBoolA = false;
};

USTRUCT()
struct FTestEval_A : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestEval_AInstanceData;

	FTestEval_A() = default;
	FTestEval_A(const FName InName) { Name = InName; }
	virtual ~FTestEval_A() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT()
struct FTestTask_BInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FloatB = 0.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 IntB = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bBoolB = false;
};

USTRUCT()
struct FTestTask_B : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestTask_BInstanceData;

	FTestTask_B() = default;
	FTestTask_B(const FName InName) { Name = InName; }
	virtual ~FTestTask_B() override {}
	
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		TestLog.Log(Name,  TEXT("EnterState"));
		return EStateTreeRunStatus::Running;
	}

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkExternalData(LogHandle);
		return true;
	}
	
	TStateTreeExternalDataHandle<FStateTreeTestLog> LogHandle;
};

USTRUCT()
struct FTestTask_PrintValueInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 Value = 0;
};

USTRUCT()
struct FTestTask_PrintValue : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestTask_PrintValueInstanceData;

	FTestTask_PrintValue() = default;
	FTestTask_PrintValue(const FName InName) { Name = InName; }
	virtual ~FTestTask_PrintValue() override {}
	
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		TestLog.Log(Name,  FString::Printf(TEXT("EnterState%d"), InstanceData.Value));
		return EStateTreeRunStatus::Running;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		TestLog.Log(Name,  FString::Printf(TEXT("ExitState%d"), InstanceData.Value));
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		TestLog.Log(Name,  FString::Printf(TEXT("Tick%d"), InstanceData.Value));
		
		return EStateTreeRunStatus::Running;
	};

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkExternalData(LogHandle);
		return true;
	}
	
	TStateTreeExternalDataHandle<FStateTreeTestLog> LogHandle;
};

USTRUCT()
struct FTestTask_PrintAndResetValue : public FTestTask_PrintValue
{
	GENERATED_BODY()

	using FTestTask_PrintValue::FTestTask_PrintValue;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		EStateTreeRunStatus Status = Super::EnterState(Context, Transition);
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		InstanceData.Value = 0;
		return Status;
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		EStateTreeRunStatus Status = Super::Tick(Context, DeltaTime);
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		InstanceData.Value = 0;
		return Status;
	};
};


USTRUCT()
struct FTestTask_StopTreeInstanceData
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTask_StopTree : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestTask_PrintValueInstanceData;

	FTestTask_StopTree() = default;
	explicit FTestTask_StopTree(const FName InName) { Name = InName; }
	virtual ~FTestTask_StopTree() override {}
	
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		if (Phase == EStateTreeUpdatePhase::EnterStates)
		{
			return Context.Stop();
		}
		return EStateTreeRunStatus::Running;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		if (Phase == EStateTreeUpdatePhase::ExitStates)
		{
			Context.Stop();
		}
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		if (Phase == EStateTreeUpdatePhase::TickStateTree)
		{
			return Context.Stop();
		}
		return EStateTreeRunStatus::Running;
	};

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkExternalData(LogHandle);
		return true;
	}
	
	TStateTreeExternalDataHandle<FStateTreeTestLog> LogHandle;
	
	/** Indicates in which phase the call to Stop should be performed. Possible values are EnterStates, ExitStats and TickStateTree */
	UPROPERTY()
	EStateTreeUpdatePhase Phase = EStateTreeUpdatePhase::Unset;
};

USTRUCT()
struct FTestTask_StandInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 Value = 0;
	
	UPROPERTY()
	int32 CurrentTick = 0;
};

USTRUCT()
struct FTestTask_Stand : public FStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestTask_StandInstanceData;
	
	FTestTask_Stand() = default;
	FTestTask_Stand(const FName InName) { Name = InName; }
	virtual ~FTestTask_Stand() {}

	virtual const UStruct* GetInstanceDataType() const { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		TestLog.Log(Name, TEXT("EnterState"));

		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		
		if (Transition.ChangeType == EStateTreeStateChangeType::Changed)
		{
			InstanceData.CurrentTick = 0;
		}
		return EnterStateResult;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);

		if (Transition.CurrentRunStatus == EStateTreeRunStatus::Succeeded)
		{
			TestLog.Log(Name, TEXT("ExitSucceeded"));
		}
		else if (Transition.CurrentRunStatus == EStateTreeRunStatus::Failed)
		{
			TestLog.Log(Name, TEXT("ExitFailed"));
		}
		else if (Transition.CurrentRunStatus == EStateTreeRunStatus::Stopped)
		{
			TestLog.Log(Name, TEXT("ExitStopped"));
		}
		
		TestLog.Log(Name, TEXT("ExitState"));
	}

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		TestLog.Log(Name, TEXT("StateCompleted"));
	}
	
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FStateTreeTestLog& TestLog = Context.GetExternalData(LogHandle);
		TestLog.Log(Name, TEXT("Tick"));

		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		
		InstanceData.CurrentTick++;
		
		return (InstanceData.CurrentTick >= TicksToCompletion) ? TickCompletionResult : EStateTreeRunStatus::Running;
	};

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkExternalData(LogHandle);
		return true;
	}
	
	TStateTreeExternalDataHandle<FStateTreeTestLog> LogHandle;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 TicksToCompletion = 1;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus TickCompletionResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};

USTRUCT()
struct FStateTreeTestConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Parameters)
	int32 Count = 1;

	static std::atomic<int32> GlobalCounter;
};

USTRUCT(meta = (Hidden))
struct FStateTreeTestCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTestConditionInstanceData;

	FStateTreeTestCondition() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override
	{
		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
		InstanceData.GlobalCounter.fetch_add(InstanceData.Count);
		return true;
	}
};

struct FStateTreeTestRunContext
{
	int32 Count = 0;
};


USTRUCT()
struct FStateTreeTest_PropertyStructA
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	int32 A = 0;
};

USTRUCT()
struct FStateTreeTest_PropertyStructB
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	int32 B = 0;
};

USTRUCT()
struct FStateTreeTest_PropertyStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	int32 A = 0;

	UPROPERTY(EditAnywhere, Category = "")
	int32 B = 0;

	UPROPERTY(EditAnywhere, Category = "")
	FStateTreeTest_PropertyStructB StructB;
};

UCLASS(HideDropdown)
class UStateTreeTest_PropertyObjectInstanced : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "")
	int32 A = 0;

	UPROPERTY(EditAnywhere, Category = "")
	FInstancedStruct InstancedStruct;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<FGameplayTag> ArrayOfTags;
};

UCLASS(HideDropdown)
class UStateTreeTest_PropertyObjectInstancedWithB : public UStateTreeTest_PropertyObjectInstanced
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "")
	int32 B = 0;
};

UCLASS(HideDropdown)
class UStateTreeTest_PropertyObject : public UObject
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere, Instanced, Category = "")
	TObjectPtr<UStateTreeTest_PropertyObjectInstanced> InstancedObject;

	UPROPERTY(EditAnywhere, Instanced, Category = "")
	TArray<TObjectPtr<UStateTreeTest_PropertyObjectInstanced>> ArrayOfInstancedObjects;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<int32> ArrayOfInts;

	UPROPERTY(EditAnywhere, Category = "")
	FInstancedStruct InstancedStruct;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<FInstancedStruct> ArrayOfInstancedStructs;

	UPROPERTY(EditAnywhere, Category = "")
	FStateTreeTest_PropertyStruct Struct;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<FStateTreeTest_PropertyStruct> ArrayOfStruct;
};

UCLASS(HideDropdown)
class UStateTreeTest_PropertyObject2 : public UObject
{
	GENERATED_BODY()
public:
};

USTRUCT()
struct FStateTreeTest_PropertyCopy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	FStateTreeTest_PropertyStruct Item;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<FStateTreeTest_PropertyStruct> Array;
};

USTRUCT()
struct FStateTreeTest_PropertyRefSourceStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	FStateTreeTest_PropertyStruct Item;

	UPROPERTY(EditAnywhere, Category = "Output")
	FStateTreeTest_PropertyStruct OutputItem;

	UPROPERTY(EditAnywhere, Category = "")
	TArray<FStateTreeTest_PropertyStruct> Array;
};

USTRUCT()
struct FStateTreeTest_PropertyRefTargetStruct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "", meta = (RefType = "/Script/StateTreeTestSuite.StateTreeTest_PropertyStruct"))
	FStateTreePropertyRef RefToStruct;

	UPROPERTY(EditAnywhere, Category = "", meta = (RefType = "Int32"))
	FStateTreePropertyRef RefToInt;

	UPROPERTY(EditAnywhere, Category = "", meta = (RefType = "/Script/StateTreeTestSuite.StateTreeTest_PropertyStruct", IsRefToArray))
	FStateTreePropertyRef RefToStructArray;
};

USTRUCT()
struct FStateTreeTest_PropertyCopyObjects
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "")
	TObjectPtr<UObject> Object;

	UPROPERTY(EditAnywhere, Category = "")
	TSubclassOf<UObject> Class;

	UPROPERTY(EditAnywhere, Category = "")
	TSoftObjectPtr<UObject> SoftObject;

	UPROPERTY(EditAnywhere, Category = "")
	TSoftClassPtr<UObject> SoftClass;
};

USTRUCT()
struct FTestPropertyFunction_InstanceData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Input = 0;

	UPROPERTY()
	int32 Result = 0;
};

USTRUCT()
struct FTestPropertyFunction : public FStateTreePropertyFunctionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FTestPropertyFunction_InstanceData;

	FTestPropertyFunction() = default;
	FTestPropertyFunction(const FName InName) { Name = InName; }
	
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual void Execute(FStateTreeExecutionContext& Context) const override
	{
		FTestPropertyFunction_InstanceData& InstanceData = Context.GetInstanceData<FTestPropertyFunction_InstanceData>(*this);
		InstanceData.Result = InstanceData.Input + 1;
	}
};
