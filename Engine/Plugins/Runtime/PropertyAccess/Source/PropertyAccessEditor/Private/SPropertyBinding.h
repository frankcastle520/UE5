// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "IPropertyAccessEditor.h"

namespace ETextCommit { enum Type : int; }

class FMenuBuilder;
class UEdGraph;
class UBlueprint;
struct FEditorPropertyPath;

class SPropertyBinding : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SPropertyBinding){}

	SLATE_ARGUMENT(FPropertyBindingWidgetArgs, Args)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UBlueprint* InBlueprint, const TArray<FBindingContextStruct>& InBindingContextStructs);

protected:
	struct FFunctionInfo
	{
		FFunctionInfo()
			: Function(nullptr)
		{
		}

		FFunctionInfo(UFunction* InFunction)
			: DisplayName(InFunction->HasMetaData("ScriptName") ? InFunction->GetMetaDataText("ScriptName") : FText::FromName(InFunction->GetFName()))
			, Tooltip(InFunction->GetMetaData("Tooltip"))
			, FuncName(InFunction->GetFName())
			, Function(InFunction)
		{}

		FText DisplayName;
		FString Tooltip;

		FName FuncName;
		UFunction* Function;
	};

	struct FBindingContextStructCategory
	{
		FText Name;
		TArray<FBindingContextStructCategory> SubCategories;
		TArray<int32> BindingContextStructIndices;
	};


	TSharedRef<SWidget> OnGenerateDelegateMenu();
	void FillPropertyMenu(FMenuBuilder& MenuBuilder, UStruct* InOwnerStruct, TArray<TSharedPtr<FBindingChainElement>> InBindingChain);
	void FillCategoryMenu(FMenuBuilder& MenuBuilder, const FBindingContextStructCategory* Category);

	const FSlateBrush* GetLinkIcon() const;
	const FSlateBrush* GetCurrentBindingImage() const;
	FText GetCurrentBindingText() const;
	FSlateColor GetCurrentBindingTextColor() const;
	FText GetCurrentBindingToolTipText() const;
	FSlateColor GetCurrentBindingColor() const;

	bool CanRemoveBinding();
	void HandleRemoveBinding();

	void HandleAddBinding(TArray<TSharedPtr<FBindingChainElement>> InBindingChain);
	void HandleSetBindingArrayIndex(int32 InArrayIndex, ETextCommit::Type InCommitType, FProperty* InProperty, TArray<TSharedPtr<FBindingChainElement>> InBindingChain);

	void HandleCreateAndAddBinding();

	UStruct* ResolveIndirection(const TArray<TSharedPtr<FBindingChainElement>>& BindingChain) const;

	EVisibility GetGotoBindingVisibility() const;

	FReply HandleGotoBindingClicked();

	// Helper function to call the OnCanAcceptProperty* delegates, handles conversion of binding chain to TConstArrayView<FBindingChainElement> as expected by the delegate.
	bool CanAcceptPropertyOrChildren(FProperty* InProperty, TConstArrayView<TSharedPtr<FBindingChainElement>> InBindingChain) const;
	
	// Helper function to call the OnCanBindProperty* delegates, handles conversion of binding chain to TConstArrayView<FBindingChainElement> as expected by the delegate.
	bool CanBindProperty(FProperty* InProperty, TConstArrayView<TSharedPtr<FBindingChainElement>> InBindingChain) const;

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	bool HasAnyBindings() const;

private:

	bool IsClassDenied(UClass* OwnerClass) const;
	bool IsFieldFromDeniedClass(FFieldVariant Field) const;
	bool HasBindableProperties(UStruct* InStruct, TArray<TSharedPtr<FBindingChainElement>>& BindingChain) const;
	bool HasBindablePropertiesRecursive(UStruct* InStruct, TSet<UStruct*>& VisitedStructs, TArray<TSharedPtr<FBindingChainElement>>& BindingChain) const;

	/**
	 * Note that an ArrayView is not used to pass the BindingChain around since the predicate can modify the array
	 * and this will invalidate the ArrayView if reallocation is performed.
	 */
	template <typename Predicate>
	void ForEachBindableProperty(UStruct* InStruct, const TArray<TSharedPtr<FBindingChainElement>>& BindingChain, Predicate Pred) const;

	template <typename Predicate>
	void ForEachBindableFunction(UClass* FromClass, Predicate Pred) const;

	void AddCategoryToMenu(FMenuBuilder& MenuBuilder, const FBindingContextStructCategory& Category);
	void BuildContextStructCategoryRecursive(TConstArrayView<FString> CategoryNames, TArray<FBindingContextStructCategory>& ParentSubCategories, int32 ContextStructIndex);
	bool HasCategorySomethingToDisplayRecursive(const FBindingContextStructCategory& Category) const;

	TSharedRef<SWidget> MakeContextStructWidget(const FBindingContextStruct& ContextStruct) const;

	UBlueprint* Blueprint = nullptr;
	TArray<FBindingContextStruct> BindingContextStructs;
	// Top level sections of the binding ContextStructs
	TArray<FBindingContextStructCategory> BindingContextStructSections;
	FPropertyBindingWidgetArgs Args;
	FName PropertyName;
};
