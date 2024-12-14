// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "IDetailCustomNodeBuilder.h"

class IAssetReferenceFilter;
class IPropertyHandle;
class IDetailGroup;
class IDetailPropertyRow;
class IPropertyHandle;
class FStructOnScope;
class SWidget;
class SInstancedStructPicker;
struct FInstancedStruct;
class FInstancedStructProvider;

/**
 * Type customization for FInstancedStruct.
 */
class STRUCTUTILSEDITOR_API FInstancedStructDetails : public IPropertyTypeCustomization
{
public:
	virtual ~FInstancedStructDetails() override;
	
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	using FReplacementObjectMap = TMap<UObject*, UObject*>;
	void OnObjectsReinstanced(const FReplacementObjectMap& ObjectMap);

	/** Handle to the struct property being edited */
	TSharedPtr<IPropertyHandle> StructProperty;

	TSharedPtr<SInstancedStructPicker> StructPicker;
	TSharedPtr<IPropertyUtilities> PropUtils;
	
	FDelegateHandle OnObjectsReinstancedHandle;
};

/** 
 * Node builder for FInstancedStruct children.
 * Expects property handle holding FInstancedStruct as input.
 * Can be used in a implementation of a IPropertyTypeCustomization CustomizeChildren() to display editable FInstancedStruct contents.
 * OnChildRowAdded() is called right after each property is added, which allows the property row to be customizable.
 * Child properties will be grouped if they 1) have "Category" metadata, and 2) have the "EnableCategories" metadata tag.
 */
class STRUCTUTILSEDITOR_API FInstancedStructDataDetails : public IDetailCustomNodeBuilder, public TSharedFromThis<FInstancedStructDataDetails>
{
public:
	FInstancedStructDataDetails(TSharedPtr<IPropertyHandle> InStructProperty);
	virtual ~FInstancedStructDataDetails() override;

	//~ Begin IDetailCustomNodeBuilder interface
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override;
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool RequiresTick() const override { return true; }
	virtual bool InitiallyCollapsed() const override { return false; }
	virtual FName GetName() const override;
	//~ End IDetailCustomNodeBuilder interface

	// Called when a child is added, override to customize a child row.
	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) {}

private:
	void OnUserDefinedStructReinstancedHandle(const class UUserDefinedStruct& Struct);

	/** Pre/Post change notifications for struct value changes */
	void OnStructValuePreChange();
	void OnStructValuePostChange();
	void OnStructHandlePostChange();

	/** Returns type of the instanced struct for each instance/object being edited. */
	TArray<TWeakObjectPtr<const UStruct>> GetInstanceTypes() const;

	/**
	 * Adds groups for the specified properties. One group is created for each unique category (from property metadata) that the properties have.
	 * If a category is pipe-separated (eg, Foo|Bar), one group is added for "Foo" and another one for "Foo|Bar". In the returned map, the key is the
	 * property, and the value is the group. If the property doesn't have a group (category), then it will not have an entry in the map. Note that
	 * the property must opt-in to grouping by specifying the "EnableCategories" metadata tag.
	 */
	void GetPropertyGroups(const TArray<TSharedPtr<IPropertyHandle>>& InProperties, IDetailChildrenBuilder& InChildBuilder, TMap<TSharedPtr<IPropertyHandle>, IDetailGroup*>& OutPropertyToGroup) const;

	/** Cached instance types, used to invalidate the layout when types change. */
	TArray<TWeakObjectPtr<const UStruct>> CachedInstanceTypes;
	
	/** Handle to the struct property being edited */
	TSharedPtr<IPropertyHandle> StructProperty;

	/** Struct provider for the structs. */
	TSharedPtr<FInstancedStructProvider> StructProvider;

	/** Delegate that can be used to refresh the child rows of the current struct (eg, when changing struct type) */
	FSimpleDelegate OnRegenerateChildren;

	/** True if we're currently handling a StructValuePostChange */
	bool bIsHandlingStructValuePostChange = false;
	
	FDelegateHandle UserDefinedStructReinstancedHandle;

protected:
	void OnStructLayoutChanges();
};
