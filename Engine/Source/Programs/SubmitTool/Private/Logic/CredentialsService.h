// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Misc/AES.h"
#include "Parameters/SubmitToolParameters.h"
#include "Tasks/Task.h"
#include "Services/Interfaces/ISubmitToolService.h"

class FCredentialsService : public ISubmitToolService
{
public:
	FCredentialsService(const FOAuthTokenParams& InOAuthParameters);
	~FCredentialsService() = default;

	bool HasCredentials() const
	{
		return !LoginString.IsEmpty();
	}

	bool AreCredentialsValid() const
	{
		return bValidatedCredentials;
	}

	void SetCredentialsValid(bool bValid)
	{
		bValidatedCredentials = bValid;
	}

	const FString& GetEncodedLoginString() const
	{
		return LoginString;
	}

	FString GetUsername() const;
	void SetLogin(const FString& InUsername, const FString& InPassword);

	bool IsOIDCTokenEnabled()
	{
		return !Parameters.OAuthTokenTool.IsEmpty();
	}

	bool IsTokenReady() const
	{
		return !OIDCToken.IsEmpty();
	}

	const FString& GetToken() const
	{
		return OIDCToken;
	}

	static const TUniquePtr<FAES::FAESKey>& GetEncryptionKey()
	{
		if(!Key.IsValid())
		{
			LoadKey();
		}

		return Key;
	}

	UE::Tasks::TTask<void> QueueWorkForToken(TFunction<void(const FString&)> InFunction)
	{
		return UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, InFunction]() {
			InFunction(OIDCToken);
			}, GetOIDCTask);
	}

private:
	static TUniquePtr<FAES::FAESKey> Key;
	static void LoadKey();
	static void GenerateKey();
	static const FString GetKeyFilepath();

	UE::Tasks::TTask<bool> GetOIDCTask;
	FString OIDCToken;
	FDateTime TokenExpiration;
	const FOAuthTokenParams Parameters;

	void GetOIDCToken();
	bool ParseOIDCTokenData(const FString& InToken);
	FString GetPassword() const;
	void SaveCredentials() const;
	void LoadCredentials();

	bool Tick(float DeltaTime);

	const FString GetCredentialsFilepath() const;

	FString LoginString;
	bool bValidatedCredentials = true;
};

Expose_TNameOf(FCredentialsService);