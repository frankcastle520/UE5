// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"

#include "FileUtilities/ZipArchiveWriter.h"
#include "FileUtilities/ZipArchiveReader.h"

#if WITH_DEV_AUTOMATION_TESTS

#if WITH_ENGINE

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZipArchiveTest, "FileUtilities.ZipArchive", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter);

bool FZipArchiveTest::RunTest(const FString& InParameter)
{	
	const FString TempDir = FPaths::AutomationTransientDir();
	const FString Prefix = TEXT("ZipArchiveTest");
	const FString TxtExtension = TEXT(".txt");
	const FString TempFileToZip = FPaths::CreateTempFilename(*TempDir, *Prefix, *TxtExtension);

	// Contents to be zipped
	const FString FileContents = TEXT("FileUtilities ZipArchive Test");

	const FString ZipExtension = TEXT(".zip");
	const FString ZipFilePath = FPaths::ConvertRelativePathToFull(FPaths::CreateTempFilename(*TempDir, *Prefix, *ZipExtension));
	const FString TestDirectory = FPaths::GetPath(ZipFilePath);

	// Make sure the directory where OpenWrite is called exists
	const bool bMakeTree = true;
	UTEST_TRUE("Making directory tree", IFileManager::Get().MakeDirectory(*TestDirectory, bMakeTree));

	ON_SCOPE_EXIT
	{
		// Make sure the Tmp folder gets deleted when the tests finishes
		const bool bRequireExists = true;
		const bool bRemoveTree = true;
		IFileManager::Get().DeleteDirectory(*TestDirectory, bRequireExists, bRemoveTree);
	};

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	
	{
		// Creates a zip file

		IFileHandle* ZipFile = PlatformFile.OpenWrite(*ZipFilePath);
		UTEST_NOT_NULL("Zip File is valid", ZipFile);

		FZipArchiveWriter ZipWriter{ ZipFile };

		auto ANSIFileContents = StringCast<ANSICHAR>(*FileContents);
		TConstArrayView<uint8> StringView((uint8*) ANSIFileContents.Get(), ANSIFileContents.Length());
		ZipWriter.AddFile(FPaths::GetCleanFilename(TempFileToZip), StringView, FDateTime::Now());
	}
	
	{
		// Reads the zip file and see if the contents are correct

		IFileHandle* ZipFile = PlatformFile.OpenRead(*ZipFilePath);
		UTEST_NOT_NULL("Zip File is valid", ZipFile);

		FZipArchiveReader ZipReader{ ZipFile };
		const TArray<FString> FileNames = ZipReader.GetFileNames();
		UTEST_EQUAL("File Count", FileNames.Num(), 1);

		for (const FString& FileName : FileNames)
		{
			TArray<uint8> FileContentsBuffer;
			UTEST_TRUE("Try Read File From Zip", ZipReader.TryReadFile(FileName, FileContentsBuffer));

			TConstArrayView<ANSICHAR> StringView((ANSICHAR*) FileContentsBuffer.GetData(), FileContentsBuffer.Num());
			UTEST_EQUAL("Are Contents the Same", FString{ StringView }, FileContents);
		}
	}

	return true;
}

#endif // WITH_ENGINE

#endif // WITH_DEV_AUTOMATION_TESTS