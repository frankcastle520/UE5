// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestRunner.h"

#if !UE_BUILD_SHIPPING && !WITH_EDITOR

#include "IAutomationControllerModule.h"
#include "IAutomationControllerManager.h"
#include "UObject/UObjectGlobals.h" // for StaticExec()

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(InsightsTestRunner);

#define LOCTEXT_NAMESPACE "UE::Insights::TestRunner"

namespace UE::Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTestRunner::Run(const FString& InCmd)
{
	FString ActualCmd = InCmd.Replace(TEXT("\""), TEXT(""));
	if (!ActualCmd.StartsWith(TEXT("Automation RunTests")))
	{
		UE_LOG(InsightsTestRunner, Warning, TEXT("[InsightsTestRunner] Command %s does not start with Automation RunTests. Command will be ignored."), *InCmd);
		return;
	}

	IAutomationControllerModule& AutomationControllerModule = FModuleManager::LoadModuleChecked<IAutomationControllerModule>(TEXT("AutomationController"));
	IAutomationControllerManagerRef AutomationControllerManager = AutomationControllerModule.GetAutomationController();

	AutomationControllerManager->OnTestsComplete().AddLambda([this]() { bIsRunningTests = false; });
	bIsRunningTests = true;

	::StaticExec(NULL, *ActualCmd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights

#undef LOCTEXT_NAMESPACE

#endif //UE_BUILD_SHIPPING && !WITH_EDITOR
