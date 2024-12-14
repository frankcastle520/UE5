// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using UnrealBuildBase;
using Log = EpicGames.Core.Log;
using Microsoft.Extensions.Logging;

namespace AutomatedPerfTest
{
	/// <summary>
	/// Helper class for parsing and accessing the metadata stored in a trace's filename
	/// </summary>
	public class TraceFileInfo
	{
		public string BuildName;
		public string Build;
		public int Changelist;
		public int PreflightChangelist = -1;
		public string PlatformName;
		public string DateTime;
		public string DeviceProfile;
		public string TestName;
		public string TraceFilePath;
		public string FilePath;
		public string FileName;

		public TraceFileInfo(string InTraceFilePath)
		{
			TraceFilePath = InTraceFilePath;
			FilePath = Path.GetDirectoryName(TraceFilePath);
			FileName = Path.GetFileNameWithoutExtension(TraceFilePath);
			
			// Trace files come out in the form:
			// Megaworlds-CL-0_Windows_20231120-112520_Windows_InitialTesting.utrace
			
			string[] FilenameTokens = FileName.Split('_');
			BuildName = FilenameTokens[0].ToLower();
			Build = BuildName.Split('-')[0].ToLower();
			Changelist = int.Parse(BuildName.Split('-')[2]);
			
			// check for a preflight changelist so we can strip out the preflight ID from the filename
			// due to path-length limitations
			if(BuildName.Contains("pf"))
			{
				// strip the preflight info out of the build name
				BuildName = String.Join('-', BuildName.Split('-').Skip(0).Take(4));
				PreflightChangelist = int.Parse(BuildName.Split('-')[4]);
			}
			
			// some platforms can only output lowercase, but the inputs will always be upper case
			// and that's what we'll compare against to see if a trace is valid
			PlatformName = FilenameTokens[1].ToLower();
			
			DateTime = FilenameTokens[2].ToLower();
			
			DeviceProfile = FilenameTokens[3].ToLower();
			
			TestName = FilenameTokens[4].ToLower();
			
		}

		/// <summary>
		/// Sanitize the trace's filename to help conform to path-lenght limits
		/// </summary>
		/// <returns></returns>
		public string GetSanitizedFileName()
		{
			// reconstruct the filename for this test to skip the preflight identifier
			string SanitizedFilename = $"{Build}-cl-{Changelist.ToString()}";
			if (PreflightChangelist > -1)
			{
				SanitizedFilename += $"-pf-{PreflightChangelist.ToString()}";
			}
			SanitizedFilename += $"_{PlatformName}_{DateTime}_{TestName}";

			return SanitizedFilename;
		}

		/// <summary>
		/// Returns the full path to the log file to use when parsing this trace
		/// </summary>
		/// <returns></returns>
		public string GetLogFilePath()
		{
			return Path.Combine(FilePath, FileName + ".log");
		}
		
		/// <summary>
		/// Checks to see if this trace has a matching CSV summary file output
		/// which would indicate that it has already been processed
		/// </summary>
		/// <returns>bool</returns>
		public bool HasMatchingLog()
		{
			return File.Exists(GetLogFilePath());
		}

		/// <summary>
		/// Return the directory to which the Insights CLI should output all the region CSVs
		/// </summary>
		/// <returns></returns>
		public string GetRegionCSVOutputPath()
		{
			return FilePath;
		}
		
		/// <summary>
		/// For after trace analysis, find all the output CSVs for all the regions
		/// </summary>
		/// <returns></returns>
		public List<string> GatherOutputCSVs()
		{
			List<string> GatheredCSVs = new List<string>();
			if (Directory.Exists(GetRegionCSVOutputPath()))
			{
				GatheredCSVs.AddRange(
					from CSVFile in Directory.GetFiles(GetRegionCSVOutputPath(), $"{FileName}*.csv",
						SearchOption.AllDirectories)
					select CSVFile);
			}

			return GatheredCSVs;
		}
	}
	public abstract class AutomatedPerfTestInsightsReport : BuildCommand
	{
		public string BuildName;
		public string ProjectName;
		public string TestName;
		public string PerfCacheRoot;
		public bool UseShippingInsights;
		
		private void InitParams()
		{
			// grab config variables from commandline
			BuildName = ParseParamValue("AutomatedPerfTest.BuildName", "NotFound").ToLower();
			ProjectName = ParseParamValue("AutomatedPerfTest.ProjectName", "NotFound");
			TestName = ParseParamValue("AutomatedPerfTest.TestName", "NotFound");
			PerfCacheRoot = ParseParamValue("AutomatedPerfTest.PerfCacheRoot", null);
			UseShippingInsights = ParseParamBool("AutomatedPerfTest.UseShippingInsights", false);
		}

		public FileReference GetInsightsExecutablePath()
		{
			FileReference InsightsPath = UseShippingInsights ? FileReference.Combine(Unreal.EngineDirectory, "Binaries", "Win64", "UnrealInsights-Win64-Shipping.exe") : FileReference.Combine(Unreal.EngineDirectory, "Binaries", "Win64", "UnrealInsights.exe");

			// ReSharper disable once InvertIf
			if (!FileReference.Exists(InsightsPath))
			{
				Log.Logger.LogError("Failed to find perf report utility at this path: \"{InsightsPath}\".", InsightsPath);
				return null;
			}

			return InsightsPath;
		}

		/// <summary>
		/// Call Unreal Insights CLI to export timer statistics for all the regions in the input trace
		/// And output the GPU timings for those regions to a CSV
		/// Then upload the results from those CSVs to the database
		/// </summary>
		/// <param name="TraceFile"></param>
		private void ProcessTraceFile(TraceFileInfo TraceFile)
		{
			Log.Logger.LogInformation("Processing trace file {TraceFilePath}", TraceFile.TraceFilePath);
			
			// make sure we can output the CSVs to the right folder by creating it first
			InternalUtils.SafeCreateDirectory(TraceFile.GetRegionCSVOutputPath(), true);

			// only export regions matching the build name, as other systems are starting to use regions and we don't need to export ALL of them
			CommandUtils.RunAndLog(GetInsightsExecutablePath().FullName,
				$"-AutoQuit -NoUI -ABSLOG=\"{TraceFile.GetLogFilePath()}\" -OpenTraceFile=\"{TraceFile.TraceFilePath}\" -ExecOnAnalysisCompleteCmd=\"TimingInsights.ExportTimerStatistics {TraceFile.GetRegionCSVOutputPath()}\\*.csv -region={TraceFile.BuildName}* -threads=GPU\"",
				out int ErrorCode);

			if (ErrorCode != 0)
			{
				Log.Logger.LogError(
					"MegaworldsPerformanceTool returned error code \"{ErrorCode}\" while generating detailed report from trace \"{TraceFile.TraceFilePath}\" CSV folder \"{CSVOutputPath}\"",
					ErrorCode, TraceFile, TraceFile.GetRegionCSVOutputPath());
				return;
			}
			
			List<string> CSVFilenames = TraceFile.GatherOutputCSVs();

			if (CSVFilenames.Count < 1)
			{
				Log.Logger.LogError("No CSVs found at output path {CSVOutputPath}", TraceFile.GetRegionCSVOutputPath());
				return;
			}

			Dictionary<string, dynamic>[] outputJSONData = new Dictionary<string, dynamic>[CSVFilenames.Count];
			
			for (int i = 0; i < CSVFilenames.Count; i++)
			{
				// create a dict to which we can load the data we want from the CSV and output it to the database
				Dictionary<string, dynamic> jsonData = new Dictionary<string, dynamic>();
				
				// find the specific region id from the end of the CSV region name, this could also just be TestName
				string RegionID = Path.GetFileNameWithoutExtension(CSVFilenames[i]).Split('_').Last();
				
				// pass along the known values
				jsonData["Build"] = TraceFile.Build;
				jsonData["Changelist"] = TraceFile.Changelist;
				jsonData["PreflightCL"] = TraceFile.PreflightChangelist;
				jsonData["PlatformName"] = TraceFile.PlatformName;
				jsonData["DateTime"] = TraceFile.DateTime;
				jsonData["DeviceProfile"] = TraceFile.DeviceProfile;
				jsonData["IsBuildMachine"] = IsBuildMachine;
				jsonData["TestName"] = TestName;
				jsonData["RegionID"] = RegionID;

				// TODO make this a config file
				
				// load the CSV to grab the columns we want from the rows of the CSV
				using (StreamReader csvParser = new StreamReader(CSVFilenames[i]))
				{
					// skip the header
					csvParser.ReadLine();

					string line;
					string[] fields;
					string TimerName;
					string InclusiveAverage;

					while (!csvParser.EndOfStream)
					{
						line = csvParser.ReadLine();
						if (line != null)
						{
							fields = line.Split(",");
							TimerName = fields[0];
							
							// the output CSV from Insights puts Inclusive Average at the 6th column
							InclusiveAverage = fields[5];
							
							// multiply the timer number by 1000, since the CSV output is in Seconds and 
							// we'd much rather see it in Milliseconds
							jsonData[TimerName] = float.Parse(InclusiveAverage) * 1000.0;
						}
					}
				}

				// output the json data to a string, and then to a file so we can quickly reconstruct the database if necessary
				string jsonString =
					JsonSerializer.Serialize(jsonData, new JsonSerializerOptions() {WriteIndented = true});

				string JsonOutfile = CSVFilenames[i].Replace(".csv", ".json");

				Log.Logger.LogInformation("Outputting {CSVFilename} to {JsonOutfile}", CSVFilenames[i], JsonOutfile);
				
				InternalUtils.SafeCreateDirectory(Path.GetDirectoryName(JsonOutfile), true);
				
				using (StreamWriter outputFile = new StreamWriter(JsonOutfile))
				{
					outputFile.WriteLine(jsonString);
				}

				outputJSONData[i] = jsonData;
			}
			
			OutputSummary(TraceFile, outputJSONData);
		}

		private void OutputSummary(TraceFileInfo TraceFile, Dictionary<string, dynamic>[] OutputJSONData)
		{
			string OutCSVName = TraceFile.TraceFilePath.Replace(".utrace", ".csv");

			string CSVHeader = "RegionName,Unaccounted";

			Log.Logger.LogInformation("Outputting CSV summary to {SummaryCSVPath}", OutCSVName);
			
			using (StreamWriter OutCSV = new StreamWriter(OutCSVName))
			{
				OutCSV.WriteLine(CSVHeader);
				Log.Logger.LogInformation(CSVHeader);
				for (int i = 0; i < OutputJSONData.Length; i++)
				{
					string lineString = $"{OutputJSONData[i]["RegionID"]},{OutputJSONData[i]["Unaccounted"]}";
					OutCSV.WriteLine(lineString);
					Log.Logger.LogInformation(lineString);
				}
			}
		}

		public override void ExecuteBuild()
		{
			InitParams();

			Log.Logger.LogInformation("BuildName from BuildGraph = {BuildName}", BuildName);

			// loop over all of the unprocessed trace files in the perf cache and upload the results
			// more brute force, but less likely to upload the wrong results or not upload any results
			
			// search through the path provided from commandline
			var DiscoveredTraces = new List<string>();
			if (Directory.Exists(PerfCacheRoot))
			{
				DiscoveredTraces.AddRange(
					from TraceFile in Directory.GetFiles(PerfCacheRoot, "*.utrace", SearchOption.AllDirectories)
					select TraceFile);
			}
			else
			{
				Log.Logger.LogError("PerfCacheRoot does not exists {PerfCacheRoot}", PerfCacheRoot);
			}

			// if we couldn't find any traces, report that and bail out
			if (DiscoveredTraces.Count == 0)
			{
				Log.Logger.LogError("Tests completed successfully but no trace results were found. Searched path was {PerfCacheRoot}", PerfCacheRoot);
			}
			
			// Sort the cases by timestamp, newest first
			string[] SortedTraces = 
				(from TraceFile in DiscoveredTraces
					let Timestamp = File.GetCreationTimeUtc(TraceFile)
					orderby Timestamp descending
					select TraceFile).ToArray();
			
			for (int i = 0; i < SortedTraces.Length; i++)
			{
				TraceFileInfo TraceInfo = new TraceFileInfo(SortedTraces[i]);
				
				if (!TraceInfo.HasMatchingLog())
				{
					Log.Logger.LogInformation("Found unprocessed trace file {MatchingTrace}", SortedTraces[i]);
					ProcessTraceFile(TraceInfo);
				}
			}
		}
	}
}