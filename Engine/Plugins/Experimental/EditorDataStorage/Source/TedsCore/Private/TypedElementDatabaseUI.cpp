// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseUI.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "Widgets/SlateControlledConstruction.h"

DEFINE_LOG_CATEGORY(LogEditorDataStorageUI);

namespace Internal
{
	// Source: https://en.cppreference.com/w/cpp/utility/variant/visit
	template<class... Ts>
	struct TOverloaded : Ts...
	{ 
		using Ts::operator()...; 
	};

	template<class... Ts> TOverloaded(Ts...) -> TOverloaded<Ts...>;
}

void UEditorDataStorageUi::Initialize(
	IEditorDataStorageProvider* StorageInterface,
	IEditorDataStorageCompatibilityProvider* StorageCompatibilityInterface)
{
	checkf(StorageInterface, TEXT("TEDS' compatibility manager is being initialized with an invalid storage target."));

	Storage = StorageInterface;
	StorageCompatibility = StorageCompatibilityInterface;
	CreateStandardArchetypes();
}

void UEditorDataStorageUi::Deinitialize()
{
}

void UEditorDataStorageUi::RegisterWidgetPurpose(FName Purpose, EPurposeType Type, FText Description)
{
	FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose);
	if (!PurposeInfo)
	{
		FPurposeInfo& NewInfo = WidgetPurposes.Add(Purpose);
		NewInfo.Type = Type;
		NewInfo.Description = MoveTemp(Description);
	}
}

bool UEditorDataStorageUi::RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor)
{
	checkf(Constructor->IsChildOf(FTypedElementWidgetConstructor::StaticStruct()),
		TEXT("Attempting to register a widget constructor '%s' that isn't derived from FTypedElementWidgetConstructor."),
		*Constructor->GetFullName());
	
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		switch (PurposeInfo->Type)
		{
		case IEditorDataStorageUiProvider::EPurposeType::Generic:
			PurposeInfo->Factories.Emplace(Constructor);
			PurposeInfo->bIsSorted = false;
			return true;
		case IEditorDataStorageUiProvider::EPurposeType::UniqueByName:
			if (PurposeInfo->Factories.IsEmpty())
			{
				PurposeInfo->Factories.Emplace(Constructor);
				PurposeInfo->bIsSorted = false;
			}
			else
			{
				PurposeInfo->Factories.EmplaceAt(0, Constructor);
			}
			return true;
		case IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn:
			UE_LOG(LogEditorDataStorageUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' requires at least one column for matching."), 
				*Constructor->GetName(), *Purpose.ToString());
			return false;
		default:
			checkf(false, TEXT("Unexpected IEditorDataStorageUiProvider::EPurposeType found provided when registering widget factory."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogEditorDataStorageUI, Warning, 
			TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), *Constructor->GetName(), *Purpose.ToString());
		return false;
	}
}

bool UEditorDataStorageUi::RegisterWidgetFactory(
	FName Purpose, const UScriptStruct* Constructor, UE::Editor::DataStorage::Queries::FConditions Columns)
{
	if (!Columns.IsEmpty())
	{
		checkf(Constructor->IsChildOf(FTypedElementWidgetConstructor::StaticStruct()),
			TEXT("Attempting to register a widget constructor '%s' that isn't deriving from FTypedElementWidgetConstructor."),
			*Constructor->GetFullName());

		if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
		{
			switch (PurposeInfo->Type)
			{
			case IEditorDataStorageUiProvider::EPurposeType::Generic:
				PurposeInfo->Factories.Emplace(Constructor);
				PurposeInfo->bIsSorted = false;
				return true;
			case IEditorDataStorageUiProvider::EPurposeType::UniqueByName:
				if (!Columns.IsEmpty())
				{
					if (PurposeInfo->Factories.IsEmpty())
					{
						PurposeInfo->Factories.Emplace(Constructor);
						PurposeInfo->bIsSorted = false;
					}
					else
					{
						PurposeInfo->Factories.EmplaceAt(0, Constructor);
					}
					return true;
				}
				else
				{
					return false;
				}
			case IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn:
				if (!Columns.IsEmpty())
				{
					PurposeInfo->Factories.Emplace(Constructor, MoveTemp(Columns));
					PurposeInfo->bIsSorted = false;
					return true;
				}
				else
				{
					return false;
				}
			default:
				checkf(false, TEXT("Unexpected IEditorDataStorageUiProvider::EPurposeType found provided when registering widget factory."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogEditorDataStorageUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), *Constructor->GetName(), *Purpose.ToString());
			return false;
		}
	}
	else
	{
		return RegisterWidgetFactory(Purpose, Constructor);
	}
}

bool UEditorDataStorageUi::RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor)
{
	checkf(Constructor->GetTypeInfo(), TEXT("Widget constructor being registered that doesn't have valid type information."));
	
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		switch (PurposeInfo->Type)
		{
		case IEditorDataStorageUiProvider::EPurposeType::Generic:
			PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
			PurposeInfo->bIsSorted = false;
			return true;
		case IEditorDataStorageUiProvider::EPurposeType::UniqueByName:
			if (PurposeInfo->Factories.IsEmpty())
			{
				PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
				PurposeInfo->bIsSorted = false;
			}
			else
			{
				PurposeInfo->Factories.EmplaceAt(0, MoveTemp(Constructor));
			}
			return true;
		case IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn:
			UE_LOG(LogEditorDataStorageUI, Warning,
				TEXT("Unable to register widget factory '%s' as purpose '%s' requires at least one column for matching."),
				*Constructor->GetTypeInfo()->GetName(), *Purpose.ToString());
			return false;
		default:
			checkf(false, TEXT("Unexpected IEditorDataStorageUiProvider::EPurposeType found provided when registering widget factory."));
			return false;
		}
	}
	else
	{
		UE_LOG(LogEditorDataStorageUI, Warning, 
			TEXT("Unable to register widget factory as purpose '%s' isn't registered."), *Purpose.ToString());
		return false;
	}
}

bool UEditorDataStorageUi::RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor, 
	UE::Editor::DataStorage::Queries::FConditions Columns)
{
	if (!Columns.IsEmpty())
	{
		checkf(Constructor->GetTypeInfo(), TEXT("Widget constructor being registered that doesn't have valid type information."));

		if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
		{
			switch (PurposeInfo->Type)
			{
			case IEditorDataStorageUiProvider::EPurposeType::Generic:
				PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
				PurposeInfo->bIsSorted = false;
				return true;
			case IEditorDataStorageUiProvider::EPurposeType::UniqueByName:
				if (!Columns.IsEmpty())
				{
					if (PurposeInfo->Factories.IsEmpty())
					{
						PurposeInfo->Factories.Emplace(MoveTemp(Constructor));
						PurposeInfo->bIsSorted = false;
					}
					else
					{
						PurposeInfo->Factories.EmplaceAt(0, MoveTemp(Constructor));
					}
					return true;
				}
				else
				{
					return false;
				}
			case IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn:
				if (!Columns.IsEmpty())
				{
					PurposeInfo->Factories.Emplace(MoveTemp(Constructor), MoveTemp(Columns));
					PurposeInfo->bIsSorted = false;
					return true;
				}
				else
				{
					return false;
				}
			default:
				checkf(false, TEXT("Unexpected IEditorDataStorageUiProvider::EPurposeType found provided when registering widget factory."));
				return false;
			}
		}
		else
		{
			UE_LOG(LogEditorDataStorageUI, Warning, TEXT("Unable to register widget factory '%s' as purpose '%s' isn't registered."), 
				*Constructor->GetTypeInfo()->GetName(), *Purpose.ToString());
			return false;
		}
	}
	else
	{
		return RegisterWidgetFactory(Purpose, MoveTemp(Constructor));
	}
}

void UEditorDataStorageUi::CreateWidgetConstructors(FName Purpose,
	const UE::Editor::DataStorage::FMetaDataView& Arguments, const WidgetConstructorCallback& Callback)
{
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		for (const FWidgetFactory& Factory : PurposeInfo->Factories)
		{
			if (!CreateSingleWidgetConstructor(Factory.Constructor, Arguments, {}, Factory.GetConditions(Storage), Callback))
			{
				return;
			}
		}
	}
}

void UEditorDataStorageUi::CreateWidgetConstructors(FName Purpose, EMatchApproach MatchApproach, 
	TArray<TWeakObjectPtr<const UScriptStruct>>& Columns, const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetConstructorCallback& Callback)
{
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		// Sort so searching can be done in a single pass. This would also allow for binary searching, but the number of columns
		// is typically small enough for a binary search to end up being more expensive than a linear search. This may change
		// if/when there are a sufficient enough number of widgets that are bound to a large number of columns.
		Columns.Sort(
			[](const TWeakObjectPtr<const UScriptStruct>& Lhs, const TWeakObjectPtr<const UScriptStruct>& Rhs)
			{
				return Lhs.Get() < Rhs.Get();
			});

		if (!PurposeInfo->bIsSorted)
		{
			// This is the only call that requires the array of factories to be sorted from largest to smallest number
			// of columns, so lazily sort only when needed.
			PurposeInfo->Factories.StableSort(
				[this](const FWidgetFactory& Lhs, const FWidgetFactory& Rhs)
				{
					int32 LeftSize = Lhs.GetConditions(Storage).MinimumColumnMatchRequired();
					int32 RightSize = Rhs.GetConditions(Storage).MinimumColumnMatchRequired();
					return LeftSize > RightSize;
				});
			PurposeInfo->bIsSorted = true;
		}

		switch (MatchApproach)
		{
		case EMatchApproach::LongestMatch:
			CreateWidgetConstructors_LongestMatch(PurposeInfo->Factories, Columns, Arguments, Callback);
			break;
		case EMatchApproach::ExactMatch:
			CreateWidgetConstructors_ExactMatch(PurposeInfo->Factories, Columns, Arguments, Callback);
			break;
		case EMatchApproach::SingleMatch:
			CreateWidgetConstructors_SingleMatch(PurposeInfo->Factories, Columns, Arguments, Callback);
			break;
		default:
			checkf(false, TEXT("Unsupported match type (%i) for CreateWidgetConstructors."), 
				static_cast<std::underlying_type_t<EMatchApproach>>(MatchApproach));
		}
	}
}

void UEditorDataStorageUi::ConstructWidgets(FName Purpose, const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetCreatedCallback& ConstructionCallback)
{
	if (FPurposeInfo* PurposeInfo = WidgetPurposes.Find(Purpose))
	{
		for (const FWidgetFactory& Factory : PurposeInfo->Factories)
		{
			std::visit(Internal::TOverloaded
				{
					[this, &Arguments, &ConstructionCallback](const UScriptStruct* ConstructorType)
					{ 
						FTypedElementWidgetConstructor* Constructor = reinterpret_cast<FTypedElementWidgetConstructor*>(
							FMemory_Alloca_Aligned(ConstructorType->GetStructureSize(), ConstructorType->GetMinAlignment()));
						if (Constructor)
						{
							ConstructorType->InitializeStruct(Constructor);
							CreateWidgetInstance(*Constructor, Arguments, ConstructionCallback);
							ConstructorType->DestroyStruct(&Constructor);
						}
						else
						{
							checkf(false, TEXT("Remaining stack space is too small to create a widget constructor from a description."));
						}
					},
					[this, &Arguments, &ConstructionCallback](const TUniquePtr<FTypedElementWidgetConstructor>& Constructor)
					{
						CreateWidgetInstance(*Constructor, Arguments, ConstructionCallback);
					}
				}, Factory.Constructor);
		}
	}
}

bool UEditorDataStorageUi::CreateSingleWidgetConstructor(
	const FWidgetFactory::ConstructorType& Constructor,
	const UE::Editor::DataStorage::FMetaDataView& Arguments,
	TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes,
	const UE::Editor::DataStorage::Queries::FConditions& QueryConditions,
	const WidgetConstructorCallback& Callback)
{
	struct Visitor
	{
		Visitor(
			TArray<TWeakObjectPtr<const UScriptStruct>>&& InMatchedColumnTypes,
			const UE::Editor::DataStorage::Queries::FConditions& InQueryConditions,
			const UE::Editor::DataStorage::FMetaDataView& InArguments,
			const WidgetConstructorCallback& InCallback) 
			: MatchedColumnTypes(MoveTemp(InMatchedColumnTypes))
			, QueryConditions(InQueryConditions)
			, Arguments(InArguments)
			, Callback(InCallback)
		{}

		TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes;
		const UE::Editor::DataStorage::Queries::FConditions& QueryConditions;
		const UE::Editor::DataStorage::FMetaDataView& Arguments;
		const WidgetConstructorCallback& Callback;

		bool operator()(const UScriptStruct* Target)
		{
			TUniquePtr<FTypedElementWidgetConstructor> Result(reinterpret_cast<FTypedElementWidgetConstructor*>(
				FMemory::Malloc(Target->GetStructureSize(), Target->GetMinAlignment())));
			if (Result)
			{
				Target->InitializeStruct(Result.Get());
				Result->Initialize(Arguments, MoveTemp(MatchedColumnTypes), QueryConditions);
				const TArray<TWeakObjectPtr<const UScriptStruct>>& MatchedColumns = Result->GetMatchedColumns();
				return Callback(MoveTemp(Result), MatchedColumns);
			}
			return true;
		}

		bool operator()(const TUniquePtr<FTypedElementWidgetConstructor>& Target)
		{
			const UScriptStruct* TargetType = Target->GetTypeInfo();
			checkf(TargetType, TEXT("Expected valid type information from a widget constructor."));
			TUniquePtr<FTypedElementWidgetConstructor> Result(reinterpret_cast<FTypedElementWidgetConstructor*>(
				FMemory::Malloc(TargetType->GetStructureSize(), TargetType->GetMinAlignment())));
			if (Result)
			{
				TargetType->InitializeStruct(Result.Get());
				TargetType->CopyScriptStruct(Result.Get(), Target.Get());
				Result->Initialize(Arguments, MoveTemp(MatchedColumnTypes), QueryConditions);
				return Callback(MoveTemp(Result), Result->GetMatchedColumns());
			}
			return true;
		}
	};
	return std::visit(Visitor(MoveTemp(MatchedColumnTypes), QueryConditions, Arguments, Callback), Constructor);
}

void UEditorDataStorageUi::CreateWidgetInstance(
	FTypedElementWidgetConstructor& Constructor, 
	const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetCreatedCallback& ConstructionCallback)
{
	UE::Editor::DataStorage::RowHandle Row = Storage->AddRow(WidgetTable);
	Storage->AddColumns(Row, Constructor.GetAdditionalColumnsList());
	TSharedPtr<SWidget> Widget = Constructor.ConstructFinalWidget(Row, Storage, this, Arguments);
	if (Widget)
	{
		ConstructionCallback(Widget.ToSharedRef(), Row);
	}
	else
	{
		Storage->RemoveRow(Row);
	}
}

TSharedPtr<SWidget> UEditorDataStorageUi::ConstructWidget(UE::Editor::DataStorage::RowHandle Row, FTypedElementWidgetConstructor& Constructor,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return Constructor.ConstructFinalWidget(Row, Storage, this, Arguments);
}

void UEditorDataStorageUi::ListWidgetPurposes(const WidgetPurposeCallback& Callback) const
{
	for (auto&& It : WidgetPurposes)
	{
		Callback(It.Key, It.Value.Type, It.Value.Description);
	}
}

bool UEditorDataStorageUi::SupportsExtension(FName Extension) const
{
	return false;
}

void UEditorDataStorageUi::ListExtensions(TFunctionRef<void(FName)> Callback) const
{
}

void UEditorDataStorageUi::CreateStandardArchetypes()
{
	WidgetTable = Storage->RegisterTable(MakeArrayView(
		{
			FTypedElementSlateWidgetReferenceColumn::StaticStruct(),
			FTypedElementSlateWidgetReferenceDeletesRowTag::StaticStruct()
		}), FName(TEXT("Editor_WidgetTable")));
}

void UEditorDataStorageUi::CreateWidgetConstructors_LongestMatch(const TArray<FWidgetFactory>& WidgetFactories,
	TArray<TWeakObjectPtr<const UScriptStruct>>& Columns, const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetConstructorCallback& Callback)
{
	TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumns;
	for (auto FactoryIt = WidgetFactories.CreateConstIterator(); FactoryIt && !Columns.IsEmpty(); ++FactoryIt)
	{
		const UE::Editor::DataStorage::Queries::FConditions& Conditions = FactoryIt->GetConditions(Storage);
		
		if (Conditions.MinimumColumnMatchRequired() > Columns.Num())
		{
			// There are more columns required for this factory than there are in the requested columns list so skip this
			// factory.
			continue;
		}

		MatchedColumns.Reset();
		
		if (Conditions.Verify(MatchedColumns, Columns, true))
		{
			// Remove the found columns from the requested list.
			Algo::SortBy(MatchedColumns, [](const TWeakObjectPtr<const UScriptStruct>& Column) { return Column.Get(); });
			MatchedColumns.SetNum(Algo::Unique(MatchedColumns), EAllowShrinking::No);
			
			TWeakObjectPtr<const UScriptStruct>* ColumnsIt = Columns.GetData();
			TWeakObjectPtr<const UScriptStruct>* ColumnsEnd = ColumnsIt + Columns.Num();
			int32 ColumnIndex = 0;
			for (const TWeakObjectPtr<const UScriptStruct>& MatchedColumn : MatchedColumns)
			{
				// Remove all the columns that were matched from the provided column list.
				while (*ColumnsIt != MatchedColumn)
				{
					++ColumnIndex;
					++ColumnsIt;
					if (ColumnsIt == ColumnsEnd)
					{
						ensureMsgf(false, TEXT("A previously found matching column can't be found in the original array."));
						return;
					}
				}
				Columns.RemoveAt(ColumnIndex, EAllowShrinking::No);
				--ColumnsEnd;
			}
			
			if (!CreateSingleWidgetConstructor(FactoryIt->Constructor, Arguments, MoveTemp(MatchedColumns), Conditions, Callback))
			{
				return;
			}
		}
	}
}

void UEditorDataStorageUi::CreateWidgetConstructors_ExactMatch(const TArray<FWidgetFactory>& WidgetFactories,
	TArray<TWeakObjectPtr<const UScriptStruct>>& Columns, const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetConstructorCallback& Callback)
{
	int32 ColumnCount = Columns.Num();
	TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumns;
	for (const FWidgetFactory& Factory : WidgetFactories)
	{
		const UE::Editor::DataStorage::Queries::FConditions& Conditions = Factory.GetConditions(Storage);
		
		// If there are more matches required that there are columns, then there will never be an exact match.
		// Less than the column count can still result in a match that covers all columns.
		if (Conditions.MinimumColumnMatchRequired() > ColumnCount)
		{
			continue;
		}

		MatchedColumns.Reset();

		if (Conditions.Verify(MatchedColumns, Columns, true))
		{
			Algo::SortBy(MatchedColumns, [](const TWeakObjectPtr<const UScriptStruct>& Column) { return Column.Get(); });
			MatchedColumns.SetNum(Algo::Unique(MatchedColumns), EAllowShrinking::No);
			if (MatchedColumns.Num() == Columns.Num())
			{
				Columns.Reset();
				CreateSingleWidgetConstructor(Factory.Constructor, Arguments, MoveTemp(MatchedColumns), Conditions, Callback);
				return;
			}
		}
	}
}

void UEditorDataStorageUi::CreateWidgetConstructors_SingleMatch(const TArray<FWidgetFactory>& WidgetFactories,
	TArray<TWeakObjectPtr<const UScriptStruct>>& Columns, const UE::Editor::DataStorage::FMetaDataView& Arguments,
	const WidgetConstructorCallback& Callback)
{
	auto FactoryIt = WidgetFactories.rbegin();
	auto FactoryEnd = WidgetFactories.rend();

	// Start from the back as the widgets with lower counts will be last.
	for (int32 ColumnIndex = Columns.Num() - 1; ColumnIndex >= 0; --ColumnIndex)
	{
		for (; FactoryIt != FactoryEnd; ++FactoryIt)
		{
			const UE::Editor::DataStorage::Queries::FConditions& Conditions = (*FactoryIt).GetConditions(Storage);
			
			TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnData = Conditions.GetColumns();
			if (ColumnData.Num() > 1)
			{
				// Moved passed the point where factories only have a single column.
				return;
			}
			else if (ColumnData.Num() == 0)
			{
				// Need to move further to find factories with exactly one column.
				continue;
			}

			if (ColumnData[0] == Columns[ColumnIndex])
			{
				Columns.RemoveAt(ColumnIndex);
				CreateSingleWidgetConstructor((*FactoryIt).Constructor, Arguments, 
					TArray<TWeakObjectPtr<const UScriptStruct>>(ColumnData), Conditions, Callback);
				// Match was found so move on to the next column in the column.
				break;
			}
		}
	}
}



//
// FWidgetFactory
//

UEditorDataStorageUi::FWidgetFactory::FWidgetFactory(const UScriptStruct* InConstructor)
	: Constructor(InConstructor)
{
}

UEditorDataStorageUi::FWidgetFactory::FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor)
	: Constructor(MoveTemp(InConstructor))
{
	checkf(std::get<TUniquePtr<FTypedElementWidgetConstructor>>(Constructor)->GetTypeInfo(), 
		TEXT("Widget constructor registered that didn't contain valid type information."));
}

UEditorDataStorageUi::FWidgetFactory::FWidgetFactory(const UScriptStruct* InConstructor, 
	UE::Editor::DataStorage::Queries::FConditions&& InColumns)
	: Constructor(InConstructor)
	, Columns(MoveTemp(InColumns))
{
}

UEditorDataStorageUi::FWidgetFactory::FWidgetFactory(TUniquePtr<FTypedElementWidgetConstructor>&& InConstructor, 
	UE::Editor::DataStorage::Queries::FConditions&& InColumns)
	: Constructor(MoveTemp(InConstructor))
	, Columns(MoveTemp(InColumns))
{
	checkf(std::get<TUniquePtr<FTypedElementWidgetConstructor>>(Constructor)->GetTypeInfo(),
		TEXT("Widget constructor registered that didn't contain valid type information."));
}

const UE::Editor::DataStorage::Queries::FConditions& UEditorDataStorageUi::FWidgetFactory::GetConditions(IEditorDataStorageProvider* DataStorage) const
{
	Columns.Compile(UE::Editor::DataStorage::Queries::FEditorStorageQueryConditionCompileContext(DataStorage));
	
	return Columns;
}
