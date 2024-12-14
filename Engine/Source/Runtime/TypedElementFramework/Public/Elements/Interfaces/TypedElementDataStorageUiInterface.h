// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Elements/Common/TypedElementQueryConditions.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Framework/TypedElementMetaData.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Features/IModularFeature.h"
#include "Templates/UnrealTypeTraits.h"
#include "Templates/SharedPointer.h"
#include "UObject/Interface.h"

#include "TypedElementDataStorageUiInterface.generated.h"

class IEditorDataStorageUiProvider;
class SWidget;

/**
 * Base class used to construct Typed Element widgets with.
 * See below for the options to register a constructor with the Data Storage.
 * In most cases you want to inherit from FSimpleWidgetConstructor instead which has a simpler pipeline to create widgets
 */
USTRUCT()
struct FTypedElementWidgetConstructor
{
	GENERATED_BODY()

	using RowHandle = UE::Editor::DataStorage::RowHandle;

public:
	TYPEDELEMENTFRAMEWORK_API explicit FTypedElementWidgetConstructor(const UScriptStruct* InTypeInfo);
	explicit FTypedElementWidgetConstructor(EForceInit) {} //< For compatibility and shouldn't be directly used.

	virtual ~FTypedElementWidgetConstructor() = default;

	/** Initializes a new constructor based on the provided arguments.. */
	TYPEDELEMENTFRAMEWORK_API virtual bool Initialize(const UE::Editor::DataStorage::FMetaDataView& InArguments,
		TArray<TWeakObjectPtr<const UScriptStruct>> InMatchedColumnTypes, const UE::Editor::DataStorage::Queries::FConditions& InQueryConditions);

	/** Retrieves the type information for the constructor type. */
	TYPEDELEMENTFRAMEWORK_API virtual const UScriptStruct* GetTypeInfo() const;
	/** Retrieves the columns, if any, that were matched to this constructor when it was created. */
	TYPEDELEMENTFRAMEWORK_API virtual const TArray<TWeakObjectPtr<const UScriptStruct>>& GetMatchedColumns() const;
	/** Retrieves the query conditions that need to match for this widget constructor to produce a widget. */
	TYPEDELEMENTFRAMEWORK_API virtual const UE::Editor::DataStorage::Queries::FConditions* GetQueryConditions() const;

	/** Returns a list of additional columns the widget requires to be added to its rows. */
	TYPEDELEMENTFRAMEWORK_API virtual TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const;

	/** 
	 * Returns a friendly name for the data the created widget represents.
	 * By default the associated column is used. If there are multiple columns associated with the constructor
	 * the default implementation will attempt to find the longest common starting string for all the columns.
	 * Individual widget constructors can override this function with a name specific to them.
	 */
	TYPEDELEMENTFRAMEWORK_API virtual FString CreateWidgetDisplayName(
		IEditorDataStorageProvider* DataStorage, RowHandle Row) const;
	
	/**
	 *	Calls Construct() to create the internal widget, and then stores it in a container before returning.
	 *	In most cases you want to call this to first create the initial TEDS widget, to ensure the internal widget is
	 *	automatically created/destroyed if the row matches/unmatches the required columns.
	 *	
	 *	Construct() can be called later to (re)create the internal widget if ever required.
	 *	@see Construct
	 */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> ConstructFinalWidget(
		RowHandle Row, /** The row the widget will be stored in. */
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		const UE::Editor::DataStorage::FMetaDataView& Arguments);

	/**
	 * Constructs the widget according to the provided information. Information is collected by calling
	 * the below functions CreateWidget and AddColumns. It's recommended to overload those
	 * functions to build widgets according to a standard recipe and to reduce the amount of code needed.
	 * If a complexer situation is called for this function can also be directly overwritten.
	 * In most cases, you want to call ConstructFinalWidget to create the actual widget.
	 */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> Construct(
		RowHandle Row, /** The row the widget will be stored in. */
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		const UE::Editor::DataStorage::FMetaDataView& Arguments);

protected:
	/** Create a new instance of the target widget. This is a required function. */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments);
	/** Create a new instance of the target widget. This is a required function. */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments);
	/** Set any values in columns if needed. The columns provided through GetAdditionalColumnsList() will have already been created. */
	TYPEDELEMENTFRAMEWORK_API virtual bool SetColumns(IEditorDataStorageProvider* DataStorage, RowHandle Row);
	
	/** Creates a (friendly) name for the provided column type. */
	TYPEDELEMENTFRAMEWORK_API virtual FString DescribeColumnType(const UScriptStruct* ColumnType) const;

	/** 
	 * Last opportunity to configure anything in the widget or the row. This step can be needed to initialize widgets with data stored
	 * in columns.
	 */
	TYPEDELEMENTFRAMEWORK_API virtual bool FinalizeWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		RowHandle Row,
		const TSharedPtr<SWidget>& Widget);

	/** Add the default misc columns we want a widget row to have. */
	TYPEDELEMENTFRAMEWORK_API void AddDefaultWidgetColumns(RowHandle Row, IEditorDataStorageProvider* DataStorage) const;

	/**
	 * Helper function to get the actual target row with the data the widget is operating on (if applicable). Returns InvalidRowHandle if there is no
	 * target row
	 */
	TYPEDELEMENTFRAMEWORK_API RowHandle GetTargetRow(IEditorDataStorageProvider* DataStorage, RowHandle WidgetRow) const;

protected:

	TArray<TWeakObjectPtr<const UScriptStruct>> MatchedColumnTypes;
	const UE::Editor::DataStorage::Queries::FConditions* QueryConditions = nullptr;
	const UScriptStruct* TypeInfo = nullptr;
};

/**
 * A simple widget constructor that cuts down on most of the boilerplate, in most cases you want to inherit from this to create your widget constructor
 * Only requires you to override CreateWidget() to create the actual SWidget
 */
USTRUCT()
struct FSimpleWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

	/** Call this constructor with StaticStruct() on your derived class to pass in the type information */
	TYPEDELEMENTFRAMEWORK_API explicit FSimpleWidgetConstructor(const UScriptStruct* InTypeInfo);

	FSimpleWidgetConstructor() : Super(StaticStruct()) {} //< For compatibility and shouldn't be directly used.
	
	virtual ~FSimpleWidgetConstructor() override = default;

	/*
	 * Required function to create the actual widget instance.
	 * 
	 * @param DataStorage A pointer to the TEDS data storage interface
	 * @param DataStorageUi A pointer to the TEDS data storage UI interface
	 * @param TargetRow The row for the actual data this widget is being created for (can be InvalidRowHandle if there is no target row attached to this widget)
	 * @param WidgetRow The row that contains information about the widget itself
	 * @param Arguments Any metadata arguments that were specified
	 * @return The actual widget instance
	 */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;

	/*
	 * Override this function to add any columns to the WidgetRow before CreateWidget is called
	 * 
	 * @param DataStorage A pointer to the TEDS data storage interface
	 * @param WidgetRow The row that contains information about the widget itself
	 * @return Whether any columns were added
	 */
	TYPEDELEMENTFRAMEWORK_API virtual bool SetColumns(IEditorDataStorageProvider* DataStorage, RowHandle WidgetRow) override;

protected:

	/** Old CreateWidget overload that exists for backwards compatibility, you should use the overload that provides the row instead */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override final;
	
	/** 
	 * Old function in the widget creation pipeline that isn't used anymore. All your logic should go in CreateWidget() itself
	 */
	TYPEDELEMENTFRAMEWORK_API virtual bool FinalizeWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		RowHandle Row,
		const TSharedPtr<SWidget>& Widget) override final;
	
	/**
	 * Helper function to call SetColumns() and CreateWidget(), you should not need to override this for simple widget constructors
	 */
	TYPEDELEMENTFRAMEWORK_API virtual TSharedPtr<SWidget> Construct(
		RowHandle WidgetRow, 
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override final;
};

template<>
struct TStructOpsTypeTraits<FTypedElementWidgetConstructor> : public TStructOpsTypeTraitsBase2<FTypedElementWidgetConstructor>
{
	enum
	{
		WithNoInitConstructor = true,
		WithPureVirtual = true,
	};
};

class IEditorDataStorageUiProvider : public IModularFeature
{
	using RowHandle = UE::Editor::DataStorage::RowHandle;

public:
	enum class EPurposeType : uint8
	{
		/** General purpose name which allows multiple factory registrations. */
		Generic,
		/**
		 * Only one factory can be registered with this purpose. If multiple factories are registered only the last factory
		 * will be stored.
		 */
		UniqueByName,
		/**
		 * Only one factory can be registered with this purpose for a specific combination of columns. If multiple factories
		 * are registered only the last factory will be stored.
		 */
		UniqueByNameAndColumn
	};

	enum class EMatchApproach : uint8
	{
		/** 
		 * Looks for the longest chain of columns matching widget factories. The matching columns are removed and the process
		 * is repeated until there are no more columns or no matches are found.
		 */
		LongestMatch,
		/** A single widget factory is reduced which matches the requested columns exactly. */
		ExactMatch,
		/** Each column is matched to widget factory. Only widget factories that use a single column are used. */
		SingleMatch
	};

	using WidgetCreatedCallback = TFunctionRef<void(const TSharedRef<SWidget>& NewWidget, RowHandle Row)>;
	using WidgetConstructorCallback = TFunctionRef<bool(TUniquePtr<FTypedElementWidgetConstructor>, TConstArrayView<TWeakObjectPtr<const UScriptStruct>>)>;
	using WidgetPurposeCallback = TFunctionRef<void(FName, EPurposeType, const FText&)>;

	/**
	 * Register a widget purpose. Widget purposes indicates how widgets can be used and categorizes/organizes the available 
	 * widget factories. If the same purpose is registered multiple times, only the first will be recorded and later registrations
	 * will be silently ignored.
	 * Widget purposes follow a specific naming convention currently:
	 * "PurposeName.Cell" for widgets created for (row, columns) pairs
	 * "PurposeName.Header" for widgets created for column headers
	 * "PurposeName.Cell.Default" or "PurposeName.Header.Default" for generic widgets not registered against column(s)
	 */
	virtual void RegisterWidgetPurpose(FName Purpose, EPurposeType Type, FText Description) = 0;
	/** 
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version registers a generic type. Construction using these are typically cheaper as they can avoid
	 * copying the Constructor and take up less memory. The downside is that they can't store additional configuration
	 * options. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * If registration is successful true will be returned otherwise false.
	 */
	virtual bool RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor) = 0;
	/**
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version registers a generic type. Construction using these are typically cheaper as they can avoid
	 * copying the Constructor and take up less memory. The downside is that they can't store additional configuration
	 * options. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * If registration is successful true will be returned otherwise false.
	 */
	template<typename ConstructorType>
	bool RegisterWidgetFactory(FName Purpose);
	/**
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version registers a generic type. Construction using these are typically cheaper as they can avoid
	 * copying the Constructor and take up less memory. The downside is that they can't store additional configuration
	 * options. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * The provided columns will be used when matching the factory during widget construction.
	 * If registration is successful true will be returned otherwise false.
	 */
	virtual bool RegisterWidgetFactory(FName Purpose, const UScriptStruct* Constructor,
		UE::Editor::DataStorage::Queries::FConditions Columns) = 0;
	/**
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version registers a generic type. Construction using these are typically cheaper as they can avoid
	 * copying the Constructor and take up less memory. The downside is that they can't store additional configuration
	 * options. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * The provided columns will be used when matching the factory during widget construction.
	 * If registration is successful true will be returned otherwise false.
	 */
	template<typename ConstructorType>
	bool RegisterWidgetFactory(FName Purpose, UE::Editor::DataStorage::Queries::FConditions Columns);
	/**
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version uses a previously created instance of the Constructor. The benefit of this is that it store
	 * configuration options. The downside is that it takes up more memory and requires copying when it's
	 * used. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * If registration is successful true will be returned otherwise false.
	 */
	virtual bool RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor) = 0;
	/**
	 * Registers a widget factory that will be called when the purpose it's registered under is requested.
	 * This version uses a previously created instance of the Constructor. The benefit of this is that it store
	 * configuration options. The downside is that it takes up more memory and requires copying when it's
	 * used. If the purpose has not been registered the factory will not be recorded and a warning will be printed.
	 * The provided columns will be used when matching the factory during widget construction.
	 * If registration is successful true will be returned otherwise false.
	 */
	virtual bool RegisterWidgetFactory(FName Purpose, TUniquePtr<FTypedElementWidgetConstructor>&& Constructor, 
		UE::Editor::DataStorage::Queries::FConditions Columns) = 0;
	
	/** 
	 * Creates widget constructors for the requested purpose.
	 * The provided arguments will be used to configure the constructor. Settings made this way will be applied to all
	 * widgets created from the constructor, if applicable.
	 */
	virtual void CreateWidgetConstructors(FName Purpose, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments, const WidgetConstructorCallback& Callback) = 0;
	/** 
	 * Finds matching widget constructors for provided columns, preferring longer matches over shorter matches.
	 * The provided list of columns will be updated to contain all columns that couldn't be matched.
	 * The provided arguments will be used to configure the constructor. Settings made this way will be applied to all
	 * widgets created from the constructor, if applicable.
	 */
	virtual void CreateWidgetConstructors(FName Purpose, EMatchApproach MatchApproach, TArray<TWeakObjectPtr<const UScriptStruct>>& Columns,
		const UE::Editor::DataStorage::FMetaDataView& Arguments, const WidgetConstructorCallback& Callback) = 0;

	/**
	 * Creates all the widgets registered under the provided name. This may be a large number of widgets for a wide name
	 * or exactly one when the exact name of the widget is registered. Arguments can be provided, but widgets are free
	 * to ignore them.
	 */
	virtual void ConstructWidgets(FName Purpose, const UE::Editor::DataStorage::FMetaDataView& Arguments,
		const WidgetCreatedCallback& ConstructionCallback) = 0;

	/** 
	 * Creates a single widget using the provided constructor. 
	 * The provided row will be used to store the widget information on. If columns have already been added to the row, the 
	 * constructor is free to use that to configure the widget. Arguments are used by the constructor to configure the widget.
	 */
	virtual TSharedPtr<SWidget> ConstructWidget(RowHandle Row, FTypedElementWidgetConstructor& Constructor,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) = 0;

	/** Calls the provided callback for all known registered widget purposes. */
	virtual void ListWidgetPurposes(const WidgetPurposeCallback& Callback) const = 0;

	/** Check if a custom extension is supported. This can be used to check for in-development features, custom extensions, etc. */
	virtual bool SupportsExtension(FName Extension) const = 0;
	/** Provides a list of all extensions that are enabled. */
	virtual void ListExtensions(TFunctionRef<void(FName)> Callback) const = 0;
};


//
// Implementations
//

template<typename ConstructorType>
bool IEditorDataStorageUiProvider::RegisterWidgetFactory(FName Purpose)
{
	return this->RegisterWidgetFactory(Purpose, ConstructorType::StaticStruct());
}

template<typename ConstructorType>
bool IEditorDataStorageUiProvider::RegisterWidgetFactory(FName Purpose, UE::Editor::DataStorage::Queries::FConditions Columns)
{
	return this->RegisterWidgetFactory(Purpose, ConstructorType::StaticStruct(), Columns);
}
