// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "WidgetPreview.generated.h"

class SWidget;
class UPanelWidget;
class UUserWidget;
class UWidget;
class UWidgetBlueprint;
class UWidgetPreview;
struct FImage;

enum class EWidgetPreviewWidgetChangeType : uint8
{
	Assignment = 0,
	Reinstanced = 1,
	Structure = 2,
	ChildReference = 3,
	Destroyed = 4				// Just before the Slate widget is destroyed, etc.
};

USTRUCT(BlueprintType)
struct UMGWIDGETPREVIEW_API FPreviewableWidgetVariant
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Widget", DisplayName = "Widget Type", meta = (AllowedClasses = "/Script/UMGEditor.WidgetBlueprint, /Script/UMGWidgetPreview.WidgetPreview"))
	FSoftObjectPath ObjectPath;

	FPreviewableWidgetVariant() = default;
	explicit FPreviewableWidgetVariant(const TSubclassOf<UUserWidget>& InWidgetType);
	explicit FPreviewableWidgetVariant(const UWidgetPreview* InWidgetPreview);

public:
	/** Flushes cached widgets and re-resolves from the ObjectPath. */
	void UpdateCachedWidget();

	/** Returns the referenced Object as a UUserWidget (CDO). Returns nullptr if not found, or we couldn't find a nested UUserWidget (ie. inside a UWidgetPreview). */
	const UUserWidget* AsUserWidgetCDO() const;

	/** Returns the referenced Object as a UWidgetPreview. Returns nullptr if not found, or not a UWidgetPreview. */
	const UWidgetPreview* AsWidgetPreview() const;

	friend bool operator==(const FPreviewableWidgetVariant& Left, const FPreviewableWidgetVariant& Right)
	{
		return Left.ObjectPath == Right.ObjectPath;
	}

	friend bool operator!=(const FPreviewableWidgetVariant& Left, const FPreviewableWidgetVariant& Right)
	{
		return !(Left == Right);
	}

private:
	UPROPERTY(Transient)
	TObjectPtr<const UUserWidget> CachedWidgetCDO;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWidgetPreview> CachedWidgetPreview;
};

UCLASS(BlueprintType, NotBlueprintable, AutoExpandCategories = "Widgets")
class UMGWIDGETPREVIEW_API UWidgetPreview
	: public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	/** Convenience function to check that all utilized widgets have bCanCallInitializedWithoutPlayerContext set to true, and reports any that don't. */
	bool CanCallInitializedWithoutPlayerContext(const bool bInRecursive, TArray<const UUserWidget*>& OutFailedWidgets);

	// @todo: move to utility func somewhere else?
	/** Convenience function to check that the provided widget (and it's children) has bCanCallInitializedWithoutPlayerContext set to true, and reports any that don't. */
	static bool CanCallInitializedWithoutPlayerContextOnWidget(const UUserWidget* InUserWidget, const bool bInRecursive, TArray<const UUserWidget*>& OutFailedWidgets);

public:
	using FOnWidgetChanged = TMulticastDelegate<void(const EWidgetPreviewWidgetChangeType)>;

	FOnWidgetChanged& OnWidgetChanged() { return OnWidgetChangedDelegate; }

	UFUNCTION(BlueprintCallable, Category = "Layout")
	const TArray<FName>& GetWidgetSlotNames() const;

	/** Returns or builds and returns an instance of the root widget for previewing. Can be used to trigger a rebuild. */
	[[maybe_unused]] UUserWidget* GetOrCreateWidgetInstance(UWorld* InWorld, const bool bInForceRecreate = false);

	/** Returns the current widget instance, if any. */
	UUserWidget* GetWidgetInstance() const;

	/** Returns the current underlying slate widget instance, if any. */
	TSharedPtr<SWidget> GetSlateWidgetInstance() const;

	/** Stores the current instance in PreviousWidgetInstance, and clears WidgetInstance. */
	void ClearWidgetInstance();

	const UUserWidget* GetWidgetCDO() const;
	const UUserWidget* GetWidgetCDOForSlot(const FName InSlotName) const;

	const FPreviewableWidgetVariant& GetWidgetType() const;
	void SetWidgetType(const FPreviewableWidgetVariant& InWidget);

	const TMap<FName, FPreviewableWidgetVariant>& GetSlotWidgetTypes() const;
	void SetSlotWidgetTypes(const TMap<FName, FPreviewableWidgetVariant>& InWidgets);

protected:
	virtual void PostLoad() override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	void OnWidgetBlueprintChanged(UBlueprint* InBlueprint);

	/** Misc. functionality to perform after a widget assignment is changed. */
	void UpdateWidgets();

	/** Creates a new WidgetInstance, replacing the current one if it exists. */
	UUserWidget* CreateWidgetInstance(UWorld* InWorld);

	void CleanupReferences();

	/** Returns slot names not already occupied in SlotWidgets. */
	UFUNCTION(BlueprintCallable, Category = "Widget")
	TArray<FName> GetAvailableWidgetSlotNames();

private:
	/** Widget to preview. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Getter, Setter, Category = "Widget", DisplayName = "Widget", meta = (AllowPrivateAccess = "true", ShowOnlyInnerProperties))
	FPreviewableWidgetVariant WidgetType;

	/** Widget per-slot, if WidgetType has any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Getter, Setter, Category = "Widget", DisplayName = "Slot Widgets", meta = (AllowPrivateAccess = "true", GetKeyOptions = "GetAvailableWidgetSlotNames", ShowOnlyInnerProperties))
	TMap<FName, FPreviewableWidgetVariant> SlotWidgetTypes;

	UPROPERTY(DuplicateTransient)
	TObjectPtr<UUserWidget> WidgetInstance;

	TSharedPtr<SWidget> SlateWidgetInstance;

	/** Slot names available in WidgetType (if any). */
	UPROPERTY(Transient)
	TArray<FName> SlotNameCache;

	/** Widgets here should be checked for validity when a new one is assigned, to allow tear-down functionality. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<const UUserWidget>> WidgetReferenceCache;

	FOnWidgetChanged OnWidgetChangedDelegate;
};
