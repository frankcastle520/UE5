// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Param/ParamType.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/STreeView.h"

class UAnimNextDataInterface;
class SWrapBox;
class UAnimNextModule_EditorData;
class UAnimNextRigVMAssetEditorData;

namespace UE::AnimNext::Editor
{

struct FDataInterfaceToAdd
{
	FDataInterfaceToAdd(UAnimNextDataInterface* InDataInterface)
		: DataInterface(InDataInterface)
	{}

	UAnimNextDataInterface* DataInterface;
};

struct FVariableToAdd
{
	FVariableToAdd(const FAnimNextParamType& InType, FName InName)
		: Type(InType)
		, Name(InName)
	{}

	// Type
	FAnimNextParamType Type;

	// Name for variable
	FName Name;
};

// Result of a filter operation via FOnFilterVariableType
enum class EFilterVariableResult : int32
{
	Include,
	Exclude
};

// Delegate called to filter variables by type for display to the user
using FOnFilterVariableType = TDelegate<EFilterVariableResult(const FAnimNextParamType& /*InType*/)>;

class SAddVariablesDialog : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SAddVariablesDialog)
		: _AllowMultiple(true)
	{}

	/** Whether we allow multiple variables to be added or just one at a time */
	SLATE_ARGUMENT(bool, AllowMultiple)

	/** Delegate called to filter variables by type for display to the user */
	SLATE_EVENT(FOnFilterVariableType, OnFilterVariableType)

	/** Initial variable type to use */
	SLATE_ARGUMENT(FAnimNextParamType, InitialParamType)

	/** Whether we should add an initial variable */
	SLATE_ARGUMENT(bool, ShouldAddInitialVariable)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TArray<UAnimNextRigVMAssetEditorData*>& InAssetEditorDatas);

	bool ShowModal(TArray<FVariableToAdd>& OutVariables, TArray<FDataInterfaceToAdd>& OutDataInterfaces);

	enum class EEntryType : uint8
	{
		Variable,
		DataInterface,
	};

	struct FEntry
	{
		FEntry(EEntryType InEntryType, const TSharedRef<SAddVariablesDialog>& InDialog)
			: Dialog(InDialog)
			, EntryType(InEntryType)
		{}

		virtual ~FEntry() = default;

		virtual bool IsValid(FText& OutReason) const = 0;

		TWeakPtr<SAddVariablesDialog> Dialog;
		TWeakPtr<FEntry> Parent;
		TArray<TSharedRef<FEntry>> Children;
		EEntryType EntryType;
		bool bIsNew = true;
	};

private:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void AddDataInterface(UAnimNextDataInterface* InDataInterface);

	void AddEntry(const FAnimNextParamType& InParamType = FAnimNextParamType());

	void RefreshEntries();

	void RefreshCanCreate();

	void DeleteSelectedItems();

	struct FDataInterfaceToAddEntry : FEntry, FDataInterfaceToAdd
	{
		FDataInterfaceToAddEntry(UAnimNextDataInterface* InDataInterface, const TSharedRef<SAddVariablesDialog>& InDialog)
			: FEntry(EEntryType::DataInterface, InDialog)
			, FDataInterfaceToAdd(InDataInterface)
		{
		}

		virtual bool IsValid(FText& OutReason) const override;
	};

	struct FVariableToAddEntry : FEntry, FVariableToAdd
	{
		FVariableToAddEntry(const FAnimNextParamType& InType, FName InName, const TSharedRef<SAddVariablesDialog>& InDialog)
			: FEntry(EEntryType::Variable, InDialog)
			, FVariableToAdd(InType, InName)
		{}

		virtual bool IsValid(FText& OutReason) const override;
	};

	TSharedRef<ITableRow> HandleGenerateRow(TSharedRef<FEntry> InEntry, const TSharedRef<STableViewBase>& InOwnerTable);

	void HandleGetChildren(TSharedRef<FEntry> InEntry, TArray<TSharedRef<FEntry>>& OutChildren);
	
	TSharedRef<SWidget> HandleGetAddVariableMenuContent(TSharedPtr<FEntry> InEntry);

	void GetPendingNamesRecursive(UAnimNextRigVMAssetEditorData* InEditorData, TArray<FName>& OutPendingNames) const;

	void GetPendingNames(TArray<FName>& OutPendingNames) const;

private:
	friend class SVariableToAdd;

	TSharedPtr<STreeView<TSharedRef<FEntry>>> EntriesTree;

	// Root entries of the tree
	TArray<TSharedRef<FEntry>> RootEntries;

	FOnFilterVariableType OnFilterVariableType;

	TArray<UAnimNextRigVMAssetEditorData*> AssetEditorDatas;

	FText CreateErrorMessage;

	bool bCanCreateVariables = false;

	bool bOKPressed = false;
};

}
