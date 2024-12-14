// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEEditorModelDataActions.h"
#include "NNEEditorOnnxModelInspector.h"

namespace UE::NNEEditor::Private::OnnxHelper
{
	void* GetSharedLibHandle(const FString& SharedLibPath)
	{
		if (!FPaths::FileExists(SharedLibPath))
		{
			UE_LOG(LogNNE, Error, TEXT("Failed to find the NNEEditorOnnxTools shared library %s."), *SharedLibPath);
			return nullptr;
		}

		void* SharedLibHandle = FPlatformProcess::GetDllHandle(*SharedLibPath);

		if (!SharedLibHandle)
		{
			UE_LOG(LogNNE, Error, TEXT("Failed to load the NNEEditorOnnxTools shared library %s."), *SharedLibPath);
		}

		return SharedLibHandle;
	}

} // namespace UE::NNEEditor::Private::OnnxHelper

class FNNEEditorModule : public IModuleInterface
{
	void* NNEEditorOnnxToolsSharedLibHandle = nullptr;

public:
	virtual void StartupModule() override
	{
#ifdef NNEEDITORONNXTOOLS_SUPPORTED
		const FString ModuleDir = FPlatformProcess::GetModulesDirectory();
		const FString NNEEditorOnnxToolsSharedLibPath = FPaths::Combine(ModuleDir, TEXT(PREPROCESSOR_TO_STRING(NNEEDITORONNXTOOLS_SHAREDLIB_FILENAME)));
		NNEEditorOnnxToolsSharedLibHandle = UE::NNEEditor::Private::OnnxHelper::GetSharedLibHandle(NNEEditorOnnxToolsSharedLibPath);
		UE::NNEEditor::Private::OnnxModelInspectorHelper::SetupSharedLibFunctionPointer(NNEEditorOnnxToolsSharedLibHandle);
#endif // ifdef NNEEDITORONNXTOOLS_SUPPORTED

		ModelDataAssetTypeActions = MakeShared<UE::NNEEditor::Private::FModelDataAssetTypeActions>();
		FAssetToolsModule::GetModule().Get().RegisterAssetTypeActions(ModelDataAssetTypeActions.ToSharedRef());
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			FAssetToolsModule::GetModule().Get().UnregisterAssetTypeActions(ModelDataAssetTypeActions.ToSharedRef());
		}

		UE::NNEEditor::Private::OnnxModelInspectorHelper::ClearSharedLibFunctionPointer();

		if (NNEEditorOnnxToolsSharedLibHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(NNEEditorOnnxToolsSharedLibHandle);
			NNEEditorOnnxToolsSharedLibHandle = nullptr;
		}
	}

private:
	TSharedPtr<UE::NNEEditor::Private::FModelDataAssetTypeActions> ModelDataAssetTypeActions;
};

IMPLEMENT_MODULE(FNNEEditorModule, NNEEditor)