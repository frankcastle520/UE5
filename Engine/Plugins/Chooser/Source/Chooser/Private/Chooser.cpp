// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"
#include "ChooserTrace.h"
#include "ObjectTrace.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/Blueprint.h"
#include "ChooserIndexArray.h"
#include "IChooserParameterGameplayTag.h"
#include "UObject/AssetRegistryTagsContext.h"

#if WITH_EDITORONLY_DATA
#include "UObject/UObjectIterator.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(Chooser)

DEFINE_LOG_CATEGORY(LogChooser)


#if WITH_EDITOR
const FName UChooserTable::PropertyNamesTag = "ChooserPropertyNames";
const FString UChooserTable::PropertyTagDelimiter = TEXT(";");
#endif

UChooserTable::UChooserTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

}

void UChooserTable::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	for (FInstancedStruct& ColumnData : ColumnsStructs)
	{
		if (ColumnData.IsValid())
		{
			FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
			Column.PostLoad();
		}
	}

	CachedPreviousOutputObjectType = OutputObjectType;
	CachedPreviousResultType = ResultType;

	if (Version != 1)
	{
		if (ParentTable)
		{
			RootChooser = ParentTable;
			ParentTable = nullptr;
		
			SetFlags(RF_Transactional); // fix nested chooser objects not created with Transactional flag
		
			// fix for broken outer object on nested chooser tables
			if (GetOuter() == GetPackage())
			{
				Rename(nullptr, RootChooser);
			}
		}

		if(RootChooser == nullptr && NestedChoosers.IsEmpty())
		{
			// data upgrade for root tables: add elements to NestedTables list
			TArray<UObject*> ChildObjects;
			GetObjectsWithOuter(GetPackage(), ChildObjects, true);
			for(UObject* ChildObject : ChildObjects)
			{
				if(UChooserTable* Chooser = Cast<UChooserTable>(ChildObject))
				{
					NestedChoosers.Add(Chooser);
				}
			}
		}
		Version = 1;
	}
#endif

	Compile();
}

void UChooserTable::BeginDestroy()
{
	ColumnsStructs.Empty();
#if WITH_EDITORONLY_DATA
	ResultsStructs.Empty();
#endif
	CookedResults.Empty();
	Super::BeginDestroy();
}

#if WITH_EDITOR

void UChooserTable::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	Super::GetAssetRegistryTags(OutTags);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

void UChooserTable::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	// Output property names we use
	TStringBuilder<256> PropertyNamesBuilder;
	PropertyNamesBuilder.Append(PropertyTagDelimiter);

	for(const FInstancedStruct& Column : ColumnsStructs)
	{
		if (const FChooserColumnBase* ColumnBase = Column.GetPtr<FChooserColumnBase>())
		{
			if (FChooserParameterBase* Parameter = const_cast<FChooserColumnBase*>(ColumnBase)->GetInputValue())
			{
				Parameter->AddSearchNames(PropertyNamesBuilder);
			}
		}
	}

	Context.AddTag(FAssetRegistryTag(PropertyNamesTag, PropertyNamesBuilder.ToString(), FAssetRegistryTag::TT_Hidden));
}

void UChooserTable::AddCompileDependency(const UStruct* InStructType)
{
	UStruct* StructType = const_cast<UStruct*>(InStructType);
	if (!CompileDependencies.Contains(StructType))
	{
		if (UUserDefinedStruct* UserDefinedStruct = Cast<UUserDefinedStruct>(StructType))
		{
			UserDefinedStruct->ChangedEvent.AddUObject(this, &UChooserTable::OnDependentStructChanged);
			CompileDependencies.Add(StructType);
		}
		else if (UClass* Class = Cast<UClass>(StructType))
		{
			if(UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
			{
				Blueprint->OnCompiled().AddUObject(this, &UChooserTable::OnDependencyCompiled);
				CompileDependencies.Add(StructType);
			}
		}
	}
}

#endif

void UChooserTable::Compile(bool bForce)
{
	IHasContextClass* ContextOwner = GetRootChooser();
	
	for (FInstancedStruct& ColumnData : ColumnsStructs)
	{
		if (ColumnData.IsValid())
		{
			FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
			Column.Compile(ContextOwner, bForce);
		}
	}

#if WITH_EDITORONLY_DATA
	for(FInstancedStruct& ResultData : ResultsStructs)
	{
		if (ResultData.IsValid())
		{
			FObjectChooserBase& Result = ResultData.GetMutable<FObjectChooserBase>();
			Result.Compile(ContextOwner, bForce);
		}
	}
#endif

	if (IsCookedData())
	{
		for(FInstancedStruct& ResultData : CookedResults)
		{
			if (ResultData.IsValid())
			{
				FObjectChooserBase& Result = ResultData.GetMutable<FObjectChooserBase>();
				Result.Compile(ContextOwner, bForce);
			}
		}
	}
}

void UChooserTable::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsCooking())
	{
		CookData();
	}
#endif
	
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		// convert old data if it exists

		if (ContextObjectType_DEPRECATED)
		{
			ContextData.SetNum(1);
			ContextData[0].InitializeAs<FContextObjectTypeClass>();
			FContextObjectTypeClass& Context = ContextData[0].GetMutable<FContextObjectTypeClass>();
			Context.Class = ContextObjectType_DEPRECATED;
			Context.Direction = EContextObjectDirection::ReadWrite;
			ContextObjectType_DEPRECATED = nullptr;
		}

		if (Results_DEPRECATED.Num() > 0 || Columns_DEPRECATED.Num() > 0)
		{
			ResultsStructs.Reserve(Results_DEPRECATED.Num());
			ColumnsStructs.Reserve(Columns_DEPRECATED.Num());

			for (TScriptInterface<IObjectChooser>& Result : Results_DEPRECATED)
			{
				ResultsStructs.SetNum(ResultsStructs.Num() + 1);
				IObjectChooser* ResultInterface = Result.GetInterface();
				if (ResultInterface)
				{
					ResultInterface->ConvertToInstancedStruct(ResultsStructs.Last());
				}
			}

			for (TScriptInterface<IChooserColumn>& Column : Columns_DEPRECATED)
			{
				ColumnsStructs.SetNum(ColumnsStructs.Num() + 1);
				IChooserColumn* ColumnInterface = Column.GetInterface();
				if (ColumnInterface)
				{
					ColumnInterface->ConvertToInstancedStruct(ColumnsStructs.Last());
				}
			}

			Results_DEPRECATED.SetNum(0);
			Columns_DEPRECATED.SetNum(0);
		}
	}
#endif
}

#if WITH_EDITORONLY_DATA
void UChooserTable::RemoveDisabledData()
{
	// remove disabled or invalid columns
	ColumnsStructs.RemoveAll([](const FInstancedStruct& ColumnStruct)
	{
		return !ColumnStruct.IsValid() || ColumnStruct.Get<FChooserColumnBase>().bDisabled;
	});
	
	// remove disabled rows and corresponding row data from columns
	TArray<uint32> RowsToDelete;
	const int NumResults = ResultsStructs.Num();
	for (int ResultIndex = NumResults - 1; ResultIndex>=0; ResultIndex--)
	{
		if (IsRowDisabled(ResultIndex))
		{
			RowsToDelete.Add(ResultIndex);
		}
	}
	
	DisabledRows.SetNum(0);

	for (uint32 Index : RowsToDelete)
	{
		ResultsStructs.RemoveAt(Index);
	}
	for(FInstancedStruct& Column : ColumnsStructs)
	{
		Column.GetMutable<FChooserColumnBase>().DeleteRows(RowsToDelete);
	}
}

void UChooserTable::CookData()
{
	RemoveDisabledData();

	// copy stripped results struct into CookedResults array
	CookedResults = ResultsStructs;
}
#endif

#if WITH_EDITOR
void UChooserTable::PostEditUndo()
{
	UObject::PostEditUndo();

	if (CachedPreviousOutputObjectType != OutputObjectType || CachedPreviousResultType != ResultType)
	{
		OnOutputObjectTypeChanged.Broadcast(OutputObjectType);
		CachedPreviousOutputObjectType = OutputObjectType;
		CachedPreviousResultType = ResultType;
	}
	OnContextClassChanged.Broadcast();
}

void UChooserTable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		static FName OutputObjectTypeName = "OutputObjectType";
		static FName ResultTypeName = "ResultType";
		if (PropertyChangedEvent.Property->GetName() == OutputObjectTypeName)
		{
			if (CachedPreviousOutputObjectType != OutputObjectType)
			{
				OnOutputObjectTypeChanged.Broadcast(OutputObjectType);
				CachedPreviousOutputObjectType = OutputObjectType;
			}
		}
		else if (PropertyChangedEvent.Property->GetName() == ResultTypeName)
		{
			if (CachedPreviousResultType != ResultType)
			{
				OnOutputObjectTypeChanged.Broadcast(OutputObjectType);
				CachedPreviousResultType = ResultType;
			}
		}
		else
		{
			OnContextClassChanged.Broadcast();
		}
	}
	else
	{
		OnOutputObjectTypeChanged.Broadcast(OutputObjectType);
		OnContextClassChanged.Broadcast();
	}
}

void UChooserTable::IterateRecentContextObjects(TFunction<void(const FString&)> Callback) const
{
	FScopeLock Lock(&DebugLock);
	for (const FString& ObjectName : RecentContextObjects)
	{
		Callback(ObjectName);
	}
}

void UChooserTable::UpdateDebugging(FChooserEvaluationContext& Context) const
{
	FScopeLock Lock(&DebugLock);

	const UChooserTable* RootTable = GetRootChooser();

	for (const FStructView& Param : Context.Params)
	{
		if (const FChooserEvaluationInputObject* ObjectParam = Param.GetPtr<const FChooserEvaluationInputObject>())
		{
			if (UObject* ContextObject = ObjectParam->Object)
			{
				FString DebugName = ContextObject->GetName();
				if (UObject* Outer = ContextObject->GetTypedOuter(AActor::StaticClass()))
				{
					DebugName += " in " + Outer->GetName();					
				}

				if (UWorld* World = ContextObject->GetWorld())
				{
					if (World->IsPreviewWorld())
					{
						DebugName += " (Preview)";
					}
					else if (World->GetNetMode() == ENetMode::NM_DedicatedServer)
					{
						DebugName += " (Server)";
					}
					else if (World->GetNetMode() == ENetMode::NM_Client)
					{
						DebugName += FString(" (Client ") + FString::FromInt(World->GetOutermost()->GetPIEInstanceID()) + ")";
					}
				}

				AddRecentContextObject(DebugName);

				if (DebugName == RootTable->GetDebugTargetName())
				{
					bDebugTestValuesValid = true;
					Context.DebuggingInfo.bCurrentDebugTarget = true;
					return;
				}
			}
		}
	}
	Context.DebuggingInfo.bCurrentDebugTarget = false;
}

#endif

bool UChooserTable::ResultAssetFilter(const FAssetData& AssetData)
{
	return !AssetData.IsInstanceOf(OutputObjectType);
}

FObjectChooserBase::EIteratorStatus UChooserTable::EvaluateChooser(FChooserEvaluationContext& Context, const UChooserTable* Chooser, FObjectChooserBase::FObjectChooserIteratorCallback Callback)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EvaluateChooser);

	if (Chooser == nullptr)
	{
		return FObjectChooserBase::EIteratorStatus::Continue;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Chooser->GetName());

	VALIDATE_CHOOSER_CONTEXT(Chooser, Chooser->ContextData, Context);

#if WITH_EDITOR
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EvaluateChooser_Debugging);
		Chooser->UpdateDebugging(Context);
	}
#endif
	
#if CHOOSER_DEBUGGING_ENABLED
	Context.DebuggingInfo.CurrentChooser = Chooser;
#endif

	const TArray<FInstancedStruct>* ResultsArray = &Chooser->CookedResults;

#if WITH_EDITORONLY_DATA
	if (!Chooser->IsCookedData())
	{
		ResultsArray = &Chooser->ResultsStructs;
	}
#endif
	

	uint32 Count = ResultsArray->Num();
	uint32 BufferSize = Count * sizeof(FChooserIndexArray::FIndexData);

	FChooserIndexArray Indices1(static_cast<FChooserIndexArray::FIndexData*>(FMemory_Alloca(BufferSize)), Count);
	FChooserIndexArray Indices2(static_cast<FChooserIndexArray::FIndexData*>(FMemory_Alloca(BufferSize)), Count);

	for(uint32 i=0;i<Count;i++)
	{
		if (!Chooser->IsRowDisabled(i))
		{
			Indices1.Push({i, 0});
		}
	}
	FChooserIndexArray* IndicesOut = &Indices1;
	FChooserIndexArray* IndicesIn = &Indices2;

	for (const FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
	{
		const FChooserColumnBase& Column = ColumnData.Get<FChooserColumnBase>();

#if WITH_EDITORONLY_DATA
		if (Column.bDisabled)
		{
			continue;
		}
#endif
		
		if (Column.HasFilters())
		{
			Swap(IndicesIn, IndicesOut);
			IndicesOut->SetNum(0);
			Column.Filter(Context, *IndicesIn, *IndicesOut);
			
			if (IndicesIn->HasCosts() || Column.HasCosts())
			{
				IndicesOut->SetHasCosts();
			}
		}
	}

	//	No need to score with only one valid option
	if (IndicesOut->Num() > 1 && IndicesOut->HasCosts())
	{
		Algo::Sort(*IndicesOut);
	}
	
	bool bSetOutputs = false;
	
	// of the rows that passed all column filters, iterate through them calling the callback until it returns Stop
	for (FChooserIndexArray::FIndexData& SelectedIndexData : *IndicesOut)
	{
		if (ResultsArray->IsValidIndex(SelectedIndexData.Index))
		{
			const FObjectChooserBase& SelectedResult = (*ResultsArray)[SelectedIndexData.Index].Get<FObjectChooserBase>();
			FObjectChooserBase::EIteratorStatus Status = SelectedResult.ChooseMulti(Context, Callback);
			if (Status != FObjectChooserBase::EIteratorStatus::Continue)
			{
				bSetOutputs = true;
				// trigger all output columns
				for (const FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
				{
					const FChooserColumnBase& Column = ColumnData.Get<FChooserColumnBase>();

					#if WITH_EDITORONLY_DATA
					if (Column.bDisabled)
					{
						continue;
					}
					#endif
					
					Column.SetOutputs(Context, SelectedIndexData.Index);
				}
				#if WITH_EDITOR
				if (Context.DebuggingInfo.bCurrentDebugTarget)
				{
					Chooser->SetDebugSelectedRow(SelectedIndexData.Index);
				}
				#endif
				TRACE_CHOOSER_EVALUATION(Chooser, Context, SelectedIndexData.Index);
			}
			if (Status == FObjectChooserBase::EIteratorStatus::Stop)
			{
				return FObjectChooserBase::EIteratorStatus::Stop;
			}
		}
	}

	// if no rows passed, or the ones which passed contained nested choosers which failed,
	// then return the FallbackResult and output the FallbackValue from each output column
	if (!bSetOutputs)
	{
		#if WITH_EDITOR
		if (Context.DebuggingInfo.bCurrentDebugTarget)
		{
			Chooser->SetDebugSelectedRow(ChooserColumn_SpecialIndex_Fallback);
		}
		#endif
		TRACE_CHOOSER_EVALUATION(Chooser, Context, ChooserColumn_SpecialIndex_Fallback);
	
		if (Chooser->FallbackResult.IsValid())
		{
			const FObjectChooserBase& SelectedResult = Chooser->FallbackResult.Get<FObjectChooserBase>();
			FObjectChooserBase::EIteratorStatus Status = SelectedResult.ChooseMulti(Context, Callback);
			if (Status != FObjectChooserBase::EIteratorStatus::Continue)
			{
				bSetOutputs = true;
				// trigger all output columns to set their default output value
				for (const FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
				{
					const FChooserColumnBase& Column = ColumnData.Get<FChooserColumnBase>();
					Column.SetOutputs(Context, ChooserColumn_SpecialIndex_Fallback);
				}
			}
			if (Status == FObjectChooserBase::EIteratorStatus::Stop)
			{
				return FObjectChooserBase::EIteratorStatus::Stop;
			}
		}
	}
	// If this is a nested chooser make sure the parent also sets the output values from the row that contained this chooser
	return bSetOutputs ? FObjectChooserBase::EIteratorStatus::ContinueWithOutputs : FObjectChooserBase::EIteratorStatus::Continue;
}

FObjectChooserBase::EIteratorStatus UChooserTable::IterateChooser(const UChooserTable* Chooser, FObjectChooserBase::FObjectChooserIteratorCallback Callback)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IterateChooser);

	if (Chooser == nullptr)
	{
		return FObjectChooserBase::EIteratorStatus::Continue;
	}

	const TArray<FInstancedStruct>* ResultsArray = &Chooser->CookedResults;

#if WITH_EDITORONLY_DATA
	if (!Chooser->IsCookedData())
	{
		ResultsArray = &Chooser->ResultsStructs;
	}
#endif
	
	uint32 Count = ResultsArray->Num();
	uint32 BufferSize = Count * sizeof(FChooserIndexArray::FIndexData);

	FChooserIndexArray Indices(static_cast<FChooserIndexArray::FIndexData*>(FMemory_Alloca(BufferSize)), Count);

	for(uint32 i=0;i<Count;i++)
	{
		if (!Chooser->IsRowDisabled(i))
		{
			Indices.Push({i, 0});
		}
	}

	for (FChooserIndexArray::FIndexData& SelectedIndexData : Indices)
	{
		if (ResultsArray->IsValidIndex(SelectedIndexData.Index))
		{
			const FObjectChooserBase& SelectedResult = (*ResultsArray)[SelectedIndexData.Index].Get<FObjectChooserBase>();
			FObjectChooserBase::EIteratorStatus Status = SelectedResult.IterateObjects(Callback);
			if (Status == FObjectChooserBase::EIteratorStatus::Stop)
			{
				return FObjectChooserBase::EIteratorStatus::Stop;
			}
		}
	}

	if (Chooser->FallbackResult.IsValid())
	{
		const FObjectChooserBase& SelectedResult = Chooser->FallbackResult.Get<FObjectChooserBase>();
		return SelectedResult.IterateObjects(Callback);
	}

	return FObjectChooserBase::EIteratorStatus::Continue;
}

UObject* FEvaluateChooser::ChooseObject(FChooserEvaluationContext& Context) const
{
	UObject* Result = nullptr;
	UChooserTable::EvaluateChooser(Context, Chooser, FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
	{
		Result = InResult;
		return FObjectChooserBase::EIteratorStatus::Stop;
	}));

	return Result;
}

FObjectChooserBase::EIteratorStatus FEvaluateChooser::ChooseMulti(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const
{
	return UChooserTable::EvaluateChooser(Context, Chooser, Callback);
}

FObjectChooserBase::EIteratorStatus FEvaluateChooser::IterateObjects(FObjectChooserIteratorCallback Callback) const
{
	return UChooserTable::IterateChooser(Chooser, Callback);
}

void FEvaluateChooser::GetDebugName(FString& OutDebugName) const
{
	if (Chooser)
	{
		OutDebugName = Chooser.GetName();
	}
}


FNestedChooser::FNestedChooser()
{
}

UObject* FNestedChooser::ChooseObject(FChooserEvaluationContext& Context) const
{
	UObject* Result = nullptr;
	UChooserTable::EvaluateChooser(Context, Chooser, FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
	{
		Result = InResult;
		return FObjectChooserBase::EIteratorStatus::Stop;
	}));

	return Result;
}

FObjectChooserBase::EIteratorStatus FNestedChooser::ChooseMulti(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const
{
	return UChooserTable::EvaluateChooser(Context, Chooser, Callback);
}

FObjectChooserBase::EIteratorStatus FNestedChooser::IterateObjects(FObjectChooserIteratorCallback Callback) const
{
	return UChooserTable::IterateChooser(Chooser, Callback);
}

void FNestedChooser::GetDebugName(FString& OutDebugName) const
{
	OutDebugName  = GetNameSafe(Chooser);
}

#if WITH_EDITORONLY_DATA
namespace {
	void TestCook()
	{
		for (TObjectIterator<UChooserTable> It; It; ++It)
		{
			It->CookData();
		}
	}
}

static FAutoConsoleCommand CCmdTestCookChoosers(
	TEXT("Chooser.TestCook"),
	TEXT(""),
	FConsoleCommandDelegate::CreateStatic(TestCook));


void UChooserTable::AddNestedChooser(UChooserTable* Chooser)
{
	NestedChoosers.Add(Chooser);
	NestedChoosersChanged.Broadcast();
}

void UChooserTable::RemoveNestedChooser(UChooserTable* Chooser)
{
	NestedChoosers.Remove(Chooser);
	NestedChoosersChanged.Broadcast();
}

#endif