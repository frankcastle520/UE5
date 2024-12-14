// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableFactory.h"
#include "HierarchyTable.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "Animation/Skeleton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Kismet2/SClassPickerDialog.h"
#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "HierarchyTableEditorModule.h"

UHierarchyTableFactory::UHierarchyTableFactory()
{
	SupportedClass = UHierarchyTable::StaticClass();
	bCreateNew = true;
	TableType = nullptr;
}

UObject* UHierarchyTableFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	TObjectPtr<UHierarchyTable> HierarchyTable = NewObject<UHierarchyTable>(InParent, Class, Name, Flags, Context);
	HierarchyTable->Skeleton = Skeleton.Get();
	HierarchyTable->TableType = TableType;

	FHierarchyTableEditorModule& HierarchyTableModule = FModuleManager::GetModuleChecked<FHierarchyTableEditorModule>("HierarchyTableEditor");
	const UHierarchyTableTypeHandler_Base* Handler = HierarchyTableModule.FindHandler(TableType);
	check(Handler);

	FInstancedStruct DefaultEntry = Handler->GetDefaultEntry();
	HierarchyTable->InitializeTable(DefaultEntry);

	return HierarchyTable;
}

bool UHierarchyTableFactory::ConfigureProperties()
{
	Skeleton = nullptr;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateUObject(this, &UHierarchyTableFactory::OnSkeletonSelected);
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;

	PickerWindow = SNew(SWindow)
		.Title(INVTEXT("Pick Skeleton"))
		.ClientSize(FVector2D(500, 600))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			]
		];

	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
	PickerWindow.Reset();

	if (Skeleton == nullptr)
	{
		return false;
	}

	FStructViewerModule& StructViewerModule = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer");

	class FRuleFilter : public IStructViewerFilter
	{
	public:
		FRuleFilter()
		{
		}

		virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			static const UScriptStruct* BaseStruct = FHierarchyTableType::StaticStruct();
			return InStruct != BaseStruct && InStruct->IsChildOf(BaseStruct);
		}

		virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			return false;
		}
	};

	static TSharedPtr<FRuleFilter> Filter = MakeShared<FRuleFilter>();
	FStructViewerInitializationOptions Options;
	{
		Options.StructFilter = Filter;
		Options.Mode = EStructViewerMode::StructPicker;
		Options.DisplayMode = EStructViewerDisplayMode::ListView;
		Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
		Options.bShowNoneOption = false;
		Options.bShowUnloadedStructs = false;
		Options.bAllowViewOptions = false;
	}

	PickerWindow = SNew(SWindow)
		.Title(INVTEXT("Pick Type"))
		.ClientSize(FVector2D(500, 600))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		[
			StructViewerModule.CreateStructViewer(Options, FOnStructPicked::CreateLambda([this](const UScriptStruct* ChosenStruct)
				{
					TableType = ChosenStruct;
					PickerWindow->RequestDestroyWindow();
				}))
		]
	];

	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
	PickerWindow.Reset();

	return TableType != nullptr;
}

void UHierarchyTableFactory::OnSkeletonSelected(const FAssetData& SelectedAsset)
{
	Skeleton = Cast<USkeleton>(SelectedAsset.GetAsset());
	PickerWindow->RequestDestroyWindow();
}