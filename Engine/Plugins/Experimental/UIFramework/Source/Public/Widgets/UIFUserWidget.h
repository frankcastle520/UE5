// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "INotifyFieldValueChanged.h"
#include "UIFWidget.h"
#include "Types/UIFSlotBase.h"

#include "UIFUserWidget.generated.h"

struct FUIFrameworkWidgetId;

class UUserWidget;
class UUIFrameworkUserWidget;

/**
 *
 */
USTRUCT()
struct FUIFrameworkUserWidgetNamedSlot : public FUIFrameworkSlotBase
{
	GENERATED_BODY()

	/** The name of the NamedSlot */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI Framework")
	FName SlotName;
};

/**
 *
 */
USTRUCT()
struct UIFRAMEWORK_API FUIFrameworkUserWidgetNamedSlotList : public FFastArraySerializer
{
	GENERATED_BODY()

	FUIFrameworkUserWidgetNamedSlotList() = default;
	FUIFrameworkUserWidgetNamedSlotList(UUIFrameworkUserWidget* InOwner)
		: Owner(InOwner)
	{}

public:
	//~ Begin of FFastArraySerializer
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	//~ End of FFastArraySerializer

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms);

	void AuthorityAddEntry(FUIFrameworkUserWidgetNamedSlot Entry);
	bool AuthorityRemoveEntry(UUIFrameworkWidget* Widget);
	FUIFrameworkUserWidgetNamedSlot* FindEntry(FUIFrameworkWidgetId WidgetId);
	const FUIFrameworkUserWidgetNamedSlot* AuthorityFindEntry(FName SlotName) const;
	void AuthorityForEachChildren(const TFunctionRef<void(UUIFrameworkWidget*)>& Func);

private:
	UPROPERTY()
	TArray<FUIFrameworkUserWidgetNamedSlot> Slots;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UUIFrameworkUserWidget> Owner;
};


template<>
struct TStructOpsTypeTraits<FUIFrameworkUserWidgetNamedSlotList> : public TStructOpsTypeTraitsBase2<FUIFrameworkUserWidgetNamedSlotList>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 *
 */
UCLASS(DisplayName = "UserWidget UIFramework")
class UIFRAMEWORK_API UUIFrameworkUserWidget : public UUIFrameworkWidget
{
	GENERATED_BODY()

public:
	UUIFrameworkUserWidget();

public:
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetWidgetClass(TSoftClassPtr<UWidget> Value);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	void SetNamedSlot(FName SlotName, UUIFrameworkWidget* Widget);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "UI Framework")
	UUIFrameworkWidget* GetNamedSlot(FName SlotName) const;

public:
	virtual bool LocalIsReplicationReady() const override;

	virtual void AuthorityForEachChildren(const TFunctionRef<void(UUIFrameworkWidget*)>& Func) override;
	virtual void AuthorityRemoveChild(UUIFrameworkWidget* Widget) override;
	virtual void LocalAddChild(FUIFrameworkWidgetId ChildId) override;

private:
	UPROPERTY(Replicated)
	FUIFrameworkUserWidgetNamedSlotList ReplicatedNamedSlotList;
};
