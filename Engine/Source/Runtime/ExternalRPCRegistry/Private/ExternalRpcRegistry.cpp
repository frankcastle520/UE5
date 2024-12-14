// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalRpcRegistry.h"
#include "HttpServerModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonWriter.h"
#include "Logging/LogMacros.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ExternalRpcRegistry)

#define USE_RPC_REGISTRY_IN_SHIPPING 0

#ifndef WITH_RPC_REGISTRY
#define WITH_RPC_REGISTRY (USE_RPC_REGISTRY_IN_SHIPPING || !UE_BUILD_SHIPPING )
#endif

DEFINE_LOG_CATEGORY(LogExternalRpcRegistry)

UExternalRpcRegistry* UExternalRpcRegistry::ObjectInstance = nullptr;


FString GetHttpRouteVerbString(EHttpServerRequestVerbs InVerbs)
{
#if WITH_RPC_REGISTRY
	switch (InVerbs)
	{
		case EHttpServerRequestVerbs::VERB_POST:
		{
			return TEXT("POST");
		}
		case EHttpServerRequestVerbs::VERB_PUT:
		{
			return TEXT("PUT");
		}
		case EHttpServerRequestVerbs::VERB_GET:
		{
			return TEXT("GET");
		}
		case EHttpServerRequestVerbs::VERB_PATCH:
		{
			return TEXT("PATCH");
		}
		case EHttpServerRequestVerbs::VERB_DELETE:
		{
			return TEXT("DELETE");
		}
		case EHttpServerRequestVerbs::VERB_NONE:
		{
			return TEXT("NONE");
		}
	}
#endif
	return TEXT("UNKNOWN");
}

bool UExternalRpcRegistry::IsActiveRpcCategory(FString InCategory)
{
#if WITH_RPC_REGISTRY
	if (!ActiveRpcCategories.Num() || ActiveRpcCategories.Contains(InCategory))
	{
		return true;
	}
#endif
	return false;
}

UExternalRpcRegistry::~UExternalRpcRegistry()
{
	CleanUpAllRoutes();
}

bool UExternalRpcRegistry::IsEnabled()
{
#if WITH_RPC_REGISTRY
	int32 RpcPort = 0;
	// Not just returning this if because it'll cause static analysis issues for unreachable code in non-shipping
	if (FParse::Value(FCommandLine::Get(), TEXT("rpcport="), RpcPort))
	{
		return true;
	}
#endif
	return false;
}

UExternalRpcRegistry* UExternalRpcRegistry::GetInstance()
{
#if WITH_RPC_REGISTRY
	if (ObjectInstance == nullptr)
	{		
		ObjectInstance = NewObject<UExternalRpcRegistry>();
		FString InCommandLineValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("enabledrpccategories="), InCommandLineValue))
		{
			TArray<FString> Substrings;
			InCommandLineValue.ParseIntoArray(Substrings, TEXT(","));
			for (int i = 0; i < Substrings.Num(); i++)
			{
				if (!ObjectInstance->ActiveRpcCategories.Contains(Substrings[i]))
				{
					ObjectInstance->ActiveRpcCategories.Add(Substrings[i]);
				}
			}
		}
		FParse::Value(FCommandLine::Get(), TEXT("rpcport="), ObjectInstance->PortToUse);
		FParse::Value(FCommandLine::Get(), TEXT("rpcledgersize="), ObjectInstance->RequestLedgerCapacity);

		// We always want the ListRegisteredRpcs route bound, no matter what.

		FHttpRequestHandler ListRoutesRequestHandler = FHttpRequestHandler::CreateUObject(ObjectInstance, &ThisClass::HttpListOpenRoutes);
		ObjectInstance->RegisterNewRoute(TEXT("ListRegisteredRpcs"), FHttpPath("/listrpcs"), EHttpServerRequestVerbs::VERB_GET,
			ListRoutesRequestHandler, true, true);

		FHttpRequestHandler PrintLedgerRequestHandler = FHttpRequestHandler::CreateUObject(ObjectInstance, &ThisClass::HttpPrintRequestLedger);
		// We always want the ListRegisteredRpcs route bound, no matter what.
		ObjectInstance->RegisterNewRoute(TEXT("GetRequestHistory"), FHttpPath("/requesthistory"), EHttpServerRequestVerbs::VERB_GET,
			PrintLedgerRequestHandler, true, true);

		FHttpRequestHandler ListOASv3RequestHandler = FHttpRequestHandler::CreateUObject(ObjectInstance, &ThisClass::HttpListOASv3JSONRoutes);
		// /swagger.json escaped as %2e
		ObjectInstance->RegisterNewRoute(TEXT("ListSwaggerJson"), FHttpPath("/swagger.json"), EHttpServerRequestVerbs::VERB_GET,
			ListOASv3RequestHandler, true, true);

		ObjectInstance->AddToRoot();
	}
#endif
	return ObjectInstance;
}

bool UExternalRpcRegistry::GetRegisteredRoute(FName RouteName, FExternalRouteInfo& OutRouteInfo)
{
#if WITH_RPC_REGISTRY
	if (RegisteredRoutes.Find(RouteName))
	{
		OutRouteInfo.RouteName = RouteName;
		OutRouteInfo.RoutePath = RegisteredRoutes[RouteName].Handle->Path;
		OutRouteInfo.RequestVerbs = RegisteredRoutes[RouteName].Handle->Verbs;
		OutRouteInfo.InputContentType = RegisteredRoutes[RouteName].InputContentType;
		OutRouteInfo.ExpectedArguments = RegisteredRoutes[RouteName].ExpectedArguments;
		return true;
	}
#endif
	return false;
}

void UExternalRpcRegistry::RegisterNewRouteWithArguments(FName RouteName, const FHttpPath& HttpPath, const EHttpServerRequestVerbs& RequestVerbs, const FHttpRequestHandler& Handler, TArray<FExternalRpcArgumentDesc> InArguments, bool bOverrideIfBound /* = false */, bool bIsAlwaysOn /* = false */, FString OptionalCategory /* = FString("Unknown") */, FString OptionalContentType /* = TEXT("")*/)
{
#if WITH_RPC_REGISTRY
	FExternalRouteInfo InRouteInfo;
	InRouteInfo.RouteName = RouteName;
	InRouteInfo.RoutePath = HttpPath;
	InRouteInfo.RequestVerbs = RequestVerbs;
	InRouteInfo.InputContentType = OptionalContentType;
	InRouteInfo.ExpectedArguments = MoveTemp(InArguments);
	InRouteInfo.RpcCategory = OptionalCategory;
	InRouteInfo.bAlwaysOn = bIsAlwaysOn;
	RegisterNewRoute(MoveTemp(InRouteInfo), Handler, bOverrideIfBound);
#endif
}


void UExternalRpcRegistry::RegisterNewRoute(FName RouteName, const FHttpPath& HttpPath, const EHttpServerRequestVerbs& RequestVerbs, const FHttpRequestHandler& Handler, bool bOverrideIfBound /* = false */, bool bIsAlwaysOn /* = false */, FString OptionalCategory /* = FString("Unknown") */, FString OptionalContentType /* = TEXT("")*/, FString OptionalExpectedFormat /*= TEXT("")*/)
{
#if WITH_RPC_REGISTRY
	FExternalRouteInfo InRouteInfo;
	InRouteInfo.RouteName = RouteName;
	InRouteInfo.RoutePath = HttpPath;
	InRouteInfo.RequestVerbs = RequestVerbs;
	InRouteInfo.InputContentType = OptionalContentType;
	InRouteInfo.RpcCategory = OptionalCategory;
	InRouteInfo.bAlwaysOn = bIsAlwaysOn;
	RegisterNewRoute(MoveTemp(InRouteInfo), Handler, bOverrideIfBound);
#endif
}

void UExternalRpcRegistry::RegisterNewRoute(FExternalRouteInfo InRouteInfo, const FHttpRequestHandler& Handler, bool bOverrideIfBound /* = false */)
{
#if WITH_RPC_REGISTRY
	if (!InRouteInfo.bAlwaysOn && !IsActiveRpcCategory(InRouteInfo.RpcCategory))
	{
		return;
	}

	TSharedPtr<IHttpRouter> HttpRouter = FHttpServerModule::Get().GetHttpRouter(PortToUse);

	if (RegisteredRoutes.Find(InRouteInfo.RouteName))
	{
		if (!bOverrideIfBound)
		{
			UE_LOG(LogExternalRpcRegistry, Error, TEXT("Failed to bind route with friendly key %s - a route at location %s already exists."), *InRouteInfo.RouteName.ToString(), *InRouteInfo.RoutePath.GetPath());
			return;
		}
		UE_LOG(LogExternalRpcRegistry, Log, TEXT("Overwriting route at friendly key %s - from %s to %s "), *InRouteInfo.RouteName.ToString(), *RegisteredRoutes[InRouteInfo.RouteName].Handle->Path, *InRouteInfo.RoutePath.GetPath());
		HttpRouter->UnbindRoute(RegisteredRoutes[InRouteInfo.RouteName].Handle);
	}
	FExternalRouteDesc RouteDesc;
	RouteDesc.Handle = HttpRouter->BindRoute(InRouteInfo.RoutePath, InRouteInfo.RequestVerbs, Handler);
	RouteDesc.InputContentType = InRouteInfo.InputContentType;
	RouteDesc.ExpectedArguments = InRouteInfo.ExpectedArguments;
	RouteDesc.RpcCategory = InRouteInfo.RpcCategory;
	RegisteredRoutes.Add(InRouteInfo.RouteName, RouteDesc);
#endif
}

void UExternalRpcRegistry::CleanUpAllRoutes()
{
#if WITH_RPC_REGISTRY
	TArray<FName> OutRouteKeys;
	RegisteredRoutes.GetKeys(OutRouteKeys);
	for (const FName& RouteKey : OutRouteKeys)
	{
		CleanUpRoute(RouteKey);
	}
#endif
}

void UExternalRpcRegistry::CleanUpRoute(FName RouteName, bool bFailIfUnbound /* = false */)
{
#if WITH_RPC_REGISTRY
	if (RegisteredRoutes.Find(RouteName))
	{
		TSharedPtr<IHttpRouter> HttpRouter = FHttpServerModule::Get().GetHttpRouter(PortToUse);
		HttpRouter->UnbindRoute(RegisteredRoutes[RouteName].Handle);
		RegisteredRoutes.Remove(RouteName);
		UE_LOG(LogExternalRpcRegistry, Log, TEXT("Route name %s was unbound!"), *RouteName.ToString());

	}
	else
	{
		UE_LOG(LogExternalRpcRegistry, Warning, TEXT("Route name %s does not exist, could not unbind."), *RouteName.ToString());
		check(!bFailIfUnbound);
	}

#endif
}

bool UExternalRpcRegistry::HttpListOpenRoutes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
#if WITH_RPC_REGISTRY
	FString ResponseStr;
	TArray<FName> OutRouteKeys;
	RegisteredRoutes.GetKeys(OutRouteKeys);
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ResponseStr);
	JsonWriter->WriteArrayStart();
	for (const FName& RouteKey : OutRouteKeys)
	{
		JsonWriter->WriteObjectStart();
		JsonWriter->WriteValue(TEXT("name"), RouteKey.ToString());
		JsonWriter->WriteValue(TEXT("route"), RegisteredRoutes[RouteKey].Handle->Path);
		JsonWriter->WriteValue(TEXT("verb"), GetHttpRouteVerbString(RegisteredRoutes[RouteKey].Handle->Verbs));
		if (!RegisteredRoutes[RouteKey].InputContentType.IsEmpty())
		{
			JsonWriter->WriteValue(TEXT("inputContentType"), RegisteredRoutes[RouteKey].InputContentType);
		}
		if (!RegisteredRoutes[RouteKey].ExpectedArguments.IsEmpty())
		{
			JsonWriter->WriteArrayStart(TEXT("args"));
			for (const FExternalRpcArgumentDesc& ArgDesc : RegisteredRoutes[RouteKey].ExpectedArguments)
			{
				JsonWriter->WriteObjectStart();
				JsonWriter->WriteValue(TEXT("name"), ArgDesc.Name);
				JsonWriter->WriteValue(TEXT("type"), ArgDesc.Type);
				JsonWriter->WriteValue(TEXT("desc"), ArgDesc.Desc);
				JsonWriter->WriteValue(TEXT("optional"), ArgDesc.bIsOptional);
				JsonWriter->WriteObjectEnd();
			}
			JsonWriter->WriteArrayEnd();
		}
		JsonWriter->WriteObjectEnd();
	}
	JsonWriter->WriteArrayEnd();
	JsonWriter->Close();
	auto Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	OnComplete(MoveTemp(Response));
#endif
	return true;
}
void UExternalRpcRegistry::AddRequestToLedger(const FHttpServerRequest& Request)
{
#if WITH_RPC_REGISTRY
	if (Request.Headers.Find(TEXT("rpcname")))
	{
		FRpcLedgerEntry NewEntry;
		NewEntry.RpcName = Request.Headers[TEXT("rpcname")][0];
		FUTF8ToTCHAR WByteBuffer(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		NewEntry.RequestBody = FString::ConstructFromPtrSize(WByteBuffer.Get(), WByteBuffer.Length());
		NewEntry.RequestTime = FDateTime::UtcNow();
		RequestLedger.Add(NewEntry);
	}
	// Reduce ledger to proper max size.
	while (RequestLedger.Num() > RequestLedgerCapacity)
	{
		RequestLedger.RemoveAt(0);
	}
#endif
}

bool UExternalRpcRegistry::HttpPrintRequestLedger(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
#if WITH_RPC_REGISTRY
	FString ResponseStr;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ResponseStr);
	JsonWriter->WriteArrayStart();
	for (const FRpcLedgerEntry& LoggedRequest : RequestLedger)
	{
		JsonWriter->WriteObjectStart();
		JsonWriter->WriteValue(TEXT("rpcname"), LoggedRequest.RpcName);
		JsonWriter->WriteValue(TEXT("requesttimestamp"), LoggedRequest.RequestTime.ToString());
		JsonWriter->WriteValue(TEXT("requestbody"), LoggedRequest.RequestBody);

		JsonWriter->WriteObjectEnd();
	}
	JsonWriter->WriteArrayEnd();
	JsonWriter->Close();
	auto Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	OnComplete(MoveTemp(Response));
#endif
	return true;
}

bool UExternalRpcRegistry::HttpListOASv3JSONRoutes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
#if WITH_RPC_REGISTRY
	FString ResponseStr;
	TArray<FName> OutRouteKeys;
	RegisteredRoutes.GetKeys(OutRouteKeys);
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ResponseStr);


	JsonWriter->WriteObjectStart();

	// Based on OpenApi Spec v3.0.0, update this string
	// as necessary.
	JsonWriter->WriteValue(TEXT("openapi"), TEXT("3.0.0"));

	JsonWriter->WriteObjectStart(TEXT("info"));
	JsonWriter->WriteValue(TEXT("title"), FString::Printf(TEXT("UE-%s - RPC API"), FApp::GetProjectName()));
	JsonWriter->WriteValue(TEXT("description"), TEXT("Auto-generated Swagger API"));
	JsonWriter->WriteValue(TEXT("version"), FApp::GetBuildVersion());
	JsonWriter->WriteObjectEnd();

	JsonWriter->WriteArrayStart(TEXT("servers"));
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(TEXT("url"), FString::Printf(TEXT("http://%s:%d"), TEXT("127.0.0.1"), PortToUse));
	JsonWriter->WriteValue(TEXT("description"), TEXT("Default server access via localhost"));
	JsonWriter->WriteObjectEnd();

	/* TODO: Add external IP support. This section untested for now.
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (SocketSubsystem)
	{
		TArray<TSharedRef<FInternetAddr>> Addresses = SocketSubsystem->GetLocalBindAddresses();
		for (TSharedRef<FInternetAddr> InternetAddr : Addresses)
		{
			JsonWriter->WriteObjectStart();
			JsonWriter->WriteValue(TEXT("url"), FString::Printf(TEXT("http://%s:%d"), InternetAddr->ToString(false), PortToUse));
			JsonWriter->WriteValue(TEXT("description"), TEXT("Default server access via external ip"));
			JsonWriter->WriteObjectEnd();
		}
	}
	*/

	JsonWriter->WriteArrayEnd();

	JsonWriter->WriteObjectStart(TEXT("paths"));
	for (const FName& RouteKey : OutRouteKeys)
	{
		JsonWriter->WriteObjectStart(RegisteredRoutes[RouteKey].Handle->Path);
		JsonWriter->WriteObjectStart(GetHttpRouteVerbString(RegisteredRoutes[RouteKey].Handle->Verbs).ToLower());
		JsonWriter->WriteValue(TEXT("summary"), RouteKey.ToString());
		JsonWriter->WriteValue(TEXT("operationId"), RouteKey.ToString());

		if (!RegisteredRoutes[RouteKey].RpcCategory.IsEmpty())
		{
			JsonWriter->WriteArrayStart(TEXT("tags"));
			JsonWriter->WriteValue(RegisteredRoutes[RouteKey].RpcCategory);
			JsonWriter->WriteArrayEnd();
		}

		// TODO: Ask C++ implementers to provide a description of their RPC call should do.
		// We'll dump the InputContentType for now.
		if (!RegisteredRoutes[RouteKey].InputContentType.IsEmpty())
		{
			JsonWriter->WriteValue(TEXT("description"), RegisteredRoutes[RouteKey].InputContentType);
		}
		else
		{
			JsonWriter->WriteValue(TEXT("description"), TEXT("No content type required to call this."));
		}

		if (!RegisteredRoutes[RouteKey].ExpectedArguments.IsEmpty())
		{
			JsonWriter->WriteObjectStart(TEXT("requestBody"));
			JsonWriter->WriteObjectStart(TEXT("content"));
			JsonWriter->WriteObjectStart(TEXT("application/json"));

			JsonWriter->WriteObjectStart(TEXT("schema"));
			JsonWriter->WriteValue(TEXT("type"), TEXT("object"));

			JsonWriter->WriteObjectStart(TEXT("properties"));
			TArray<FString> RequiredObjects;
			for (const FExternalRpcArgumentDesc& ArgDesc : RegisteredRoutes[RouteKey].ExpectedArguments)
			{
				JsonWriter->WriteObjectStart(ArgDesc.Name);

				//TODO: Provide better typing in RPC framework so we can auto-gen some values here.
				JsonWriter->WriteValue(TEXT("description"), ArgDesc.Desc);
				//JsonWriter->WriteValue(TEXT("type"), ArgDesc.Type);
				JsonWriter->WriteValue(TEXT("type"), TEXT("string"));

				if (!ArgDesc.bIsOptional)
				{
					RequiredObjects.Push(ArgDesc.Name);
				}

				JsonWriter->WriteObjectEnd();
			}
			JsonWriter->WriteObjectEnd();

			if (RequiredObjects.Num() > 0)
			{
				JsonWriter->WriteArrayStart("required");
				for (FString RequiredName : RequiredObjects)
				{
					JsonWriter->WriteValue(RequiredName);
				}
				JsonWriter->WriteArrayEnd();
			}
			JsonWriter->WriteObjectEnd();

			JsonWriter->WriteObjectEnd();
			JsonWriter->WriteObjectEnd();
			JsonWriter->WriteObjectEnd();
		}

		JsonWriter->WriteObjectStart(TEXT("responses"));
		JsonWriter->WriteObjectStart(TEXT("200"));
		JsonWriter->WriteValue(TEXT("description"), TEXT("Successful return."));
		JsonWriter->WriteObjectEnd();
		JsonWriter->WriteObjectEnd();

		JsonWriter->WriteObjectEnd();
		JsonWriter->WriteObjectEnd();
	}
	JsonWriter->WriteObjectEnd();
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();
	auto Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	OnComplete(MoveTemp(Response));
#endif
	return true;
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ExternalRpcRegistry);

