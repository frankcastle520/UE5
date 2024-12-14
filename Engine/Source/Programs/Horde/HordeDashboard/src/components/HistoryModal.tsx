// Copyright Epic Games, Inc. All Rights Reserved.

import { Checkbox, ComboBox, ConstrainMode, DefaultButton, DetailsHeader, DetailsList, DetailsListLayoutMode, DetailsRow, Dialog, DialogFooter, DialogType, FontIcon, GroupedList, GroupHeader, ICheckbox, IColumn, IComboBoxOption, IContextualMenuItem, IContextualMenuProps, IDetailsHeaderProps, IDetailsHeaderStyles, IDetailsListProps, IGroup, ITextField, ITooltipHostStyles, Label, mergeStyleSets, Modal, Pivot, PivotItem, PrimaryButton, ScrollablePane, ScrollbarVisibility, Selection, SelectionMode, Spinner, SpinnerSize, Stack, Sticky, StickyPositionType, Text, TextField } from "@fluentui/react";
import { action, makeObservable, observable } from "mobx";
import { observer } from "mobx-react-lite";
import React, { useEffect, useState } from "react";
import { Link } from "react-router-dom";
import { Sparklines, SparklinesLine, SparklinesReferenceLine } from "react-sparklines";
import backend from "../backend";
import { agentStore } from "../backend/AgentStore";
import { AgentData, GetAgentLeaseResponse, GetAgentSessionResponse, GetAgentTelemetrySampleResponse, GetJobResponse, JobStepBatchError, JobStepOutcome, LeaseData, SessionData, UpdateAgentRequest } from "../backend/Api";
import dashboard from "../backend/Dashboard";
import { getNiceTime, getShortNiceTime } from "../base/utilities/timeUtils";
import { getHordeStyling } from "../styles/Styles";
import { getHordeTheme } from "../styles/theme";
import { BatchStatusIcon, LeaseStatusIcon, StepStatusIcon } from "./StatusIcon";


type LeaseTooltip = {
   lease?: GetAgentLeaseResponse;
   time?: Date;
   id?: string;
   x?: number;
   y?: number;
   frozen?: boolean;
}

type InfoPanelItem = {
   key: string;
   name: string;
   data?: string;
   selected: boolean;
};

type InfoPanelSubItem = {
   name: string;
   value: string;
}

const historyStyles = mergeStyleSets({
   dialog: {
      selectors: {
         ".ms-Label,.ms-Button-label": {
            fontWeight: "unset",
            fontFamily: "Horde Open Sans SemiBold"
         }
      }
   },
   detailsList: {
      selectors: {
         ".ms-DetailsHeader-cellName": {
            fontWeight: "unset",
            fontFamily: "Horde Open Sans SemiBold"
         }
      }
   }
});

class HistoryModalState {
   constructor() {
      makeObservable(this);
   }

   @observable
   selectedAgentUpdated = 0;

   @observable
   currentLeaseTooltip?: LeaseTooltip;

   get selectedAgent(): AgentData | undefined {
      // subscribe in any observers
      if (this.selectedAgentUpdated) { }
      return this._selectedAgent;
   }

   private _selectedAgent: AgentData | undefined = undefined;
   @observable.shallow currentData: any = [];
   @observable.shallow infoItems: InfoPanelItem[] = [];
   @observable.shallow infoSubItems: InfoPanelSubItem[] = [];
   agentItemCount: number = 0;
   devicesItemCount: number = 0;
   workspaceItemCount: number = 0;
   sortedLeaseColumn = "startTime";
   sortedLeaseColumnDescending = true;
   sortedSessionColumn = "startTime";
   sortedSessionColumnDescending = true;

   @observable mode: string | undefined = undefined;
   modeCurrentIndex: number = 0;
   bUpdatedQueued: boolean = false;

   // sets the pool editor dialog open
   @action
   setSelectedAgent(selectedAgent?: AgentData | undefined) {
      // if we're closing don't reset
      this._selectedAgent = selectedAgent;
      if (this.selectedAgent) {
         // set date to now
         this._initBuilderItems();
         if (!this.mode) {
            this.setMode("info");
         }
         else {
            this.setMode(this.mode, true);
         }
         this._doUpdate();
      }

      this.selectedAgentUpdated++;
   }

   @action
   setMode(newMode?: string, force?: boolean) {
      if (newMode) {
         newMode = newMode.toLowerCase();
         if (this.mode !== newMode || force) {
            this.mode = newMode;
            this.currentData = [];
            this.modeCurrentIndex = 0;
            this._doUpdate();
         }
      }
   }

   @action
   setLeaseTooltip(tooltip?: LeaseTooltip) {
      this.currentLeaseTooltip = tooltip;
   }

   setInfoItemSelected(item: InfoPanelItem) {
      this.infoItems.forEach(infoItem => {
         if (infoItem.key === item.key) {
            infoItem.selected = true;
         }
         else {
            infoItem.selected = false;
         }
      });
      this.setInfoItems(this.infoItems);
   }

   private _initBuilderItems() {
      this.agentItemCount = 0;
      this.devicesItemCount = 0;
      this.workspaceItemCount = 0;
      const items: InfoPanelItem[] = [
         {
            key: "overview",
            name: "Overview",
            selected: false
         }
      ];
      this.agentItemCount = 1;
      if (this.selectedAgent) {
         if (this.selectedAgent.capabilities?.devices) {
            for (const deviceIdx in this.selectedAgent.capabilities?.devices) {
               const device = this.selectedAgent.capabilities?.devices[deviceIdx];
               if (!device["name"]) {
                  device["name"] = "Primary";
               }
               if (device.properties) {
                  items.push({ key: `device${deviceIdx}`, name: device["name"], selected: device["name"] === "Primary" ? true : false });
                  this.devicesItemCount++;
               }
            }
         }
         if (this.selectedAgent.workspaces) {
            const streamCount = new Map<string, number>();
            for (const workspaceIdx in this.selectedAgent.workspaces) {
               const workspace = this.selectedAgent.workspaces[workspaceIdx];
               let count = streamCount.get(workspace.stream) ?? 0;
               if (!count) {
                  count++;
                  streamCount.set(workspace.stream, 1);
               } else {
                  count++;
                  streamCount.set(workspace.stream, count);
               }

               const name: string = count > 1 ? `${workspace.stream} (${count})` : workspace.stream;

               (workspace as any)._hackName = name;
               ;
               items.push({ key: `workspace${workspaceIdx}`, name: name, selected: false, data: name });
               this.workspaceItemCount++;
            }
         }
      }
      this.setInfoItems(items);
   }

   @action
   setInfoItems(items: InfoPanelItem[]) {
      const subItems: InfoPanelSubItem[] = [];
      if (this.selectedAgent) {
         const selectedItem = items.find(item => item.selected);
         if (selectedItem) {
            if (selectedItem.key === "overview") {
               subItems.push({ name: 'Enabled', value: this.selectedAgent.enabled.toString() });
               subItems.push({ name: 'Comment', value: this.selectedAgent.comment ?? "None" });
               subItems.push({ name: 'Ephemeral', value: this.selectedAgent.ephemeral.toString() });
               subItems.push({ name: 'ForceVersion', value: this.selectedAgent.forceVersion ?? "None" });
               subItems.push({ name: 'Id', value: this.selectedAgent.id });
               subItems.push({ name: 'Online', value: this.selectedAgent.online.toString() });
               subItems.push({ name: 'Last Update', value: this.selectedAgent.updateTime.toString() });
               subItems.push({ name: 'Version', value: this.selectedAgent.version ?? "None" });
            }
            else if (selectedItem.key.indexOf("workspace") !== -1) {
               if (this.selectedAgent.workspaces) {
                  for (const workspaceIdx in this.selectedAgent.workspaces) {
                     const workspace = this.selectedAgent.workspaces[workspaceIdx];

                     if ((workspace as any)._hackName === selectedItem.data) {
                        subItems.push({ name: 'Identifier', value: workspace.identifier });
                        subItems.push({ name: 'Stream', value: workspace.stream });
                        subItems.push({ name: 'Incremental', value: workspace.bIncremental.toString() });
                        workspace.serverAndPort && subItems.push({ name: 'Server and Port', value: workspace.serverAndPort });
                        workspace.userName && subItems.push({ name: 'Username', value: workspace.userName });
                        workspace.password && subItems.push({ name: 'Password', value: workspace.password });
                        workspace.view && subItems.push({ name: 'View', value: workspace.view?.join("\n") });
                     }
                  }
               }
            }
            else if (selectedItem.key.indexOf("device") !== -1) {
               if (this.selectedAgent.capabilities?.devices) {
                  for (const deviceIdx in this.selectedAgent.capabilities?.devices) {
                     const device = this.selectedAgent.capabilities?.devices[deviceIdx];
                     if (device.name === selectedItem.name) {
                        if (device.properties) {
                           for (const propIdx in device.properties) {
                              const prop = device.properties[propIdx];
                              const subItemData = prop.split('=');
                              if (subItemData[0].indexOf("RAM") !== -1) {
                                 subItemData[1] += " GB";
                              }
                              else if (subItemData[0].indexOf("Disk") !== -1) {
                                 subItemData[1] = (Number(subItemData[1]) / 1073741824).toLocaleString(undefined, { maximumFractionDigits: 0 }) + " GiB";
                              }
                              subItems.push({ name: subItemData[0], value: subItemData[1] });
                           }
                        }
                     }
                  }
               }
            }
         }
      }
      this.infoItems = [...items];
      this.infoSubItems = subItems;
   }

   private _doUpdate() {
      if (this.mode === "leases") {
         this.UpdateLeases();
      }
      else if (this.mode === "sessions") {
         this.UpdateSessions();
      }
   }

   nullItemTrigger() {
      if (!this.bUpdatedQueued) {
         this.bUpdatedQueued = true;
         this._doUpdate();
      }
   }

   UpdateLeases() {
      const data: LeaseData[] = [];
      backend.getLeases(this.selectedAgent!.id, this.modeCurrentIndex, 30, true).then(responseData => {
         responseData.forEach((dataItem: GetAgentLeaseResponse) => {
            data.push(dataItem);
         });
         this.appendData(data);
      });
   }

   UpdateSessions() {
      const data: SessionData[] = [];
      backend.getSessions(this.selectedAgent!.id, this.modeCurrentIndex, 30).then(responseData => {
         responseData.forEach((dataItem: GetAgentSessionResponse) => {
            data.push(dataItem);
         });
         this.appendData(data);
      });
   }

   jobData = new Map<string, GetJobResponse | boolean>();

   UpdateTelemetry(minutes: number) {

      const requests = [backend.getAgentTelemetry(this.selectedAgent!.id, new Date(Date.now() - 1000 * 60 * minutes), new Date()), backend.getAgentLeases(this.selectedAgent!.id, new Date(Date.now() - 1000 * 60 * minutes), new Date())]
      Promise.all(requests).then((data) => {
         this.setTelemetryData((data[0] as GetAgentTelemetrySampleResponse[]).reverse(), data[1] as GetAgentLeaseResponse[]);
      })
   }

   @action
   setTelemetryData(telemetryData: GetAgentTelemetrySampleResponse[], sessionData: GetAgentLeaseResponse[]) {

      const linear: GetAgentTelemetrySampleResponse[] = [];

      if (telemetryData.length) {

         let ctime: number = 0;

         telemetryData.forEach(t => {

            if (!ctime) {
               ctime = t.time.getTime();
               linear.push(t);
            } else {
               const ntime = t.time.getTime();
               // dependant on agent sample rate!               
               while ((ntime - ctime) > 60000) {

                  ctime += 30000;
                  linear.push({
                     time: new Date(ctime),
                     userCpu: 0,
                     idleCpu: 0,
                     systemCpu: 0,
                     freeRam: 0,
                     usedRam: 0,
                     totalRam: 1,
                     freeDisk: 0,
                     totalDisk: 0
                  });
               }

               linear.push(t);
               ctime = ntime;
            }
         });
      }

      this.currentData = [linear, sessionData];

   }

   @action
   appendData(newData: any[]) {

      if (this.mode === "telemetry") {
         return;
      }

      // if there's any data, there might be more data next time, so add another callback.
      if (newData.length > 0) {
         newData.push(null);
      }

      let combinedData = [...this.currentData];

      // remove previous null if it exists
      if (combinedData[combinedData.length - 1] === null) {
         combinedData.splice(-1, 1);
      }
      // add all the new data
      Array.prototype.push.apply(combinedData, newData);

      const dedupe = new Set<string>();

      this.currentData = combinedData.filter(d => {
         if (dedupe.has(d?.id)) {
            return false;
         }
         dedupe.add(d?.id);
         return true;
      });

      this.modeCurrentIndex += newData.length;
      this.bUpdatedQueued = false;
   }

}

const state = new HistoryModalState();


const TelemetryTooltip: React.FC<{ id: string }> = observer(({ id }) => {

   const { modeColors } = getHordeStyling();

   const tooltip = state.currentLeaseTooltip;

   if (!tooltip?.lease || tooltip.id !== id) {
      return null;
   }

   let tipX = tooltip.x;
   let offsetX = 32;
   let translateX = "0%";

   if ((tipX ?? 0) > 600) {
      offsetX = -32;
      translateX = "-100%";
   }

   const translateY = "-50%";

   const textSize = 11;
   const titleWidth = 48;

   const dataElement = (title: string, value: string, link?: string, agent?: string, linkTarget?: string) => {

      let valueElement = <Stack>
         <Stack style={{ fontSize: textSize }}>
            {value}
         </Stack>
      </Stack>

      if (link) {
         valueElement = <Link className="horde-link" to={link} target={linkTarget}>
            {valueElement}
         </Link>
      }

      let component = <Stack style={{ cursor: (link || agent) ? "pointer" : undefined }} onClick={() => {
      }}>
         <Stack horizontal>
            <Stack style={{ width: titleWidth }}>
               {!!title && <Text style={{ fontFamily: "Horde Open Sans SemiBold", fontSize: textSize }} >{title}</Text>}
            </Stack>
            {valueElement}
         </Stack>
      </Stack>

      return component;
   }

   const elements: JSX.Element[] = [];

   const lease = tooltip.lease;

   if (lease?.details?.type === "Job") {
      elements.push(dataElement("Job:", tooltip.lease?.name ?? "Unknown Job Lease", `/job/${lease.details.jobId}`));
      const job = state.jobData.get(lease.details.jobId);
      if (job && job !== true && job.batches) {
         const step = job.batches.map(b => b.agentId === state.selectedAgent?.id ? b.steps : []).flat().find(s => {
            if (s.startTime && tooltip.time) {
               if (tooltip.time >= new Date(s.startTime) && (!s.finishTime || tooltip.time <= new Date(s.finishTime))) {
                  return true;
               }
            }
            return false;
         });
         if (step) {
            elements.push(dataElement("Step:", step.name ?? "Unknown Job Step", `/job/${lease.details.jobId}?step=${step.id}`));
            if (step.logId)
               elements.push(dataElement("Log:", `Step`, `/log/${step.logId}`));
         }
      }
   } else {
      elements.push(dataElement("Lease:", tooltip.lease?.name ?? "Unknown Lease"));
   }

   if (lease?.details?.logId) {
      elements.push(dataElement("Log:", `Lease`, `/log/${lease.details.logId}`));
   }



   if (tooltip.time) {
      elements.push(dataElement("Time:", getNiceTime(tooltip.time)));
   }


   return <div style={{
      position: "absolute",
      display: "block",
      top: `${tooltip.y}px`,
      left: `${(tooltip.x ?? 0) + offsetX}px`,
      backgroundColor: modeColors.background,
      zIndex: 1,
      border: "solid",
      borderWidth: "1px",
      borderRadius: "3px",
      width: "max-content",
      borderColor: dashboard.darktheme ? "#413F3D" : "#2D3F5F",
      pointerEvents: tooltip.frozen ? undefined : "none",
      transform: `translate(${translateX}, ${translateY})`
   }}>
      <Stack horizontal>
         <Stack tokens={{ childrenGap: 6, padding: 12 }}>
            {elements}
         </Stack>
         {!!tooltip.frozen && <Stack style={{ paddingLeft: 32, paddingRight: 12, paddingTop: 12, cursor: "pointer" }} onClick={() => { state.setLeaseTooltip(undefined) }}>
            <FontIcon iconName="Cancel" />
         </Stack>}
         {!tooltip.frozen && <Stack style={{ paddingLeft: 32, paddingRight: 12 + 16, paddingTop: 12 }} />}

      </Stack>
   </div>

});

type TimeSelection = {
   text: string;
   key: string;
   minutes: number;
}

const timeSelections: TimeSelection[] = [
   {
      text: "Past 1 Hour", key: "time_1_hour", minutes: 60
   },
   {
      text: "Past 2 Hours", key: "time_2_hours", minutes: 60 * 2
   },
   {
      text: "Past 4 Hours", key: "time_4_hours", minutes: 60 * 4
   },
   {
      text: "Past 1 Day", key: "time_1_day", minutes: 60 * 24
   },
   {
      text: "Past 2 Days", key: "time_2_days", minutes: 60 * 24 * 2
   },
   {
      text: "Past 1 Week", key: "time_1_week", minutes: 60 * 24 * 7
   },
   {
      text: "Past 2 Weeks", key: "time_2_weeks", minutes: 60 * 24 * 7 * 2
   },
   {
      text: "Past Month", key: "time_4_weeks", minutes: 60 * 24 * 7 * 4
   }
]

const cpuSelections: IComboBoxOption[] = [
   {
      text: "Combined", key: "combined"
   },
   {
      text: "User", key: "user"
   },
   {
      text: "System", key: "system"
   }
]

const TelemetryPanel: React.FC<{}> = observer(({ }) => {

   const [timeKey, setTimeKey] = useState("time_2_weeks");
   const [cpuKey, setCPUKey] = useState("combined");

   useEffect(() => {

      const time = timeSelections.find(t => t.key === timeKey);
      if (time) {
         state.UpdateTelemetry(time.minutes);
      }

      return () => {
      };

   }, []);


   // subscribe
   if (state.currentData) { }

   const currentData = (state.currentData ?? []);

   if (!currentData.length) {
      return <Stack horizontalAlign="center">
         <Spinner size={SpinnerSize.large} />
      </Stack>;
   }


   const sparkWidth = 1100;
   const data = (currentData[0] as GetAgentTelemetrySampleResponse[]);
   const leases = ((currentData.length > 1) ? currentData[1] : []) as GetAgentLeaseResponse[];

   if (!data.length) {
      return null;
   }

   const cpuData = data.map(d => {
      if (cpuKey == "user") return d.userCpu;
      if (cpuKey == "system") return d.systemCpu;
      if (cpuKey == "combined") return d.userCpu + d.systemCpu;
      return 1;
   })

   const min = data[0].time;
   const max = data[data.length - 1].time;

   const minTime = getShortNiceTime(min, true);
   const maxTime = getShortNiceTime(max, true);



   const ramData = data.map(d => {
      return d.usedRam / d.totalRam;
   })

   let cpuText = "";
   let ramText = "";

   let cpuLabel = "";
   if (cpuKey == "user") cpuLabel = "CPU - User";
   if (cpuKey == "system") cpuLabel = "CPU - System";
   if (cpuKey == "combined") cpuLabel = "CPU - Combined";


   const devices = state.selectedAgent?.capabilities?.devices;
   if (devices?.length) {
      const device = devices[0];
      device.properties?.forEach(v => {
         if (v.startsWith("CPU=")) {
            cpuText = `${v.replace("CPU=", "")}`;
         }
   
         if (v.startsWith("RAM=")) {
            ramText = `${v.replace("RAM=", "")} GB`;
         }
      })   
   }

   function relativeCoords(event: any, id: string) {

      if (state.currentLeaseTooltip?.frozen) {
         return;
      }

      let bounds = event.target.getBoundingClientRect();
      let x = event.clientX - bounds.left;
      let y = event.clientY - bounds.top;

      // interpolate time

      const percent = ((x - 16) / sparkWidth);

      const time = new Date(min.getTime() + ((max.getTime() - min.getTime()) * percent));

      let lease: GetAgentLeaseResponse | undefined;
      leases.forEach(s => {
         if (time >= s.startTime && (!s.finishTime || time <= s.finishTime)) {
            lease = s;
         }
      });

      if (lease?.details?.type === "Job") {
         const jobId = lease.details.jobId;
         if (jobId) {
            if (!state.jobData.has(jobId)) {
               state.jobData.set(jobId, true);
               backend.getJob(jobId, { filter: "id,batches" }).then(job => {
                  state.jobData.set(jobId, job);
                  state.setLeaseTooltip(state.currentLeaseTooltip ? { ...state.currentLeaseTooltip } : undefined);
               })
            }
         }
      }

      state.setLeaseTooltip({ lease: lease, x: x, y: y, id: id, time: time });
   };

   return <Stack tokens={{ childrenGap: 24 }} style={{ paddingBottom: 32, paddingLeft: 8 }}>
      <Stack horizontal verticalAlign="center" verticalFill>
         <Text variant="mediumPlus">{minTime} - {maxTime}</Text>
         <Stack grow />
         <Stack horizontal tokens={{ childrenGap: 18 }}>

            <ComboBox
               label={"CPU"}
               styles={{ root: { width: 128 } }}
               options={cpuSelections}
               selectedKey={cpuKey}
               onChange={(ev, option, index, value) => {
                  if (!option) {
                     return;
                  }
                  setCPUKey(option.key as string);
               }}
            />

            <ComboBox
               label="Time"
               styles={{ root: { width: 180 } }}
               options={timeSelections}
               selectedKey={timeKey}
               onChange={(ev, option, index, value) => {
                  const select = option as TimeSelection;
                  setTimeKey(select.key);
                  state.UpdateTelemetry(select.minutes);
               }}
            />
         </Stack>
      </Stack>
      <Stack verticalAlign="center" verticalFill>
         <Stack horizontal>
            <Label>{cpuLabel}</Label>
            <Stack grow></Stack>
            {!!cpuText && <Label>{cpuText}</Label>}
         </Stack>
         <div onMouseLeave={() => state.setLeaseTooltip(undefined)} onMouseMove={(ev) => { relativeCoords(ev, "cpu") }} onClick={() => {

            if (!state.currentLeaseTooltip?.frozen) {
               if (state.currentLeaseTooltip) {
                  state.setLeaseTooltip({ ...state.currentLeaseTooltip, frozen: true });
               }
            }
         }}>
            <div style={{ position: "relative" }}>
               <TelemetryTooltip id={"cpu"} />
            </div>

            <Sparklines width={sparkWidth} height={128} data={cpuData} max={100} style={{ backgroundColor: dashboard.darktheme ? "#060709" : "#F3F2F1", padding: 8, border: "solid 1px #181A1B" }}>
               <SparklinesLine color={dashboard.darktheme ? "lightblue" : "#1E90FF"} />
               <SparklinesReferenceLine type="avg" />
            </Sparklines>
         </div>
      </Stack>
      <Stack verticalAlign="center" verticalFill>
         <Stack horizontal>
            <Label>RAM</Label>
            <Stack grow></Stack>
            {!!ramText && <Label>{ramText}</Label>}
         </Stack>
         <div onMouseLeave={() => state.setLeaseTooltip(undefined)} onMouseMove={(ev) => { relativeCoords(ev, "ram") }} onClick={() => {

            if (!state.currentLeaseTooltip?.frozen) {
               if (state.currentLeaseTooltip) {
                  state.setLeaseTooltip({ ...state.currentLeaseTooltip, frozen: true });
               }
            }
         }}>
            <div style={{ position: "relative" }}>
               <TelemetryTooltip id="ram" />
            </div>
            <Sparklines width={sparkWidth} height={128} data={ramData} max={1} style={{ backgroundColor: dashboard.darktheme ? "#060709" : "#F3F2F1", padding: 8, border: "solid 1px #181A1B" }}>
               <SparklinesLine color={dashboard.darktheme ? "lightblue" : "#1E90FF"} />
               <SparklinesReferenceLine type="avg" />
            </Sparklines>
         </div>
      </Stack>

   </Stack>
})

export const HistoryModal: React.FC<{ agentId: string | undefined, onDismiss: (...args: any[]) => any; }> = observer(({ agentId, onDismiss }) => {

   const [selectedAgent, setSelectedAgent] = useState<string | undefined>(undefined);
   const [actionState, setActionState] = useState<{ action?: string, confirmed?: boolean, comment?: string }>({});
   const actionTextInputRef = React.useRef<ITextField>(null);
   const forceRestartCheckboxRef = React.useRef<ICheckbox>(null);
   const [agentError, setAgentError] = useState(false);

   const { hordeClasses, modeColors } = getHordeStyling();
   const theme = getHordeTheme();

   if (agentError) {
      return <Dialog hidden={false} onDismiss={onDismiss} dialogContentProps={{
         type: DialogType.normal,
         title: 'Missing Agent',
         subText: `Unable to find agent ${agentId}`
      }}
         modalProps={{ styles: { main: { width: "640px !important", minWidth: "640px !important", maxWidth: "640px !important" } } }}>
         <DialogFooter>
            <PrimaryButton onClick={() => onDismiss()} text="Ok" />
         </DialogFooter>
      </Dialog>
   }

   //  subscribe to updates
   if (state.selectedAgent) { }

   if (!agentId) {
      if (selectedAgent) {
         setSelectedAgent(undefined);
      }
      return null;
   }

   if (selectedAgent !== agentId) {

      agentStore.updateAgent(agentId).then(agent => {

         state.setSelectedAgent(agent);

      }).catch(error => {
         console.error(`Unable to find agent id: ${agentId} ${error}`);
         setAgentError(true);
         return;
      })

      setSelectedAgent(agentId);
   }

   if (!state.selectedAgent) {
      if (agentId) {
         return <Modal isOpen={true} isBlocking={true} topOffsetFixed={true} styles={{ main: { padding: 16, width: 1200, hasBeenOpened: false, top: "80px", position: "absolute" } }} className={hordeClasses.modal}>
            <Stack tokens={{ childrenGap: 40 }} styles={{ root: { padding: 8 } }}>
               <Stack grow verticalAlign="center">
                  <Text variant="mediumPlus" styles={{ root: { fontWeight: "unset", fontFamily: "Horde Open Sans SemiBold", fontSize: "20px" } }}>Agent {agentId}</Text>
               </Stack>
               <Stack verticalAlign="center">
                  <Spinner size={SpinnerSize.large} />
               </Stack>
            </Stack>
         </Modal>
      }

      return null;
   }

   type AgentAction = {
      name: string;
      confirmText: string;
      textInput?: boolean;
      update?: (request: UpdateAgentRequest, comment?: string) => void;
   }
   const actions: AgentAction[] = [
      {
         name: 'Enable',
         confirmText: "Are you sure you would like to enable this agent?",
         update: (request) => { request.enabled = true }
      },
      {
         name: 'Disable',
         confirmText: "Are you sure you would like to disable this agent?",
         textInput: true,
         update: (request, comment) => { request.enabled = false; request.comment = comment }
      },
      {
         name: 'Cancel Leases',
         confirmText: "Are you sure you would like to cancel this agent's leases?"
      },
      {
         name: 'Request Conform',
         confirmText: "Are you sure you would like to request an agent conform?",
         update: (request) => { request.requestConform = true }
      },
      {
         name: 'Request Full Conform',
         confirmText: "Are you sure you would like to request a full agent conform?",
         update: (request) => { request.requestFullConform = true }

      },
      {
         name: 'Request Restart',
         confirmText: "Are you sure you would like to request an agent restart?",
         update: (request) => { request.requestRestart = !forceRestartCheckboxRef.current?.checked; request.requestForceRestart = !!forceRestartCheckboxRef.current?.checked; }
      },
      {
         name: 'Edit Comment',
         confirmText: "Please enter new comment",
         textInput: true,
         update: (request, comment) => { request.comment = comment }
      }
   ];

   // cancel leases action
   const cancelLeases = async () => {

      const leases = new Set<string>();

      state.selectedAgent?.leases?.forEach(lease => {
         if (!lease.finishTime) {
            leases.add(lease.id);
         }
      });

      let requests = Array.from(leases).map(id => {
         return backend.updateLease(id, { aborted: true });
      });

      while (requests.length) {

         const batch = requests.slice(0, 5);

         await Promise.all(batch).then(() => {

         }).catch((errors) => {
            console.log(errors);
            // eslint-disable-next-line
         }).finally(() => {

            requests = requests.slice(5);
         });
      }
      agentStore.update();
   };


   const currentAction = actions.find(a => a.name === actionState.action);

   if (currentAction && actionState.confirmed) {
      if (currentAction.update) {
         const request: UpdateAgentRequest = {};
         currentAction.update(request, actionState.comment);
         backend.updateAgent(agentId, request).then(() => {
            agentStore.update(agentStore.pools?.length ? true : false).then(() => {
               state.setSelectedAgent(agentStore.agents.find(agent => agent.id === agentId));
               setActionState({});
            });
         }).catch((reason) => {
            console.error(reason);
         });
      } else if (currentAction.name === "Cancel Leases") {
         (async () => {
            await cancelLeases();
            setActionState({});
         })();
      }
   }

   const actionMenuProps: IContextualMenuProps = {
      shouldFocusOnMount: true,
      items: actions.map(a => {
         return {
            key: a.name.toLowerCase(),
            text: a.name,
            onClick: () => {
               setActionState({ action: a.name })
            }
         }
      })
   };

   const poolItems: IContextualMenuItem[] = [];;
   const agent = state.selectedAgent;
   agent.pools?.forEach(poolId => {

      const pool = agentStore.pools?.find(pool => poolId === pool.id);
      if (pool) {
         poolItems.push({
            key: `pools_${poolId}`,
            text: pool.name,
            href: `/pools?pool=${pool.id}`,
            target: "_blank"
         })
      }
   })

   function onColumnClick(ev: React.MouseEvent<HTMLElement>, column: IColumn) {
      //historyModalState.setSorted(column.key);
   }

   function generateColumns() {
      let columns: IColumn[] = [];
      if (state.mode === "leases") {
         columns = [
            {
               key: 'type',
               name: 'Type',
               minWidth: 125,
               maxWidth: 125,
               isResizable: false,
               isSorted: false,
               isSortedDescending: false,
               onColumnClick: onColumnClick
            },
            {
               key: 'id',
               name: 'ID',
               minWidth: 185,
               maxWidth: 185,
               isResizable: false,
               isSorted: false,
               isSortedDescending: false,
               onColumnClick: onColumnClick
            },
            {
               key: 'description',
               name: 'Description',
               minWidth: 350,
               isResizable: false,
               isSorted: false,
               isSortedDescending: false,
               onColumnClick: onColumnClick
            }
         ];
      }
      else if (state.mode === "sessions") {
         columns = [
            {
               key: 'id',
               name: 'ID',
               minWidth: 692,
               maxWidth: 692,
               isResizable: false,
               isSorted: false,
               isSortedDescending: false,
               onColumnClick: onColumnClick
            }
         ];
      }

      if (state.mode === "leases" || state.mode === "sessions") {


         columns.push({
            key: 'startTime',
            name: 'Start Time',
            minWidth: 200,
            maxWidth: 200,
            isResizable: false,
            isSorted: false,
            isSortedDescending: false,
            onColumnClick: onColumnClick
         });
         columns.push({
            key: 'endTime',
            name: 'Finish Time',
            minWidth: 200,
            maxWidth: 200,
            isResizable: false,
            isSorted: false,
            isSortedDescending: false,
            onColumnClick: onColumnClick
         });
      }

      if (state.mode === "leases") {
         columns.find(col => col.key === state.sortedLeaseColumn)!.isSorted = true;
         columns.find(col => col.key === state.sortedLeaseColumn)!.isSortedDescending = state.sortedLeaseColumnDescending;
      }
      else if (state.mode === "sessions") {
         columns.find(col => col.key === state.sortedSessionColumn)!.isSorted = true;
         columns.find(col => col.key === state.sortedSessionColumn)!.isSortedDescending = state.sortedSessionColumnDescending;
      }

      return columns;
   }

   // main header
   const onRenderDetailsHeader: IDetailsListProps['onRenderDetailsHeader'] = (props) => {
      const customStyles: Partial<IDetailsHeaderStyles> = {};
      customStyles.root = { paddingTop: 0 };
      if (props) {
         return (
            <Sticky stickyPosition={StickyPositionType.Header} isScrollSynced={true}>
               <DetailsHeader {...props} styles={customStyles} onRenderColumnHeaderTooltip={onRenderColumnHeaderTooltip} />
            </Sticky>
         );
      }
      return null;
   };

   const onRenderColumnHeaderTooltip: IDetailsHeaderProps['onRenderColumnHeaderTooltip'] = (props: any) => {
      const customStyles: Partial<ITooltipHostStyles> = {};
      if (props) {
         customStyles.root = { selectors: { "> span": { paddingLeft: '8px !important', display: 'flex', justifyContent: 'center' } } };

         /*
          // no other way to filter children being centered other than to drill into private members??
          if (props.children._owner.child?.child.child.child.elementType === "span") {
              const data = props.children._owner.child.child.child.child.child.child.stateNode.data;
              // if this cluster happens to be true, reset back to the default, because doing this the other way around
              // takes too long when the columns update on details switch
              // ugh :(
              if (data === "ID" || data === "Description") {
                  customStyles.root = {};
              }
          }
          */
         return <Text styles={customStyles}>{props.children}</Text>;
      }
      return null;
   };

   function onRenderHistoryItem(item: LeaseData | SessionData, index?: number, column?: IColumn) {

      let link = "";

      if (state.mode === "leases") {
         const lease = item as LeaseData;
         switch (column!.key) {
            case 'type':

               if (lease.batch) {

                  const step = lease.batch.steps.find(s => s.outcome === JobStepOutcome.Failure);

                  let name = lease.type;

                  if (lease.batch.error !== JobStepBatchError.None) {
                     name = lease.batch.error;
                  } else if (step) {
                     name = "StepError";
                  }

                  return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'start'} verticalAlign="center" tokens={{ childrenGap: 4 }}>
                     <Stack >
                        {!step && <BatchStatusIcon batch={lease.batch} />}
                        {!!step && <StepStatusIcon step={step} />}
                     </Stack>
                     <Stack >{name}</Stack>
                  </Stack>
               } else if (lease.outcome) {
                  return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'start'} verticalAlign="center" tokens={{ childrenGap: 4 }}>
                     <Stack >
                        <LeaseStatusIcon lease={lease} />
                     </Stack>
                     <Stack >{lease.type}</Stack>
                  </Stack>
               }
               return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'center'}><Stack.Item align={"center"}>{lease.type}</Stack.Item></Stack>
            case 'id':
               link = "";
               if (lease.details && 'LogId' in lease.details) {
                  link = '/log/' + lease.details['LogId'];
               }
               else if (lease.logId) {
                  link = `/log/${lease.logId}`;
               }
               if (link) {
                  return <Stack style={{ height: "100%" }} verticalAlign="center"><Link to={link}>{lease.id}</Link></Stack>
               }
               return <Stack style={{ height: "100%" }} verticalAlign="center">{lease.id}</Stack>;
            case 'name':
               return <Stack>{lease.name}</Stack>;
            case 'startTime':
               return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'center'}><Stack.Item align={"center"}>{getShortNiceTime(lease.startTime, false, true, true)}</Stack.Item></Stack>
            case 'endTime':
               return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'center'}><Stack.Item align={"center"}>{getShortNiceTime(lease.finishTime, false, true, true)}</Stack.Item></Stack>
            case 'description':
               link = "";
               let name = lease.name;
               if (lease.details) {
                  if ('jobId' in lease.details) {
                     link = `/job/${lease.details['jobId']}`;
                     if ('batchId' in lease.details) {
                        link += `?batch=${lease.details['batchId']}`
                     }
                  }
               }
               else if (lease.jobId) {
                  link = `/job/${lease.jobId}`;
               }

               if (!link && (lease.parentId && lease.details && lease.details["parentLogId"])) {
                  link = `/log/${lease.details["parentLogId"]}`
                  return <Stack styles={{ root: { height: '100%' } }} horizontal><Stack.Item align={"center"}><Text style={{ fontSize: "12px" }}>{name} (parent: </Text><Link style={{ fontSize: "12px" }} to={link}>{lease.parentId}</Link><Text style={{ fontSize: "12px" }}>)</Text></Stack.Item></Stack>;
               }

               if (link !== "") {
                  return <Stack styles={{ root: { height: '100%' } }} horizontal><Stack.Item align={"center"}><Link style={{ fontSize: 12 }} key={"leaseText_" + lease.id} to={link}>{name}</Link></Stack.Item></Stack>;
               }
               else {
                  return <Stack styles={{ root: { height: '100%', } }} horizontal><Stack.Item align={"center"}><Text styles={{ root: { fontSize: 12 } }} key={"leaseText_" + lease.id}>{name}</Text></Stack.Item></Stack>;
               }
            default:
               return <span>{lease[column!.fieldName as keyof LeaseData] as string}</span>;
         }
      }
      else if (state.mode === "sessions") {
         const session = item as SessionData;
         switch (column!.key) {
            case 'id':
               return <Stack styles={{ root: { height: '100%', } }} horizontal><Stack.Item align={"center"}>{session.id}</Stack.Item></Stack>
            case 'startTime':
               return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'center'}><Stack.Item align={"center"}>{getShortNiceTime(session.startTime, false, true)}</Stack.Item></Stack>
            case 'endTime':
               return <Stack styles={{ root: { height: '100%', } }} horizontal horizontalAlign={'center'}><Stack.Item align={"center"}>{getShortNiceTime(session.finishTime, false, true)}</Stack.Item></Stack>
            default:
               return <span>{session[column!.fieldName as keyof SessionData] as string}</span>;
         }
      }
   }

   const onRenderInfoDetailsHeader: IDetailsListProps['onRenderDetailsHeader'] = (props) => {
      const customStyles: Partial<IDetailsHeaderStyles> = {};
      customStyles.root = { paddingTop: 0 };
      if (props) {
         return (
            <Sticky stickyPosition={StickyPositionType.Header} isScrollSynced={true}>
               <DetailsHeader {...props} styles={customStyles} />
            </Sticky>
         );
      }
      return null;
   };

   const c1 = theme.horde.darkTheme ? theme.horde.neutralBackground : "#f3f2f1";
   const c2 = theme.horde.darkTheme ? theme.horde.contentBackground : "#ffffff";

   function onRenderBuilderInfoCell(nestingDepth?: number | undefined, item?: any, index?: number | undefined) {
      return (
         <Stack horizontal onClick={(ev) => { state.setInfoItemSelected(item); ev.preventDefault(); }} styles={{
            root: {
               background: item!.selected ? c1 : c2,
               paddingLeft: 48 + (10 * nestingDepth!),
               paddingTop: 8,
               paddingBottom: 8,
               selectors: {
                  ":hover": {
                     background: c1,
                     cursor: 'pointer'
                  }
               }
            }
         }}>
            <Stack>
               <Link to="" onClick={(ev) => { state.setInfoItemSelected(item); ev.preventDefault(); }}><Text>{item!.name}</Text></Link>
            </Stack>
         </Stack>
      );
   }

   const groups: IGroup[] = [
      {
         count: state.agentItemCount,
         key: "agent",
         name: "Agent",
         startIndex: 0,
         level: 0,
         isCollapsed: false,
      },
      {
         count: state.devicesItemCount,
         key: "devices",
         name: "Devices",
         startIndex: state.agentItemCount,
         level: 0,
         isCollapsed: false,
      },
      {
         count: state.workspaceItemCount,
         key: "workspaces",
         name: "Workspaces",
         startIndex: state.agentItemCount + state.devicesItemCount,
         level: 0,
         isCollapsed: false,
      },

   ];

   const onDismissHistoryModal = function (ev?: React.MouseEvent<HTMLButtonElement, MouseEvent> | undefined) {
      state.setSelectedAgent(undefined);
      onDismiss();
   };

   return (
      <Dialog
         modalProps={{
            isBlocking: false,
            topOffsetFixed: true,
            styles: {
               main: {
                  hasBeenOpened: false,
                  top: "80px",
                  position: "absolute"
               },
               root: {
                  selectors: {
                     ".ms-Dialog-title": {
                        paddingTop: '24px',
                        paddingLeft: '32px'
                     }
                  }
               }
            }
         }}
         onDismiss={onDismissHistoryModal}
         className={historyStyles.dialog}
         hidden={!state.selectedAgent}
         minWidth={1200}
         dialogContentProps={{
            type: DialogType.close,
            onDismiss: onDismissHistoryModal,
            title: `Agent ${state.selectedAgent?.name}`,
         }}
      >
         <Stack>
            {!!currentAction && <Dialog
               hidden={false}
               onDismiss={() => { setActionState({}) }}
               minWidth={512}
               dialogContentProps={{
                  type: DialogType.normal,
                  title: `Confirm Action`,
               }}
               modalProps={{ isBlocking: true }} >
               <Stack style={{ paddingBottom: 18, paddingLeft: 4 }}>
                  <Text>{currentAction.confirmText}</Text>
               </Stack>
               {!!currentAction.textInput && <TextField componentRef={actionTextInputRef} label={currentAction.name === "Edit Comment" ? "New Comment" : "Disable Reason"} />}
               {currentAction.name === "Request Restart" && <Checkbox componentRef={forceRestartCheckboxRef} label={"Force Restart"} />}
               <DialogFooter>
                  <PrimaryButton disabled={actionState.confirmed} onClick={() => { setActionState({ ...actionState, confirmed: true, comment: actionTextInputRef.current?.value }) }} text={currentAction.name} />
                  <DefaultButton disabled={actionState.confirmed} onClick={() => { setActionState({}) }} text="Cancel" />
               </DialogFooter>
            </Dialog>
            }
            <Stack horizontal styles={{ root: { paddingLeft: 2 } }}>
               <Stack grow>
                  <Stack horizontalAlign={"start"}>
                     <Pivot className={hordeClasses.pivot}
                        onLinkClick={(item?: PivotItem | undefined) => { state.setMode(item?.props.itemKey); }}
                        linkSize="normal"
                        linkFormat="links"
                        defaultSelectedKey={state.mode ?? "info"}
                     >
                        <PivotItem headerText="Info" itemKey="info" />
                        <PivotItem headerText="Sessions" itemKey="sessions" />
                        <PivotItem headerText="Leases" itemKey="leases" />
                        <PivotItem headerText="Telemetry" itemKey="telemetry" />
                     </Pivot>
                  </Stack>
               </Stack>
               {!!poolItems.length && <Stack style={{ paddingRight: 24 }}><DefaultButton text="Pools" menuProps={{ shouldFocusOnMount: true, items: poolItems }} /></Stack>}
               {!!dashboard.user?.dashboardFeatures?.showRemoteDesktop && <Stack> <Stack horizontal tokens={{ childrenGap: 24 }}>
                  <Stack>
                     <DefaultButton text="Actions" menuProps={actionMenuProps} />
                  </Stack>
                  <Stack>
                     <Link to={`/audit/agent/${encodeURIComponent(agentId)}`} ><DefaultButton text="Audit" onClick={(ev) => { }} /></Link>
                  </Stack>

                  <Stack>
                     <DefaultButton text="Remote Desktop" onClick={() => {
                        if (state.selectedAgent?.id) {
                           // @todo: this assumes the id is the ip, should have a private ip property on agents instead
                           let ip = state.selectedAgent?.id;
                           if (ip.startsWith("10-")) {
                              ip = ip.replaceAll("-", ".")
                           }
                           window.open(`ugs://rdp?host=${ip}`, "_self")
                        }
                     }} />
                  </Stack>
               </Stack>
               </Stack>}
            </Stack>
            <Stack styles={{ root: { paddingTop: 30 } }}>
               {state.mode === "info" && <Stack horizontal tokens={{ childrenGap: 20 }}>
                  <Stack styles={{ root: { width: 300 } }}>
                     <GroupedList
                        items={state.infoItems}
                        compact={true}
                        onRenderCell={onRenderBuilderInfoCell}
                        groups={groups}
                        selection={new Selection()}
                        selectionMode={SelectionMode.none}
                        groupProps={{
                           showEmptyGroups: true,
                           onRenderHeader: (props) => {
                              return <Link to="" onClick={(ev) => { ev.preventDefault(); props!.onToggleCollapse!(props!.group!); }}> <GroupHeader {...props} /></Link>;
                           },
                           headerProps: {
                              styles: {
                                 title: {
                                    fontFamily: "Horde Open Sans Semibold",
                                    color: modeColors.text,
                                    paddingLeft: 0

                                 },
                                 headerCount: {
                                    display: 'none'
                                 }
                              }
                           }
                        }}
                     />
                  </Stack>
                  <Stack styles={{ root: { width: '100%' } }}>
                     <Stack.Item className={hordeClasses.relativeModalSmall}>
                        <ScrollablePane scrollbarVisibility={ScrollbarVisibility.auto} styles={{ contentContainer: { overflowX: 'hidden' } }}>
                           <DetailsList
                              className={historyStyles.detailsList}
                              compact={true}
                              items={state.infoSubItems}
                              columns={[
                                 { key: 'column1', name: 'Name', fieldName: 'name', minWidth: 200, maxWidth: 200, isResizable: false },
                                 { key: 'column2', name: 'Value', fieldName: 'value', minWidth: 200, isResizable: false }
                              ]}
                              layoutMode={DetailsListLayoutMode.justified}
                              onRenderDetailsHeader={onRenderInfoDetailsHeader}
                              constrainMode={ConstrainMode.unconstrained}
                              selectionMode={SelectionMode.none}
                              onRenderRow={(props) => {

                                 if (props) {
                                    return <DetailsRow styles={{ cell: { whiteSpace: "pre-line", overflowWrap: "break-word" } }} {...props} />
                                 }

                                 return null;
                              }}
                           />
                        </ScrollablePane>
                     </Stack.Item>
                  </Stack>
               </Stack>
               }
               {(state.mode === "leases" || state.mode === "sessions") &&
                  <Stack.Item className={hordeClasses.relativeModalSmall}>
                     <ScrollablePane scrollbarVisibility={ScrollbarVisibility.auto} styles={{ contentContainer: { overflowX: 'hidden' } }}>
                        <DetailsList
                           className={historyStyles.detailsList}
                           compact={true}
                           items={state.currentData}
                           columns={generateColumns()}
                           onRenderDetailsHeader={onRenderDetailsHeader}
                           onRenderItemColumn={onRenderHistoryItem}
                           layoutMode={DetailsListLayoutMode.justified}
                           constrainMode={ConstrainMode.unconstrained}
                           selectionMode={SelectionMode.none}
                           listProps={{ renderedWindowsAhead: 1, renderedWindowsBehind: 1 }}
                           onRenderMissingItem={() => { state.nullItemTrigger(); return <div></div> }}
                        />
                     </ScrollablePane>
                  </Stack.Item>}
               {state.mode === "telemetry" && <TelemetryPanel />}
            </Stack>
         </Stack>
         <DialogFooter>
            <PrimaryButton onClick={() => { state.setSelectedAgent(undefined); onDismiss() }}>Close</PrimaryButton>
         </DialogFooter>
      </Dialog>
   );
});