// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StateTreeRunEnvQueryTask.h"

#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "GameplayStateTree"

EStateTreeRunStatus FStateTreeRunEnvQueryTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.QueryTemplate)
	{
		return EStateTreeRunStatus::Failed;
	}

	FEnvQueryRequest Request(InstanceData.QueryTemplate, InstanceData.QueryOwner);

	for (FAIDynamicParam& DynamicParam : InstanceData.QueryConfig)
	{
		Request.SetDynamicParam(DynamicParam, nullptr);
	}

	InstanceData.RequestId = Request.Execute(InstanceData.RunMode,
		FQueryFinishedSignature::CreateLambda([InstanceDataRef = Context.GetInstanceDataStructRef(*this)](TSharedPtr<FEnvQueryResult> QueryResult) mutable
			{
				if (FInstanceDataType* InstanceData = InstanceDataRef.GetPtr())
				{
					InstanceData->QueryResult = QueryResult;
					InstanceData->RequestId = INDEX_NONE;
				}
			}));
	return InstanceData.RequestId != INDEX_NONE ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FStateTreeRunEnvQueryTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.QueryResult)
	{
		if (InstanceData.QueryResult->IsSuccessful())
		{
			auto [VectorPtr, ActorPtr, ArrayOfVector, ArrayOfActor] = InstanceData.Result.GetMutablePtrTuple<FVector, AActor*, TArray<FVector>, TArray<AActor*>>(Context);
			if (VectorPtr)
			{
				*VectorPtr = InstanceData.QueryResult->GetItemAsLocation(0);
			}
			else if (ActorPtr)
			{
				*ActorPtr = InstanceData.QueryResult->GetItemAsActor(0);
			}
			else if (ArrayOfVector)
			{
				InstanceData.QueryResult->GetAllAsLocations(*ArrayOfVector);
			}
			else if (ArrayOfActor)
			{
				InstanceData.QueryResult->GetAllAsActors(*ArrayOfActor);
			}
			return EStateTreeRunStatus::Succeeded;
		}
		else
		{
			return EStateTreeRunStatus::Failed;
		}
	}
	return EStateTreeRunStatus::Running;
}

void FStateTreeRunEnvQueryTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.RequestId != INDEX_NONE)
	{
		if (UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(Context.GetOwner()))
		{
			QueryManager->AbortQuery(InstanceData.RequestId);
		}
		InstanceData.RequestId = INDEX_NONE;
	}
	InstanceData.QueryResult.Reset();
}

#if WITH_EDITOR
void FStateTreeRunEnvQueryTask::PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FStateTreeRunEnvQueryInstanceData, QueryTemplate))
	{
		FInstanceDataType& InstanceData = InstanceDataView.GetMutable<FInstanceDataType>();
		if (InstanceData.QueryTemplate)
		{
			InstanceData.QueryTemplate->CollectQueryParams(*InstanceData.QueryTemplate, InstanceData.QueryConfig);
			for (FAIDynamicParam& DynamicParam : InstanceData.QueryConfig)
			{
				DynamicParam.bAllowBBKey = false;
			}
		}
		else
		{
			InstanceData.QueryConfig.Reset();
		}
	}
	else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAIDynamicParam, bAllowBBKey))
	{
		FInstanceDataType& InstanceData = InstanceDataView.GetMutable<FInstanceDataType>();
		const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(FStateTreeRunEnvQueryInstanceData, QueryConfig).ToString());
		if (InstanceData.QueryConfig.IsValidIndex(ChangedIndex))
		{
			if (!InstanceData.QueryConfig[ChangedIndex].bAllowBBKey)
			{
				InstanceData.QueryConfig[ChangedIndex].BBKey.InvalidateResolvedKey();
			}
		}
	}
}

FText FStateTreeRunEnvQueryTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText QueryTemplateValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, QueryTemplate)), Formatting);
	if (QueryTemplateValue.IsEmpty())
	{
		QueryTemplateValue = FText::FromString(GetNameSafe(InstanceData->QueryTemplate));
	}

	if (Formatting == EStateTreeNodeFormatting::RichText)
	{
		return FText::Format(LOCTEXT("RunEQSRich", "<b>Run EQS Query</> {0}"), QueryTemplateValue);	
	}
	return FText::Format(LOCTEXT("RunEQS", "Run EQS Query {0}"), QueryTemplateValue);
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
