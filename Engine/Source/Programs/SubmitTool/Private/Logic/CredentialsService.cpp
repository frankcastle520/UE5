// Copyright Epic Games, Inc. All Rights Reserved.

#include "CredentialsService.h"

#include "SubmitToolUtils.h"
#include "Logging/LogMacros.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/ProcessWrapper.h"

#include "JsonObjectConverter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Tasks/Task.h"

TUniquePtr<FAES::FAESKey> FCredentialsService::Key = nullptr;

FCredentialsService::FCredentialsService(const FOAuthTokenParams& InOAuthParameters)
	: Parameters(InOAuthParameters)
{
	if(IsOIDCTokenEnabled())
	{
		GetOIDCToken();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FCredentialsService::Tick), 5);
	}

	LoadKey();
	LoadCredentials();
}

void FCredentialsService::LoadKey()
{
	if(!IFileManager::Get().FileExists(*GetKeyFilepath()))
	{
		GenerateKey();
	}

	if(IFileManager::Get().FileExists(*GetKeyFilepath()))
	{
		FArchive* File = IFileManager::Get().CreateFileReader(*GetKeyFilepath());
		if(File->TotalSize() < 4)
		{
			UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected file size encryption key invalidated"));
			File->Close();
			delete File;
			File = nullptr;
			return;
		}

		int32 Size;
		*File << Size;

		// see if the file has exactly the length we expect
		// two int32 (a size and a garbage one) + the data + one garbage int32 in between the data
		if(File->TotalSize() != 4 + 4 + Size + 4)
		{
			UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected file size encryption key invalidated"));
			File->Close();
			delete File;
			File = nullptr;
			return;
		}

		int32 Garbage;
		*File << Garbage;

		TArray<uint8> Bytes;
		uint8 byte;

		for(size_t i = 0; i < Size; ++i)
		{
			if(i == 2)
			{
				*File << Garbage;
			}

			*File << byte;
			Bytes.Add(byte);
		}
		File->Close();
		delete File;
		File = nullptr;

		Key = MakeUnique<FAES::FAESKey>();
		check(Bytes.Num() == sizeof(FAES::FAESKey::Key));
		FMemory::Memcpy(Key->Key, &Bytes[0], sizeof(FAES::FAESKey::Key));
	}
}

void FCredentialsService::GenerateKey()
{
	TArray<uint8> dataArray;
	for(size_t i = 0; i < FAES::FAESKey::KeySize; ++i)
	{
		int32 random = FMath::Rand();
		dataArray.Add(random);
	}

	Key = MakeUnique<FAES::FAESKey>();
	FMemory::Memcpy(Key->Key, dataArray.GetData(), sizeof(FAES::FAESKey::Key));

	FArchive* File = IFileManager::Get().CreateFileWriter(*GetKeyFilepath(), EFileWrite::FILEWRITE_EvenIfReadOnly);
	int32 Size = FAES::FAESKey::KeySize;
	*File << Size;
	int32 Garbage = FMath::Rand();
	*File << Garbage;

	for(size_t i = 0; i < Size; ++i)
	{
		if(i == 2)
		{
			Garbage = FMath::Rand();
			*File << Garbage;
		}

		*File << dataArray[i];
	}

	File->Close();
	delete File;
	File = nullptr;
}

const FString FCredentialsService::GetKeyFilepath()
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), TEXT("SubmitTool"), TEXT(".cache"));
}

const FString FCredentialsService::GetCredentialsFilepath() const
{
	return FPaths::Combine(FSubmitToolUtils::GetLocalAppDataPath(), TEXT("SubmitTool"), TEXT("jira.dat"));
}

void FCredentialsService::GetOIDCToken()
{	
	UE_LOG(LogSubmitTool, Log, TEXT("Obtaining new OIDCToken"));
	GetOIDCTask = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[this] {
			FOnOutputLine OutputLineProcess = FOnOutputLine::CreateLambda([this](const FString& OutputLine, const EProcessOutputType& OutputType) {
				if(OutputType == EProcessOutputType::ProcessError)
				{
					UE_LOG(LogSubmitTool, Error, TEXT("%s"), *OutputLine);
				}
				else
				{
					// OIDCToken console output token starts with a {
					if(OutputLine.StartsWith(TEXT("{")))
					{
						ParseOIDCTokenData(OutputLine);
					}
					else
					{
						UE_LOG(LogSubmitToolDebug, Log, TEXT("%s"), *OutputLine);
					}
				}
				});

			FProcessWrapper GetToken = FProcessWrapper(TEXT("Oidc"), Parameters.OAuthTokenTool, FString::Printf(TEXT("%s"), *Parameters.OAuthArgs), FOnCompleted(), OutputLineProcess);
			GetToken.Start(true);

			if(GetToken.ExitCode != 0)
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Couldn't obtain OIDC credentials"));
				return false;
			}

			return true;
		});
}

bool FCredentialsService::ParseOIDCTokenData(const FString& InToken)
{
	TSharedPtr<FJsonObject> RootJsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InToken);
	FJsonSerializer::Deserialize(Reader, RootJsonObject);

	if(RootJsonObject.IsValid())
	{
		FString Expiration = RootJsonObject->GetStringField(TEXT("ExpiresAt"));
		FDateTime::ParseIso8601(*Expiration, TokenExpiration);
		UE_LOG(LogSubmitToolDebug, Log, TEXT("OIDC Token Expiration %s"), *Expiration);
		
		OIDCToken = RootJsonObject->GetStringField(TEXT("Token"));
		UE_LOG(LogSubmitTool, Log, TEXT("OIDC Token loaded correctly"));
		return true;
	}

	return false;
}

constexpr int JiraCredentialDatVersion = 1;
void FCredentialsService::SaveCredentials() const
{
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(LoginString.Len());
	StringToBytes(LoginString, Bytes.GetData(), LoginString.Len());

	int32 ActualLength = Bytes.Num();

	int32 NumBytesEncrypted = Align(Bytes.Num(), FAES::AESBlockSize);
	Bytes.SetNum(NumBytesEncrypted);
	FAES::EncryptData(Bytes.GetData(), Bytes.Num(), *Key);

	FArchive* File = IFileManager::Get().CreateFileWriter(*GetCredentialsFilepath(), EFileWrite::FILEWRITE_EvenIfReadOnly);

	if(File != nullptr)
	{
		int32 Version = JiraCredentialDatVersion;
		*File << Version;

		*File << NumBytesEncrypted;
		*File << ActualLength;
		int32 Garbage = FMath::Rand();
		*File << Garbage;
		File->Serialize(Bytes.GetData(), Bytes.Num());
		Garbage = FMath::Rand();
		*File << Garbage;

		File->Close();
		delete File;
		File = nullptr;
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("Could not create file '%s'."), *GetCredentialsFilepath());
	}
}

void FCredentialsService::LoadCredentials()
{
	if(Key.IsValid())
	{
		if(IFileManager::Get().FileExists(*GetCredentialsFilepath()))
		{
			FArchive* File = IFileManager::Get().CreateFileReader(*GetCredentialsFilepath());

			if(File != nullptr)
			{
				// Read the size
				if(File->TotalSize() < 4 + 4)
				{
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected file size login key invalidated"));
					File->Close();
					delete File;
					return;
				}

				int32 Version;
				*File << Version;

				// Check Versions here
				if(Version != JiraCredentialDatVersion)
				{
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected Credentials Version, aborting credentials loading."));
					File->Close();
					delete File;
					return;
				}

				int32 PaddedLength;
				int32 LengthWithoutPadding;

				*File << PaddedLength;
				*File << LengthWithoutPadding;

				// see if the file has exactly the length we expect
				// four int32 (Version, two sizes and one garbage) + the data + a final garbage int32
				if(File->TotalSize() != (4 * sizeof(int32) + PaddedLength + sizeof(int32)))
				{
					UE_LOG(LogSubmitToolDebug, Warning, TEXT("Unexpected file size login invalidated"));
					File->Close();
					delete File;
					return;
				}

				int32 Garbage;
				*File << Garbage;

				TArray<uint8> DeserializedBytes;
				DeserializedBytes.SetNum(PaddedLength);
				File->Serialize(DeserializedBytes.GetData(), PaddedLength);

				FAES::DecryptData(DeserializedBytes.GetData(), DeserializedBytes.Num(), *Key);

				LoginString = BytesToString(DeserializedBytes.GetData(), LengthWithoutPadding);

				if(!GetUsername().IsEmpty() && !GetPassword().IsEmpty())
				{
					UE_LOG(LogSubmitTool, Log, TEXT("Local Credentials loaded"));
				}

				File->Close();
				delete File;
			}
			else
			{
				UE_LOG(LogSubmitTool, Warning, TEXT("Could not read file '%s'."), *GetCredentialsFilepath());
			}
		}
		else
		{
			UE_LOG(LogSubmitToolDebug, Warning, TEXT("File %s does not exists, no credentials were loaded"), *GetCredentialsFilepath())
		}
	}
}

FString FCredentialsService::GetUsername() const
{
	FString DecodedString;
	if(!FBase64::Decode(LoginString, DecodedString))
	{
		UE_LOG(LogSubmitToolDebug, Error, TEXT("Error while trying to decode Jira Login"));
	}

	TArray<FString> LoginValues;
	DecodedString.ParseIntoArray(LoginValues, TEXT(":"));

	if(LoginValues.Num() == 2)
	{
		return LoginValues[0];
	}

	return FString();
}

FString FCredentialsService::GetPassword() const
{
	FString DecodedString;
	if(!FBase64::Decode(LoginString, DecodedString))
	{
		UE_LOG(LogSubmitToolDebug, Error, TEXT("Error while trying to decode Jira Password"));
	}

	TArray<FString> LoginValues;
	DecodedString.ParseIntoArray(LoginValues, TEXT(":"));

	if(LoginValues.Num() == 2)
	{
		return LoginValues[1];
	}

	return FString();
}

void FCredentialsService::SetLogin(const FString& InUsername, const FString& InPassword)
{
	int32 ChopLocation = InUsername.Find(TEXT("@"));
	FString FormattedUsername = InUsername;

	// Just grab the username if they accidentally entered their full email.
	if(ChopLocation != -1)
	{
		FormattedUsername = InUsername.LeftChop(InUsername.Len() - ChopLocation);
	}

	FString newLogin = FBase64::Encode(FormattedUsername + TEXT(":") + InPassword);
	if(newLogin != LoginString)
	{
		LoginString = newLogin;
		this->SaveCredentials();
	}
}

bool FCredentialsService::Tick(float DeltaTime)
{
	if(TokenExpiration != FDateTime() && TokenExpiration < FDateTime::UtcNow())
	{
		GetOIDCToken();
	}

	return true;
}
