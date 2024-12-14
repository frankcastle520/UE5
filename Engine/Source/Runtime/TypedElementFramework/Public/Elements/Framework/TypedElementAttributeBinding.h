// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Internationalization/Text.h"
#include "Misc/Attribute.h"
#include "TypedElementAttributeBindingText.h"

class IEditorDataStorageProvider;

namespace UE::Editor::DataStorage
{
	/**
	 * Builder class that can be used as a shorthand to bind data inside a TEDS row, column pair to a TAttribute so the attribute updates if the data
	 * in the column is changed.
	 * 
	 * Usage Example:
	 * 
	 * FAttributeBinder Binder(RowHandle);
	 * TAttribute<int> TestAttribute(Binder.BindData(&FTestColumnInt::TestInt))
	 */
	class FAttributeBinder
	{
	public:

		/* Create an attribute binder for a given row. */
		TYPEDELEMENTFRAMEWORK_API FAttributeBinder(RowHandle InTargetRow);
		
		/* Create an attribute binder for a given row. */
		TYPEDELEMENTFRAMEWORK_API FAttributeBinder(RowHandle InTargetRow, IEditorDataStorageProvider* InDataStorage);

		/**
		 * Bind a specific data member inside a TEDS column to an attribute of the same type as the data
		 * 
		 * @param InVariable The data member inside a column to be bound
		 * @param InDefaultValue The default value to be used when the column isn't present on a row
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A TAttribute bound to the row, column pair specified
		 *
		 * Example:
		 * FAttributeBinder Binder(RowHandle);
		 * TAttribute<FString> TestAttribute(Binder.BindData(&FTypedElementLabelColumn::Label))
		 */
		template <typename AttributeType, TDataColumnType ColumnType>
		TAttribute<AttributeType> BindData(AttributeType ColumnType::* InVariable, const AttributeType& InDefaultValue = AttributeType(),
			const FName& InIdentifier = NAME_None);

		/**
		 * Bind a specific data member inside a TEDS column to an attribute of a different type than the data by providing a conversion function
		 * NOTE: the default value is not the actual attribute type but rather the data type in the column and it gets passed to the conversion function
		 *
		 * @param InVariable The data member inside a column to be bound
		 * @param InConverter Conversion function to convert from DataType -> AttributeType
		 * @param InDefaultValue The default value to be used when the column isn't present on a row
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A TAttribute bound to the row, column pair specified
		 *
		 * Example:
		 * UE::Editor::DataStorage::FAttributeBinder Binder(RowHandle);
		 * TAttribute<FText> TestAttribute(Binder.BindData(&FTypedElementLabelColumn::Label),
		 *                                 [](const FString& Data)
		 *                                   {
		 *                                      return FText::FromString(Data);
		 *                                   }
		 *                                 ));
		 */
		template <typename AttributeType, typename DataType, TDataColumnType ColumnType>
		TAttribute<AttributeType> BindData(DataType ColumnType::* InVariable, TFunction<AttributeType(const DataType&)> InConverter, const DataType& InDefaultValue = DataType(), const FName& InIdentifier = NAME_None);

		/**
		 * Overload for the conversion binder to accept lambdas instead of TFunctions
		 *
		 * @param InVariable The data member inside a column to be bound
		 * @param InConverter Conversion function to convert from DataType -> AttributeType
		 * @param InDefaultValue The default value to be used when the column isn't present on a row\
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A TAttribute bound to the row, column pair specified
		 */
		template <typename DataType, TDataColumnType ColumnType, typename FunctionType>
			requires Private::AttributeBinderInvocable<FunctionType, DataType>
		auto BindData(DataType ColumnType::* InVariable, FunctionType InConverter, const DataType& InDefaultValue = DataType(),
			const FName& InIdentifier = NAME_None);

		/**
		 * Bind a delegate inside a TEDS column to a SLATE_EVENT macro on a widget
		 * @param InVariable The delegate inside the TEDS column
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A delegate that can be provided to an event on a slate widget
		 */
		template <typename InRetValType, typename... ParamTypes, TDataColumnType ColumnType>
		TDelegate<InRetValType(ParamTypes...)> BindEvent(TDelegate<InRetValType(ParamTypes...)> ColumnType::* InVariable,
			const FName& InIdentifier = NAME_None);

		/**
		 * Directly bind an FString member in a TEDS column to an FText attribute as a shortcut
		 * @param InFStringVariable The FString variable
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A delegate that can be provided to a text widget in Slate (e.g STextBlock)
		 */
		template <TDataColumnType ColumnType>
		TAttribute<FText> BindText(FString ColumnType::* InFStringVariable, const FName& InIdentifier = NAME_None);

		/**
		 * Directly bind an FName member in a TEDS column to an FText attribute as a shortcut
		 * @param InFNameVariable The FName variable
		 * @param InIdentifier The identifier for this column if it is a dynamic column, NAME_None if it is not a dynamic column
		 * @return A delegate that can be provided to a text widget in Slate (e.g STextBlock)
		 */
		template <TDataColumnType ColumnType>
		TAttribute<FText> BindText(FName ColumnType::* InFNameVariable, const FName& InIdentifier = NAME_None);

		/**
		 * Composite a FText attribute using a format string and named arguments bound to TEDS columns.
		 * Arguments are passed in using the .Arg(...) function. Each argument starts with the name of the
		 * argument in the format string followed by one of the following options:
		 *		- A column variable that's a string (FText, FString or FName).
		 *		- A column variable with a converter to a string.
		 *		- A direct value that's supported by FFormatArgumentValue (FText and numbers).
		 * The final parameter for an argument is an optional default that optionally takes a FFormatArgumentValue value.
		 * @param Format The format to use composite a string together.
		 * @return A delegate that can be provided to a text widget in Slate (e.g STextBlock)
		 * 
		 * Example:
		 * TAttribute<FText> Attribute(Binder.BindTextFormat(
		 *		LOCTEXT("Format", "{Label}: {Value1}, {Value2}, {Value3}, {Value4}")
		 *		.Arg(TEXT("Label"), &FTypedElementLabelColumn::Label)
		 *		.Arg(TEXT("Value1"), &FValueColumn::Integer, [](const int32& Value) { return FText::AsNumber(Value); }, 42)
		 *		.Arg(TEXT("Value2"), &FTextColumn::Text, LOCTEXT("Default", "<no value>"))
		 *		.Arg(TEXT("Value3"), 42)
		 */
		TYPEDELEMENTFRAMEWORK_API FTextAttributeFormatted BindTextFormat(FTextFormat Format) const;
		
	private:

		// The target row for this binder
		RowHandle TargetRow;

		// A ptr to the data storage for quick access
		IEditorDataStorageProvider* DataStorage;
	};
} // namespace UE::Editor::DataStorage

#include "TypedElementAttributeBinding.inl"