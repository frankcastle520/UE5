// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// TraceServices
#include "TraceServices/Model/TableImport.h"
#include "TraceServices/Containers/Tables.h"

class SDockTab;
class FSpawnTabArgs;

namespace TraceServices
{
	struct FTableImportCallbackParams;
}

namespace UE::Insights
{

class SUntypedTableTreeView;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FTableImporter : public TSharedFromThis<FTableImporter>
{
private:
	struct FOpenTableTabData
	{
		TSharedPtr<SDockTab> Tab;
		TSharedPtr<SUntypedTableTreeView> TableTreeView;

		bool operator ==(const FOpenTableTabData& Other) const
		{
			return Tab == Other.Tab && TableTreeView == Other.TableTreeView;
		}
	};

public:
	FTableImporter(FName InLogListingName);
	virtual ~FTableImporter();

	void StartImportProcess();
	void ImportFile(const FString& Filename);

	void StartDiffProcess();
	void DiffFiles(const FString& FilenameA, const FString& FilenameB);

	void CloseAllOpenTabs();

private:
	TSharedRef<SDockTab> SpawnTab_TableImportTreeView(const FSpawnTabArgs& Args, FName TableViewID, FText InDisplayName);
	TSharedRef<SDockTab> SpawnTab_TableDiffTreeView(const FSpawnTabArgs& Args, FName TableViewID, FText InDisplayName);
	void OnTableImportTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	void DisplayImportTable(FName TableViewID);
	void DisplayDiffTable(FName TableViewID);
	void TableImportServiceCallback(TSharedPtr<TraceServices::FTableImportCallbackParams> Params);

	FName GetTableID(const FString& Path);

private:
	FName LogListingName;
	TMap<FName, FOpenTableTabData> OpenTablesMap;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
