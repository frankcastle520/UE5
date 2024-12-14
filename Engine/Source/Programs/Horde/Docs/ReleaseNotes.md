# Release Notes

## 2024-10-10

* EpicGames.Perforce: Assume and mark workspace having untracked files prior to starting a sync (36972802)
* Allow HTTP for OIDC discovery endpoints
* Log errors when mongodb config/log files contain non-latin characters as mongodb.exe cannot handle these paths (36930465)
* Improve description text for several tools bundled with Horde. Mention that P4V needs to be restarted after installing P4VUtils. (36912499)
* Use P4 syntax on artifact modal to download current folder (36912473)
* Fix issues with theming and login/setup view for installer build (36907272)
* Don't add all files to filter when there is no selection in the artifact modal to avoid overflowing the max request length (36906929)
* Add Horde's built-in user ID claim when issuing OIDC userinfo for Horde accounts (36899176)
* Ensure auto-conforming is requested only when not already pending (36894611)
* Distinguish between no matching compute resources vs all resources in use (36857071)
* Allow resetting blob id for length scan. (36848920)
* Add schedule auditing (36825106)
* Prevent infinite loop when cancelling during agent registration (36819203)
* Allow clients to specify a locator when uploading blobs. The server will not allow overwriting any existing blob, but allowing the client to determine the locator opens the door to it being able to write multiple blobs in parallel. (36813875)
* Grant claims to leases identifying them as running certain projects, streams, and templates. (36807664)
* Support running jobs in streams that do not have the engine directly under the stream root. The "EnginePath" property in the stream config can be used to configure the path to the engine folder. (36804827)
* Include a Version.json file in Unreal Toolbox builds which can be used to detect upgrades/downgrades. When auto-updating from builds with this file, only upgrades will be allowed. (36789633)
* Unreal Toolbox: Always show the settings dialog on launch unless the -Quiet argument is passed on the command line. This makes a more sensible default than requiring a -Settings argument. (36785739)
* Remove agent enrollment server json upon uninstall (36784558)
* Fix agent store updates upon cache invalidation/deletes (36739556)
* Move UnrealBuildTool.Tests into Engine\Programs\Shared (36735944)
* Fix issues with agent enrollment and deletion latency (36735037)
* Fix bug where leases weren't immediately aborted when agent is marked as busy (36733795)
* Fix issue with artfifact log rendering when there is no artifact type (36730128)
* Fix AgentWorkspaceInfo objects constructed from RPC workspace messages having an empty string for 'method' instead of null, causing agents to get stuck in a conform loop. (36718257)
* Fix issue with enrolling agent from installer goes to default server (36710606)
* Fixed a bug where the same error/warning message using different slash directions would not match with an existing issue when handled by the hashed issue handler (36698600)
* Add more logging and tracing for updating agents via REST API (36697817)
* Allow specifying arbitary key/value metadata on tools, and add product ids for MSI installers. Unreal Toolbox will automatically install/uninstall MSIs using the given product id. (36694744)
* Unreal Toolbox: Various improvements with installation process (36678430)
* Fix bug where not all fields were included in Equals() check for AgentWorkspaceInfo (36667931)
* Add scratch and min conform space to REST API agent response (36667252)
* Fix issue with mixed ungrouped and grouped tool downloads (36647304)
* Use the correct token for writing artifacts. Adds an IArtifactBuilder interface returned from IArtifactCollection.CreateAsync() which can be used to upload artifact data with the correct permissions. (36645064)
* Resurrect option to explicitly update build health issues in advanced parameters (36644246)
* Use same logic for shutting down on MacOS as Linux. (36578059)

## 2024-09-25

* Add filter for agent properties in REST API (36563629)
* Re-add logical cores as an agent property. UBA uses this property for determining number of agents to allocate. (36532400)
* Rename sample stream project to 5.5, and add a ugs-pcb artifact type. (36508675)
* Fix path for constructing Azure storage backends. (36508415)
* Move agent sandbox to be independent of agent settings, not in a hidden folder, and to avoid tripping path length warning by default (36505143)
* Fix issue with conform threshold for workspaces not propagating (36502791)

## 2024-09-19

* Expose .NET runtime version in agent properties (36427479)
* Add a separate build of the Horde Agent MSI for the toolbox, which includes metadata for running the installer. (36425974)
* Remove tray app and idle behavior setting from agent installer (36410206)
* Add support for group and platforms to bundled tools, adjust the build accordingly (36407524)
* Rename HordeToolbox again. Now: "UnrealToolbox!" (36401901)
* Add a dedicated installer for the tray app, and include it in the server installer. (36394095)
* Fix infinite loop issue when querying for jobs (36392029)
* Fix bug where physical cores are counted twice in capability detection (36387918)
* Replace OpenTracing with OpenTelemetry in JobDriver (36386025)
* Document the location of the server config file. (36384366)
* Ensure all streams register default artifact types.  We recently introduced the requirement that artifact types are explicitly configured (CL 35531333). The job driver expects certain artifact types to be present, and jobs will fail if these types are not registered.  This issue affects new installations and older installations upgrading to 5.5. (36383858)
* Remove OpenTelemetry.Instrumentation.AspNetCore from agent (36383393)
* Add an option for automatically updating tools in the tray app. (36356951)
* Fixed a bug where issues would have their FixCommitId set to 0 rather than cleared after re-assignment to a new owner.  Note we set fixChange to 0 in the onAssign function in IssueViewV2.tsx.  I now check whether fixChange is == 0 and if so set FixCommitId to be empty, this causes the FIX CL to be cleared. (36345723)
* Add information about processes with a file locked when it cannot be overwritten when extracting data from a bundle. (36341246)
* Add a dataflow graph with multiple deletion workers to improve GC performance. (36340210)
* Upgrade Horde Agent to NET 8. (36340066)
* Add an exponential pause between iterations over the GC tick queue. This should reduce the time that we spend idle during blob deletion. (36321827)
* Fix changes not being enumerated in descending order by PerforceService, causing the minimum changelist number for ICommitCollection.SubscribeAsync() not being updated. This prevents issue fixed tags from being processed. (36321198)
* Add missing file. (36318745)
* Replace OpenTracing with OpenTelemetry in agent (36307242)
* Move OpenTelemetry settings to EpicGames.Horde for sharing with agent (36303013)
* Added an additional sanitise case for the PerforceMetadataLogger when trying to get file annotations.  We could potentially handle this in LogEventParser AddLine but I wanted to limit the impact of these changes. (36299319)
* Add a proxy server to the Horde tray app, allowing users to connect to the Horde server via an unauthenticated connection bound to localhost. (36292016)
* Add a flag for showing a tool in the Horde tray app. (36284430)
* Add settings menu to tray app, as well as functionality for downloading tools from Horde. (36265467)
* Handle .horde.json file not existing when configuring server url on Mac/Linux. (36264830)
* Changed code traversing workspace on disk to use EnumerateFileSystemInfos instead of directories and files separately. This reduces number of kernel calls and save 20% time on a machine with attached ssd (36149765)
* Add timeouts for available port checking in tests (36117480)
* Add server-defined properties for an agent
  - Allows setting properties that will overwrite and merge with properties reported by agent itself
  - Refactor parameters for agent creation into an options object
  - When a user/agent can create new agents outside enrollment process, it's marked as trusted for the time being (grandfathering existing registration in JobRpcService) (36114930)
* Ignore server-defined properties when sent by agent (36108891)
* Adding Horde installer custom actions, data directory selector, and fix server to be able to bootstrap using custom data directory (36082966)
* Add tooltips for step times (36043646)
* Additional logging for terminating sessions. (36041396)
* Set the max thread count for managed workspace operations to one less than the number of reported CPUs on the machine. (36038061)
* Add better error for failing to start a process from the Horde Agent. (36026676)
* Advertise a new OSFamilyCompatibility property from agents, indicating OSes that the system can emulate (for Linux agents with WINE to indicate Windows compatibility). (36018324)
* Allow specifying a list of properties required for agents to execute compute leases. (36012503)
* Re-enable dedupe for the tools bundled with Horde. The permissions affecting access to this data are now waived for bundled tools. (36012474)
* Fix installed server not correctly identifying code changes correctly. Server was incorrectly setting a flag indicating that all files for a change had been enumerated, preventing it scanning the entire set. (35977472)
* Add a symbol store plugin. Symbol stores use aliases in the storage system to map symbol store paths onto content streams from existing artifacts, allowing reuse of data already available in artifacts.
  - Symbols can be tagged with the appropriate metadata by setting the Symbols=true attribute on the CreateArtifact task. Referencing the namespace that the symbols will be uploaded to from the symbol store config will allow accessing them through the api/v1/symbols route.
  - Hashing for symbols is compatible with symstore.exe, but is handled by a custom implementation in SymStore.cs. (35971584)
* Fix linq expression for generation of alias index. (35967893)
* Pause the addition of new blobs for GC once the queue is longer than 50,000 entries. (35967249)
* Allow graceful draining of leases when yielding to local user activity
  - The termination signal file used for spot interruptions is now written to let a workload know about the lease being drained.
  - This requires workload to scan and respect the file for this to be effective, which UBA does for example. (35962694)
* Add agent version to compute resource class, used for exposing version in compute resource API. (35954995)
* Log Horde server and agent version for a UBA session (35954941)
* Fix name of aliases over blobs collection. (35950154)
* Remove code to read imports from uploaded blobs. These should now be set at upload time. (35949781)
* Support loading artifacts out of log which are not in the step artifacts (35931864)
* Improve issue button rendering to not obscure the structured logging eyeball, also change where we render the structured log (35931043)
* Include artifacts created through the CreateArtifact BuildGraph task in the list of artifacts for the job. (35924757)
* Catch compute cancellation exceptions and avoid flagging them as errors (35923687)
* Guard against socket errors during closing of compute socket, also ensure CloseAsync can't be invoked twice. (35903197)
* Set the UE_HORDE_STREAMID environment variable containing the current stream id when running under Horde. (35900707)
* Document the look up of P4TRUST file (35892976)
* Change tray app to use Avalonia rather than Winforms. (35883986)
* Dashboard side of bundled tool management (35877843)
* Add clear conform to agent context menu (35877280)
* Adding bundled information to tool responses (35876724)
* Fix broken ServiceAccountAuth test (35876245)
* Add name field to service accounts and use that in audit logs, previously, name of service accounts resolved to "unknown". (35873276)
* Keep track of the agents which are currently able to execute sessions.  Each server now caches the state of a session based on the last update time, allowing it to perform most operations with minimal context fetches in the common case where an agent is always being updated on the same server. (35872405)
* Fixed issue in recent UHT changes that prevented errors begin generated from the header file object. (35863940)
* Add agent settings for CPU count and multiplier.  Only provided as hinting to workloads, which can chose to respect these. The initial use-case is letting UBA limit number of CPUs in use, much how maxcpu/mulcpu works for UBT.  Later on these values could configure job objects (on Windows) for proper OS-enforced CPU limiting. (35861968)
* Document use of ssl: prefix for connecting to Perforce servers (35859151)
* Replace NuGet package with more explicit one to reduce dependency size (35822082)
* Fix description and creation time not being deserialized as part of artifact responses correctly. (35811531)
* Store session state in Redis rather than MongoDB. (35780092)
* Add HordeHTTPClient GetUgsMetadataAsync method (35775820)
* Fixed symbol table lookup to properly include the header file when walking up the outer chain. (35773746)
* Agent enrollment improvements (35770720)
* Add REST API endpoint for listing ACL permissions for current user (35770032)
* Fix invalid dep injection of ServerSettings in JwtHandler (35769300)
* Show permissions for ACL scopes on account page, useful for debugging permissions in Horde. (35768929)
* Improve OIDC configuration experience
  - Add debug mode for better explaining why a JWT bearer token is rejected
  - Validate required settings are set for auth mode OIDC/Okta
  - Improve docs for OIDC settings (35768735)

## 2024-08-22

* Lock fluent to fix upstream regression (35716215)
* Add customizable landing pages (35713203)
* Surface when log data is missing in log event (35712179)
* Address additional artifact search feedback (35710162)
* Make it more clear when a step was canceled by Horde vs a user (35709294)
* Add lock around invoking nftables CLI tool (35690511)

## 2024-08-19

* Add ability to override agent's compute IP used for incoming connections (35621653)
* Generate issues for completed job steps asynchronously. (35601740)
* Randomize port assignment for relay port mappings. There's a worry of re-using the same port leads to potential races with how nftables/conntrack cleans up entries. In previous impl, lowest available port number was always used which led to higher contention. (35594027)
* Check for invalid AWS region names in config (35589406)
* Allow configuring the namespace for artifact types, tools, and replicated Perforce data. (35568444, 35568869, 35570127)
* Skip expiration of artifacts if no retention policy is specified. Previous behavior was to purge all that weren't explicitly kept. (35543239)
* Add ushell to Horde installer. (35541567)
* Fix registration of bundled tools in installed builds. (35538391)
* Require artifact types to be declared in config files. Also allow setting expiration policies per-project and per-stream, and handle expiration of artifact types in deleted streams or of a type which are no longer declared. (35531333)
* Improve lease termination handling during AWS spot interruptions. Only terminate session once a lease has finished. (35529116)
* Ensure resolved issues are excluded from queries when resolved by timeout. (35449290)
* Fix rendering of message templates containing width and format specifiers. (35446849)

## 2024-07-29

* Fix changelist number not being returned in artifact responses. (35140759)
* Change artifact paths to Type/Stream/Commit/Name/Id, to reflect permissions hierarchy. (35129994)
* Add extension methods to allow extracting IBlobRef<DirectoryNode> directly into directories. (35094979)
* Preliminary support for VCS commit ids that aren't an integer. All endpoints still support passing changelist numbers for now. (35091676)

## 2024-07-25

* Add a separate flag for marking an issue as fixed as a systemic issue, rather than having to pass a negative value as a fix changelist. Still supports passing/returning negative fix changelists as well for now, but will be removed in future. (35029295)
* Serialize config for pool size strategies and fleet managers as actual json objects, rather than json objects embedded in strings. Supports reading from json objects embedded in strings for backwards compatibility. (35026676)
* Set output assembly name for the command line tool to "Horde". (34984531)
* Separate out log event errors and critical failures into a separate fingerprint when we exceed the 25 event limit in the Hashed Issue Handler. (34930826)
* Add support for tagging log events with issue fingerprints. The $issue property on a log event can contain a serialized IssueFingerprint, which will be parsed by the ExternalIssueHandler handler inside Horde. This property can be added from C# code using the ILogger.BeginIssueScope() extension method. (34913930)
* ParentView is optional per perforce documentation. Fixes Unhandled Exception (Missing 'ParentView' tag when parsing 'StreamRecord') for streams that does not have it set (34890081)

## 2024-07-16

* Support for disabling conform tasks through the build config file. (34836886)
* Allow setting access permissions for different artifact types on a per-type, per-project and per-stream basis. (34836758)
* Allow specifying a secondary object store to read objects from, allowing a migration from one store in the background without causing downtime. (34828731)

## 2024-07-15

* Split server functionality into plugins. Plugins are still currently statically configured, which will be changed in future. (34620916, many others)
* Fix threading issue causing block cache to always attempt to access element zero, causing tests to get stuck in an infinite loop. (34759616)
* Add a debug endpoint for writing memory mapped file cache stats to the log. (34757974)
* Fix resource leaks for blob data reads. (34756957, 34757889)
* Fix tracking of allocated size in MemoryMappedFileCache. (34747643)
* Fix binding of plugin server config instances. (34745408)
* Move build functionality into a plugin. (34744727)
* Respect Forwarded-For header for client's IP during compute cluster ID resolving. Also return a better error message when no network range or cluster is found. (34743435)
* Add support for automatic computer cluster assignment in Horde, based on internal and external IP. (34734153)
* Add endpoint for resolving a suitable compute cluster ID (34706120)
* Set agent property if Wine executable is configured. Needed for scheduling Wine-compatible compute tasks. (34675604)
* Do not log cancellation exception as a warning when checking if blob exists. (34672928)
* Remove legacy artifacts and log system. (34663531)
* Resolve HTTP client IP for Datadog trace enricher (34644555)
* Fix user ID/username not getting set in Datadog enricher for OpenTelemetry traces. Must be accessed once response has been sent. (34636046)
* Support for artifact searches (34605400)
* Log each configuration source during agent start for better visibility where the agent is reading config from. Also log the actual logs dir. (34603382)
* Log where agent registration file is saved to/loaded from. When the file exists, the agent provides no hinting it reads this file which can be confusing when trying to reset the agent. (34600505)
* Add documentation for setting up a secret store. (34569155)
* Add support for compression of all responses using gzip, brotli, and zstd. (34565711)
* Add creation time to artifact responses. (34559619)
* Change config reader to operate on JsonNode instances rather than writing directly into target objects. (34546472)

## 2024-06-20

* Compute a digest for each block stored in the block cache, and validate it before returning values. (34515388)
* Remove requirement to specify a stream or key to enumerate artifacts. (34513227)
* Downgrade log event about duplicate build products to information, since we don't have the list of duplicate build products to ignore in Horde. (34481396)
* Exclude blocks for empty files from the Unsync manifest. (34464454)
* Add IPoolSizeStrategyFactory interface, which is used for creating IPoolSizeStrategy instances. This allows us to remove the graph/job/stream collection interfaces from FleetService, allowing it to exist without any job handling code in the solution. (34457658)
* Do not shutdown disabled agents by default. Prior to this change, this value defaulted to 8 hours which assumes you want this behavior in the first place. Making this nullable allow for opt-in instead. (34455677)
* Remove tracing span from shared tickers. For long-running callbacks, this can create data which is difficult for Otel/DD to handle. (34444738)
* Add a debug endpoint which streams a random block of data to the caller. Usage is: /api/v1/debug/randomdata?size=1mb (34426596)

## 2024-06-17

* Add support for auto-assigning cluster ID during compute allocation requests (34423910)
* Increase MongoDB client wait queue size from 300 -> 5000 (34423018)
* Allow specifying cache sizes using standard binary suffixes (eg. "4gb") (34382251)
* Add the shutdown timeout for Kestrel server (34379789, 34379660)
* Add an AWS parameter store secret provider (34379595)

## 2024-06-13

* Add a pipelined blob read class for storage blobs. (34322019)
* Add a disk-based cache for compressed Unsync blocks. Uses memory mapped files to read/write to underlying storage, LRU eviction via random sampling. (34288152, 34292867)
* Tweak MongoDB client's config defaults to curb wait queue full errors (34276157)
* Resolving of capabilities has been known to stall at times. Adding trace spans to these should help see which part is causing slowdowns. (34232453)
* Add zstd as a compression format for bundles. Also add some basic compression tests, and fix an isuse with gzip streams being truncated. (34211536)
* Include rolling hashes in bundle archives to support unsync manifests. DirectoryNode/ChunkedDataNodeRef now use HordeApiVersion for versioning purposes, rather than their own custom versioning scheme. These blob types will serialize the most relevant Horde API version number they support into archives. (34207581)
* Ignore cancellations in AWS instance lifecycle (34207087)
* Include modification times for file entries in bundles by default, but do not write them unless the configured API version allows it. (34205938)
* Add compute endpoint that automatically finds the best cluster. Currently uses IP of requester together with networks from global config. (34201862)
* Fix some artifacts not showing up in job responses, due to default cap of 100 artifacts returned. (34200332)
* Check agent's compute cluster membership on assignment. Previously it was only checked by the task source. (34199317)
* Set user ID in compute assignment request track usage by user (34195976)
* Disable config updates when running locally in Redis read-only mode, preventing an exception on startup. (34169346)
* Make horde chunk boundary calculation consistent with unsync, add unit tests to validate determinism (34168267)

## 2024-06-06

* Add a retry policy to the Epic telemetry sink, to stop 502 errors causing server log entries. (34141477)
* Add a GC verification mode, which sets a flag in the DB rather than deleting an object from the store. (34135143)
* Fix issue where files will not be uploaded to temp storage if they were previously tagged as outputs in another step. Now allows output files to be produced by multiple steps, as long as their attributes match. (34132683)
* Adding support for optional cancellation reasons for jobs and steps (34131480)
* Forward all claims for access tokens minted for Horde accounts The role claim was missing which is used for resolving many permissions. (34119849)
* Log reasons when a lease is terminated due to the cancellation token being set. (34111700)
* Add a link to the producing job to .uartifact files, as well as other metadata and keys on the artifact object. (34095063)
* Allow user configured backends and namespaces to override the defaults in defaults.global.json. (34092465)
* Print a message to the batch log whenever a lease is cancelled or fails due to an unhandled exception. (34084201)
* Add a setting ("ReportWarnings") to exclude issues which are only warnings from summary reports. (34078316)
* Fix P4 server health not being updated when HTTP health check endpoint returns degraded. (34077493)
* Re-encode json log events which are downgraded from warning to information level. (34066649)
* Add more trace attributes to compute resource allocation (34064620)
* Record endpoint address in MongoDB command tracer, jelps differentiate between primary and secondary use (read-only ops) (34063808)
* Use cached agent data for JobTaskSource, this ticks every 5 seconds refreshing agent and pool data. The cached agent data is refreshed at the same rate. (34062377)

## 2024-05-31

* Use cached agent data for fleet and pool size handling (34029489)
* Fix conform commands using the incorrect access token to communicate with the server. (34012793)
* Allow registering multiple workspace materializer factories. (34010953)
* Add an agent document property for the combined list of pools, and upgrade existing agent documents on read to include it. This allows indexed searches for dynamic pools. (34009396)
* Add a log enricher which adds the server version. (34003927, 34005245, 34004705)

## 2024-05-30

* Update test projects to NET 8. (34002443)
* Refactor WorkspaceInfo to avoid keeping a long-lived connection. Keeping idle connections to the Perforce server after the initial workspace setup is unnecessary and wastes server resources. (33949575)
* Log in to the server when querying for config files from Perforce. (33945114)
* Fix http auth handler not using correct configuration for server when run locally. (33940162)
* Skip cleaning files for AutoSDK workspace. (33858727)
* Only enable debug controllers if enabled in settings (33857362)
* Add a fingerprint description as part of issue response (33833567)
* EpicGames.Perforce: Fix parsing of array fields from Perforce records. (33807413)
* EpicGames.Perforce: Fix parsing of records which have duplicate field names in child records. (33804970)
* Allow overriding the targets to execute for a job. The optional 'Targets' property can be specified when creating a job, and will replace any -Target= arguments in the command line if set. (33799426)
* Pass -Target arguments to individual steps to ensure that BuildGraph can correctly filter out which blocks to embed in temp storage manifests. (33799034)
* Remove server OS version info from anonymous endpoint responses. (#11909) (33793366)
* Allow overriding the driver for jobs through the JobOptions object. (33791494)
* Prevent AWS status queries from causing session update failures. (33782757)
* Move job driver into a separate executable. (33775640, 33782224)

## 2024-05-20

* Add grouping keys and platforms to tools. Intent is for the dashboard to only show one tool for each grouping key by default (the one with the closest matching platform based on the browser's user-agent), but users can manually expand the list of tools to show other platforms if desired.
  Platforms should be NET RIDs, eg. win-x64, osx-arm64, etc... (https://learn.microsoft.com/en-us/dotnet/core/rid-catalog). (33698418)
* Use native GrpcChannel instances for agent connection management, rather than IRpcConnection. (33660725, 33638856)
* Add an AdditionalArguments property to jobs, which can be used to append arbitrary arguments to those derived from a job's parameters. This field is preserved - but not appended - to the arguments list if arguments are specified explicitly. Needs hooking up to the dashboard. (33630321)
* Prevent default parameters being appended to jobs when an explicit argument list is specified. (33628649)
* Move Perforce/BuildGraph functionality into a new Horde.Agent.Driver application. Intent is to separate this from the core Agent application over time, making it easier to iterate on Job/BuildGraph related functionality outside of the core agent deployment. (33606684)
* Move settings for different executors into their own files. (33605105)
* Trap exceptions when parsing invalid workflow ids from node annotations. (33602812)
* Enable HTTP compression for server-sent responses (33600392)
* Add support for agent queries via replica read (33575058)
* Optimize agent assignment for compute tasks by filtering by pool (33488689)
* Add command-based tracer for MongoDB. Old tracer operates at the collection level, this instead listens for events emitted by the Mongo client. (33479889)
* Keep a cached list of current agents in AgentService (33454500)
* Add an explicit error when the server URL does not have a valid scheme or host name. (33427628)
* Move expired and ephemeral agent clean up to shared ticker (33424708)

## 2024-05-01

* Explicitly check that the expiry time is set when searching for refs to expire, so that DocDB will use an indexed query. (33367777)
* Add CheckConnectionAsync to HordeHttpClient. (33353008)
* Add an index to the refs collection to prevent collection scans when finding refs to expire. (33338629)
* Reduce how long ephemeral and deleted agents are kept in database. Heavy use of auto-scaling can lead to excessive history of ephemeral agents being kept. In the case of AWS, each new instance has a unique agent ID. Many of the queries for agents (and in turn indices) are not optimized for a large collection. (33317167)
* Disable file modified errors in job executor. (33299280)
* Remove legacy job methods from horde_rpc interface. (33298879)
* Remove support for legacy artifact uploads. (33298589)
* Enable agents sending CPU/RAM usage metrics to the server. (33290135, 33289584)

## 2024-04-25

* Allow setting project-wide workspace types. Will automatically be inserted to the workspace type list of each stream belonging to the project. This also allows defining base types which can be inherited from. (33213109)
* EpicGames.Perforce: Fix errors parsing change views from labels. (33207649)
* Delete step logs when deleting a job. (33206294)
* Use the shared registry location to configure the Horde command line tool on Windows, and use a .horde.json in the user folder on Mac and Linux. (33205861)
* Rename step -> job step, report -> job report, batch -> job batch in job responses. (33191560)
* Include graph information in job responses. (33189353)
* Add a service to expire jobs after a period of time. Jobs are kept forever by default, though this may be modified through the ExpireAfterDays parameter in the JobOptions object in streams or projects. (33109364)
* Add auto conform for agents with workspaces below a certain free disk space threshold (33102671)
* Use a standard message for server subsystems that are operating normally. (33100047)
* Add min scratch space to workspace config (33098369)
* Recognize tags in Perforce changelists of the form '#horde 123' as indication that a change fixes a particular issue. The specific tag can be configured in the globals.json file to support multiple deployments with the same Perforce server. (33089498)
* Clear the default ASPNETCORE_HTTP_PORTS environment variable in Docker images to prevent warnings about port overrides on server startup. We manage HTTP port configuration through Horde settings, and don't want the NET runtime image defaults. (33072683)

## 2024-04-18

* Add dashboard's time since last user activity to HTTP log (33068679)
* Fix agent registration not being invalidated when server is reinstalled. Server returns "unauthenticated" (401) response, not "forbidden" (403). Forbidden is only returned when the server deliberately invalidates an agent. (33060431)
* Send DMs to individual users rather than the notification channel when config updates succeed. (33060054)
* Prevent hypens before a period in a sanitized string id. (33044611)
* EpicGames.Perforce: Fix parsing of records with multiple arrays. (33038480)
* Do not allow multiple agent services to run at once. (33037301)
* Handle zero byte reads due to socket shutdown when reading messages into a compute buffer. (33034396)

## 2024-04-12

* Ensure blobs uploaded to the tools endpoint have the tool id as a prefix to the blob locator (32907981)
* Allow specifying a unique id for each parameter in a job template (32906466)
* Send Slack notifications whenever config changes are applied (32897388)
* Make log output directory configurable in agent (32886152)
* Use strongly typed identifiers for artifact log messages in agent. (32861971)
* Fix memory leak and incorrect usage of native P4API library (32798937, 32794622)
* Add debug endpoint for capturing a dotMemory snapshot (32790300)
* Refactor API message names, project locations and reduce gRPC dependency (32785690, 32783652)
* Allow clients to upload the list of references for each blob, removing the need for the server to do it later. (32768299)

## 2024-04-05

* Handle socket shutdown errors gracefully rather than throwing exceptions. (32756972)
* Add log message whenever native P4 connection buffer increases in size above 16mb. (32751407)
* Prevent log appearing empty if tailing is enabled but no new data has been received before the existing tail data is expired. Rpc log sink was waiting forever for new log tail data, so server was discarding the data already received. (32749401)
* Fix default settings for the analytics dashboard, and update documentation to specify the correct telemetry store name. (32741193)
* Update derived data for issues when they are closed by timeout. (32733991)
* Explicitly set parent context for telemetry spans in Perforce updates, to work around async context mismatches. (32732888)
* Rework commit metadata replication to run a single task for each replicated cluster, for a simpler code flow and better tracing data. (32730286)
* Instrument JobTaskSource ticker. We see occasional slow downs in agent assignment for jobs. Adding some additional tracing should help see what's causing it. (32727003)
* Do not assign compute resource to same machine as requester (32723450)

## 2024-04-01

* Fix decoding of UTF8 characters as part of unescaping Json strings. (32638382)
* Increase Jira timeout to 30 seconds, and improve cancellation handling. (32636702)
* Add a defaults.json file for backwards compatibility with older server installers. Just includes default.global.json. (32619739)
* Move tool update packages onto a separate "Internal" tab on the dashboard. (32619239)
* Only export nodes to Horde that are referenced by the initial job parameters, unless -AllowTargetChanges is specified. Prevents data being copied to temp storage that will never be used unless the target list changes. (32616197)
* Add notes for setting up a self-signed cert for testing. (32613289)
* Doc updates. (32595767)
* Comment out the placeholder ticket value in the default config file. (32585713)
* Improve ordering of nodes when writing large file trees to storage, such that the nodes which are read after each other are adjacent in the storage blobs. Nodes are read in the reverse order to which they are written, depth-first. Arranging nodes in the blob in this order prevents thrashing of the cache and improves performance. (32585474)

## 2024-03-28 (UE 5.4 Release)

* Remove /userinfo call when authenticating via JWT. If an access token is passed, it's not guaranteed it has permission to access /userinfo from OIDC. ID tokens during normal web-based login does on the other hand. (32539012)
* Fix parsing of true/false literals in condition expressions. (32518640)
* Add a default pool to include interactive agents, and map it to the TestWin64 agent type. (32514202)
* Add logging to trace when blobs are scanned for imports, to help debug some logs being expired while still referenced. (32506875)
* Normalize keys for artifacts to allow case-insensitive searching. (32495123)
* Support setting arbitrary artifact metadata. This cannot be queried, but will be returned in artifact responses. (32464180)
* Run a background task to monitor for hangs when executing Perforce commands. (32461961)
* Allow configuring different telemetry stores for each project and stream. (32460547)
* Fix slow queries in storage service garbage collection. (32460451)
* Add a [TryAuthorize] attribute which attempts authorization, but which does not return a challenge or forbid result on failure. Should fix tool download requests using service account tokens. (32447487)
* Fix OIDC issues when using Horde auth with UBA. (32406994)

## 2024-03-21

* Lower log level of failed AWS instance start (32397273)
* Assume any exceptions while checking for config files being out of date as meaning they need to be read again. (32388229)
* Use the first specified credentials for server P4 commands if the serviceAccount property is not set. (32383940)
* Update docs for deploying server on Mac. (32370724)
* Log a warning whenever an obsolete endpoint is used. (32329528)
* Fix separate RAM slots being reported separately in agent properties. (32328916)
* Fix use of auth tokens from environment variables when the server URL is also derived from environment variables. (32305895)
* Fix StorageService not being registered as a hosted service, preventing GC from running. (32291615)
* Fix artifacts not being added to graphs. (32286255)
* Add output artifacts to the graph from the exported BuildGraph definition. (32281784)

## 2024-03-15

* Get the userinfo endpoint from the OIDC discovery document rather than assuming it's a fixed path. (32250458)
* Fix serialization issue with global config path on Linux (32240996)
* Improve error reporting when exceptions are thrown during leases. (32233035)
* Prevent DeadlineExceeded exceptions in log rpc task from being logged as errors. (32232976)
* Update EpicGames.Perforce.Native to use the 2023.2 Perforce libraries. (32232242)
* Enable notification service on all server types, otherwise we keep queuing up new tasks without ever removing them. (32220252)
* Invalidate the current session when deleting an agent from the farm. (32212590)
* Invalidate registrations with a server if creating a session fails with a permission denied error. (32212298)
* Fix job costs displaying for all users. (32154404)
* Expose the bundle page size as a configurable option. Still defaults to 1mb. (32153263)
* Use BitFaster.Caching for the LRU bundle cache. (32153088)
* Pool connections in ManagedWorkspace rather than creating a new instance each time. (32144653)
* Exclude appsettings.Local.json files from publish output folders. (32140083)
* Add a UGS config file to command line tool. (32120883)
* Add endpoints to check permissions for certain ACL actions in different scopes. (32118158)
* Immediately cancel running leases if agent status gets set to busy (paused) (32102044)
* Include a separate file containing default values for Horde configuration from the a file copy into C:\ProgramData. (32098391)
* Add OAuth/OIDC support for internal Horde accounts. (32097065)

## 2024-03-07

* Store the URL of the default Horde server in the registry on Windows (31994865)
* Download UGS deployment settings from Horde parameters endpoint (32001513)
* Allow using partitioned workspaces for P4 replication (32041630)
* Allow only keeping a fixed number of artifacts rather than expiring by time (32043455)
* Include dashboard web UI files in public Docker image for Horde server (32054568)
* Turn off native Perforce client on non-Windows agents (31991203)

## 2024-02-29

* Add a back-off delay when RPC client gets interrupted in JsonRpcLogSink (31881762)
* Add a manual approval process for agents joining the farm. New EnrollmentRpc endpoint allows agents to connect and wait for approval without being assigned an agent id or being able to take on work. (31869942)
* Fix bug with tray app not setting agent in paused state during idle (31837900)
* Copy the tray app to a folder under C:\Users\xxx\AppData\Local before running, so the agent can update without having to restart the tray app (which may be running under a different user account). (31815878)
* Use the agent's reported UpdateChannel property to choose which tool to use for updating the installation. ()
* Write the name of the channel to use for agent updates into the agent config file, and report it to the server through the agent properties. (31806993, 31810088)
* Separate implementations for regular user accounts from service accounts. (31802961)
* Added endpoints for querying entitlements of the current user (or any other account). /account/entitlements will return entitlements for the logged in user, and /api/v1/accounts/xxx/entitlements will return entitlements for a Horde account. (31777667)
* Accounts now use the user's full name for the standard OIDC name claim. (https://openid.net/specs/openid-connect-core-1_0.html#StandardClaims) (31773722)

## 2024-02-23

* Fix regression in UGS metadata filtering, where metadata entries with an empty project string should be returned for any project. (31741737)
* Artifact expiry times are now retroactively updated whenever the configured expiry time changes. (31736576)
* Allow specifying a description string for artifacts. (31729492)
* Disable internal Horde account login by default. (31724108)
* Fix bundled tools not being handled correctly in installed builds. (31721936)
* Read registry config first so files and env vars can override (31720705)

## 2024-02-22

* Fixed bundled tools not being handled correctly in installed builds. (31721936)
* Read registry config first so files and env vars can override (31720705)
* Fix leases not being cancelled when a batch moves into the NoLongerNeeded state. (31709570)
* Prevent Windows service startup from completing until all Horde services have started correctly. (31691578)
* Add firewall exception in installer (31678384, 31677487)
* Always return a mime type of text/plain for .log files, allowing them to be viewed in-browser (31648543)
* Configure HTTP/2 port in server installer (31647511)
* Enable new temp storage backend by default. (31637506)
* Support for defining ACL profiles which can be shared between different ACL entries. Intent is to allow configuring some standard permission sets which can be inherited and modified by users. Two default profiles exist - 'generic-read' and 'generic-run' - which match how we have roles configured internally. New profiles can be created in the same scopes that ACL entries can be defined. (31618249)
* Add a new batch error code for agents that fail during syncing. (31576140)
