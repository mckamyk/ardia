export const PICTURE_IN_PICTURE_STATE = 'post("result", JSON.stringify(!!document.pictureInPictureElement || !!window.documentPictureInPicture?.window))';
export const CLOSE_DOCUMENT_PICTURE_IN_PICTURE = "window.documentPictureInPicture?.window?.close()";

export const UNSAVED_WORK = `(() => {
  const fields = Array.from(document.querySelectorAll("input:not([type=hidden]):not([type=password]):not([type=submit]):not([type=button]), textarea, select"));
  const changed = (f) => f.type === "checkbox" || f.type === "radio" ? f.checked !== f.defaultChecked : f.tagName === "SELECT" ? Array.from(f.options).some((o) => o.selected !== o.defaultSelected) : f.value !== f.defaultValue;
  const editing = document.activeElement && document.activeElement.isContentEditable;
  post("result", JSON.stringify(fields.some(changed) || !!editing));
})()`;

export type HostPageScript = { command: string; args: unknown[]; map?: (result: unknown) => unknown };

export function hostPageScript(method: string, args: readonly unknown[]): HostPageScript | null {
  if (args.length !== 1) return null;
  if (method === "evaluate" && args[0] === PICTURE_IN_PICTURE_STATE) return { command: "pictureInPictureState", args: [] };
  if (method === "executeJavaScript" && args[0] === CLOSE_DOCUMENT_PICTURE_IN_PICTURE) return { command: "exitPictureInPicture", args: [] };
  if (method === "evaluate" && args[0] === UNSAVED_WORK) return { command: "editedInput", args: [], map: (edited) => edited !== false };
  return null;
}
