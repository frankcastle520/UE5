// Copyright Epic Games, Inc. All Rights Reserved.
#include "ScopedLogSection.h"
#include "Logging/LogMacros.h"
//#include "UObject/Class.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMutableValidation, Log, All);
DEFINE_LOG_CATEGORY(LogMutableValidation);


FScopedLogSection::FScopedLogSection(EMutableLogSection Section)
{
	// Two scopes can not be active at the same time (not handled at the parsing level)
	check(CurrentSection == EMutableLogSection::Undefined);
	
	UE_LOG(LogMutableValidation,Log,TEXT(" SECTION START : %s "), *GetLogSectionName(Section));
	
	// Set the current section handled by this object
	CurrentSection = Section;
}


FScopedLogSection::~FScopedLogSection()
{
	// You are trying to close a section that is already closed. This should not be possible as we handle it in this objects
	check(CurrentSection != EMutableLogSection::Undefined);
	
	UE_LOG(LogMutableValidation,Log,TEXT(" SECTION END : %s "), *GetLogSectionName(CurrentSection));
	
	// Set the current section to none (undefined)
	CurrentSection = EMutableLogSection::Undefined;
}


	
FString FScopedLogSection::GetLogSectionName(EMutableLogSection Section) const
{
	FString Output (TEXT("unknown"));
	
	 switch(Section)
	 {
	 case EMutableLogSection::Undefined:
	 	Output = TEXT("undefined");
 		break;
	 case EMutableLogSection::Compilation:
	 	Output = TEXT("compilation");
 		break;
	 case EMutableLogSection::Update:
	 	Output = TEXT("update");
 		break;
	 case EMutableLogSection::Bake:
	 	Output = TEXT("bake");
 		break;
	 default:
	 	checkNoEntry();
	 	break;
	 }

	return Output;
}

