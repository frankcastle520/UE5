// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaHordeMetaClient.h"
#include "Horde.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY(LogUbaHorde);

bool FUbaHordeMetaClient::RefreshHttpClient()
{
	FString ServerUrlConfigSource;
	if (FHorde::GetServerUrl(ServerUrl, &ServerUrlConfigSource))
	{
		UE_LOG(LogUbaHorde, Display, TEXT("Getting Horde server URL succeeded [URL: %s, Source: %s]"), *ServerUrl, *ServerUrlConfigSource);
	}
	else
	{
		UE_LOG(LogUbaHorde, Warning, TEXT("Getting Horde server URL failed [Source: %s]"), *ServerUrlConfigSource);
		return false;
	}

	// Try to connect to Horde with HTTP and v2 API
	HttpClient = MakeUnique<FHordeHttpClient>(ServerUrl);

	if (!HttpClient->Login(FApp::IsUnattended()))
	{
		UE_LOG(LogUbaHorde, Warning, TEXT("Login to Horde server [URL: %s, Source: %s] failed"), *ServerUrl, *ServerUrlConfigSource);
		return false;
	}

	return true;
}

TSharedPtr<FUbaHordeMetaClient::HordeMachinePromise, ESPMode::ThreadSafe> FUbaHordeMetaClient::RequestMachine(const FString& PoolId, const FString& Machine)
{
	TSharedPtr<HordeMachinePromise> Promise = MakeShared<HordeMachinePromise, ESPMode::ThreadSafe>();

	const FString ResourcePath = FString::Format(TEXT("api/v2/compute/{0}"), { Machine });

	FHttpRequestRef Request = HttpClient->CreateRequest(TEXT("POST"), *ResourcePath);

	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	// Post JSON document with constraints to acquire a Horde agent. Use pool of agents (e.g. "BoxLinux" or "BoxWin")
	// and require exclusive access or UbaStorage will fail to initialize the next time we connect to the same machine.
	const FString Body = FString::Format(TEXT("{\"requirements\":{\"pool\":\"{0}\",\"exclusive\":true}}"), { PoolId });
	Request->SetContentAsString(Body);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	UE_LOG(LogUbaHorde, Log, TEXT("Requesting Horde agent with JSON descriptor: '%s'"), *Body);

	Request->OnProcessRequestComplete().BindLambda(
		[this, Promise](FHttpRequestPtr /*Request*/, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully)
		{
			FHordeRemoteMachineInfo Info;
			Info.Ip = TEXT("");
			Info.Port = 0xFFFF;
			Info.bRunsWindowOS = false;
			FMemory::Memset(Info.Nonce, 0, sizeof(Info.Nonce));

			if (!bConnectedSuccessfully || !HttpResponse.IsValid())
			{
				UE_LOG(LogUbaHorde, Verbose, TEXT("No response from Horde"));
				Promise->SetValue(MakeTuple(HttpResponse, Info));
				return;
			}

			FString ResponseStr = HttpResponse->GetContentAsString();

			if (HttpResponse->GetResponseCode() == 503) // HTTP 503 Service Unavailable
			{
				UE_LOG(LogUbaHorde, Verbose, TEXT("No resources available in Horde (%s)"), *ResponseStr);
				Promise->SetValue(MakeTuple(HttpResponse, Info));
				return;
			}

			TSharedPtr<FJsonValue> OutJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);

			if (!FJsonSerializer::Deserialize(Reader, OutJson, FJsonSerializer::EFlags::None))
			{
				// Report invalid response body with Display verbosity only, since this should not fail a CIS job
				UE_LOG(LogUbaHorde, Display, TEXT("Invalid response body: %s"), *ResponseStr);
				Promise->SetValue(MakeTuple(HttpResponse, Info));
				return;
			}

			TSharedPtr<FJsonValue> NonceValue = OutJson->AsObject()->TryGetField(TEXT("nonce"));
			TSharedPtr<FJsonValue> IpValue = OutJson->AsObject()->TryGetField(TEXT("ip"));
			TSharedPtr<FJsonValue> PortValue = OutJson->AsObject()->TryGetField(TEXT("port"));

			if (!NonceValue.Get() || !IpValue.Get() || !PortValue.Get())
			{
				// Report invalid response body with Display verbosity only, since this should not fail a CIS job
				UE_LOG(LogUbaHorde, Display, TEXT("Invalid response body: %s"), *ResponseStr);
				Promise->SetValue(MakeTuple(HttpResponse, Info));
				return;
			}

			FString OsFamily(TEXT("UNKNOWN-OS"));

			if (TSharedPtr<FJsonValue> PropertiesValue = OutJson->AsObject()->TryGetField(TEXT("properties")))
			{
				for (const TSharedPtr<FJsonValue>& PropertyEntryValue : PropertiesValue->AsArray())
				{
					checkf(PropertyEntryValue.Get(), TEXT("null pointer in JSON array object of node \"properties\""));
					const FString PropertyElementString = PropertyEntryValue->AsString();
					if (PropertyElementString.StartsWith(TEXT("OSFamily=")))
					{
						OsFamily = *PropertyElementString + 9;
						if (OsFamily == TEXT("Windows"))
							Info.bRunsWindowOS = true;
					}
					if (PropertyElementString.StartsWith(TEXT("LogicalCores=")))
					{
						Info.LogicalCores = (uint16)FCString::Atoi(*PropertyElementString + 13);
					}
				}
			}

			FString NonceString = NonceValue->AsString();
			FString IpString = IpValue->AsString();
			uint16 PortNumber = (uint16)PortValue->AsNumber();

			if (TSharedPtr<FJsonValue> LeaseIdValue = OutJson->AsObject()->TryGetField(TEXT("leaseId")))
			{
				const FString AgentWebPortalUrl = FString::Format(TEXT("{0}lease/{1}"), { this->ServerUrl, LeaseIdValue->AsString() });
				UE_LOG(
					LogUbaHorde, Display, TEXT("UBA Horde machine assigned (%s) [%s:%u]: %s"), *OsFamily, *IpString, (uint32)PortNumber, *AgentWebPortalUrl
				);
			}
			else
			{
				UE_LOG(LogUbaHorde, Display, TEXT("UBA Horde machine assigned [%s:%u]"), *IpString, (uint32)PortNumber);
			}

			Info.Ip = IpString;
			Info.Port = PortNumber;
			FString::ToHexBlob(NonceString, Info.Nonce, HORDE_NONCE_SIZE);

			Promise->SetValue(MakeTuple(HttpResponse, Info));
		});

	Request->ProcessRequest();

	return Promise;
}
