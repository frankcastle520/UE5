// Copyright Epic Games, Inc. All Rights Reserved. 

#include "MaterialX/InterchangeMaterialXTranslator.h"
#include "InterchangeManager.h"
#include "MaterialX/MaterialXUtils/MaterialXManager.h"
#include "Nodes/InterchangeSourceNode.h"
#include "UObject/GCObjectScopeGuard.h"

#if WITH_EDITOR
#include "MaterialXFormat/Util.h"
#include "MaterialX/InterchangeMaterialXDefinitions.h"
#include "MaterialX/MaterialXUtils/MaterialXBase.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeMaterialXTranslator)

#define LOCTEXT_NAMESPACE "InterchangeMaterialXTranslator"

static bool GInterchangeEnableMaterialXImport = true;
static FAutoConsoleVariableRef CCvarInterchangeEnableMaterialXImport(
	TEXT("Interchange.FeatureFlags.Import.MTLX"),
	GInterchangeEnableMaterialXImport,
	TEXT("Whether MaterialX support is enabled."),
	ECVF_Default);

EInterchangeTranslatorType UInterchangeMaterialXTranslator::GetTranslatorType() const
{
	return EInterchangeTranslatorType::Assets;
}

EInterchangeTranslatorAssetType UInterchangeMaterialXTranslator::GetSupportedAssetTypes() const
{
	return EInterchangeTranslatorAssetType::Materials;
}

TArray<FString> UInterchangeMaterialXTranslator::GetSupportedFormats() const
{
	// Call to UInterchangeMaterialXTranslator::GetSupportedFormats is not supported out of game thread
	// A more global solution must be found for translators which require some initialization
	if(!IsInGameThread() || (!GInterchangeEnableMaterialXImport && !GIsAutomationTesting))
	{
		return TArray<FString>{};
	}

	return UE::Interchange::MaterialX::AreMaterialFunctionPackagesLoaded() ? TArray<FString>{ TEXT("mtlx;MaterialX File Format") } : TArray<FString>{};
}

namespace
{
#if WITH_EDITOR
	bool ValidateDocument(MaterialX::DocumentPtr Document, const UInterchangeTranslatorBase* Translator)
	{
		namespace mx = MaterialX;
		
		if(std::string MaterialXMessage; !Document->validate(&MaterialXMessage))
		{
			UInterchangeResultError_Generic* Message = Translator->AddMessage<UInterchangeResultError_Generic>();
			Message->Text = FText::Format(LOCTEXT("MaterialXDocumentInvalid", "{0}"),
										  FText::FromString(MaterialXMessage.c_str()));
			return false;
		}

		for(mx::ElementPtr Elem : Document->traverseTree())
		{
			//make sure to read only the current file otherwise we'll process the entire library
			if(Elem->getActiveSourceUri() != Document->getActiveSourceUri())
			{
				continue;
			}

			mx::NodePtr Node = Elem->asA<mx::Node>();

			if(Node)
			{
				// Validate that all nodes in the file are strictly respecting their node definition
				if(!Node->getNodeDef())
				{
					UInterchangeResultError_Generic* Message = Translator->AddMessage<UInterchangeResultError_Generic>();
					Message->Text = FText::Format(LOCTEXT("NodeDefNotFound", "<{0}> has no matching NodeDef, aborting import..."),
												  FText::FromString(Node->getName().c_str()));
					return false;
				}
				
				if(!Node->getTypeDef())
				{
					UInterchangeResultError_Generic* Message = Translator->AddMessage<UInterchangeResultError_Generic>();
					Message->Text = FText::Format(LOCTEXT("TypeDefNotFound", "<{0}> has no matching TypeDef, aborting import..."),
												  FText::FromString(Node->getName().c_str()));
					return false;
				}
			}
		}

		return true;
	}
#endif // WITH_EDITOR
}

bool UInterchangeMaterialXTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	bool bIsDocumentValid = false;

#if WITH_EDITOR
	namespace mx = MaterialX;

	FString Filename = GetSourceData()->GetFilename();

	if(!FPaths::FileExists(Filename))
	{
		return false;
	}
	try
	{
		mx::FileSearchPath MaterialXFolder{ TCHAR_TO_UTF8(*FPaths::Combine(
			FPaths::EngineDir(),
			TEXT("Binaries"),
			TEXT("ThirdParty"),
			TEXT("MaterialX"))) };

		mx::DocumentPtr MaterialXLibrary = mx::createDocument();

		mx::StringSet LoadedLibs = mx::loadLibraries({ mx::Library::Libraries }, MaterialXFolder, MaterialXLibrary);
		if(LoadedLibs.empty())
		{
			UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
			Message->Text = FText::Format(LOCTEXT("MaterialXLibrariesNotFound", "Couldn't load MaterialX libraries from {0}"),
										  FText::FromString(MaterialXFolder.asString().c_str()));
			return false;
		}

		mx::DocumentPtr Document = mx::createDocument();
		mx::readFromXmlFile(Document, TCHAR_TO_UTF8(*Filename));
		Document->importLibrary(MaterialXLibrary);

		// Read the document to be sure that the file is valid (meaning all nodes have their nodedef and typedef well-defined)
		bIsDocumentValid = ValidateDocument(Document, this);
		if(!bIsDocumentValid)
		{
			return false;
		}

		//Update the document by initializing and reorganizing the different nodes and subgraphs
		FMaterialXBase::UpdateDocumentRecursively(Document);

		// coming to this point we know for sure that the document is valid
		for(mx::ElementPtr Elem : Document->traverseTree())
		{
			//make sure to read only the current file otherwise we'll process the entire library
			if(Elem->getActiveSourceUri() != Document->getActiveSourceUri())
			{
				continue;
			}

			mx::NodePtr Node = Elem->asA<mx::Node>();

			if(Node)
			{
				bool bIsMaterialShader = Node->getType() == mx::Type::Material;
				bool bIsLightShader = Node->getType() == mx::Type::LightShader;

				//The entry point is only surfacematerial or lightshader
				if(bIsMaterialShader || bIsLightShader)
				{
					TSharedPtr<FMaterialXBase> ShaderTranslator = FMaterialXManager::GetInstance().GetShaderTranslator(Node->getCategory().c_str(), BaseNodeContainer);
					if(ShaderTranslator)
					{
						ShaderTranslator->Translate(Node);
					}
				}
			}
		}
	}
	catch(std::exception& Exception)
	{
		bIsDocumentValid = false;
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = FText::Format(LOCTEXT("MaterialXException", "{0}"),
									  FText::FromString(Exception.what()));
	}

#endif // WITH_EDITOR

	if(bIsDocumentValid)
	{
		UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(&BaseNodeContainer);
		SourceNode->SetCustomImportUnusedMaterial(true);
	}

	return bIsDocumentValid;
}

TOptional<UE::Interchange::FImportImage> UInterchangeMaterialXTranslator::GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const
{
	FString Filename = PayloadKey;
	TextureCompressionSettings CompressionSettings = TextureCompressionSettings::TC_Default;

#if WITH_EDITOR
	if(int32 IndexTextureCompression; PayloadKey.FindChar(FMaterialXManager::TexturePayloadSeparator, IndexTextureCompression))
	{
		Filename = PayloadKey.Mid(0, IndexTextureCompression);
		CompressionSettings = TextureCompressionSettings(FCString::Atoi(*PayloadKey.Mid(IndexTextureCompression + 1)));
	}
#endif
	UInterchangeSourceData* PayloadSourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData(Filename);
	FGCObjectScopeGuard ScopedSourceData(PayloadSourceData);

	if(!PayloadSourceData)
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	UInterchangeTranslatorBase* SourceTranslator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(PayloadSourceData);
	FGCObjectScopeGuard ScopedSourceTranslator(SourceTranslator);
	const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(SourceTranslator);

	if(!ensure(TextureTranslator))
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	SourceTranslator->SetResultsContainer(Results);

	AlternateTexturePath = Filename;

	TOptional<UE::Interchange::FImportImage> TexturePayloadData = TextureTranslator->GetTexturePayloadData(PayloadKey, AlternateTexturePath);

	if(TexturePayloadData.IsSet())
	{
		TexturePayloadData.GetValue().CompressionSettings = CompressionSettings;
	}

	return TexturePayloadData;
}

#undef LOCTEXT_NAMESPACE
