import { onCommand, type CommandEvent } from "../../browser/src/platform/commands";
import { copyText } from "../../browser/src/platform/clipboard";
import { isDefaultBrowser, setAsDefaultBrowser, systemInfo } from "../../browser/src/platform/app";
import { beginTracing, endTracing, isTracing, openTaskManager } from "../../browser/src/platform/performance";
import { confirm } from "../../browser/src/platform/dialogs";
import { revealFile } from "../../browser/src/platform/files";
import { readDocument, saveDocument } from "../../browser/src/platform/documents";
import { afterFirstWindow } from "../../browser/src/lib/afterFirstWindow";
import { diagnostics, sendFeedback } from "../../browser/src/state/app/appIntegration";
import { finishOnboarding, maybeStartOnboarding, onboardingCompletedAt, startOnboarding, useOnboarding } from "../../browser/src/state/onboarding/onboarding";
import { offerDefaultBrowserAgain } from "../../browser/src/state/onboarding/defaultBrowserOffer";
import { openVideoTour, startToolTour, videoTourUrl } from "../../browser/src/state/onboarding/tour";
import { trackAppVersion } from "../../browser/src/state/releaseNotes/releaseNotes";
import { openReleaseNotesAfterUpdate, openReleaseNotesPage } from "../../browser/src/state/releaseNotes/releaseNotesPage";
import { showToast } from "../../browser/src/state/splits/actions";
import type { LinuxCapabilities } from "./hostCapabilities.ts";

export const APP_LAUNCH_LOCK = "ardia.linux.app-launch";
export const APP_LAUNCH_MARK = "ardia.linux.app-launch";
export const ONBOARDING_DOCUMENT = "onboarding.json";

export type LaunchLocks = {
  request(name: string, options: { ifAvailable: true }, callback: (lock: unknown) => Promise<void> | void): Promise<unknown>;
};
export type LaunchMarks = { getItem(key: string): string | null; setItem(key: string, value: string): void };
export type LaunchClaim = "ran" | "notStartup" | "alreadyRan" | "heldElsewhere" | "unavailable";

const report = (error: unknown) => console.warn("[ardia linux] app services", error);

export function claimAppLaunch(options: { startup: boolean; locks?: LaunchLocks; marks?: LaunchMarks; run: () => void }): Promise<LaunchClaim> {
  const { startup, locks, marks, run } = options;
  return new Promise((resolve) => {
    if (!startup) return resolve("notStartup");
    if (marks?.getItem(APP_LAUNCH_MARK)) return resolve("alreadyRan");
    if (!locks) return resolve("unavailable");
    locks.request(APP_LAUNCH_LOCK, { ifAvailable: true }, (lock) => {
      if (!lock) return resolve("heldElsewhere");
      marks?.setItem(APP_LAUNCH_MARK, "1");
      try {
        run();
      } catch (error) {
        report(error);
      }
      resolve("ran");
      return new Promise<void>(() => {});
    }).catch((error: unknown) => {
      report(error);
      resolve("unavailable");
    });
  });
}

export function runLaunchServices(windowId: string, existingProfile: boolean, capabilities: LinuxCapabilities) {
  const updated = trackAppVersion();
  if (existingProfile && readDocument(ONBOARDING_DOCUMENT) === null) finishOnboarding();
  else maybeStartOnboarding();
  const onboarding = useOnboarding.getState().windowId;
  if (onboarding && onboarding !== windowId) useOnboarding.setState({ windowId });
  if (updated) openReleaseNotesAfterUpdate();
  if (!capabilities.defaultBrowser) return;
  afterFirstWindow(() => {
    void offerDefaultBrowserAgain({
      read: readDocument,
      save: saveDocument,
      testInstance: systemInfo().isolatedInstance !== false,
      onboarding: !onboardingCompletedAt() || useOnboarding.getState().windowId !== null,
      isDefault: isDefaultBrowser,
      setDefault: setAsDefaultBrowser,
    }).catch(report);
  });
}

const unavailable = (windowId: string, feature: string) => showToast(windowId, "Not available on Linux yet", `${feature} isn't available in this Linux build.`);

async function recordPerformanceIssue() {
  if (await isTracing()) return;
  const start = await confirm({
    title: "Record a performance issue?",
    message: "Ardia records what the engine does while you reproduce the problem. The recording is saved to your Downloads folder.",
    confirmTitle: "Start Recording",
  });
  if (!start.confirmed || !(await beginTracing())) return;
  const stop = await confirm({ title: "Recording…", message: "Reproduce the problem now, then click Stop.", confirmTitle: "Stop", cancelTitle: "Discard" });
  const path = await endTracing(stop.confirmed);
  if (path) void revealFile(path);
}

export function runAppCommand(windowId: string, command: CommandEvent["command"], capabilities: LinuxCapabilities): boolean {
  switch (command) {
    case "sendFeedback":
      void sendFeedback(windowId).then(
        () => !capabilities.externalLinks && !systemInfo().feedbackURL && showToast(windowId, "Feedback report copied", "Paste it into an email to the Ardia team."),
        () => unavailable(windowId, "Send Feedback"),
      );
      return true;
    case "copyDiagnostics":
      void diagnostics().then(copyText, report);
      return true;
    case "recordPerformanceIssue":
      if (capabilities.performanceRecording) void recordPerformanceIssue().catch(() => unavailable(windowId, "Performance recording"));
      else unavailable(windowId, "Performance recording");
      return true;
    case "showOnboarding":
      startOnboarding(windowId);
      return true;
    case "toolTour":
      void startToolTour(windowId).catch(report);
      return true;
    case "videoTour":
      if (videoTourUrl()) openVideoTour(windowId);
      else unavailable(windowId, "The video tour");
      return true;
    case "releaseNotes":
      if (systemInfo().releaseNotesURL) openReleaseNotesPage(windowId);
      else unavailable(windowId, "Release notes");
      return true;
    case "taskManager":
      if (capabilities.taskManager) void openTaskManager().catch(() => unavailable(windowId, "Task Manager"));
      else unavailable(windowId, "Task Manager");
      return true;
  }
  return false;
}

export function startAppCommands(windowId: string, capabilities: LinuxCapabilities, subscribe: typeof onCommand = onCommand) {
  return subscribe((event) => {
    if (event.windowId === windowId) runAppCommand(windowId, event.command, capabilities);
  });
}

export function startLinuxAppServices(options: {
  windowId: string;
  startup: boolean;
  existingProfile: boolean;
  capabilities: LinuxCapabilities;
  locks?: LaunchLocks;
  marks?: LaunchMarks;
}) {
  const { windowId, startup, existingProfile, capabilities, locks, marks } = options;
  const commands = startAppCommands(windowId, capabilities);
  const launch = claimAppLaunch({ startup, locks, marks, run: () => runLaunchServices(windowId, existingProfile, capabilities) });
  return { commands, launch };
}
