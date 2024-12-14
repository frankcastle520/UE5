// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementAttributeBindingText.h"

namespace UE::Editor::DataStorage
{
	FTextAttributeFormatted::FTextAttributeFormatted(
		FTextFormat InFormat, RowHandle InTargetRow, IEditorDataStorageProvider* InDataStorage)
		: Format(MoveTemp(InFormat))
		, TargetRow(InTargetRow)
		, DataStorage(InDataStorage)
	{
	}
	
	FTextAttributeFormatted& FTextAttributeFormatted::Arg(FString Name, FFormatArgumentValue Value)
	{
		NamedProperties.Add(MoveTemp(Name), FPropertyInfo{ .Default = MoveTemp(Value) });
		return *this;
	}

	FTextAttributeFormatted::operator TAttribute<FText>()
	{
		using namespace Private; 
		
		return TAttribute<FText>::CreateLambda(
			[
				Format = MoveTemp(Format),
				NamedProperties = MoveTemp(NamedProperties),
				Storage = DataStorage,
				Row = TargetRow
			]()
			{
				FFormatNamedArguments NamedArguments;
				NamedArguments.Reserve(NamedProperties.Num());
				for (const TPair<FString, FPropertyInfo>& Property : NamedProperties)
				{
					if (Property.Value.Property.IsBound())
					{
						const UScriptStruct* ColumnType = Property.Value.Property.GetObjectTypeInfo();

						if(Property.Value.DynamicColumnIdentifier != NAME_None)
						{
							ColumnType = Storage->GenerateDynamicColumn(FDynamicColumnDescription{
								.TemplateType = ColumnType,
								.Identifier = Property.Value.DynamicColumnIdentifier
							});
						}

						if(ColumnType)
						{
							if (void* Column = Storage->GetColumnData(Row, ColumnType))
							{
								// We use the original column type (Property.GetObjectTypeInfo()) instead of the dynamic ColumnType
								// so that the type info in the property matches
								NamedArguments.Add(Property.Key, Property.Value.Property.Get(Column, Property.Value.Property.GetObjectTypeInfo()));
								continue;
							}
						}
					}
					NamedArguments.Add(Property.Key, Property.Value.Default);
				}
				return FText::Format(Format, NamedArguments);
			});
	}
}
