// Copyright Epic Games, Inc. All Rights Reserved.

using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Threading;

namespace Gauntlet
{
	/// <summary>
	/// Helper library for all RPCs not specific to Fortnite. This will eventually be moved to the engine side of gauntlet once 
	/// we feel the RPC system is ready for prime time.
	/// </summary>
	public class RpcLibrary
	{
		public static bool ValidateTarget(RpcTarget InTarget)
		{
			try
			{
				if (InTarget == null)
				{
					throw new TestException("RPC Target Passed in is null.");
				}

				return true;
			}
			catch
			{
				throw;
			}
		}

		/// <summary>
		/// Method to check the returned HttpResponseMessage to ensure it is valid.
		/// </summary>
		/// <param name="InResponse">Response Object</param>
		/// <returns></returns>
		public static bool ValidateResponse(RpcTarget InTarget, HttpResponseMessage InResponse)
		{
			try
			{
				if (InResponse == null)
				{
					throw new TestException($"No HTTP Response from Target '{InTarget.TargetName}'. Process may not be responding or the RPC failed.");
				}

				return true;
			}
			catch
			{
				throw;
			}
		}

		/// <summary>
		/// Generic way to validate and deserialize an HttpResponseMessage into any type that extends FortBaseRpcHttpResponse.
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="InResponse">Response object received from RPC call</param>
		/// <param name="bExceptionOnFailure">Throw a TestException on failure</param>
		/// <returns>Deserialized response object</returns>
		/// <exception cref="TestException">When InResponse is null or when bExceptionOnFailure is true and RPC call was unsuccessful</exception>
		public static T ValidateResponse<T>(RpcTarget InTarget, HttpResponseMessage InResponse, bool bExceptionOnFailure = false) where T : FortBaseRpcHttpResponse
		{
			if (!ValidateResponse(InTarget, InResponse))
			{
				throw new TestException("No HTTP Response from Target. Process may not be responding or the RPC failed.");
			}

			T ResponseObject = RpcLibrary.GetResponseAsObject<T>(InResponse);

			if (bExceptionOnFailure && !ResponseObject.WasSuccessful)
			{
				if (!string.IsNullOrEmpty(ResponseObject.Message))
				{
					throw new TestException($"{ResponseObject.Message}");
				}
				else
				{
					throw new TestException("Response Validation failed for unknown reason.");
				}
			}
			InTarget.LogInfo(ResponseObject.Message);
			return ResponseObject;
		}

		public static string GetResponseAsString(HttpResponseMessage InResponse, bool bExceptionIfEmpty = false)
		{
			try
			{
				StreamReader responseReader = new StreamReader(InResponse.Content.ReadAsStream());
				string ResponseString = responseReader.ReadToEnd();

				if (bExceptionIfEmpty && string.IsNullOrEmpty(ResponseString))
				{
					throw new TestException("Response String is empty!");
				}
				return ResponseString;
			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				string ErrorMsg = string.Format("Failed to Get Response as String. Message: {0}, Stacktrace: {1}", Ex.Message, Ex.StackTrace);
				Log.Error(ErrorMsg);
				throw new TestException(ErrorMsg);
			}
		}

		/// <summary>
		/// Generic way to deserialize a HttpResponseMessage to your desired type. Handles null exceptions.
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="InResponse">Response object received from RPC call</param>
		/// <returns>Deserialized response object</returns>
		/// <exception cref="TestException">When deserialization fails and return object would be null</exception>
		public static T GetResponseAsObject<T>(HttpResponseMessage InResponse)
		{
			try
			{
				string ResponseString = GetResponseAsString(InResponse);
				T ResponseObject = JsonConvert.DeserializeObject<T>(ResponseString);

				if (ResponseObject == null)
				{
					throw new TestException("Failed to deserialize HTTP Response!");
				}

				return ResponseObject;
			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				string ErrorMsg = string.Format("Failed to Get Response as Object. Message: {0}, Stacktrace: {1}", Ex.Message, Ex.StackTrace);
				Log.Error(ErrorMsg);
				throw new TestException(ErrorMsg);
			}

		}

		public static bool DoWhile(string FuncName, Func<bool> RpcCall, 
			int DelayInMilliseconds = 5 * 1000, int TimeoutInMilliseconds = 60 * 1000, bool bSuppressExceptions = false)
		{
			Stopwatch StopTimer = Stopwatch.StartNew();
			while (true)
			{
				try
				{
					// Do while RpcCall() returns true
					if (!RpcCall())
					{
						return true; // Return true to caller to signal we are done
					}
				}
				catch (TestException)
				{
					if (!bSuppressExceptions)
					{
						throw;
					}
				}

				if (StopTimer.ElapsedMilliseconds > TimeoutInMilliseconds)
				{
					throw new TestException($"{FuncName} timed out after {TimeoutInMilliseconds}ms");
				}

				Thread.Sleep(DelayInMilliseconds);
			}
		}

		public static bool DoUntil(string FuncName, Func<bool> RpcCall,
			int DelayInMilliseconds = 5 * 1000, int TimeoutInMilliseconds = 60 * 1000, bool bSuppressExceptions = false)
		{
			return DoWhile(FuncName, () => { return !RpcCall(); }, DelayInMilliseconds, TimeoutInMilliseconds, bSuppressExceptions);
		}

		public static bool ExecuteRpcCheckSuccess(RpcTarget InTarget, string RpcName, Dictionary<string, object> Args = null)
		{
			RpcLibrary.ValidateTarget(InTarget);
			InTarget.LogInfo($"Calling {RpcName}");

			HttpResponseMessage Response = RpcExecutor.CallRpc(InTarget, RpcName, InArgs: Args);
			RpcLibrary.ValidateResponse(InTarget, Response);

			FortBaseRpcHttpResponse ResponseObject = RpcLibrary.GetResponseAsObject<FortBaseRpcHttpResponse>(Response);
			if (ResponseObject.WasSuccessful)
			{
				return true;
			}
			else if (!string.IsNullOrEmpty(ResponseObject.Message))
			{
				throw new TestException($"{ResponseObject.Message}");
			}
			else
			{
				throw new TestException($"Unknown failure in {RpcName}");
			}
		}

		public static bool RunCheatCommand(RpcTarget InTarget, string CheatString)
		{
			if (InTarget == null)
			{
				return false;
			}
			Dictionary<string, object> RequestArgs = new Dictionary<string, object>();
			RequestArgs.Add("command", CheatString);
			HttpResponseMessage Response = RpcExecutor.CallRpc(InTarget, "CheatCommand", 5000, RequestArgs);
			return Response != null && Response.IsSuccessStatusCode;
		}


		public static List<RpcLedgerEntry> GetRequestHistory(RpcTarget InTarget)
		{ 
			if (InTarget == null)
			{
				return null;
			}

			List<RpcLedgerEntry> RequestHistory = null;
			using (HttpResponseMessage RpcResponse = RpcExecutor.CallRpc(InTarget, "GetRequestHistory", 5000))
			{
				StreamReader responseReader = new StreamReader(RpcResponse.Content.ReadAsStream());
				string ResponseString = responseReader.ReadToEnd();
				JsonSerializerSettings SerializerSettings = new JsonSerializerSettings();
				SerializerSettings.Formatting = Formatting.Indented;
				RequestHistory = JsonConvert.DeserializeObject<List<RpcLedgerEntry>>(ResponseString, SerializerSettings);
			}

			return RequestHistory;
		}

		public static bool RunAndWaitForCheatCommand(RpcTarget InTarget, string CheatString)
        {
        	if (InTarget == null)
        	{
        		return false;
        	}
	        
        	Dictionary<string, object> RequestArgs = new Dictionary<string, object>();
        	RequestArgs.Add("command", CheatString);

	        using (HttpResponseMessage RpcResponse = RpcExecutor.CallRpc(InTarget, "CheatCommand", 5000, RequestArgs))
	        {
		        if (RpcResponse == null)
		        {
			        Log.Info($"No response from CheatCommand with argument: {CheatString}");
			        return false;
		        }
	        }

	        return true;
        }

		public static bool ExecuteConsoleCommand(RpcTarget InTarget, string ConsoleCommandString)
		{
			if (InTarget == null)
			{
				return false;
			}
			Dictionary<string, object> RequestArgs = new Dictionary<string, object>();
			RequestArgs.Add("Command", ConsoleCommandString);
			RpcExecutor.CallRpc(InTarget, "ExecuteConsoleCommand", 5000, RequestArgs);
			return true;
		}

		public static bool ExitClient(RpcTarget InTarget)
		{
			if (InTarget == null)
			{
				return false;
			}
			RpcExecutor.CallRpc(InTarget, "BotExitClient");
			return true;
		}

		public static bool QuitEditor(RpcTarget InTarget)
		{
			try
			{
				ValidateTarget(InTarget);

				InTarget.LogInfo("Calling Quit Editor RPC.");

				HttpResponseMessage Response = RpcExecutor.CallRpc(InTarget, "QuitEditor");

				ValidateResponse(InTarget, Response);

				FortBaseRpcHttpResponse ResponseObject = GetResponseAsObject<FortBaseRpcHttpResponse>(Response);

				if (ResponseObject.WasSuccessful == false)
				{
					if (!string.IsNullOrEmpty(ResponseObject.Message))
					{
						throw new TestException("QuitEditor failed! Error Reported from Editor: {0}", ResponseObject.Message);
					}

					throw new TestException("QuitEditor Failed, but no error message was reported.");
				}

				if (!string.IsNullOrEmpty(ResponseObject.Message))
				{
					Log.Info("Response from Editor: {0}", ResponseObject.Message);
				}

				// Wait a bit for the editor to shut itself down
				Thread.Sleep(10000);

				return true;

			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				throw new TestException(InTarget.LogError("QuitEditor RPC failed due to an exception. Message: {0}, Stacktrace: {1}", Ex.Message, Ex.StackTrace));
			}
		}

		public static bool CheckFEngineLoopInitComplete(RpcTarget InTarget)
		{
			try
			{
				ValidateTarget(InTarget);

				HttpResponseMessage Response = RpcExecutor.CallRpc(InTarget, "CheckFEngineLoopInitComplete");

				// This RPC may be called when the targets are not yet ready, so we don't exception on failure.
				if (Response == null)
				{
					InTarget.LogWarning("RPC Target {0} did not respond or refused HTTP connection.", InTarget.TargetName);
					return false;
				}

				FortBaseRpcHttpResponse ResponseObject = GetResponseAsObject<FortBaseRpcHttpResponse>(Response);

				if (ResponseObject.WasSuccessful == false && !string.IsNullOrEmpty(ResponseObject.Message))
				{
					throw new TestException("CheckFEngineLoopInitComplete failed! Error: {0}", ResponseObject.Message);
				}

				return ResponseObject.WasSuccessful;

			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				throw new TestException(InTarget.LogError("CheckFEngineLoopInitComplete RPC failed due to an unhandled exception. Message: {0}, Stacktrace: {1}", Ex.Message, Ex.StackTrace));
			}
		}

		/// <summary>
		/// Waits for a message to be received from a RPC Target (Unreal process). 
		/// This is a blocking method.
		/// </summary>
		/// <param name="InTarget">Target we are waiting for a message from</param>
		/// <param name="MessageCategory">Category of message</param>
		/// <param name="MessageContents">Some or all of the text in the message payload</param>
		/// <param name="TimeoutInSeconds">Total time to wait for the message </param>
		/// <param name="TimeBetweenRetriesInSeconds">How long to wait in between retries</param>
		public static void WaitForIncomingTargetMessage(RpcTarget InTarget, string MessageCategory,
			string MessageContents, int TimeoutInSeconds = 300, int TimeBetweenRetriesInSeconds = 60)
		{
			WaitForIncomingTargetMessage(new List<RpcTarget>(new RpcTarget[] { InTarget }), MessageCategory, MessageContents,
				TimeoutInSeconds, TimeBetweenRetriesInSeconds);
		}

		/// <summary>
		/// Waits for the same message to be received from multiple RPC Targets (Unreal processes). Will wait
		/// until all messages for all processes have been received. This is a blocking method.
		/// </summary>
		/// <param name="InTargets">Target we are waiting for a message from</param>
		/// <param name="MessageCategory">Category of message</param>
		/// <param name="MessageContents">Some or all of the text in the message payload</param>
		/// <param name="TimeoutInSeconds">Total time to wait for the message </param>
		/// <param name="TimeBetweenRetriesInSeconds">How long to wait in between retries</param>
		public static void WaitForIncomingTargetMessage(List<RpcTarget> InTargets, string MessageCategory,
		string MessageContents, int TimeoutInSeconds = 300, int TimeBetweenRetriesInSeconds = 60)
		{
			int TotalTargets = InTargets.Count;
			int TotalMatches = 0;

			DateTime TimeoutEnd = DateTime.UtcNow.AddSeconds(TimeoutInSeconds);

			Log.Info("Beginning wait for {0} RPC Target(s) to send a message. Category: {1}, Contents: {2}", TotalTargets, MessageCategory, MessageContents);
			Log.Info("Timeout will occur after {0} seconds ({1}).", TimeoutInSeconds, TimeoutEnd.ToShortTimeString());

			while (TotalMatches < TotalTargets && DateTime.UtcNow < TimeoutEnd)
			{
				TotalMatches += RpcExecutor.Instance.CheckIncomingMessages(InTargets, MessageCategory, MessageContents, true);

				Log.Info("{0} out of {1} targets have sent the requested message.", TotalMatches, TotalTargets);

				if (TotalMatches < TotalTargets)
				{
					Log.Info("Not all targets have reported ready. Will check again in {0} seconds", TimeBetweenRetriesInSeconds);
					Thread.Sleep(TimeBetweenRetriesInSeconds * 1000);
				}
			}

			if (DateTime.UtcNow >= TimeoutEnd)
			{
				string ErrorMsg = string.Format("Timed out waiting for targets to send requested message. {0} targets reported ready before timeout.", TotalMatches);
				Log.Error(ErrorMsg);
				throw new TestException(ErrorMsg);
			}

			Log.Info("Message received from all RPC Targets!");
		}

		#region Target Setup and Ready
		/// <summary>
		/// Polls all RPC Targets to see if they are ready. Specifically for platforms that cannot send
		/// messages to Gauntlet.
		/// </summary>
		/// <param name="TimeoutInSeconds"></param>
		/// <param name="TimeBetweenRetriesInSeconds"></param>
		/// <exception cref="TestException"></exception>
		public static void PollAllTargetsForReady(List<RpcTarget> InTargets, int TimeoutInSeconds = 300, int TimeBetweenRetriesInSeconds = 60)
		{
			try
			{
				int TotalTargets = InTargets.Count;
				int TargetsReportingReady = 0;

				DateTime TimeoutEnd = DateTime.UtcNow.AddSeconds(TimeoutInSeconds);

				while (TargetsReportingReady < TotalTargets && DateTime.UtcNow < TimeoutEnd)
				{
					TargetsReportingReady = 0;
					Log.Info("Polling all RPC targets to check if ready.");

					foreach (RpcTarget Target in InTargets)
					{
						if (CheckFEngineLoopInitComplete(Target))
						{
							TargetsReportingReady++;
						}
					}

					if (TargetsReportingReady < TotalTargets)
					{
						Log.Warning("Not all processes have reported ready. Will check again in {0} seconds", TimeBetweenRetriesInSeconds);
						Thread.Sleep(TimeBetweenRetriesInSeconds * 1000);
					}
				}

				if (DateTime.UtcNow >= TimeoutEnd)
				{
					string ErrorMsg = string.Format("Polling timed out waiting for targets to be ready. {0} targets reported ready before timeout.", TargetsReportingReady);
					Log.Error(ErrorMsg);
					throw new TestException(ErrorMsg);
				}

				Log.Info("All RPC Targets Reported Ready!");
			}
			catch (TestException)
			{
				throw;
			}
			catch (Exception Ex)
			{
				string ErrorMsg = string.Format("PollAllTargetsForReady failed due to an unhandled exception. Message: {0}, Stacktrace: {1}", Ex.Message, Ex.StackTrace);
				Log.Error(ErrorMsg);
				throw new TestException(ErrorMsg);
			}

		}

		public static void WaitForAllTargetsToReportReady(List<RpcTarget> InTargets, int TimeoutInSeconds = 600, int TimeBetweenRetriesInSeconds = 60)
		{
			string ReadyMessageCategory = "TargetStatus";
			string ReadyMessageContents = "FEngineLoopInitComplete";

			Log.Info("Starting wait for all targets to report ready.");
			WaitForIncomingTargetMessage(InTargets, ReadyMessageCategory, ReadyMessageContents, TimeoutInSeconds, TimeBetweenRetriesInSeconds);
		}

		/// <summary>
		/// Blocking method that polls all targets for endpoints with configurable retry intervals and timeout.
		/// </summary>
		/// <param name="InTargets">All targts to get endpoints for</param>
		/// <param name="TimeoutInSeconds">Total time the method is allowed to run for in seconds. Default is 300 (5 minutes)</param>
		/// <param name="TimeBetweenRetriesInSeconds">Interval to wait between retries in seconds. Default is 60 (1 minute)</param>
		/// <exception cref="TestException"></exception>
		public static void WaitForAllTargetsToGetEndpoints(List<RpcTarget> InTargets, int TimeoutInSeconds = 300, int TimeBetweenRetriesInSeconds = 60)
		{
			int TotalTargets = InTargets.Count;
			int TargetsThatHaveRpcs = 0;
			DateTime TimeoutEnd = DateTime.UtcNow.AddSeconds(TimeoutInSeconds);

			// Assuming that all RPC-enabled processes need to have RPC's.
			while (DateTime.UtcNow < TimeoutEnd)
			{
				TargetsThatHaveRpcs = 0;

				foreach (RpcTarget Target in InTargets)
				{
					Target.LogInfo("Attempting to get RPC's from Remote Process");

					// target already retrieved RPC's, don't re-query
					if (Target.AvailableRPCs.Count > 0)
					{
						Target.LogInfo("{0} already has RPC's. Going to next target.", Target.TargetName);
						TargetsThatHaveRpcs++;
						continue;
					}

					// Update Available RPCs for Target
					if (Target.UpdateAvailableRpcs())
					{
						Target.LogInfo("{0} Retrieved RPCs! Count: {1}", Target.TargetName, Target.AvailableRPCs.Count);
						TargetsThatHaveRpcs++;
						continue;
					}

					Target.LogWarning("Failed To Get RPC List from Remote Process.");
				}

				if (TargetsThatHaveRpcs == TotalTargets)
				{
					Log.Info("All Processes Have retrieved RPC's.");
					return;
				}

				Log.Warning("Could not update RPC Registry for all expected processes. Will try again in {0} seconds.", TimeBetweenRetriesInSeconds);

				Thread.Sleep(TimeBetweenRetriesInSeconds * 1000);
			}

			string ErrorMsg = string.Format("Failed to retrieve endpoints from one or more processes. {0} targets retrieved RPCs before the operation timed out.", TargetsThatHaveRpcs);
			Log.Error(ErrorMsg);
			throw new TestException(ErrorMsg);
		}
		#endregion Target Setup and Ready
	}

	public static class RpcLibraryExtensions
	{
		public static bool SaveDirtyAssets(this RpcTarget InTarget)
		{
			return RpcLibrary.ExecuteRpcCheckSuccess(InTarget, "EditorSaveDirtyPackages");
		}

		public static bool AutomationControllerClearReports(this RpcTarget InTarget)
		{
			return RpcLibrary.ExecuteRpcCheckSuccess(InTarget, "AutomationControllerClearReports");
		}
	}

	/// <summary>
	/// Common base RPC HTTP response class with minimal information that can be inherited
	/// from for more complex responses.
	/// Helper macros in UE are in FortBaseExternalRpcHttpResponse.h
	/// </summary>
	public class FortBaseRpcHttpResponse
	{
		public bool WasSuccessful = false;
		public string Message = string.Empty;
	}
}