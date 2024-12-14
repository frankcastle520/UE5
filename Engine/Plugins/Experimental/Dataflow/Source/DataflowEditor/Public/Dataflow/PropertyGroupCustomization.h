// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/ManagedArrayCollection.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class ITableRow;
class SComboButton;
class STableViewBase;
class SWidget;
class SDataflowGraphEditor;

namespace UE::Dataflow
{
	class FContext;

	/**
	 * Property group customization to allow selecting of a group from a list of the groups currently held by the node's collection.
	 */
	class FPropertyGroupCustomization : public IPropertyTypeCustomization
	{
	public:
		static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	private:
		//~ Begin IPropertyTypeCustomization interface
		DATAFLOWEDITOR_API virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
		DATAFLOWEDITOR_API virtual void CustomizeChildren(TSharedRef<IPropertyHandle> /*InPropertyHandle*/, IDetailChildrenBuilder& /*ChildBuilder*/, IPropertyTypeCustomizationUtils& /*CustomizationUtils*/) override {}
		//~ End IPropertyTypeCustomization interface

		/**
		 * Return the FManagedArrayCollection with the specified name from the property held by the top level struct owner of ChildPropertyHandle.
		 * @param Context The current editor Dataflow context.
		 * @param ChildPropertyHandle The handle of a customized property from which to find the owner struct.
		 * @param CollectionPropertyName
		 * @return The collection.
		 */
		static DATAFLOWEDITOR_API const FManagedArrayCollection& GetPropertyCollection(
			const TSharedPtr<UE::Dataflow::FContext>& Context,
			const TSharedPtr<IPropertyHandle>& ChildPropertyHandle,
			const FName CollectionPropertyName);

		/**
		 * Turn a string into a valid collection group or attribute name.
		 * The resulting name won't contains spaces and any other special characters as listed in
		 * INVALID_OBJECTNAME_CHARACTERS (currently "',/.:|&!~\n\r\t@#(){}[]=;^%$`).
		 * It will also have all leading underscore removed, as these names are reserved for internal use.
		 * @param InOutString The string to turn into a valid collection name.
		 * @return Whether the InOutString was already a valid collection name.
		 */
		static DATAFLOWEDITOR_API bool MakeGroupName(FString& InOutString);

		/** List of valid group names for the drop down list. Override this method to filter to a specific set of group names. */
		virtual TArray<FName> GetTargetGroupNames(const FManagedArrayCollection& Collection) const
		{
			return Collection.GroupNames();  // By default returns all of the collection's group names
		}

		/** Name of the collection property. Override this method to specify your own collection property name. */
		virtual FName GetCollectionPropertyName() const
		{
			return DefaultCollectionPropertyName;  // By default returns "Collection"
		}

		FText GetText() const;
		void OnTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo);
		void OnSelectionChanged(TSharedPtr<FText> ItemSelected, ESelectInfo::Type SelectInfo);
		bool OnVerifyTextChanged(const FText& Text, FText& OutErrorMessage);
		TSharedRef<ITableRow> MakeCategoryViewWidget(TSharedPtr<FText> Item, const TSharedRef<STableViewBase>& OwnerTable);
		TSharedRef<SWidget> OnGetMenuContent();
		
		static inline const FName DefaultCollectionPropertyName = TEXT("Collection");

		TWeakPtr<const SDataflowGraphEditor> DataflowGraphEditor;
		TSharedPtr<IPropertyHandle> ChildPropertyHandle;
		TWeakPtr<SComboButton> ComboButton;
		TArray<TSharedPtr<FText>> GroupNames;
	};
}
