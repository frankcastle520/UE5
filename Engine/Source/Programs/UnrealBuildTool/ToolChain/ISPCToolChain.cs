// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	abstract class ISPCToolChain : UEToolChain
	{
		public ISPCToolChain(ILogger InLogger) : base(InLogger)
		{
		}

		protected FileReference? ProjectFile = null;
		protected bool bMergeModules = false;
		protected bool bAllowUbaCompression = false;

		public override void SetUpGlobalEnvironment(ReadOnlyTargetRules Target)
		{
			base.SetUpGlobalEnvironment(Target);
			ProjectFile = Target.ProjectFile;
			bMergeModules = Target.bMergeModules;
			bAllowUbaCompression = Target.bAllowUbaCompression;
		}

		/// <summary>
		/// Get CPU Instruction set targets for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <param name="Arch">Which architecture inside an OS platform to target. Only used for Android currently.</param>
		/// <returns>List of instruction set targets passed to ISPC compiler</returns>
		public virtual List<string> GetISPCCompileTargets(UnrealTargetPlatform Platform, UnrealArch Arch)
		{
			List<string> ISPCTargets = new List<string>();

			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Apple))
			{
				if (Arch.bIsX64)
				{
					ISPCTargets.AddRange(new string[] { "avx512skx-i32x8", "avx2", "avx", "sse4" });
				}
				else
				{
					ISPCTargets.Add("neon");
				}
			}
			else if (Platform == UnrealTargetPlatform.Android)
			{
				if (Arch == UnrealArch.X64)
				{
					ISPCTargets.Add("sse4");
				}
				else if (Arch == UnrealArch.Arm64)
				{
					ISPCTargets.Add("neon");
				}
				else
				{
					Logger.LogWarning("Invalid Android architecture for ISPC. At least one architecture (arm64, x64) needs to be selected in the project settings to build");
				}
			}
			else
			{
				Logger.LogWarning("Unsupported ISPC platform target!");
			}

			return ISPCTargets;
		}

		/// <summary>
		/// Get OS target for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <returns>OS string passed to ISPC compiler</returns>
		public virtual string GetISPCOSTarget(UnrealTargetPlatform Platform)
		{
			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows))
			{
				return "windows";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix))
			{
				return "linux";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Android))
			{
				return "android";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.IOS))
			{
				return "ios";
			}
			else if (Platform == UnrealTargetPlatform.Mac)
			{
				return "macos";
			}
			
			Logger.LogWarning("Unsupported ISPC platform target!");
			return String.Empty;
		}

		/// <summary>
		/// Get CPU architecture target for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <param name="Arch">Which architecture inside an OS platform to target. Only used for Android currently.</param>
		/// <returns>Arch string passed to ISPC compiler</returns>
		public virtual string GetISPCArchTarget(UnrealTargetPlatform Platform, UnrealArch Arch)
		{
			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Apple) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Android))
			{
				if (Arch.bIsX64)
				{
					return "x86-64";
				}
				return "aarch64";
			}
			
			Logger.LogWarning("Unsupported ISPC platform target!");
			return String.Empty;
		}

		/// <summary>
		/// Get CPU target for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS platform to target.</param>
		/// <returns>CPU string passed to ISPC compiler</returns>
		public virtual string? GetISPCCpuTarget(UnrealTargetPlatform Platform)
		{
			return null;  // no specific CPU selected
		}

		/// <summary>
		/// Get host compiler path for ISPC.
		/// </summary>
		/// <param name="HostPlatform">Which OS build platform is running on.</param>
		/// <returns>Path to ISPC compiler</returns>
		public virtual string GetISPCHostCompilerPath(UnrealTargetPlatform HostPlatform)
		{
			string ISPCCompilerPathCommon = Path.Combine(Unreal.EngineSourceDirectory.FullName, "ThirdParty", "Intel", "ISPC", "bin");
			string ISPCArchitecturePath = "";
			string ExeExtension = ".exe";

			if (UEBuildPlatform.IsPlatformInGroup(HostPlatform, UnrealPlatformGroup.Windows))
			{
				ISPCArchitecturePath = "Windows";
			}
			else if (HostPlatform == UnrealTargetPlatform.Linux)
			{
				ISPCArchitecturePath = "Linux";
				ExeExtension = "";
			}
			else if (HostPlatform == UnrealTargetPlatform.Mac)
			{
				ISPCArchitecturePath = "Mac";
				ExeExtension = "";
			}
			else
			{
				Logger.LogWarning("Unsupported ISPC host!");
			}

			return Path.Combine(ISPCCompilerPathCommon, ISPCArchitecturePath, "ispc" + ExeExtension);
		}

		/// <summary>
		/// Get the host bytecode-to-obj compiler path for ISPC. Only used for platforms that support compiling ISPC to LLVM bytecode
		/// </summary>
		/// <param name="HostPlatform">Which OS build platform is running on.</param>
		/// <returns>Path to bytecode to obj compiler</returns>
		public virtual string? GetISPCHostBytecodeCompilerPath(UnrealTargetPlatform HostPlatform)
		{
			// Return null if the platform toolchain doesn't support separate bytecode to obj compilation
			return null;
		}

		static readonly Dictionary<string, string> s_ISPCCompilerVersions = new Dictionary<string, string>();

		/// <summary>
		/// Returns the version of the ISPC compiler for the specified platform. If GetISPCHostCompilerPath() doesn't return a valid path
		/// this will return a -1 version.
		/// </summary>
		/// <param name="platform">Which OS build platform is running on.</param>
		/// <returns>Version reported by the ISPC compiler</returns>
		public virtual string GetISPCHostCompilerVersion(UnrealTargetPlatform platform)
		{
			string compilerPath = GetISPCHostCompilerPath(platform);
			if (!s_ISPCCompilerVersions.ContainsKey(compilerPath))
			{
				if (File.Exists(compilerPath))
				{
					s_ISPCCompilerVersions[compilerPath] = RunToolAndCaptureOutput(new FileReference(compilerPath), "--version", "(.*)")!;
				}
				else
				{
					Logger.LogWarning("No ISPC compiler at {CompilerPath}", compilerPath);
					s_ISPCCompilerVersions[compilerPath] = "-1";
				}
			}

			return s_ISPCCompilerVersions[compilerPath];
		}

		/// <summary>
		/// Get object file format for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS build platform is running on.</param>
		/// <returns>Object file suffix</returns>
		public virtual string GetISPCObjectFileFormat(UnrealTargetPlatform Platform)
		{
			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Apple) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Android))
			{
				return "obj";
			}

			Logger.LogWarning("Unsupported ISPC platform target!");
			return String.Empty;
		}

		/// <summary>
		/// Get object file suffix for ISPC.
		/// </summary>
		/// <param name="Platform">Which OS build platform is running on.</param>
		/// <returns>Object file suffix</returns>
		public virtual string GetISPCObjectFileSuffix(UnrealTargetPlatform Platform)
		{
			if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Windows))
			{
				return ".obj";
			}
			else if (UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Unix) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Apple) ||
				UEBuildPlatform.IsPlatformInGroup(Platform, UnrealPlatformGroup.Android))
			{
				return ".o";
			}

			Logger.LogWarning("Unsupported ISPC platform target!");
			return String.Empty;
		}

		private string EscapeDefinitionForISPC(string Definition)
		{
			// See: https://github.com/ispc/ispc/blob/4ee767560cd752eaf464c124eb7ef1b0fd37f1df/src/main.cpp#L264 for ispc's argument parsing code, which does the following (and does not support escaping):
			// Argument      Parses as 
			// "abc""def"    One agrument:  abcdef
			// "'abc'"       One argument:  'abc'
			// -D"X="Y Z""   Two arguments: -DX=Y and Z
			// -D'X="Y Z"'   One argument:  -DX="Y Z"  (i.e. with quotes in value)
			// -DX="Y Z"     One argument:  -DX=Y Z    (this is what we want on the command line)

			// Assumes that quotes at the start and end of the value string mean that everything between them should be passed on unchanged.

			int DoubleQuoteCount = Definition.Count(c => c == '"');
			bool bHasSingleQuote = Definition.Contains('\'');
			bool bHasSpace = Definition.Contains(' ');

			string Escaped = Definition;

			if (DoubleQuoteCount > 0 || bHasSingleQuote || bHasSpace)
			{
				int EqualsIndex = Definition.IndexOf('=');
				string Name = Definition[0..EqualsIndex];
				string Value = Definition[(EqualsIndex + 1)..];

				string UnquotedValue = Value;

				// remove one layer of quoting, if present
				if (Value.StartsWith('"') && Value.EndsWith('"') && Value.Length != 1)
				{
					UnquotedValue = Value[1..^1];
					DoubleQuoteCount -= 2;
				}

				if (DoubleQuoteCount == 0 && (bHasSingleQuote || bHasSpace))
				{
					Escaped = $"{Name}=\"{UnquotedValue}\"";
				}
				else if (!bHasSingleQuote && (bHasSpace || DoubleQuoteCount > 0))
				{
					// If there are no single quotes, we can use them to quote the value string
					Escaped = $"{Name}='{UnquotedValue}'";
				}
				else
				{
					// Treat all special chars in the value string as needing explicit extra quoting. Thoroughly clumsy.
					StringBuilder Requoted = new StringBuilder();
					foreach (char c in UnquotedValue)
					{
						if (c == '"')
						{
							Requoted.Append("'\"'");
						}
						else if (c == '\'')
						{
							Requoted.Append("\"'\"");
						}
						else if (c == ' ')
						{
							Requoted.Append("\" \"");
						}
						else
						{
							Requoted.Append(c);
						}
					}
					Escaped = $"{Name}={Requoted}";
				}
			}

			return Escaped;
		}

		/// <summary>
		/// Normalize a path for use in a command line, making it relative to Engine/Source if under the root directory
		/// </summary>
		/// <param name="Reference">The FileSystemReference to normalize</param>
		/// <returns>Normalized path as a string</returns>
		protected virtual string NormalizeCommandLinePath(FileSystemReference Reference)
		{
			string path = Reference.FullName;
			// Try to use a relative path to shorten command line length.
			if (Reference.IsUnderDirectory(Unreal.RootDirectory))
			{
				path = Reference.MakeRelativeTo(Unreal.EngineSourceDirectory);
			}
			if (Path.DirectorySeparatorChar == '/')
			{
				path = path.Replace("\\", "/");
			}
			else
			{
				path = path.Replace("\\", "\\\\");
			}
			return path;
		}

		/// <summary>
		/// Normalize a path for use in a command line, making it relative if under the Root Directory
		/// </summary>
		/// <param name="Item">The FileItem to normalize</param>
		/// <returns>Normalized path as a string</returns>
		protected virtual string NormalizeCommandLinePath(FileItem Item)
		{
			return NormalizeCommandLinePath(Item.Location);
		}

		protected virtual IEnumerable<DirectoryItem> GetEnvironmentBasePaths(CppCompileEnvironment CompileEnvironment)
		{
			yield return DirectoryItem.GetItemByDirectoryReference(Unreal.EngineDirectory);
			if (ProjectFile != null && (!CompileEnvironment.bUseSharedBuildEnvironment || CompileEnvironment.AllIncludePath.Any(x => x.IsUnderDirectory(ProjectFile.Directory))))
			{
				yield return DirectoryItem.GetItemByDirectoryReference(ProjectFile.Directory);
			}
			yield return DirectoryItem.GetItemByDirectoryReference(Unreal.RootDirectory);
		}

		protected virtual IEnumerable<DirectoryItem> GetEnvironmentBasePaths(LinkEnvironment LinkEnvironment)
		{
			yield return DirectoryItem.GetItemByDirectoryReference(Unreal.EngineDirectory);
			if (ProjectFile != null && LinkEnvironment.InputFiles.Any(x => x.Location.IsUnderDirectory(ProjectFile.Directory)))
			{
				yield return DirectoryItem.GetItemByDirectoryReference(ProjectFile.Directory);
			}
			yield return DirectoryItem.GetItemByDirectoryReference(Unreal.RootDirectory);
		}

		protected override CPPOutput GenerateISPCHeaders(CppCompileEnvironment CompileEnvironment, IEnumerable<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();

			if (!CompileEnvironment.bCompileISPC)
			{
				return Result;
			}

			List<string> CompileTargets = GetISPCCompileTargets(CompileEnvironment.Platform, CompileEnvironment.Architecture);

			List<string> GlobalArguments = new List<string>();

			// Build target string. No comma on last
			string TargetString = String.Join(',', CompileTargets);

			string ISPCArch = GetISPCArchTarget(CompileEnvironment.Platform, CompileEnvironment.Architecture);

			// Build target triplet
			GlobalArguments.Add($"--target-os={GetISPCOSTarget(CompileEnvironment.Platform)}");
			GlobalArguments.Add($"--arch={ISPCArch}");
			GlobalArguments.Add($"--target={TargetString}");
			GlobalArguments.Add($"--emit-{GetISPCObjectFileFormat(CompileEnvironment.Platform)}");

			string? CpuTarget = GetISPCCpuTarget(CompileEnvironment.Platform);
			if (!String.IsNullOrEmpty(CpuTarget))
			{
				GlobalArguments.Add($"--cpu={CpuTarget}");
			}

			// PIC is needed for modular builds except on Microsoft platforms, and for android
			if ((CompileEnvironment.bIsBuildingDLL ||
				CompileEnvironment.bIsBuildingLibrary) &&
				!UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Microsoft) ||
				UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Android))
			{
				GlobalArguments.Add("--pic");
			}

			// Include paths. Don't use AddIncludePath() here, since it uses the full path and exceeds the max command line length.
			// Because ISPC response files don't support white space in arguments, paths with white space need to be passed to the command line directly.
			foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
			{
				GlobalArguments.Add($"-I\"{NormalizeCommandLinePath(IncludePath)}\"");
			}

			// System include paths.
			foreach (DirectoryReference SystemIncludePath in CompileEnvironment.SystemIncludePaths)
			{
				GlobalArguments.Add($"-I\"{NormalizeCommandLinePath(SystemIncludePath)}\"");
			}

			// Preprocessor definitions.
			foreach (string Definition in CompileEnvironment.Definitions)
			{
				// TODO: Causes ISPC compiler to generate a spurious warning about the universal character set
				if (!Definition.Contains("\\\\U") && !Definition.Contains("\\\\u"))
				{
					GlobalArguments.Add($"-D{EscapeDefinitionForISPC(Definition)}");
				}
			}

			List<DirectoryItem> RootPaths = new(GetEnvironmentBasePaths(CompileEnvironment));

			foreach (FileItem ISPCFile in InputFiles)
			{
				Action CompileAction = Graph.CreateAction(ActionType.Compile);
				CompileAction.RootPaths.AddRange(RootPaths);

				CompileAction.CommandDescription = $"Generate Header [{ISPCArch}]";
				CompileAction.WorkingDirectory = Unreal.EngineSourceDirectory;
				CompileAction.CommandPath = new FileReference(GetISPCHostCompilerPath(BuildHostPlatform.Current.Platform));
				CompileAction.StatusDescription = Path.GetFileName(ISPCFile.AbsolutePath);
				CompileAction.CommandVersion = GetISPCHostCompilerVersion(BuildHostPlatform.Current.Platform).ToString();
				CompileAction.ArtifactMode = ArtifactMode.Enabled;

				CompileAction.bCanExecuteRemotely = true;

				// Disable remote execution to workaround mismatched case on XGE
				CompileAction.bCanExecuteRemotelyWithXGE = false;

				// TODO: Remove, might work
				CompileAction.bCanExecuteRemotelyWithSNDBS = false;

				List<string> Arguments = new List<string>();

				// Add the ISPC obj file as a prerequisite of the action.
				Arguments.Add($"\"{NormalizeCommandLinePath(ISPCFile)}\"");

				// Add the ISPC h file to the produced item list.
				FileItem ISPCIncludeHeaderFile = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(ISPCFile.AbsolutePath) + ".generated.dummy.h"
						)
					);

				// Add the ISPC file to be compiled.
				Arguments.Add($"-h \"{NormalizeCommandLinePath(ISPCIncludeHeaderFile)}\"");

				// Generate the included header dependency list
				FileItem DependencyListFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, Path.GetFileName(ISPCFile.AbsolutePath) + ".txt"));
				Arguments.Add($"-MMM \"{NormalizeCommandLinePath(DependencyListFile)}\"");
				CompileAction.DependencyListFile = DependencyListFile;
				CompileAction.ProducedItems.Add(DependencyListFile);

				Arguments.AddRange(GlobalArguments);

				CompileAction.ProducedItems.Add(ISPCIncludeHeaderFile);

				FileReference ResponseFileName = GetResponseFileName(CompileEnvironment, ISPCIncludeHeaderFile);
				FileItem ResponseFileItem = Graph.CreateIntermediateTextFile(ResponseFileName, Arguments.Select(x => Utils.ExpandVariables(x)));
				CompileAction.CommandArguments = $"@\"{NormalizeCommandLinePath(ResponseFileName)}\"";
				CompileAction.PrerequisiteItems.Add(ResponseFileItem);

				// Add the source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(ISPCFile);

				FileItem ISPCFinalHeaderFile = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(ISPCFile.AbsolutePath) + ".generated.h"
						)
					);

				// Fix interrupted build issue by copying header after generation completes
				Action CopyAction = Graph.CreateCopyAction(ISPCIncludeHeaderFile, ISPCFinalHeaderFile);
				CopyAction.CommandDescription = $"{CopyAction.CommandDescription} [{ISPCArch}]";
				CopyAction.DeleteItems.Clear();
				CopyAction.PrerequisiteItems.Add(ISPCFile);
				CopyAction.bShouldOutputStatusDescription = false;

				Result.GeneratedHeaderFiles.Add(ISPCFinalHeaderFile);

				Logger.LogDebug("   ISPC Generating Header {StatusDescription}: \"{CommandPath}\" {CommandArguments}", CompileAction.StatusDescription, CompileAction.CommandPath, CompileAction.CommandArguments);
			}

			return Result;
		}

		protected override CPPOutput CompileISPCFiles(CppCompileEnvironment CompileEnvironment, IEnumerable<FileItem> InputFiles, DirectoryReference OutputDir, IActionGraphBuilder Graph)
		{
			CPPOutput Result = new CPPOutput();

			if (!CompileEnvironment.bCompileISPC)
			{
				return Result;
			}

			List<string> CompileTargets = GetISPCCompileTargets(CompileEnvironment.Platform, CompileEnvironment.Architecture);

			List<string> GlobalArguments = new List<string>();

			// Build target string. No comma on last
			string TargetString = "";
			foreach (string Target in CompileTargets)
			{
				if (Target == CompileTargets[^1]) // .Last()
				{
					TargetString += Target;
				}
				else
				{
					TargetString += Target + ",";
				}
			}

			// Build target triplet
			string PlatformObjectFileFormat = GetISPCObjectFileFormat(CompileEnvironment.Platform);
			string ISPCArch = GetISPCArchTarget(CompileEnvironment.Platform, CompileEnvironment.Architecture);

			GlobalArguments.Add($"--target-os={GetISPCOSTarget(CompileEnvironment.Platform)}");
			GlobalArguments.Add($"--arch={ISPCArch}");
			GlobalArguments.Add($"--target={TargetString}");
			GlobalArguments.Add($"--emit-{PlatformObjectFileFormat}");

			string? CpuTarget = GetISPCCpuTarget(CompileEnvironment.Platform);
			if (!String.IsNullOrEmpty(CpuTarget))
			{
				GlobalArguments.Add($"--cpu={CpuTarget}");
			}

			bool bByteCodeOutput = (PlatformObjectFileFormat == "llvm");

			List<string> CommonArgs = new List<string>();
			if (CompileEnvironment.Configuration == CppConfiguration.Debug)
			{
				if (CompileEnvironment.Platform == UnrealTargetPlatform.Mac)
				{
					// Turn off debug symbols on Mac due to dsym generation issue
					CommonArgs.Add("-O0");
					// Ideally we would be able to turn on symbols and specify the dwarf version, but that does
					// does not seem to be working currently, ie:
					//    GlobalArguments.Add("-g -O0 --dwarf-version=2");

				}
				else
				{
					CommonArgs.Add("-g -O0");
				}
			}
			else
			{
				CommonArgs.Add("-O3");
			}
			GlobalArguments.AddRange(CommonArgs);

			// PIC is needed for modular builds except on Microsoft platforms, and for android
			if ((CompileEnvironment.bIsBuildingDLL ||
				CompileEnvironment.bIsBuildingLibrary) &&
				!UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Microsoft) ||
				UEBuildPlatform.IsPlatformInGroup(CompileEnvironment.Platform, UnrealPlatformGroup.Android))
			{
				GlobalArguments.Add("--pic");
			}

			// Include paths. Don't use AddIncludePath() here, since it uses the full path and exceeds the max command line length.
			foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
			{
				GlobalArguments.Add($"-I\"{NormalizeCommandLinePath(IncludePath)}\"");
			}

			// System include paths.
			foreach (DirectoryReference SystemIncludePath in CompileEnvironment.SystemIncludePaths)
			{
				GlobalArguments.Add($"-I\"{NormalizeCommandLinePath(SystemIncludePath)}\"");
			}

			// Preprocessor definitions.
			foreach (string Definition in CompileEnvironment.Definitions)
			{
				// TODO: Causes ISPC compiler to generate a spurious warning about the universal character set
				if (!Definition.Contains("\\\\U") && !Definition.Contains("\\\\u"))
				{
					GlobalArguments.Add($"-D{EscapeDefinitionForISPC(Definition)}");
				}
			}

			List<DirectoryItem> RootPaths = new(GetEnvironmentBasePaths(CompileEnvironment));

			foreach (FileItem ISPCFile in InputFiles)
			{
				Action CompileAction = Graph.CreateAction(ActionType.Compile);
				CompileAction.RootPaths.AddRange(RootPaths);
				CompileAction.CommandDescription = $"Compile [{ISPCArch}]";
				CompileAction.WorkingDirectory = Unreal.EngineSourceDirectory;
				CompileAction.CommandPath = new FileReference(GetISPCHostCompilerPath(BuildHostPlatform.Current.Platform));
				CompileAction.StatusDescription = Path.GetFileName(ISPCFile.AbsolutePath);
				CompileAction.CommandVersion = GetISPCHostCompilerVersion(BuildHostPlatform.Current.Platform).ToString();
				if (bAllowUbaCompression)
				{
					CompileAction.CommandVersion = $"{CompileAction.CommandVersion} Compressed";
				}

				CompileAction.bCanExecuteRemotely = true;

				// Disable remote execution to workaround mismatched case on XGE
				CompileAction.bCanExecuteRemotelyWithXGE = false;

				// TODO: Remove, might work
				CompileAction.bCanExecuteRemotelyWithSNDBS = false;

				CompileAction.ArtifactMode = ArtifactMode.Enabled;

				List<string> Arguments = new List<string>();

				// Add the ISPC file to be compiled.
				Arguments.Add($"\"{NormalizeCommandLinePath(ISPCFile)}\"");

				List<FileItem> CompiledISPCObjFiles = new List<FileItem>();

				string FileName = Path.GetFileName(ISPCFile.AbsolutePath);

				string CompiledISPCObjFileSuffix = bByteCodeOutput ? ".bc" : GetISPCObjectFileSuffix(CompileEnvironment.Platform);
				foreach (string Target in CompileTargets)
				{
					string ObjTarget = Target;

					if (Target.Contains('-'))
					{
						// Remove lane width and gang size from obj file name
						ObjTarget = Target.Split('-')[0];
					}

					FileItem CompiledISPCObjFile;

					if (CompileTargets.Count > 1)
					{
						CompiledISPCObjFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							FileName + "_" + ObjTarget + CompiledISPCObjFileSuffix
							)
						);
					}
					else
					{
						CompiledISPCObjFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							FileName + CompiledISPCObjFileSuffix
							)
						);
					}

					// Add the ISA specific ISPC obj files to the produced item list.
					CompiledISPCObjFiles.Add(CompiledISPCObjFile);
				}

				// Add the common ISPC obj file to the produced item list if it's not already in it
				FileItem CompiledISPCObjFileNoISA = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						FileName + CompiledISPCObjFileSuffix
						)
					);

				if (CompileTargets.Count > 1)
				{
					CompiledISPCObjFiles.Add(CompiledISPCObjFileNoISA);
				}

				// Add the output ISPC obj file
				Arguments.Add($"-o \"{NormalizeCommandLinePath(CompiledISPCObjFileNoISA)}\"");

				// Generate the timing info
				if (CompileEnvironment.bPrintTimingInfo)
				{
					FileItem TraceFile = FileItem.GetItemByFileReference(FileReference.FromString($"{CompiledISPCObjFileNoISA}.json"));
					Arguments.Add("--time-trace");
					CompileAction.ProducedItems.Add(TraceFile);
				}

				Arguments.AddRange(GlobalArguments);

				// Consume the included header dependency list
				FileItem DependencyListFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, FileName + ".txt"));
				CompileAction.DependencyListFile = DependencyListFile;
				CompileAction.PrerequisiteItems.Add(DependencyListFile);

				CompileAction.ProducedItems.UnionWith(CompiledISPCObjFiles);

				FileReference ResponseFileName = GetResponseFileName(CompileEnvironment, CompiledISPCObjFileNoISA);
				FileItem ResponseFileItem = Graph.CreateIntermediateTextFile(ResponseFileName, Arguments.Select(x => Utils.ExpandVariables(x)));

				string AdditionalArguments = "";
				// Must be added after response file is created just to make sure it ends up on the command line and not in the response file
				if (!bByteCodeOutput && bMergeModules)
				{
					// EXTRACTEXPORTS can only be interpreted by UBA.. so this action won't build outside uba
					AdditionalArguments = " /EXTRACTEXPORTS";
					CompileAction.ProducedItems.Add(FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, FileName + ".exi")));
				}

				CompileAction.CommandArguments = $"@\"{ResponseFileName}\"{AdditionalArguments}";
				CompileAction.PrerequisiteItems.Add(ResponseFileItem);

				// Add the source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(ISPCFile);

				Logger.LogDebug("   ISPC Compiling {StatusDescription}: \"{CommandPath}\" {CommandArguments}", CompileAction.StatusDescription, CompileAction.CommandPath, CompileAction.CommandArguments);

				if (bByteCodeOutput)
				{
					// If the platform toolchain supports bytecode compilation for ISPC, compile the bytecode object files to actual native object files 
					string? ByteCodeCompilerPath = GetISPCHostBytecodeCompilerPath(BuildHostPlatform.Current.Platform);
					if (ByteCodeCompilerPath != null)
					{
						List<FileItem> FinalObjectFiles = new List<FileItem>();
						foreach (FileItem CompiledBytecodeObjFile in CompiledISPCObjFiles)
						{
							string FileNameWithoutExtension = Path.GetFileNameWithoutExtension(CompiledBytecodeObjFile.AbsolutePath);
							FileItem FinalCompiledISPCObjFile = FileItem.GetItemByFileReference(
								FileReference.Combine(
									OutputDir,
									FileNameWithoutExtension + GetISPCObjectFileSuffix(CompileEnvironment.Platform)
									)
								);

							Action PostCompileAction = Graph.CreateAction(ActionType.Compile);

							List<string> PostCompileArgs = new List<string>();
							PostCompileArgs.Add($"\"{NormalizeCommandLinePath(CompiledBytecodeObjFile)}\"");
							PostCompileArgs.Add("-c");
							PostCompileArgs.AddRange(CommonArgs);
							PostCompileArgs.Add($"-o \"{NormalizeCommandLinePath(FinalCompiledISPCObjFile)}\"");

							if (bMergeModules)
							{
								// EXTRACTEXPORTS can only be interpreted by UBA.. so this action won't build outside uba
								AdditionalArguments = " /EXTRACTEXPORTS";
								PostCompileAction.ProducedItems.Add(FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, FileNameWithoutExtension + ".exi")));
							}

							// Write the args to a response file
							FileReference PostCompileResponseFileName = GetResponseFileName(CompileEnvironment, FinalCompiledISPCObjFile);
							FileItem PostCompileResponseFileItem = Graph.CreateIntermediateTextFile(PostCompileResponseFileName, PostCompileArgs.Select(x => Utils.ExpandVariables(x)));
							PostCompileAction.CommandArguments = $"@\"{PostCompileResponseFileName}\"{AdditionalArguments}";
							PostCompileAction.PrerequisiteItems.Add(PostCompileResponseFileItem);

							PostCompileAction.PrerequisiteItems.Add(CompiledBytecodeObjFile);
							PostCompileAction.ProducedItems.Add(FinalCompiledISPCObjFile);
							PostCompileAction.CommandDescription = $"CompileByteCode [{ISPCArch}]";
							PostCompileAction.WorkingDirectory = Unreal.EngineSourceDirectory;
							PostCompileAction.CommandPath = new FileReference(ByteCodeCompilerPath);
							PostCompileAction.StatusDescription = Path.GetFileName(ISPCFile.AbsolutePath);
							CompileAction.CommandVersion = GetISPCHostCompilerVersion(BuildHostPlatform.Current.Platform).ToString();
							if (bAllowUbaCompression)
							{
								CompileAction.CommandVersion = $"{CompileAction.CommandVersion} Compressed";
							}

							PostCompileAction.RootPaths.AddRange(RootPaths);
							PostCompileAction.ArtifactMode = ArtifactMode.Enabled;

							// Disable remote execution to workaround mismatched case on XGE
							PostCompileAction.bCanExecuteRemotelyWithXGE = false;

							FinalObjectFiles.Add(FinalCompiledISPCObjFile);
							Logger.LogDebug("   ISPC Compiling bytecode {StatusDescription}: \"{CommandPath}\" {CommandArguments} {ProducedItems}", PostCompileAction.StatusDescription, PostCompileAction.CommandPath, PostCompileAction.CommandArguments, PostCompileAction.ProducedItems);
						}
						// Override the output object files
						CompiledISPCObjFiles = FinalObjectFiles;
					}
				}

				Result.ObjectFiles.AddRange(CompiledISPCObjFiles);
			}

			return Result;
		}
	}
}
