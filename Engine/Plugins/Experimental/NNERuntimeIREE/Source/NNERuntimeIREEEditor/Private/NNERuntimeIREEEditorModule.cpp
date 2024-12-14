// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREEEditorModule.h"
#include "NNERuntimeIREELog.h"

#define LOCTEXT_NAMESPACE "FNNERuntimeIREEEditorModule"

DEFINE_LOG_CATEGORY(LogNNERuntimeIREE);

void FNNERuntimeIREEEditorModule::StartupModule()
{
}

void FNNERuntimeIREEEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNNERuntimeIREEEditorModule, NNERuntimeIREEEditor)