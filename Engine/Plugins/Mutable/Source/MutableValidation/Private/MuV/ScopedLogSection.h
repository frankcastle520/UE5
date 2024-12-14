// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include  "HAL/Platform.h"

class FString;

/**
 * Names for the possible sections used during the logging of this module commandlets.
 * Do not change the name of them before consulting the Mutable team
 */
enum class EMutableLogSection : uint8
{
	Undefined = 0,
	Compilation,
	Update,
	Bake
};


/**
* Object that handles the logging of scope based log sections that we can later parse out and interpret externally 
*/
class FScopedLogSection
{
public:
	FScopedLogSection(EMutableLogSection Section);
	~FScopedLogSection();

private:

	/** Get the name of the context as a string of characters */
	FString GetLogSectionName(EMutableLogSection Section) const;
	
  	/** Section that this object is currently representing */
	static inline EMutableLogSection CurrentSection = EMutableLogSection::Undefined;
};