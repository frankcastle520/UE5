// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "IPropertyTypeCustomization.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

namespace ESelectInfo
{
	enum Type : int;
}

class IPropertyHandle;
class IPropertyUtilities;
class SWidget;
class UBlackboardData;
struct FValueOrBlackboardKeyBase;

class BEHAVIORTREEEDITOR_API FValueOrBBKeyDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

	bool CanEditDefaultValue() const;

protected:
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> KeyProperty;
	TSharedPtr<IPropertyHandle> DefaultValueProperty;
	TSharedPtr<IPropertyUtilities> CachedUtils;
	TArray<FName> MatchingKeys;

	virtual void ValidateData();
	virtual TSharedRef<SWidget> CreateDefaultValueWidget();

	void GetMatchingKeys(TArray<FName>& OutNames);
	bool HasAccessToBlackboard() const;
	TSharedRef<SWidget> OnGetKeyNames();
	void OnKeyChanged(int32 Index);
	FText GetKeyDesc() const;
	const FValueOrBlackboardKeyBase* GetDataPtr() const;
};

class FValueOrBBKeyDetails_Class : public FValueOrBBKeyDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:
	TSharedPtr<IPropertyHandle> BaseClassProperty;

	virtual void ValidateData() override;
	virtual TSharedRef<SWidget> CreateDefaultValueWidget() override;

	void OnBaseClassChanged();
	void OnSetClass(const UClass* NewClass);
	const UClass* OnGetSelectedClass() const;
	void BrowseToClass() const;
};

class FValueOrBBKeyDetails_Enum : public FValueOrBBKeyDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:
	TSharedPtr<IPropertyHandle> EnumTypeProperty;
	TSharedPtr<IPropertyHandle> NativeEnumTypeNameProperty;

	virtual void ValidateData() override;
	virtual TSharedRef<SWidget> CreateDefaultValueWidget() override;

	void OnEnumSelectionChanged(int32 NewValue, ESelectInfo::Type);
	void OnEnumTypeChanged();
	void OnNativeEnumTypeNameChanged();
	int32 GetEnumValue() const;
	bool CanEditEnumType() const;
};

class FValueOrBBKeyDetails_Object : public FValueOrBBKeyDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:
	TSharedPtr<IPropertyHandle> BaseClassProperty;

	virtual void ValidateData() override;
	virtual TSharedRef<SWidget> CreateDefaultValueWidget() override;

	void OnBaseClassChanged();
	void OnObjectChanged(const FAssetData& AssetData);
	FString OnGetObjectPath() const;
	void BrowseToObject() const;
};

class FValueOrBBKeyDetails_Struct : public FValueOrBBKeyDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:
	TSharedPtr<IPropertyHandle> EditDefaultsOnlyProperty;
};

class BEHAVIORTREEEDITOR_API FValueOrBBKeyDetails_WithChild : public FValueOrBBKeyDetails
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
};