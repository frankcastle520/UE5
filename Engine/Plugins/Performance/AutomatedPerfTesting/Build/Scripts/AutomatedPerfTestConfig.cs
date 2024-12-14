// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EpicGame;
using Gauntlet;
using Microsoft.Extensions.Logging;
using Log = EpicGames.Core.Log;

namespace AutomatedPerfTest
{
	public class AutomatedPerfTestConfigBase : UnrealTestConfiguration
	{
		/// <summary>
		/// Used to override the test controller portion of the data source name
		/// Will be appended to Automation.ProjectName to construct the full data source name for the test
		/// can be overridden with AutomatedPerfTest.DataSourceNameOverride 
		/// </summary>
		[AutoParamWithNames("AutomatedPerfTest.DataSourceTypeOverride")]
		public string DataSourceTypeOverride = "";
		
		/// <summary>
		/// Fully override the data source name
		/// </summary>
		[AutoParamWithNames("AutomatedPerfTest.DataSourceNameOverride")]
		public string DataSourceNameOverride = "";

		/// <summary>
		/// Name of the test, useful for identifying it later
		/// </summary>
		[AutoParamWithNames("AutomatedPerfTest.TestName")]
		public string TestName = "";
		
		/// <summary>
		/// If set, will prepend platform name and use this device profile instead of the default
		/// </summary>
		[AutoParamWithNames("AutomatedPerfTest.DeviceProfileOverride")]
		public string DeviceProfileOverride = "";
		
		/// <summary>
		/// If we're running on the build machine
		/// </summary>
		[AutoParamWithNames(false, "AutomatedPerfTest.DoInsightsTrace")]
		public bool DoInsightsTrace;
		
		/// <summary>
		/// Which trace channels to test with
		/// </summary>
		[AutoParamWithNames("AutomatedPerfTest.TraceChannels")]
		public string TraceChannels = "default,screenshot,stats";
		
		/// <summary>
		/// If we're running on the build machine
		/// </summary>
		[AutoParamWithNames(false, "AutomatedPerfTest.DoCSVProfiler")]
		public bool DoCSVProfiler;
		
		/// <summary>
		/// If we're running on the build machine
		/// </summary>
		[AutoParamWithNames(false, "AutomatedPerfTest.DoFPSChart")]
		public bool DoFPSChart;
		
		/// <summary>
		/// Let BuildGraph tell us where we should output the Insights trace after running a test so that we know where to grab it from when we're done
		/// </summary>
		[AutoParamWithNames("", "AutomatedPerfTest.PerfCacheRoot")]
		public string PerfCacheRoot;

		/// <summary>
		/// Path to a JSON file with ignored issues (ensures, warnings, errros). Can be used to suppress hard-to-fix issues, on a per-branch basis
		/// </summary>
		[AutoParamWithNames("", "AutomatedPerfTest.IgnoredIssuesConfigAbsPath")]
		public string IgnoredIssuesConfigAbsPath;
		
		/// <summary>
		/// If we should trigger a video capture
		/// </summary>
		[AutoParamWithNames(false, "AutomatedPerfTest.DoVideoCapture")]
		public bool DoVideoCapture;
		
		/// <summary>
		/// If true, will look for the shipping configuration of Unreal Insights in order to parse Insights trace files
		/// This should be True unless you need to test an issue in parsing the Insights file itself.
		/// </summary>
		[AutoParamWithNames(true, "AutomatedPerfTest.UseShippingInsights")]
		public bool UseShippingInsights;
		
		public string DataSourceName;

		/// <summary>
		/// Call this in the test node's GetConfiguration function to set the DataSourceName used for later data processing
		/// </summary>
		/// <param name="ProjectName">Name of the project</param>
		/// <param name="DataSourceType">Which test controller is being used to generate the data</param>
		/// <returns> the properly formatted data source name </returns>
		public string GetDataSourceName(string ProjectName, string DataSourceType="")
		{
			// if the name has been fully overridden by the calling process, just use that
			if (!string.IsNullOrEmpty(DataSourceNameOverride))
			{
				return DataSourceNameOverride;
			}
			
			// otherwise, if the data source type has been override, use either that or the one passed into this function

			if (string.IsNullOrEmpty(DataSourceType) && string.IsNullOrEmpty(DataSourceTypeOverride))
			{
				Log.Logger.LogError("No DataSourceType or DataSourceTypeOverride has been provided.");
				return null;
			}
			
			string dataSourceType = string.IsNullOrEmpty(DataSourceTypeOverride) ? DataSourceType : DataSourceTypeOverride;
			
			return $"Automation.{ProjectName}.{dataSourceType}";
		}
	}

	public class AutomatedSequencePerfTestConfig : AutomatedPerfTestConfigBase
	{
		/// <summary>
		/// Which map to run the test on
		/// </summary>
		[AutoParamWithNames("", "AutomatedPerfTest.SequencePerfTest.MapSequenceName")]
		public string MapSequenceComboName;
	}

	public class AutomatedStaticCameraPerfTestConfig : AutomatedPerfTestConfigBase
	{
		/// <summary>
		/// Which map to run the test on
		/// </summary>
		[AutoParamWithNames("", "AutomatedPerfTest.StaticCameraPerfTest.MapName")]
		public string MapName;
	}
	
	public class AutomatedMaterialPerfTestConfig : AutomatedPerfTestConfigBase
	{
	}
}
