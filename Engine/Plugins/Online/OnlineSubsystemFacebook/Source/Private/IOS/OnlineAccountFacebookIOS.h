// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once
 
// Module includes
#include "OnlineAccountFacebookCommon.h"

@class FBSDKProfile;

/**
 * IOS specialization to support limited login
 */
class FUserOnlineAccountFacebookIOS : public FUserOnlineAccountFacebookCommon
{

public:
	using FUserOnlineAccountFacebookCommon::FUserOnlineAccountFacebookCommon;

	// FOnlineAccountFacebookIOS
	/**
	 * Create from FBSDKProfile when using limited login
	 */
	FUserOnlineAccountFacebookIOS(FBSDKProfile* FromProfile);
	
	/**
	 * Default destructor
	 */
	virtual ~FUserOnlineAccountFacebookIOS();

};
