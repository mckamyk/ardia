# Fix handoff: file and folder choosers per window, never stuck (#330)

Native, in `ac_linux_files.cc` and a new header `ac_linux_picker_slots.h`. Needs the next Chromium build.

## Cause

`LinuxPickFiles` and `LinuxPickFolder` refused a chooser while any other chooser of their kind was open anywhere in the app (`FilesPickerOpen()`/`FolderPickerOpen()`), and only the chooser's own answer cleared the flag. Suppose a GTK chooser was left behind after its window closed, or xdg-desktop-portal died mid-dialog (Chromium's portal request has no timeout). Then Open File and the download-folder picker failed with `ERR_*_PICKER_OPEN` in every window until restart. Both choosers also passed no `SelectFilePolicy`, so the enterprise `AllowFileSelectionDialogs` policy was ignored.

## Fix

- `PickerSlots<Window, Picker>` keeps one chooser per window. `Admit` answers `kOpen` when the window has none. It answers `kAlreadyShowing` when the held chooser's dialog still runs for that window (`SelectFileDialog::IsRunning(window)`), and the request is refused with `ERR_FILE_PICKER_OPEN`/`ERR_FOLDER_PICKER_OPEN` ("… already open in this window"). It answers `kReplaceStale` when the dialog no longer runs without having answered: the old chooser is abandoned (its caller gets the cancelled answer, an empty list or null; `ListenerDestroyed` detaches it from the dead dialog), and a new chooser opens. Other windows are never affected.
- Both choosers are created with `ChromeSelectFilePolicy(nullptr)`. When `ChromeSelectFilePolicy::FileSelectDialogsAllowed()` is false, the request fails with `ERR_FILE_CHOOSER_DISABLED`, the code the import chooser already uses, instead of opening a dialog.

## Tests

- `tests/test_picker_slots.py` (5): compiles `picker_slots_test.cc` under ASan/UBSan and checks the per-window admission and the stale replacement. It also checks that both entry points use the slots, abandon a stale chooser as cancelled, release only their own slot, and honour the policy.
- `test_open_files.py` and `test_downloads.py` follow the slot release.
- `scripts/linux/syntax-check-156.sh ac_linux_files.cc`: passes.
