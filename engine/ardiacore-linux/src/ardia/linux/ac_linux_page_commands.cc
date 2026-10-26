#include "ardia/linux/ac_linux_page_commands.h"

#include <utility>

#include "ardia/linux/ac_linux_find.h"
#include "base/functional/bind.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_toggle_action.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/content_restriction.h"
#include "chrome/common/pref_names.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_utils.h"
#include "pdf/buildflags.h"
#include "printing/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_PRINTING)
#include "chrome/browser/printing/print_view_manager_common.h"
#endif
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
#include "chrome/browser/printing/print_preview_dialog_controller.h"
#include "chrome/browser/printing/print_view_manager.h"
#endif
#if BUILDFLAG(ENABLE_PDF)
#include "chrome/browser/pdf/pdf_extension_util.h"
#include "components/pdf/browser/pdf_frame_util.h"
#include "pdf/pdf_features.h"
#endif

namespace ardiacore {
namespace {

base::DictValue Error(std::string_view code, std::string_view message) {
  return base::DictValue().Set("code", code).Set("message", message);
}

}

LinuxPageCommands::LinuxPageCommands(content::WebContents* contents,
                                     Profile* service_profile,
                                     Owner owner,
                                     Emit emit,
                                     bool allow_native_ui,
                                     Presentation devtools_presentation)
    : WebContentsObserver(contents),
      contents_(contents->GetWeakPtr()),
      service_profile_(service_profile),
      owner_(std::move(owner)),
      allow_native_ui_(allow_native_ui),
      devtools_presentation_(std::move(devtools_presentation)) {
  find_ = std::make_unique<LinuxFind>(
      contents,
      base::BindRepeating(
          [](base::WeakPtr<LinuxPageCommands> commands) {
            return commands && commands->IsCurrent();
          },
          weak_factory_.GetWeakPtr()),
      std::move(emit));
}

LinuxPageCommands::~LinuxPageCommands() {
  Reset();
}

bool LinuxPageCommands::Handles(std::string_view command) {
  return command == "find" || command == "stopFinding" || command == "print" ||
         command == "runPageCommand" || command == "showDevTools";
}

bool LinuxPageCommands::IsCurrent() const {
  auto contents = contents_;
  auto* profile = service_profile_.get();
  auto owner = owner_;
  if (!contents || !profile || !owner)
    return false;
  auto* browser = owner.Run();
  return contents && browser && browser->GetProfile() == profile &&
         profile->IsRegularProfile() && !profile->IsOffTheRecord() &&
         profile->GetPath().BaseName().value() == chrome::kInitialProfile &&
         contents->GetBrowserContext() == profile &&
         browser->GetType() != BrowserWindowInterface::TYPE_DEVTOOLS &&
         browser->GetTabStripModel() &&
         browser->GetTabStripModel()->GetIndexOfWebContents(contents.get()) !=
             TabStripModel::kNoTab;
}

std::optional<base::DictValue> LinuxPageCommands::Dispatch(
    std::string_view command,
    const base::ListValue& args) {
  if (!IsCurrent())
    return Error("ERR_PAGE_STALE_TARGET", "Page claim is no longer current");
  if (command == "find") {
    if (args.size() != 3 || !args[0].is_string() || !args[1].is_bool() ||
        !args[2].is_bool()) {
      return Error("ERR_PAGE_ARGUMENT",
                   "find requires text, forward and findNext");
    }
    if (!find_->Start(base::UTF8ToUTF16(args[0].GetString()),
                      args[1].GetBool())) {
      return Error("ERR_PAGE_UNAVAILABLE", "Find helper is unavailable");
    }
    return std::nullopt;
  }
  if (command == "stopFinding") {
    if (args.size() != 1 || !args[0].is_bool()) {
      return Error("ERR_PAGE_ARGUMENT", "stopFinding requires clearSelection");
    }
    if (!find_->Stop(args[0].GetBool())) {
      return Error("ERR_PAGE_UNAVAILABLE", "Find helper is unavailable");
    }
    return std::nullopt;
  }
  if (command == "print") {
    if (!args.empty())
      return Error("ERR_PAGE_ARGUMENT", "print takes no arguments");
    return Print(false);
  }
  if (command == "runPageCommand") {
    if (args.size() != 1 || !args[0].is_string()) {
      return Error("ERR_PAGE_ARGUMENT", "runPageCommand requires a command");
    }
    const auto& action = args[0].GetString();
    if (action == "savePage")
      return Save();
    if (action == "systemPrint")
      return Print(true);
    return Error("ERR_PAGE_UNSUPPORTED", "Unsupported page command");
  }
  if (command == "showDevTools") {
    if (args.size() > 1 || (!args.empty() && !args[0].is_string())) {
      return Error("ERR_PAGE_ARGUMENT", "showDevTools takes an optional panel");
    }
    const std::string action = args.empty() ? "show" : args[0].GetString();
    if (!args.empty() && action != "toggle" && action != "inspect" &&
        action != "console") {
      return Error("ERR_PAGE_ARGUMENT", "Unknown DevTools panel");
    }
    return DevTools(action);
  }
  return Error("ERR_PAGE_UNSUPPORTED", "Unsupported page command");
}

std::optional<base::DictValue> LinuxPageCommands::Print(bool system) {
#if BUILDFLAG(ENABLE_PRINTING)
  auto* contents = contents_.get();
  auto* prefs = service_profile_->GetPrefs();
  auto* core = CoreTabHelper::FromWebContents(contents);
  if (!prefs || !core)
    return Error("ERR_PAGE_UNAVAILABLE", "Print helpers are unavailable");
  if (!prefs->GetBoolean(prefs::kPrintingEnabled)) {
    return Error("ERR_PAGE_POLICY", "Printing is disabled");
  }
  bool preview = false;
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  auto* previews = printing::PrintPreviewDialogController::GetInstance();
  preview = previews && previews->GetPrintPreviewForContents(contents);
#endif
  auto* dialogs =
      web_modal::WebContentsModalDialogManager::FromWebContents(contents);
  if ((contents->IsCrashed() || (dialogs && dialogs->IsDialogActive()) ||
       (core->content_restrictions() & CONTENT_RESTRICTION_PRINT)) &&
      !(system && preview)) {
    return Error("ERR_PAGE_POLICY", "Printing is not allowed for this page");
  }
  if (!allow_native_ui_)
    return Error("ERR_PAGE_UNAVAILABLE", "Native print UI is disabled");
  if (system) {
#if BUILDFLAG(ENABLE_BASIC_PRINT_DIALOG) && BUILDFLAG(ENABLE_PRINT_PREVIEW)
    auto* frame = printing::GetFrameToPrint(contents);
#if BUILDFLAG(ENABLE_PDF)
    auto* embedder =
        chrome_pdf::features::IsOopifPdfEnabled()
            ? pdf_frame_util::FindFullPagePdfExtensionHost(contents)
            : printing::GetFullPagePlugin(contents);
    if (auto* pdf = pdf_frame_util::FindPdfChildFrame(
            embedder ? embedder : contents->GetPrimaryMainFrame())) {
      frame = pdf;
    }
#endif
    auto* frame_contents =
        frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
    if (!frame || !frame->IsRenderFrameLive() || !frame_contents ||
        !printing::PrintViewManager::FromWebContents(frame_contents)) {
      return Error("ERR_PAGE_UNAVAILABLE",
                   "System print helpers are unavailable");
    }
    printing::StartBasicPrint(contents);
#else
    return Error("ERR_PAGE_UNAVAILABLE", "System printing is not built");
#endif
  } else if (!printing::StartPrint(
                 contents, prefs->GetBoolean(prefs::kPrintPreviewDisabled),
                 false)) {
    return Error("ERR_PAGE_DISPATCH", "Print dispatch was refused");
  }
  return std::nullopt;
#else
  return Error("ERR_PAGE_UNAVAILABLE", "Printing is not built");
#endif
}

std::optional<base::DictValue> LinuxPageCommands::Save() {
  auto* contents = contents_.get();
  auto* prefs = service_profile_->GetPrefs();
  auto* core = CoreTabHelper::FromWebContents(contents);
  auto* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  auto* entry = contents->GetController().GetLastCommittedEntry();
  if (!prefs || !core || !local_state || !entry || contents->IsCrashed()) {
    return Error("ERR_PAGE_UNAVAILABLE",
                 "Save helpers or committed page are unavailable");
  }
  if (!local_state->GetBoolean(prefs::kAllowFileSelectionDialogs) ||
      static_cast<policy::DownloadRestriction>(
          prefs->GetInteger(policy::policy_prefs::kDownloadRestrictions)) ==
          policy::DownloadRestriction::ALL_FILES ||
      (core->content_restrictions() & CONTENT_RESTRICTION_SAVE) ||
      !content::IsSavableURL(entry->GetURL())) {
    return Error("ERR_PAGE_POLICY", "Saving is not allowed for this page");
  }
  if (!allow_native_ui_)
    return Error("ERR_PAGE_UNAVAILABLE", "Native save UI is disabled");
  base::RecordAction(base::UserMetricsAction("SavePage"));
#if BUILDFLAG(ENABLE_PDF)
  if (contents->GetContentsMimeType() == "application/pdf" &&
      chrome_pdf::features::IsOopifPdfEnabled() &&
      pdf_extension_util::MaybeDispatchSaveEvent(
          contents->GetPrimaryMainFrame())) {
    return std::nullopt;
  }
#endif
  contents->OnSavePage();
  return std::nullopt;
}

std::optional<base::DictValue> LinuxPageCommands::DevTools(
    std::string_view action) {
  auto* contents = contents_.get();
  if (!DevToolsWindow::AllowDevToolsFor(service_profile_, contents)) {
    return Error("ERR_PAGE_POLICY", "DevTools is disabled for this page");
  }
  if (!allow_native_ui_ || !devtools_presentation_) {
    return Error("ERR_PAGE_UNAVAILABLE",
                 "DevTools presentation is unavailable");
  }
  auto* tools = DevToolsWindow::GetInstanceForInspectedWebContents(contents);
  if (action == "toggle" && tools && tools->IsDocked()) {
    auto* frontend = tools->GetDevToolsWebContents().get();
    if (!frontend)
      return Error("ERR_PAGE_UNAVAILABLE", "DevTools frontend is unavailable");
    frontend->DispatchBeforeUnload(false);
    return std::nullopt;
  }
  const auto toggle = action == "inspect" ? DevToolsToggleAction::Inspect()
                      : action == "console"
                          ? DevToolsToggleAction::ShowConsolePanel()
                          : DevToolsToggleAction::Show();
  auto weak = weak_factory_.GetWeakPtr();
  DevToolsWindow::OpenDevToolsWindow(
      contents, toggle, DevToolsOpenedByAction::kMainMenuOrMainShortcut);
  if (!weak || !weak->IsCurrent())
    return Error("ERR_PAGE_STALE_TARGET",
                 "Page claim changed during DevTools dispatch");
  tools = DevToolsWindow::GetInstanceForInspectedWebContents(contents);
  if (!tools)
    return Error("ERR_PAGE_DISPATCH", "DevTools dispatch was refused");
  tools->SetLoadCompletedCallback(base::BindOnce(
      &LinuxPageCommands::RefreshDevTools, weak_factory_.GetWeakPtr()));
  return std::nullopt;
}

void LinuxPageCommands::RefreshDevTools() {
  if (!IsCurrent())
    return;
  auto present = devtools_presentation_;
  if (present)
    present.Run();
}

void LinuxPageCommands::Reset() {
  weak_factory_.InvalidateWeakPtrs();
  owner_.Reset();
  devtools_presentation_.Reset();
  find_->Reset();
  contents_.reset();
  service_profile_ = nullptr;
  Observe(nullptr);
}

void LinuxPageCommands::WebContentsDestroyed() {
  Reset();
}

}
