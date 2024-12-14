// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrossChangelistValidator.h"
#include "Misc/Paths.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/ChangelistService.h"

bool FCrossChangelistValidator::Validate(const FString& InCLDescription, const TArray<FSourceControlStateRef>& InFilteredFilesInCL, const TArray<const FTag*>& InTags)
{
	const TArray<FSourceControlChangelistStatePtr> OtherChangelistsStates = ServiceProvider.Pin()->GetService<FChangelistService>()->GetOtherChangelistsStates();
	bool bValid = true;

	bValid &= CheckHeaderAndCppInDifferentChangelist(OtherChangelistsStates);

	ValidationFinished(bValid);
	return true;
}

bool FCrossChangelistValidator::CheckHeaderAndCppInDifferentChangelist(const TArray<FSourceControlChangelistStatePtr>& OtherChangelistsStates)
{
	const FString HeaderExt(TEXT(".h"));
	const FString CppExt(TEXT(".cpp"));
	const FString CExt(TEXT(".c"));

	const TArray<FSourceControlStateRef> &FilesInChangelist = ServiceProvider.Pin()->GetService<FChangelistService>()->GetFilesInCL();

	bool bValid = true;

	for (FSourceControlStateRef FileInCL : FilesInChangelist)
	{
		const FString& Filename = FileInCL->GetFilename();
		if ( Filename.EndsWith(HeaderExt, ESearchCase::IgnoreCase)
		  || Filename.EndsWith(CExt, ESearchCase::IgnoreCase)
		  || Filename.EndsWith(CppExt, ESearchCase::IgnoreCase))
		{
			TArray<FString> FilenamesToCheck;

			if (Filename.EndsWith(HeaderExt, ESearchCase::IgnoreCase))
			{
				FilenamesToCheck.Add(FPaths::GetCleanFilename(Filename.Replace(*HeaderExt, *CppExt)));
				FilenamesToCheck.Add(FPaths::GetCleanFilename(Filename.Replace(*HeaderExt, *CExt)));
			}

			if (Filename.EndsWith(CExt, ESearchCase::IgnoreCase))
			{
				FilenamesToCheck.Add(FPaths::GetCleanFilename(Filename.Replace(*CExt, *HeaderExt)));
			}

			if (Filename.EndsWith(CppExt, ESearchCase::IgnoreCase))
			{
				FilenamesToCheck.Add(FPaths::GetCleanFilename(Filename.Replace(*CppExt, *HeaderExt)));
			}

			for (const FSourceControlChangelistStatePtr& ChangelistState : OtherChangelistsStates)
			{
				for (const FSourceControlStateRef& FileSate : ChangelistState->GetFilesStates())
				{
					FString OtherFilename = FPaths::GetCleanFilename(FileSate->GetFilename());
					if (FilenamesToCheck.Contains(OtherFilename))
					{
						bValid = false;

						FString Message = FString::Printf(TEXT("[%s] %s file '%s' is not in the current CL, it is in CL '%s'"),
							*GetValidatorName(),
							OtherFilename.EndsWith(HeaderExt) ? TEXT("Header") : TEXT("CPP | C"),
							*OtherFilename,
							*(ChangelistState->GetChangelist()->GetIdentifier()));

						LogFailure(Message);
					}
				}
			}
		}
	}

	return bValid;
}
