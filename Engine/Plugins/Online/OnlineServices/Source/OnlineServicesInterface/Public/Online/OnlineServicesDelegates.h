// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Online/CoreOnline.h"

namespace UE::Online {

class FOnlineServicesCommon;
class FOnlineError;

/**
 * Online services delegates that are more external to the online services themselves
 */

/**
 * Notification that a new online subsystem instance has been created
 *
 * @param NewSubsystem the new instance created
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOnlineServicesCreated, TSharedRef<class IOnlineServices> /*NewServices*/);
extern ONLINESERVICESINTERFACE_API FOnOnlineServicesCreated OnOnlineServicesCreated;

/**
 * Notification that an online operation has completed
 *
 * !!!NOTE!!! The notification can happen on off-game threads, make sure the callbacks are thread-safe
 *
 * @param OpName the name of completed operation
 * @param OnlineServicesCommon the OnlineServices instance
 * @param OnlineError the result of completed operation
 * @param DurationInSeconds the duration of the operation from start to complete
 */
DECLARE_MULTICAST_DELEGATE_FourParams(FOnOnlineAsyncOpCompleted, const FString& OpName, const FOnlineServicesCommon& OnlineServicesCommon, const UE::Online::FOnlineError& OnlineError, double DurationInSeconds);
extern ONLINESERVICESINTERFACE_API FOnOnlineAsyncOpCompleted OnOnlineAsyncOpCompleted;

/* UE::Online */ }

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
