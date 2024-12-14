// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VVMPackageTypes.generated.h"

UENUM()
enum class EVersePackageScope : uint8
{
	PublicAPI,    // Created by Epic and only public definitions will be visible to public users
	InternalAPI,  // Created by Epic and is entirely hidden from public users
	PublicUser,   // Created by a public user
	InternalUser, // Created by an Epic internal user
};

UENUM()
enum class EVersePackageType : uint8
{
	VNI,              // A package associated with a C++ module
	Content,          // A package associated with a plugin
	PublishedContent, // A package associated with the published content of a plugin
	Assets            // A package associated with the reflected binary assets of a plugin
};
