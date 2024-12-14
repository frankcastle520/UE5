// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementQueryBuilder.h"

#include "Algo/BinarySearch.h"
#include "Elements/Common/TypedElementDataStorageLog.h"
#include "Elements/Framework/TypedElementMetaData.h"
#include "GenericPlatform/GenericPlatformMath.h"

namespace UE::Editor::DataStorage::Queries
{
	using namespace UE::Editor::DataStorage;

	const UScriptStruct* Type(FTopLevelAssetPath Name)
	{
		const UScriptStruct* StructInfo = TypeOptional(Name);
		checkf(StructInfo, TEXT("Type name '%s' used as part of building a typed element query was not found."), *Name.ToString());
		return StructInfo;
	}

	const UScriptStruct* TypeOptional(FTopLevelAssetPath Name)
	{
		constexpr bool bExactMatch = true;
		return static_cast<UScriptStruct*>(StaticFindObject(UScriptStruct::StaticClass(), Name, bExactMatch));
	}

	const UScriptStruct* operator""_Type(const char* Name, std::size_t NameSize)
	{
		return Type(FTopLevelAssetPath{ FAnsiStringView{ Name, IntCastChecked<int32>(NameSize) } });
	}

	const UScriptStruct* operator""_TypeOptional(const char* Name, std::size_t NameSize)
	{
		return TypeOptional(FTopLevelAssetPath{ FAnsiStringView{ Name, IntCastChecked<int32>(NameSize) } });
	}



	//
	// DependsOn
	//

	FDependency::FDependency(FQueryDescription* Query)
		: Query(Query)
	{
	}

	FDependency& FDependency::ReadOnly(const UClass* Target)
	{
		checkf(Target, TEXT("The Dependency section in the Typed Elements query builder doesn't support nullptrs as Read-Only input."));
		Query->DependencyTypes.Emplace(Target);
		Query->DependencyFlags.Emplace(EQueryDependencyFlags::ReadOnly);
		Query->CachedDependencies.AddDefaulted();
		return *this;
	}

	FDependency& FDependency::ReadOnly(TConstArrayView<const UClass*> Targets)
	{
		int32 NewSize = Query->CachedDependencies.Num() + Targets.Num();
		Query->DependencyTypes.Reserve(NewSize);
		Query->CachedDependencies.Reserve(NewSize);
		Query->DependencyFlags.Reserve(NewSize);
		
		for (const UClass* Target : Targets)
		{
			ReadOnly(Target);
		}
		return *this;
	}

	FDependency& FDependency::ReadWrite(const UClass* Target)
	{
		checkf(Target, TEXT("The Dependency section in the Typed Elements query builder doesn't support nullptrs as Read/Write input."));
		Query->DependencyTypes.Emplace(Target);
		Query->DependencyFlags.Emplace(EQueryDependencyFlags::None);
		Query->CachedDependencies.AddDefaulted();
		return *this;
	}

	FDependency& FDependency::ReadWrite(TConstArrayView<const UClass*> Targets)
	{
		int32 NewSize = Query->CachedDependencies.Num() + Targets.Num();
		Query->DependencyTypes.Reserve(NewSize);
		Query->CachedDependencies.Reserve(NewSize);
		Query->DependencyFlags.Reserve(NewSize);
		
		for (const UClass* Target : Targets)
		{
			ReadWrite(Target);
		}
		return *this;
	}

	FDependency& FDependency::SubQuery(QueryHandle Handle)
	{
		checkf(Query->Callback.ExecutionMode != EExecutionMode::ThreadedChunks,
			TEXT("TEDS sub-queries can not be added to queries with a callback that process chunks in parallel."));
		Query->Subqueries.Add(Handle);
		return *this;
	}
	
	FDependency& FDependency::SubQuery(TConstArrayView<QueryHandle> Handles)
	{
		checkf(Query->Callback.ExecutionMode != EExecutionMode::ThreadedChunks,
			TEXT("TEDS sub-queries can not be added to queries with a callback that process chunks in parallel."));
		Query->Subqueries.Insert(Handles.GetData(), Handles.Num(), Query->Subqueries.Num());
		return *this;
	}

	FQueryDescription&& FDependency::Compile()
	{
		return MoveTemp(*Query);
	}


	/**
	 * Simple Query
	 */

	FSimpleQuery::FSimpleQuery(FQueryDescription* Query)
		: Query(Query)
	{
	}

	FSimpleQuery& FSimpleQuery::All(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleAll);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::All(TConstArrayView<const UScriptStruct*> Targets)
	{
		int32 NewSize = Query->ConditionTypes.Num() + Targets.Num();
		Query->ConditionTypes.Reserve(NewSize);
		Query->ConditionOperators.Reserve(NewSize);
		
		for (const UScriptStruct* Target : Targets)
		{
			All(Target);
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::All(const FValueTag& Tag, const FName& Value)
	{
		Query->ValueTags.Emplace(
			FQueryDescription::FValueTagData
			{
				.Tag = Tag,
				.MatchValue = Value
			});
		return *this;
	}

	FSimpleQuery& FSimpleQuery::All(const UEnum& Enum)
	{
		return All(FValueTag(Enum.GetFName()));
	}

	FSimpleQuery& FSimpleQuery::All(const UEnum& Enum, int64 Value)
	{
		const FName ValueName = Enum.GetNameByValue(Value);
		if (ValueName == NAME_None)
		{
			UE_LOG(LogEditorDataStorage, Warning, TEXT("Invalid value '%lld' for enum '%s'"), Value, *Enum.GetName());
			return *this;
		}
		return All(FValueTag(Enum.GetFName()), ValueName);
	}
	
	FSimpleQuery& FSimpleQuery::All(const FDynamicColumnDescription& Description)
	{
		Query->DynamicConditionDescriptions.Add(Description);
		Query->DynamicConditionOperations.Add(FQueryDescription::EOperatorType::SimpleAll);
		return *this;
	}

	FSimpleQuery& FSimpleQuery::All(const FValueTag& Tag)
	{
		return All(Tag, NAME_None);
	}

	FSimpleQuery& FSimpleQuery::Any(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleAny);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::Any(TConstArrayView<const UScriptStruct*> Targets)
	{
		int32 NewSize = Query->ConditionTypes.Num() + Targets.Num();
		Query->ConditionTypes.Reserve(NewSize);
		Query->ConditionOperators.Reserve(NewSize);

		for (const UScriptStruct* Target : Targets)
		{
			Any(Target);
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::Any(const FDynamicColumnDescription& Description)
	{
		Query->DynamicConditionDescriptions.Add(Description);
		Query->DynamicConditionOperations.Add(FQueryDescription::EOperatorType::SimpleAny);
		return *this;
	}

	FSimpleQuery& FSimpleQuery::None(const UScriptStruct* Target)
	{
		if (Target)
		{
			Query->ConditionTypes.Add(FQueryDescription::EOperatorType::SimpleNone);
			Query->ConditionOperators.AddZeroed_GetRef().Type = Target;
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::None(TConstArrayView<const UScriptStruct*> Targets)
	{
		int32 NewSize = Query->ConditionTypes.Num() + Targets.Num();
		Query->ConditionTypes.Reserve(NewSize);
		Query->ConditionOperators.Reserve(NewSize);

		for (const UScriptStruct* Target : Targets)
		{
			None(Target);
		}
		return *this;
	}

	FSimpleQuery& FSimpleQuery::None(const FDynamicColumnDescription& Description)
	{
		Query->DynamicConditionDescriptions.Add(Description);
		Query->DynamicConditionOperations.Add(FQueryDescription::EOperatorType::SimpleNone);
		return *this;
	}

	FDependency FSimpleQuery::DependsOn()
	{
		return FDependency{ Query };
	}

	FQueryDescription&& FSimpleQuery::Compile()
	{
		Query->Callback.BeforeGroups.Shrink();
		Query->Callback.AfterGroups.Shrink();
		Query->SelectionTypes.Shrink();
		Query->SelectionAccessTypes.Shrink();
		for (FColumnMetaData& Metadata : Query->SelectionMetaData)
		{
			Metadata.Shrink();
		}
		Query->SelectionMetaData.Shrink();
		Query->ConditionTypes.Shrink();
		Query->ConditionOperators.Shrink();
		Query->DynamicConditionDescriptions.Shrink();
		Query->DynamicConditionOperations.Shrink();
		Query->DynamicSelectionAccessTypes.Shrink();
		Query->DynamicSelectionMetaData.Shrink();
		Query->DynamicSelectionTypes.Shrink();
		Query->DependencyTypes.Shrink();
		Query->DependencyFlags.Shrink();
		Query->CachedDependencies.Shrink();
		Query->Subqueries.Shrink();
		Query->MetaData.Shrink();
		return MoveTemp(*Query);
	}


	/**
	 * FProcessor
	 */
	FProcessor::FProcessor(EQueryTickPhase Phase, FName Group)
		: Phase(Phase)
		, Group(Group)
	{}

	FProcessor& FProcessor::SetPhase(EQueryTickPhase NewPhase)
	{
		Phase = NewPhase;
		return *this;
	}

	FProcessor& FProcessor::SetGroup(FName GroupName)
	{
		Group = GroupName;
		return *this;
	}

	FProcessor& FProcessor::SetBeforeGroup(FName GroupName)
	{
		BeforeGroup = GroupName;
		return *this;
	}

	FProcessor& FProcessor::SetAfterGroup(FName GroupName)
	{
		AfterGroup = GroupName;
		return *this;
	}

	FProcessor& FProcessor::SetExecutionMode(EExecutionMode Mode)
	{
		ExecutionMode = Mode;
		return *this;
	}

	FProcessor& FProcessor::MakeActivatable(FName Name)
	{
		ActivationName = Name;
		return *this;
	}


	/**
	 * FObserver
	 */
	
	FObserver::FObserver(EEvent MonitorForEvent, const UScriptStruct* MonitoredColumn)
		: Monitor(MonitoredColumn)
		, Event(MonitorForEvent)
	{}

	FObserver& FObserver::SetEvent(EEvent MonitorForEvent)
	{
		Event = MonitorForEvent;
		return *this;
	}

	FObserver& FObserver::SetMonitoredColumn(const UScriptStruct* MonitoredColumn)
	{
		Monitor = MonitoredColumn;
		return *this;
	}

	FObserver& FObserver::SetExecutionMode(EExecutionMode Mode)
	{
		ExecutionMode = Mode;
		return *this;
	}

	FObserver& FObserver::MakeActivatable(FName Name)
	{
		ActivationName = Name;
		return *this;
	}


	/**
	 * FPhaseAmble
	 */

	FPhaseAmble::FPhaseAmble(ELocation InLocation, EQueryTickPhase InPhase)
		: Phase(InPhase)
		, Location(InLocation)
	{}

	FPhaseAmble& FPhaseAmble::SetLocation(ELocation NewLocation)
	{
		Location = NewLocation;
		return *this;
	}

	FPhaseAmble& FPhaseAmble::SetPhase(EQueryTickPhase NewPhase)
	{
		Phase = NewPhase;
		return *this;
	}

	FPhaseAmble& FPhaseAmble::SetExecutionMode(EExecutionMode Mode)
	{
		ExecutionMode = Mode;
		return *this;
	}

	FPhaseAmble& FPhaseAmble::MakeActivatable(FName Name)
	{
		ActivationName = Name;
		return *this;
	}


	/**
	 * Select
	 */

	Select::Select()
	{
		Query.Action = FQueryDescription::EActionType::Select;
	}
	
	Select& Select::ReadOnly(const UScriptStruct* Target)
	{
		checkf(Target, TEXT("The Select section in the Typed Elements query builder doesn't support nullptrs as Read-Only input."));
		Query.SelectionTypes.Emplace(Target);
		Query.SelectionAccessTypes.Emplace(EQueryAccessType::ReadOnly);
		Query.SelectionMetaData.Emplace(Target, FColumnMetaData::EFlags::None);
		
		return *this;
	}

	Select& Select::ReadOnly(TConstArrayView<const UScriptStruct*> Targets)
	{
		int32 NewCount = Query.SelectionTypes.Num() + Targets.Num();
		Query.SelectionTypes.Reserve(NewCount);
		Query.SelectionAccessTypes.Reserve(NewCount);
		Query.SelectionMetaData.Reserve(NewCount);

		for (const UScriptStruct* Target : Targets)
		{
			ReadOnly(Target);
		}
		return *this;
	}

	Select& Select::ReadOnly(const FDynamicColumnDescription& Description)
	{
		Query.DynamicSelectionTypes.Emplace(Description);
		Query.DynamicSelectionAccessTypes.Emplace(EQueryAccessType::ReadOnly);
		Query.DynamicSelectionMetaData.Emplace(FColumnMetaData::EFlags::None);
		return *this;
	}

	Select& Select::ReadOnly(const UScriptStruct* Target, EOptional Optional)
	{
		checkf(Target, TEXT("The Select section in the Typed Elements query builder doesn't support nullptrs as Read-Only input."));
		Query.SelectionTypes.Emplace(Target);
		Query.SelectionAccessTypes.Emplace(Optional == EOptional::Yes
			? EQueryAccessType::OptionalReadOnly
			: EQueryAccessType::ReadOnly);
		Query.SelectionMetaData.Emplace(Target, FColumnMetaData::EFlags::None);

		return *this;
	}

	Select& Select::ReadOnly(TConstArrayView<const UScriptStruct*> Targets, EOptional Optional)
	{
		int32 NewCount = Query.SelectionTypes.Num() + Targets.Num();
		Query.SelectionTypes.Reserve(NewCount);
		Query.SelectionAccessTypes.Reserve(NewCount);
		Query.SelectionMetaData.Reserve(NewCount);

		for (const UScriptStruct* Target : Targets)
		{
			ReadOnly(Target, Optional);
		}
		return *this;
	}

	Select& Select::ReadWrite(const UScriptStruct* Target)
	{
		checkf(Target, TEXT("The Select section in the Typed Elements query builder doesn't support nullptrs as Read/Write input."));
		Query.SelectionTypes.Emplace(Target);
		Query.SelectionAccessTypes.Emplace(EQueryAccessType::ReadWrite);
		Query.SelectionMetaData.Emplace(Target, FColumnMetaData::EFlags::IsMutable);
		return *this;
	}

	Select& Select::ReadWrite(TConstArrayView<const UScriptStruct*> Targets)
	{
		int32 NewCount = Query.SelectionTypes.Num() + Targets.Num();
		Query.SelectionTypes.Reserve(NewCount);
		Query.SelectionAccessTypes.Reserve(NewCount);
		Query.SelectionMetaData.Reserve(NewCount);

		for (const UScriptStruct* Target : Targets)
		{
			ReadWrite(Target);
		}
		return *this;
	}

	Select& Select::ReadWrite(const FDynamicColumnDescription& Descirption)
	{
		Query.DynamicSelectionTypes.Emplace(Descirption);
		Query.DynamicSelectionAccessTypes.Emplace(EQueryAccessType::ReadWrite);
		Query.DynamicSelectionMetaData.Emplace(FColumnMetaData::EFlags::IsMutable);
		return *this;
	}

	FSimpleQuery Select::Where()
	{
		return FSimpleQuery{ &Query };
	}

	FDependency Select::DependsOn()
	{
		return FDependency{ &Query };
	}

	FQueryDescription&& Select::Compile()
	{
		return MoveTemp(Query);
	}


	/**
	 * Count
	 */

	Count::Count()
	{
		Query.Action = FQueryDescription::EActionType::Count;
	}

	FSimpleQuery Count::Where()
	{
		return FSimpleQuery{ &Query };
	}

	FDependency Count::DependsOn()
	{
		return FDependency{ &Query };
	}
} // namespace UE::Editor::DataStorage::Queries
