// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/DataValidation/Fixer.h"

#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "DataValidationFixers"

namespace UE::DataValidation
{

TSharedRef<FFixToken> IFixer::CreateToken(const FText& Label)
{
	return FFixToken::Create(FText::Format(LOCTEXT("SingleFix", "Fix: {0}"), Label), AsShared(), 0);
}

}

#undef LOCTEXT_NAMESPACE
