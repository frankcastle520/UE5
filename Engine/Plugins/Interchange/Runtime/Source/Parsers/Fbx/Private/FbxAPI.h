// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FbxInclude.h"
#include "HAL/CriticalSection.h"
#include "InterchangeResultsContainer.h"


#define FBX_METADATA_PREFIX TEXT("FBX.")
#define INVALID_UNIQUE_ID 0xFFFFFFFFFFFFFFFF

namespace UE
{
	namespace Interchange
	{
		struct FAnimationPayloadQuery;
		struct FAnimationPayloadData;
		namespace Private
		{
			class FPayloadContextBase;
			struct FFbxHelper;
		}
#if WITH_ENGINE
		struct FMeshPayloadData;
#endif
	}
}
class UInterchangeBaseNodeContainer;

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			class FFbxParser
			{
			public:
				explicit FFbxParser(TWeakObjectPtr<UInterchangeResultsContainer> InResultsContainer)
					: ResultsContainer(InResultsContainer)
				{}

				~FFbxParser();

				void Reset();

				void SetResultContainer(UInterchangeResultsContainer* Result)
				{
					ResultsContainer = Result;
				}

				void SetConvertSettings(const bool InbConvertScene, const bool InbForceFrontXAxis, const bool InbConvertSceneUnit, const bool InbKeepFbxNamespace)
				{
					bConvertScene = InbConvertScene;
					bForceFrontXAxis = InbForceFrontXAxis;
					bConvertSceneUnit = InbConvertSceneUnit;
					bKeepFbxNamespace = InbKeepFbxNamespace;
				}

				//return the fbx helper for this parser
				const TSharedPtr<FFbxHelper> GetFbxHelper();

				/* Load an fbx file into the fbx sdk, return false if the file could not be load. */
				bool LoadFbxFile(const FString& Filename, UInterchangeBaseNodeContainer& NodeContainer);

				/* Extract the fbx data from the sdk into our node container */
				void FillContainerWithFbxScene(UInterchangeBaseNodeContainer& NodeContainer);

				/* Extract the fbx data from the sdk into our node container */
				bool FetchPayloadData(const FString& PayloadKey, const FString& PayloadFilepath);

				/* Extract the fbx mesh data from the sdk into our node container */
				bool FetchMeshPayloadData(const FString& PayloadKey, const FTransform& MeshGlobalTransform, const FString& PayloadFilepath);
#if WITH_ENGINE
				bool FetchMeshPayloadData(const FString& PayloadKey, const FTransform& MeshGlobalTransform, FMeshPayloadData& OutMeshPayloadData);
#endif

				/* Extract the fbx data from the sdk into our node container
				* @Param PayloadQueries - Will be grouped based on their TimeDescription Hashes (so that we acquire the same timings in one iteration, avoiding cache rebuilds)
				*/
				bool FetchAnimationBakeTransformPayload(const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries, const FString& ResultFolder, FCriticalSection* ResultPayloadsCriticalSection, TAtomic<int64>& UniqueIdCounter, TMap<FString, FString>& ResultPayloads/*PayloadUniqueID to FilePath*/);
				
				/**
				 * This function is used to add the given message object directly into the results for this operation.
				 */
				template <typename T>
				T* AddMessage() const
				{
					check(ResultsContainer.IsValid());
					T* Item = ResultsContainer->Add<T>();
					Item->SourceAssetName = SourceFilename;
					return Item;
				}


				void AddMessage(UInterchangeResult* Item) const
				{
					check(ResultsContainer.IsValid());
					ResultsContainer->Add(Item);
					Item->SourceAssetName = SourceFilename;
				}

				FbxScene* GetSDKScene() { return SDKScene; }

				double GetFrameRate() { return FrameRate; }

				bool IsCreatorBlender() { return bCreatorIsBlender; }
				/**
				 * Critical section to avoid getting multiple payload in same time.
				 * The FBX evaluator use a cache mechanism for evaluating global transform that is not thread safe.
				 * There si other stuff in the sdk which are not thread safe, so all fbx payload should be fetch one by one
				 */
				FCriticalSection PayloadCriticalSection;
			private:

				void EnsureNodeNameAreValid(const FString& BaseFilename);
				void CleanupFbxData();
				void ProcessExtraInformation(UInterchangeBaseNodeContainer& NodeContainer);

				TWeakObjectPtr<UInterchangeResultsContainer> ResultsContainer;
				FbxManager* SDKManager = nullptr;
				FbxScene* SDKScene = nullptr;
				FbxImporter* SDKImporter = nullptr;
				FbxGeometryConverter* SDKGeometryConverter = nullptr;
				FbxIOSettings* SDKIoSettings = nullptr;
				FString SourceFilename;
				TMap<FString, TSharedPtr<FPayloadContextBase>> PayloadContexts;
				TSharedPtr<FFbxHelper> FbxHelper;

				//For PivotReset and Animation Conversion:
				double FrameRate = 30.0;

				//Convert settings
				bool bConvertScene = true;
				bool bForceFrontXAxis = false;
				bool bConvertSceneUnit = true;
				bool bKeepFbxNamespace = false;
				bool bCreatorIsBlender = false;

				struct FileDetails
				{
					FString FbxFileVersion;
					FString FbxFileCreator;
					FString FbxFileCreatorApplication;
					FString UnitSystem;
					FString AxisDirection;
					FString FrameRate;
				} FileDetails;
				
			};
		}//ns Private
	}//ns Interchange
}//ns UE
