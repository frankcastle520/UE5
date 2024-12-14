// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using DatasmithSolidworks.Names;
using static DatasmithSolidworks.Addin;
using SolidWorks.Interop.swconst;
using static DatasmithSolidworks.FBody;
using static DatasmithSolidworks.FDocumentTracker;

namespace DatasmithSolidworks
{

	// Build DatasmithScene from Solidworks Document, supporting scene changes/modifications
	// Doesn't incorporate change notifications callbacks/handlers(i.e. notification handlers call into this class to tell what was changed and it updates Datasmith scene accordingly)
	public abstract class FDocumentTracker
	{
		public readonly FDocument Doc;  // Top Document that is tracked/exported
		public ModelDoc2 SwDoc => Doc.SwDoc;

		public Dictionary<int, FMaterial> ExportedMaterialsMap = new Dictionary<int, FMaterial>();
		public bool bHasConfigurations = false;

		public FDatasmithFacadeScene DatasmithScene = null;
		public FDatasmithExporter Exporter = null;

		public bool bDocumentIsDirty = true;
		public string DatasmithFileExportPath = "";

		public uint FaceCounter = 1; // Face Id generator

		// Encapsulates SW PersistentReference of a face
		public readonly struct FFaceId
		{
			public readonly byte[] Value;
			
			public FFaceId(byte[] InValue=null)
			{
				Value = InValue;
			}
			
			public bool IsValid()
			{
				return Value != null;
			}
			
			// Get key to use in a dictionary
			public string GetKey()
			{
				return Convert.ToBase64String(Value);
			}
		}
		
		protected FDocumentTracker(FDocument InDoc, FDatasmithExporter InExporter)
		{
			Doc = InDoc;

			DatasmithScene = new FDatasmithFacadeScene("StdMaterial", "Solidworks", "Solidworks", "");
			DatasmithScene.SetName(InDoc.SwDoc.GetTitle());

			Exporter = InExporter ?? new FDatasmithExporter(DatasmithScene);
		}

		public string GetPathName()
		{
			return SwDoc.GetPathName();
		}

		public bool GetDirty()
		{
			return bDocumentIsDirty;
		}

		public virtual void SetDirty(bool bInDirty)
		{
			bDocumentIsDirty = bInDirty;
		}

		// Currently, FaceId uses PersistentReference which replaced internal FaceId(GetFaceId/SetFaceId). GetFaceId/SetFaceId was too unstable.
		// PersistentReference is always defined. But GetOrAssignFaceId is left to identify places in code where Id is used with intent that it has to exist.
		// Calls to GetFaceId don't expect Id to be defined and can ignore if it's not.
		public FFaceId GetOrAssignFaceId(IFace2 InFace)
		{
			return new FFaceId(Doc.SwDoc.Extension.GetPersistReference3(InFace));
		}
		
		public FFaceId GetFaceId(IFace2 InFace)
		{
			return InFace != null ? new FFaceId(Doc.SwDoc.Extension.GetPersistReference3(InFace)) : new FFaceId();
		}
		
		public static bool IsValidFaceId(FFaceId InFaceId)
		{
			return InFaceId.IsValid();
		}

		public bool IsUpdateInProgress()
		{
			return Doc.bDirectLinkSyncInProgress || Doc.bFileExportInProgress;
		}

		public void SetExportStatus(string InMessage)
		{
			Doc.SetExportStatus(InMessage);
		}

		private void ExportConfigurations(List<FConfigurationData> Configs)
		{
			// Remove any existing Datasmith LevelVariantSets as we'll re-add data
			while (DatasmithScene.GetLevelVariantSetsCount() > 0)
			{
				for (int Index = 0; Index < DatasmithScene.GetLevelVariantSetsCount(); Index++)
				{
					DatasmithScene.RemoveLevelVariantSets(DatasmithScene.GetLevelVariantSets(Index));
				}
			}

			Dictionary<FDatasmithFacadeActorBinding, int> MaterialBindings = null;

			if (Configs != null)
			{
				MaterialBindings = new Dictionary<FDatasmithFacadeActorBinding, int>();
				Exporter.ExportLevelVariantSets(Configs, MaterialBindings);
			}

			// Export materials after processing variants - so that any material used in variant is registered and exported
			ExportMaterials();

			// Assign exported datasmith material instances to bindings
			foreach (KeyValuePair<FDatasmithFacadeActorBinding, int> KVP in MaterialBindings)
			{
				FDatasmithFacadeActorBinding Binding = KVP.Key;
				int MaterialId = KVP.Value;

				if (Exporter.GetDatasmithMaterial(MaterialId, out FDatasmithFacadeMaterialInstance DatasmithMaterial))
				{
					Binding.AddMaterialCapture(DatasmithMaterial);
				}					
			}
		}

		public void ExportMaterials()
		{
			Exporter.ExportMaterials(ExportedMaterialsMap);
		}

		private void ExportLights()
		{
			List<FLight> Lights  = FLightExporter.ExportLights(SwDoc);

			foreach (FLight Light in Lights)
			{
				Exporter.ExportLight(Light);
			}
		}

		public abstract FMeshes GetMeshes(string ActiveConfigName);

		public void Export()
		{
			string OutDir = Path.GetDirectoryName(DatasmithFileExportPath);
			string CleanFileName = Path.GetFileNameWithoutExtension(DatasmithFileExportPath);

			// Save/restore scene and exporter in order not to mess up DirectLink state
			FDatasmithFacadeScene OldScene = DatasmithScene;
			FDatasmithExporter OldExporter = Exporter;

			DatasmithScene = new FDatasmithFacadeScene("StdMaterial", "Solidworks", "Solidworks", "");
			DatasmithScene.SetName(CleanFileName);
			DatasmithScene.SetOutputPath(OutDir);

			Exporter = new FDatasmithExporter(DatasmithScene);

			ConfigurationManager ConfigManager = SwDoc.ConfigurationManager;
			string[] ConfigurationNames = SwDoc?.GetConfigurationNames();
			string ActiveConfigurationName = ConfigManager.ActiveConfiguration.Name;


			FMeshes Meshes = new FMeshes(ActiveConfigurationName);

			FConfigurationExporter ConfigurationExporter = new FConfigurationExporter(Meshes, ConfigurationNames, ActiveConfigurationName, bInExportDisplayStates: true, bInExportExplodedViews: true);

			List<FConfigurationData> Configs = ConfigurationExporter.ExportConfigurations(this);

			bHasConfigurations = (Configs != null) && (Configs.Count != 0);

			ExportToDatasmithScene(ConfigurationExporter, new FVariantName(ConfigManager.ActiveConfiguration));

			ExportLights();
			ExportConfigurations(Configs);

			DatasmithScene.PreExport();
			DatasmithScene.ExportScene(DatasmithFileExportPath);

			DatasmithScene = OldScene;
			Exporter = OldExporter;
		}

		public void Sync(string InOutputPath)
		{
			LogDebug($"Sync('{InOutputPath}')");
			LogIndent();

			DatasmithScene.SetName(SwDoc.GetTitle());
			DatasmithScene.SetOutputPath(InOutputPath);

#if DEBUG
			Stopwatch Watch = Stopwatch.StartNew();
#endif
			ConfigurationManager ConfigManager = SwDoc.ConfigurationManager;
			string ActiveConfigurationName = ConfigManager.ActiveConfiguration.Name;

			FMeshes Meshes = GetMeshes(ActiveConfigurationName);
			FConfigurationExporter ConfigurationExporter = new FConfigurationExporter(Meshes, new []{ ActiveConfigurationName }, ActiveConfigurationName, bInExportDisplayStates: false, bInExportExplodedViews: false);
			List<FConfigurationData> Configs = ConfigurationExporter.ExportConfigurations(this);
			bHasConfigurations = (Configs != null) && (Configs.Count != 0);
			ExportToDatasmithScene(ConfigurationExporter, new FVariantName(ConfigManager.ActiveConfiguration));
			DatasmithScene.SerializeLevelSequences();

			ExportLights();

#if DEBUG
			Watch.Stop();
			Debug.WriteLine($"EXPORT TIME: {(double)Watch.ElapsedMilliseconds / 1000.0}");
#endif
			LogDedent();
		}

		// todo: make abstract
		public virtual void ExportToDatasmithScene(FConfigurationExporter ConfigurationExporter, FVariantName ActiveVariantName)
		{
			throw new NotImplementedException();
		}

		public virtual bool NeedExportComponent(FConfigurationTree.FComponentTreeNode InComponent,
			FConfigurationTree.FComponentConfig ActiveComponentConfig)
		{
			return true;
		}

		public virtual void AddPartDocument(FConfigurationTree.FComponentTreeNode InNode){}

		public virtual void AddCollectedComponent(FConfigurationTree.FComponentTreeNode InNode) {}

		public abstract Dictionary<FComponentName, FObjectMaterials> LoadDocumentMaterials(HashSet<FComponentName> ComponentNamesToExportSet);
		public abstract void AddComponentMaterials(FComponentName ComponentName, FObjectMaterials Materials);
		public abstract FObjectMaterials GetComponentMaterials(Component2 Comp);
		
		public FMetadata GetComponentMetadata(Component2 InComponent, string CfgName)
		{
			ModelDoc2 ModelDoc = (ModelDoc2)InComponent.GetModelDoc2();
			if (ModelDoc == null)
			{
				return new FMetadata(FMetadata.EOwnerType.Actor);
			}
			
			FMetadata Metadata = new FMetadata(FMetadata.EOwnerType.Actor);
			
			string Doctype = "";
			bool bIsPart = false;
			switch (ModelDoc)
			{
				case AssemblyDoc _:
				{
					Doctype = "Assembly";
					break;
				}
				case PartDoc _:
				{
					Doctype = "Part";
					bIsPart = true;
					break;
				}
			}
			Metadata.AddPair("Document_Type", Doctype);
			Metadata.AddPair("Document_Filename", System.IO.Path.GetFileName(ModelDoc.GetPathName()));

			
			Metadata.AddPair("Document_Author", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoAuthor]);
			Metadata.AddPair("Document_Comment", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoComment]);
			Metadata.AddPair("Document_CreateDate", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoCreateDate]);
			Metadata.AddPair("Document_CreateDate2", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoCreateDate2]);
			Metadata.AddPair("Document_Keywords", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoKeywords]);
			Metadata.AddPair("Document_SaveDate", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoSaveDate]);
			Metadata.AddPair("Document_SaveDate2", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoSaveDate2]);
			Metadata.AddPair("Document_SavedBy", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoSavedBy]);
			Metadata.AddPair("Document_Subject", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoSubject]);
			Metadata.AddPair("Document_Title", ModelDoc.SummaryInfo[(int)swSummInfoField_e.swSumInfoTitle]);

			FMetadataManager.ExportCustomProperties(ModelDoc, Metadata);
			FMetadataManager.ExportCustomProperties(ModelDoc, Metadata, CfgName);
			if (bIsPart == false)
			{
				FMetadataManager.AddAssemblyDisplayStateMetadata(ModelDoc as AssemblyDoc, Metadata);
			}
			FMetadataManager.ExportCommentsAndBom(ModelDoc, Metadata);
			
			return Metadata;
		}

		// Record which meshes are used aby a component
		public abstract void AddMeshForComponent(FComponentName ComponentName, FMeshName MeshName);
		public abstract void ReleaseComponentMeshes(FComponentName CompName);

		// Remove unused meshes from Datasmith scene
		public abstract void CleanupComponentMeshes();

		public virtual void Destroy()
		{
		}

		public abstract FMeshData ExtractComponentMeshData(Component2 Comp);
		
		// Extracts meshes used for the assembly configuration
		public void ProcessConfigurationMeshes(List<FDatasmithExporter.FMeshExportInfo> MeshExportInfos, FMeshes.FConfiguration MeshesConfiguration)
		{
			LogDebug($"ProcessConfigurationMeshes:");
			LogIndent();

			// Extract meshes data and prepare for parallel datasmith export
			// note: mesh data need to be extracted from the component when required configuration is active(i.e. can't move it outside of configuration enumeration loop)
			// Enumerate components stable
			foreach (var CP in MeshesConfiguration.EnumerateComponents().Select(Comp => new {Comp, Name=new FComponentName(Comp)} ) .OrderBy(CP => CP.Name.ToString()))
			{
				
				FMeshData MeshData = ExtractComponentMeshData(CP.Comp);
				FComponentName ComponentName = CP.Name;
				LogDebug($"{ComponentName}:");
				LogDebug($"{MeshData}");
				
				if (MeshData != null)
				{
					MeshesConfiguration.AddMesh(ComponentName, MeshData, out FMeshName MeshName);
					MeshExportInfos.Add(new FDatasmithExporter.FMeshExportInfo()
					{
						ComponentName = ComponentName,
						MeshName = MeshName,
						MeshData = MeshData
					});
				}
			}
			LogDedent();
		}

		public void AssignMaterialsToDatasmithMeshes(List<FDatasmithExporter.FMeshExportInfo> CreatedMeshes)
		{
			Exporter.AssignMaterialsToDatasmithMeshes(CreatedMeshes);
		}
		
	};


	// Controls Syncing of a tracked Document, handling all events
	public interface IDocumentSyncer
	{

		// Make Datasmith Scene up to date with the Solidworks Document
		void Sync(string InOutputPath);

		// Indicate that change handling should start(after first Sync)
		void Start();

		// Resume all change tracking(after document was made active, foreground in Solidworks)
		void Resume();

		// Pause all change tracking(when document deactivated, background)
		void Pause();

		// Is any changes pending(simple test for AutoSync)
		bool GetDirty();

		FDocumentTracker GetTracker();

		// After Sync
		// todo: Probably not needed at all(i.e. all SetDirty(false) is only deep inside in notifiers, and with false can be moved  into Sync?)
		void SetDirty(bool bInDirty);  

		FDatasmithFacadeScene GetDatasmithScene();

		// Explicit immediate release of resources(line event handlers)
		void Destroy();

		void Idle();
	}

	/**
	 * Base document class.
	 */
	public abstract class FDocument
	{
		public int DocId = -1;
		public ModelDoc2 SwDoc = null;

		// Datasmith scene synced with the Solidworks document
		public IDocumentSyncer DocumentSyncer;

		// DirectLink
		public FDatasmithFacadeDirectLink DatasmithDirectLink = null;
		public string DirectLinkPath;
		public bool bDirectLinkAutoSync { get; set; } = false;
		public int DirectLinkSyncCount { get; private set; } = 0;

		// Sync/Export 
		public bool bDirectLinkSyncInProgress = false;
		public bool bFileExportInProgress = false;
		public string DatasmithFileExportPath = null;


		public FDocument(int InDocId, ModelDoc2 InSwDoc)
		{
			DocId = InDocId;
			SwDoc = InSwDoc;

			DirectLinkPath = Path.Combine(Path.GetTempPath(), "sw_dl_" + Guid.NewGuid().ToString());

			if (!Directory.Exists(DirectLinkPath))
			{
				Directory.CreateDirectory(DirectLinkPath);
			}
		}

		public void OnDirectLinkSync()
		{
			if (SwDoc.SketchManager.ActiveSketch != null)
			{
				// Do not run direct link in sketch mode! 
				// This will falsely detect meshes as deleted.
				return;
			}

			DirectLinkSyncCount++;
			
			bDirectLinkSyncInProgress = true;

			DocumentSyncer.Sync(DirectLinkPath);
			DatasmithDirectLink.UpdateScene(DocumentSyncer.GetDatasmithScene());
			DocumentSyncer.SetDirty(false);

			bDirectLinkSyncInProgress = false;

			SetExportStatus("Done");

			DocumentSyncer.Start();
		}

		public void ToggleDirectLinkAutoSync()
		{
			bDirectLinkAutoSync = !bDirectLinkAutoSync;

			if (bDirectLinkAutoSync && DirectLinkSyncCount == 0)
			{
				// Run first sync
				OnDirectLinkSync();
			}
		}

		public void OnIdle()
		{
			DocumentSyncer.Idle();
			if (bDirectLinkAutoSync && DocumentSyncer.GetDirty())
			{
				OnDirectLinkSync();
			}
		}

		public void OnExportToFile(string InFilePath)
		{
			bFileExportInProgress = true;
			DatasmithFileExportPath = InFilePath;  // For status
			Export(InFilePath);
			bFileExportInProgress = false;
		}

		// Toggle if this document is synced with DirectLink
		public void MakeActive(bool bInActive)
		{
			if (bInActive)
			{
				if (DatasmithDirectLink == null)
				{
					DatasmithDirectLink = new FDatasmithFacadeDirectLink();
					if (!DatasmithDirectLink.InitializeForScene(DocumentSyncer.GetDatasmithScene()))
					{
						throw new Exception("DirectLink: failed to initialize");
					}
				}

				DocumentSyncer.Resume();
			}
			else
			{
				// Suspend material checker(while it's a heavy threaded procedure)
				// other event handling is not suspended to register changes in the model
				DocumentSyncer?.Pause();

				DatasmithDirectLink?.CloseCurrentSource();
				DatasmithDirectLink?.Dispose();  // aka 'Close DirectLink Connection'
				DatasmithDirectLink = null;
			}
		}

		public abstract void Export(string InFilePath);

		public string GetPathName()
		{
			return SwDoc.GetPathName();
		}

		// Dispose all resources explicitly
		// todo: DatasmithDirectLink should be disposed here too?
		public virtual void Destroy()
		{
			DocumentSyncer?.Destroy();
		}

		public void SetExportStatus(string InMessage)
		{
			if (bDirectLinkSyncInProgress)
			{
				Addin.Instance.LogStatusBarMessage($"DirectLink sync...{InMessage}");
			}
			else if (bFileExportInProgress)
			{
				string FileName = Path.GetFileName(DatasmithFileExportPath);
				Addin.Instance.LogStatusBarMessage($"Exporting file {FileName}...{InMessage}");
			}
		}

	}
}