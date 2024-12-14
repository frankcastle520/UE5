// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "TestDriver.h"
#include "TestHarness.h"

#include "Interfaces/OnlinePurchaseInterface.h"
#include "OnlineError.h"
#include "OnlineSubsystem.h"

struct FPurchaseGetReceiptsStep : public FTestPipeline::FStep
{
	FPurchaseGetReceiptsStep(FUniqueNetIdPtr* InUserId, const FString& InUniqueId, const FString& InItemName)
		: UserId(InUserId)
		, UniqueId(InUniqueId)
		, ItemName(InItemName)
	{}
	
	virtual ~FPurchaseGetReceiptsStep() = default;
	
	enum class EState { Init, GetReceiptsCall, GetReceiptsCalled, FinalizePurchaseCall, FinalizePurchaseCalled, Done } State = EState::Init;
	
	virtual EContinuance Tick(IOnlineSubsystem* OnlineSubsystem) override
	{
		switch (State)
		{
		case EState::Init:
		{
			OnlinePurchasePtr = OnlineSubsystem->GetPurchaseInterface();
			REQUIRE(OnlinePurchasePtr != nullptr);

			State = EState::GetReceiptsCall;
			break;
		}
		case EState::GetReceiptsCall:
		{
			OnlinePurchasePtr->GetReceipts(*UserId->Get(), OutReceipt);
		
			CHECK(OutReceipt.GetData()->ReceiptOffers[0].LineItems[0].UniqueId == UniqueId);
			CHECK(OutReceipt.GetData()->ReceiptOffers[0].LineItems[0].ItemName == ItemName);
		
			State = EState::GetReceiptsCalled;
			break;
		}
		case EState::GetReceiptsCalled:
		{
			State = EState::FinalizePurchaseCall;
			break;
		}
		case EState::FinalizePurchaseCall:
		{
			OnlinePurchasePtr->FinalizePurchase(*UserId->Get(), UniqueId);

			State = EState::FinalizePurchaseCalled;
			break;
		}
		case EState::FinalizePurchaseCalled:
		{
			State = EState::Done;
			break;
		}
		case EState::Done:
		{
			return EContinuance::Done;
		}
		}

		return EContinuance::ContinueStepping;
	}

protected:
	FUniqueNetIdPtr* UserId = nullptr;
	TArray<FPurchaseReceipt> OutReceipt = {};
	IOnlinePurchasePtr OnlinePurchasePtr = nullptr;
	FString UniqueId;
	FString ItemName;
};