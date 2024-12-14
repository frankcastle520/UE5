// Copyright Epic Games, Inc. All Rights Reserved.

using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Collections.Generic;
using DatasmithSolidworks.Names;
using static DatasmithSolidworks.Addin;

namespace DatasmithSolidworks
{
	public interface IMetadataPair
	{
		void WriteToDatasmithMetaData(FDatasmithFacadeMetaData metadata);
	}

	public class FMetadataStringPair : IMetadataPair
	{
		private string Name;
		private string Value;

		public FMetadataStringPair(string InName, string InValue)
		{
			Name = InName;
			Value = InValue;
		}

		public void WriteToDatasmithMetaData(FDatasmithFacadeMetaData InMetadata)
		{
			InMetadata.AddPropertyString(Name, Value);
		}
	}

	public class FMetadataBoolPair : IMetadataPair
	{
		private string Name;
		private bool bValue;

		public FMetadataBoolPair(string InName, bool bInValue)
		{
			Name = InName;
			bValue = bInValue;
		}

		public void WriteToDatasmithMetaData(FDatasmithFacadeMetaData InMetadata)
		{
			InMetadata.AddPropertyBoolean(Name, bValue);
		}
	}

	public class FMetadata
	{
		public enum EOwnerType
		{
			Actor,
			MeshActor,
			None
		}

		public FActorName OwnerName { get; set; }
		public EOwnerType OwnerType = EOwnerType.None;
		public List<IMetadataPair> Pairs = new List<IMetadataPair>();

		public FMetadata(EOwnerType mdatatype)
		{
			OwnerType = mdatatype;
		}

		public void AddPair(string name, string value)
		{
			Pairs.Add(new FMetadataStringPair(name, value));
		}

		public void AddPair(string name, bool value)
		{
			Pairs.Add(new FMetadataBoolPair(name, value));
		}
	}

	public class FMetadataManager
	{
		public static bool ExportCustomProperties(ModelDoc2 InModeldoc, FMetadata InMetadata, string CfgName="")
		{
			LogDebug($"ExportCustomProperties {InModeldoc.GetPathName()}, Config:{CfgName} ");

			ModelDocExtension Ext = InModeldoc.Extension;
			if (Ext == null)
			{
				return false;
			}

			// Add document related custom properties metadata
			CustomPropertyManager PropertyManager = Ext.get_CustomPropertyManager(CfgName);

			if (PropertyManager == null)
			{
				return false;
			}
			
			return ExportCustomPropertyManagerToMetadata(PropertyManager, InMetadata, string.IsNullOrEmpty(CfgName) ?  $"Document_Custom_Property_" : $"Document_Configuration_Property_{CfgName}_");
		}
		
		// Recursively visit all sub-features of the feature
		// e.g. BomFeat is a suf-feature of a TableFolder feature
		public static bool TraverseFeature(Feature Feat, System.Func<Feature, bool> Callback)
		{
			LogDebug($"Feature: {Feat.Name} <{Feat.GetTypeName2()}>");
			
			if (!Callback(Feat))
			{
				return false;
			}
			
			Feature SubFeat = (Feature)Feat.GetFirstSubFeature();
			
			while (SubFeat != null)
			{
				using (LogScopedIndent())
				{
					if (!TraverseFeature(SubFeat, Callback))
					{
						return false;
					}
				}
				
				SubFeat = (Feature)SubFeat.GetNextSubFeature();
			}
			
			return true;
		}
		
		public static bool ExportCommentsAndBom(ModelDoc2 InModelDoc, FMetadata InMetadata)
		{
			FeatureManager FeatureMan = InModelDoc.FeatureManager;
			if (FeatureMan == null)
			{
				return false;
			}

			Feature Feat = InModelDoc.FirstFeature();
			
			while (Feat !=  null)
			{
				TraverseFeature(Feat, F =>
				{
					ExtractFeatureMetadata(F, F.GetTypeName2(), InMetadata);
					return true;
				});

				Feat = Feat.GetNextFeature();
			}
			return true;
		}
		
		private static void ExtractFeatureMetadata(Feature Feat, string FeatureType, FMetadata InMetadata)
		{
			switch (FeatureType)
			{
				case "CommentsFolder":
				{
					CommentFolder CommentFolder = (CommentFolder)Feat.GetSpecificFeature2();
					if (CommentFolder != null)
					{
						int CommentCount = CommentFolder.GetCommentCount();
						if (CommentCount > 0)
						{
							object[] Comments = (object[])CommentFolder.GetComments();
							if (Comments != null)
							{
								for (int I = 0; I < CommentCount; I++)
								{
									Comment Comment = (Comment)Comments[I];
									InMetadata.AddPair("Comment_" + Comment.Name, Comment.Text);
								}
							}
						}
					}
					break;
				}

				case "BomFeat":
				{
					BomFeature BomFeat = (BomFeature)Feat.GetSpecificFeature2();
					if (BomFeat != null)
					{
						ExportBomFeature(BomFeat, InMetadata);
					}
					break;
				}
			}
		}
		
		public static bool ExportBomFeature(BomFeature InBomFeature, FMetadata InMetadata)
		{
			Feature Feat = InBomFeature.GetFeature();
			string FeatureName = Feat.Name;
			InMetadata.AddPair("BOMTable_FeatureName", FeatureName);

			object[] Tables = (object[])InBomFeature.GetTableAnnotations();
			if (Tables == null || Tables.Length == 0)
			{
				return false;
			}

			foreach (object Table in Tables)
			{
				ExportTable((TableAnnotation)Table, InMetadata, FeatureName);
			}
			return true;
		}
		
		public static void ExportTable(TableAnnotation InTable, FMetadata InMetadata, string FeatureNamePrefix)
		{
			int HeaderCount = InTable.GetHeaderCount();
			if (HeaderCount == 0)
			{
				return;
			}

			int Index = 0;
			int SplitCount = 0;
			int RangeStart = 0;
			int RangeEnd = 0;
			int ColumnCount = 0;
			swTableSplitDirection_e SplitDir = (swTableSplitDirection_e) InTable.GetSplitInformation(ref Index, ref SplitCount, ref RangeStart, ref RangeEnd);

			if (SplitDir == swTableSplitDirection_e.swTableSplit_None)
			{
				int RowCount = InTable.RowCount;
				ColumnCount = InTable.ColumnCount;
				RangeStart = HeaderCount;
				RangeEnd = RowCount - 1;
			}
			else
			{
				ColumnCount = InTable.ColumnCount;
				if (Index == 1)
				{
					// Add header offset for first portion of table
					RangeStart += HeaderCount;
				}
			}

			if (InTable.TitleVisible)
			{
				InMetadata.AddPair("BOMTable_Feature_" + FeatureNamePrefix, InTable.Title);
			}

			string[] HeadersTitles = new string[ColumnCount];
			for (int ColumnIndex = 0; ColumnIndex < ColumnCount; ColumnIndex++)
			{
				HeadersTitles[ColumnIndex] = InTable.GetColumnTitle2(ColumnIndex, true);
			}

			for (int RowIndex = RangeStart; RowIndex <= RangeEnd; RowIndex++)
			{
				for (int ColumnIndex = 0; ColumnIndex < ColumnCount; ColumnIndex++)
				{
					string HeadersTitle = HeadersTitles[ColumnIndex];
					InMetadata.AddPair("BOMTable_Feature_" + FeatureNamePrefix + "_" + HeadersTitle, InTable.Text2[RowIndex, ColumnIndex, true]);
				}
			}
		}

		public static void AddAssemblyDisplayStateMetadata(AssemblyDoc InAssemblyDoc, FMetadata InMetadata)
		{
			ModelDocExtension Ext = ((ModelDoc2)InAssemblyDoc).Extension;
			if (Ext == null)
			{
				return;
			}

			ConfigurationManager Cfm = ((ModelDoc2)InAssemblyDoc).ConfigurationManager;
			if (Cfm != null)
			{
				Configuration ActiveConf = Cfm.ActiveConfiguration;
				object[] DisplayStates = ActiveConf.GetDisplayStates();
				if (DisplayStates != null)
				{
					string ActiveDisplayStateName = (string)DisplayStates[0];
					if (ActiveDisplayStateName != null)
					{
						InMetadata.AddPair("ActiveDisplayState", (string)DisplayStates[0]);
					}
				}
			}

			object[] VarComp = (object[])(InAssemblyDoc.GetComponents(false));
			int ComponentCount = InAssemblyDoc.GetComponentCount(false);
			Component2[] ListComp = new Component2[ComponentCount];
			DisplayStateSetting DSS = Ext.GetDisplayStateSetting((int)swDisplayStateOpts_e.swThisDisplayState);
			DSS.Option = (int)swDisplayStateOpts_e.swThisDisplayState;

			for (int ComponentIndex = 0; ComponentIndex < ComponentCount; ComponentIndex++)
			{
				ListComp[ComponentIndex] = (Component2)VarComp[ComponentIndex];
			}
			DSS.Entities = ListComp;

			System.Array DisplayModes = (System.Array)Ext.DisplayMode[DSS];
			System.Array Transparencies = (System.Array)Ext.Transparency[DSS];
			for (int Idx = 0; Idx < ComponentCount; Idx++)
			{
				InMetadata.AddPair("Component_Display_State_DisplayMode_" + ((Component2)VarComp[Idx]).Name2, ((swDisplayMode_e)DisplayModes.GetValue(Idx)).ToString());
				InMetadata.AddPair("Component_Display_State_Transparency_" + ((Component2)VarComp[Idx]).Name2, ((swTransparencyState_e)Transparencies.GetValue(Idx)).ToString());
			}
			return;
		}

		private static bool ExportCustomPropertyManagerToMetadata(CustomPropertyManager InCustomPropertyManager, FMetadata InMetadata, string InPrefix)
		{
			if (InCustomPropertyManager == null)
			{
				LogDebug($"    NULL");
				return false;
			}

			object PropertiesNamesObject = null;
			object PropertiesValuesObject = null;
			object PropertiesTypesObject = null;
			object ResolvedObject = false;
			object LinkToPropObject = false;

			InCustomPropertyManager.GetAll3(ref PropertiesNamesObject, ref PropertiesTypesObject, ref PropertiesValuesObject, ref ResolvedObject, ref LinkToPropObject);
			string[] PropertiesNames = (string[])PropertiesNamesObject;
			object[] PropertiesValues = (object[])PropertiesValuesObject;
			int[] PropertiesTypes = (int[])PropertiesTypesObject;
			
			LogDebug($"  Count: {InCustomPropertyManager.Count}");
			for (int Idx = 0; Idx < InCustomPropertyManager.Count; Idx++)
			{
				LogDebug($"    {PropertiesNames[Idx]}: Type={PropertiesTypes[Idx]}, Value={PropertiesValues[Idx]}");
				switch (PropertiesTypes[Idx])
				{
					case (int)swCustomInfoType_e.swCustomInfoUnknown:
						break;

					case (int)swCustomInfoType_e.swCustomInfoNumber:
					case (int)swCustomInfoType_e.swCustomInfoDouble:
					case (int)swCustomInfoType_e.swCustomInfoText:
					case (int)swCustomInfoType_e.swCustomInfoDate:
						InMetadata.AddPair(InPrefix + PropertiesNames[Idx], PropertiesValues[Idx].ToString());
						break;

					case (int)swCustomInfoType_e.swCustomInfoYesOrNo:
						InMetadata.AddPair(InPrefix + PropertiesNames[Idx], PropertiesValues[Idx].ToString() == "Yes");
						break;

					default:
						break;
				}
			}
			return true;
		}
	}
}