// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Modules/ModuleInterface.h"
#include "PropertyHandle.h"
#include "Templates/Function.h"

// Forward Declarations
class IDetailPropertyRow;
class IPropertyHandle;
class SSearchableComboBox;
class UEdGraphPin;
class UMetasoundEditorGraph;
class UMetasoundEditorGraphMemberDefaultLiteral;
class UMetasoundEditorGraphNode;


namespace Metasound::Editor
{
	class METASOUNDEDITOR_API FMetasoundDefaultLiteralCustomizationBase
	{
	protected:
		IDetailCategoryBuilder* DefaultCategoryBuilder = nullptr;

	public:
		using FOnDefaultPageRowAdded = TFunction<void(IDetailPropertyRow& /* Value Row */, TSharedRef<IPropertyHandle> /* Page Property Handle */)>;

		FMetasoundDefaultLiteralCustomizationBase(IDetailCategoryBuilder& InDefaultCategoryBuilder);
		virtual ~FMetasoundDefaultLiteralCustomizationBase();

		virtual void CustomizeDefaults(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout);

		UE_DEPRECATED(5.5, "Use CustomizeDefaults instead and provide returned customized handles")
		virtual TArray<IDetailPropertyRow*> CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout);

		virtual TAttribute<EVisibility> GetDefaultVisibility() const;
		virtual TAttribute<bool> GetEnabled() const;

		virtual void SetDefaultVisibility(TAttribute<EVisibility> VisibilityAttribute);
		virtual void SetEnabled(TAttribute<bool> EnableAttribute);
		virtual void SetResetOverride(const TOptional<FResetToDefaultOverride>& InResetOverride);

	protected:
		void CustomizePageDefaultRows(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout);
		virtual void BuildDefaultValueWidget(IDetailPropertyRow& ValueRow, TSharedPtr<IPropertyHandle> ValueProperty);

		TArray<TSharedPtr<IPropertyHandle>> DefaultProperties;

	private:
		TSharedRef<SWidget> BuildPageDefaultNameWidget(UMetasoundEditorGraphMemberDefaultLiteral& Literal, TSharedRef<IPropertyHandle> ElementProperty);
		void BuildPageDefaultComboBox(UMetasoundEditorGraphMemberDefaultLiteral& Literal, FText RowName);
		void UpdatePagePickerNames(TWeakObjectPtr<UMetasoundEditorGraphMemberDefaultLiteral> LiteralPtr);

		TArray<TSharedPtr<FString>> AddablePageStringNames;
		TSet<FName> ImplementedPageNames;
		TSharedPtr<SSearchableComboBox> PageDefaultComboBox;
		FDelegateHandle OnPageSettingsUpdatedHandle;

		TAttribute<bool> Enabled;
		TAttribute<EVisibility> Visibility;
		TOptional<FResetToDefaultOverride> ResetOverride;
	};

	class METASOUNDEDITOR_API IMemberDefaultLiteralCustomizationFactory
	{
	public:
		virtual ~IMemberDefaultLiteralCustomizationFactory() = default;

		virtual TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const = 0;
	};
} // namespace Metasound::Editor
