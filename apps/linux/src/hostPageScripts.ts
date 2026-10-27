export const PICTURE_IN_PICTURE_STATE = 'post("result", JSON.stringify(!!document.pictureInPictureElement || !!window.documentPictureInPicture?.window))';
export const CLOSE_DOCUMENT_PICTURE_IN_PICTURE = "window.documentPictureInPicture?.window?.close()";

export type HostPageScript = { command: string; args: unknown[] };

export function hostPageScript(method: string, args: readonly unknown[]): HostPageScript | null {
  if (args.length !== 1) return null;
  if (method === "evaluate" && args[0] === PICTURE_IN_PICTURE_STATE) return { command: "pictureInPictureState", args: [] };
  if (method === "executeJavaScript" && args[0] === CLOSE_DOCUMENT_PICTURE_IN_PICTURE) return { command: "exitPictureInPicture", args: [] };
  return null;
}
