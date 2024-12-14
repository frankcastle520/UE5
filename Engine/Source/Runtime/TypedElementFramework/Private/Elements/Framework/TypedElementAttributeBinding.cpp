// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementAttributeBinding.h"

#include "Elements/Common/EditorDataStorageFeatures.h"

namespace UE::Editor::DataStorage
{
	FAttributeBinder::FAttributeBinder(RowHandle InTargetRow)
		: FAttributeBinder(InTargetRow, GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
	{
	}

	FAttributeBinder::FAttributeBinder(
		RowHandle InTargetRow, IEditorDataStorageProvider* InDataStorage)
		: TargetRow(InTargetRow)
		, DataStorage(InDataStorage)
	{
		ensureMsgf(DataStorage, TEXT("The Editor Data Storage plugin needs to be enabled to use attribute bindings."));
	}

	FTextAttributeFormatted FAttributeBinder::BindTextFormat(FTextFormat Format) const
	{
		return FTextAttributeFormatted(MoveTemp(Format), TargetRow, DataStorage);
	}
} // namespace UE::Editor::DataStorage
