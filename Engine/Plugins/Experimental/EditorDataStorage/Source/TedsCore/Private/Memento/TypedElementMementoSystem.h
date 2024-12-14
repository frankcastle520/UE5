// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Elements/Common/TypedElementHandles.h"

class IEditorDataStorageProvider;
class UTedsMementoTranslatorBase;

namespace UE::Editor::DataStorage
{
	class FMementoSystem
	{
	public:
		explicit FMementoSystem(IEditorDataStorageProvider& InDataStorage);

		RowHandle CreateMemento(RowHandle SourceRow);
		void CreateMemento(RowHandle ReservedMementoRow, RowHandle SourceRow);
		void RestoreMemento(RowHandle MementoRow, RowHandle TargetRow);
		void DestroyMemento(RowHandle MementoRow);

	private:
		void CreateMementoInternal(RowHandle MementoRow, RowHandle SourceRow);

		TArray<const UTedsMementoTranslatorBase*> MementoTranslators;
		TableHandle MementoRowBaseTable;
		IEditorDataStorageProvider& DataStorage;
	};
} // namespace UE::Editor::DataStorage
