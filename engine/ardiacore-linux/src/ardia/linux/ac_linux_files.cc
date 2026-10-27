#include "ardia/linux/ac_linux_files.h"

#include <sys/stat.h>

#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ardia/linux/ac_linux_downloads.h"
#include "base/base64.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/icon_loader.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/drop_data.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/compositor.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace ardia {
namespace {

using Done = base::OnceCallback<void(bool, base::Value)>;
namespace downloads = linux_downloads;

constexpr base::TaskTraits kFileReads = {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                                         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN};
constexpr base::TaskTraits kFileWrites = {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                                          base::TaskShutdownBehavior::BLOCK_SHUTDOWN};
constexpr size_t kMaxDropName = 255;

void Fail(Done done, std::string_view code, std::string_view message) {
  std::move(done).Run(false, base::Value(base::DictValue().Set("code", code).Set("message", message)));
}

int64_t NowMs() {
  return base::Time::Now().InMillisecondsSinceUnixEpoch();
}

std::vector<std::string> DownloadTargets(Profile* profile, bool complete_only) {
  std::vector<std::string> targets;
  if (!profile || profile->IsOffTheRecord()) return targets;
  content::DownloadManager* manager = profile->GetDownloadManager();
  if (!manager) return targets;
  content::DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  for (download::DownloadItem* item : items) {
    if (item->IsTransient() || item->IsDangerous() || item->IsInsecure()) continue;
    if (complete_only && item->GetState() != download::DownloadItem::COMPLETE) continue;
    targets.push_back(item->GetTargetFilePath().value());
  }
  return targets;
}

std::optional<std::string> PathArgument(const base::ListValue& values, size_t count) {
  if (values.size() != count || !values[0].is_string() || !downloads::ValidLocalPath(values[0].GetString()))
    return std::nullopt;
  return values[0].GetString();
}

bool RegularFile(const std::string& path) {
  struct stat info;
  return lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool SessionBus(base::Environment& environment) {
  if (environment.GetVar("DBUS_SESSION_BUS_ADDRESS").value_or("").size()) return true;
  const std::string runtime = environment.GetVar("XDG_RUNTIME_DIR").value_or("");
  return downloads::ValidLocalPath(runtime) && base::PathExists(base::FilePath(runtime).Append("bus"));
}

struct RevealCheck {
  bool exists = false;
  downloads::RevealBackend backend = downloads::RevealBackend::kUnavailable;
};

RevealCheck CheckReveal(const std::string& path) {
  auto environment = base::Environment::Create();
  return {base::PathExists(base::FilePath(path)),
          downloads::ChooseRevealBackend(SessionBus(*environment),
                                         base::ExecutableExistsInPath(environment.get(), "xdg-open"))};
}

downloads::TrashOutcome Trash(const std::string& path) {
  auto environment = base::Environment::Create();
  const std::string data = environment->GetVar("XDG_DATA_HOME").value_or("");
  const std::string home = environment->GetVar("HOME").value_or("");
  return downloads::MoveToTrash(path, downloads::TrashDirectory(data.c_str(), home.c_str()), std::time(nullptr));
}

base::ListValue FileStatus(const std::vector<std::string>& paths) {
  base::ListValue result;
  for (const auto& path : paths) {
    result.Append(downloads::ValidLocalPath(path) && base::PathExists(base::FilePath(path)));
  }
  return result;
}

IconLoader::IconSize IconSize(double size) {
  if (size <= 16) return IconLoader::SMALL;
  if (size <= 32) return IconLoader::NORMAL;
  return IconLoader::LARGE;
}

void ReplyIcon(Done done, gfx::Image image) {
  if (image.IsEmpty()) return std::move(done).Run(true, base::Value());
  const SkBitmap* bitmap = image.ToSkBitmap();
  auto png = bitmap ? gfx::PNGCodec::EncodeBGRASkBitmap(*bitmap, false) : std::nullopt;
  if (!png) return std::move(done).Run(true, base::Value());
  std::move(done).Run(true, base::Value("data:image/png;base64," + base::Base64Encode(*png)));
}

void StartDrag(base::WeakPtr<LinuxShellWebView> shell, std::string path) {
  if (!shell) return;
  views::Widget* widget = shell->GetWidget();
  if (!widget || widget->dragged_view() || !widget->GetRootView()) return;
  auto data = std::make_unique<ui::OSExchangeData>();
  data->SetFilename(base::FilePath(path));
  gfx::Point location = display::Screen::Get()->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(widget->GetRootView(), &location);
  widget->RunDragDropLoop(shell.get(), std::move(data), location,
                          ui::DragDropTypes::DRAG_COPY | ui::DragDropTypes::DRAG_LINK,
                          ui::mojom::DragEventSource::kMouse);
}

void Open(Profile* profile, const std::string& path, Done done) {
  platform_util::OpenItem(profile, base::FilePath(path), platform_util::OPEN_FILE,
      base::BindOnce([](Done done, platform_util::OpenOperationResult result) {
        switch (result) {
          case platform_util::OPEN_SUCCEEDED:
            return std::move(done).Run(true, base::Value(true));
          case platform_util::OPEN_FAILED_PATH_NOT_FOUND:
            return Fail(std::move(done), "ERR_FILE_MISSING", "The file no longer exists");
          case platform_util::OPEN_FAILED_INVALID_TYPE:
            return Fail(std::move(done), "ERR_FILE_TYPE", "The download is not a file");
          case platform_util::OPEN_FAILED_NO_HANLDER_FOR_FILE_TYPE:
            return Fail(std::move(done), "ERR_OPEN_UNAVAILABLE", "No desktop application opens this file");
          case platform_util::OPEN_FAILED_FILE_ERROR:
            return Fail(std::move(done), "ERR_OPEN", "The desktop could not open the file");
        }
        Fail(std::move(done), "ERR_OPEN", "The desktop could not open the file");
      }, std::move(done)));
}

}

LinuxShellWebView::LinuxShellWebView(content::BrowserContext* context) : views::WebView(context) {}

LinuxShellWebView::~LinuxShellWebView() = default;

bool LinuxShellWebView::CanDragEnter(content::WebContents* source, const content::DropData& data,
                                     blink::DragOperationsMask operations_allowed) {
  std::vector<std::string> paths;
  if (source == GetWebContents() && !data.did_originate_from_renderer) {
    for (const auto& file : data.filenames) paths.push_back(file.path.value());
  }
  if (paths.empty()) drops_.Clear();
  else drops_.Record(std::move(paths), NowMs());
  return views::WebView::CanDragEnter(source, data, operations_allowed);
}

BEGIN_METADATA(LinuxShellWebView)
END_METADATA

bool LinuxFilesHandles(std::string_view method) {
  for (const auto handled : {"ArdiaSystem.fileStatus", "ArdiaSystem.openFile", "ArdiaSystem.revealFile",
                             "ArdiaSystem.moveToTrash", "ArdiaSystem.fileIcon", "ArdiaFileDrop.takeDrop",
                             "ArdiaFileDrag.begin"}) {
    if (method == handled) return true;
  }
  return false;
}

void LinuxFilesInvoke(Profile* profile, LinuxShellWebView* shell, const std::string& method,
                      const base::ListValue& values, Done done) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || profile->IsOffTheRecord() || !shell)
    return Fail(std::move(done), "ERR_PROFILE", "Only the default regular profile is supported");
  auto weak = shell->GetLinuxWeakPtr();
  if (method == "ArdiaSystem.fileStatus") {
    if (values.size() != 1 || !values[0].is_list() || values[0].GetList().size() > downloads::kMaxStatusPaths)
      return Fail(std::move(done), "ERR_FILE_PATH", "A bounded list of paths is required");
    std::vector<std::string> paths;
    for (const auto& path : values[0].GetList()) {
      if (!path.is_string() || path.GetString().size() > downloads::kMaxPathBytes)
        return Fail(std::move(done), "ERR_FILE_PATH", "Paths must be strings");
      paths.push_back(path.GetString());
    }
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, kFileReads,
        base::BindOnce(&FileStatus, std::move(paths)),
        base::BindOnce([](Done done, base::ListValue result) {
          std::move(done).Run(true, base::Value(std::move(result)));
        }, std::move(done)));
    return;
  }
  if (method == "ArdiaSystem.fileIcon") {
    if (values.size() != 2 || !values[0].is_string() || values[0].GetString().empty() ||
        values[0].GetString().size() > downloads::kMaxPathBytes ||
        values[0].GetString().find('\0') != std::string::npos || !(values[1].is_int() || values[1].is_double()))
      return Fail(std::move(done), "ERR_FILE_PATH", "A file name and icon size are required");
    IconManager* icons = g_browser_process ? g_browser_process->icon_manager() : nullptr;
    if (!icons) return std::move(done).Run(true, base::Value());
    float scale = 1.0f;
    if (views::Widget* widget = shell->GetWidget(); widget && widget->GetCompositor())
      scale = widget->GetCompositor()->device_scale_factor();
    icons->LoadIcon(base::FilePath(values[0].GetString()), IconSize(values[1].GetDouble()), scale,
                    base::BindOnce(&ReplyIcon, std::move(done)), &shell->icon_tracker());
    return;
  }
  if (method == "ArdiaFileDrop.takeDrop") {
    if (values.size() != 1 || !values[0].is_list() ||
        values[0].GetList().size() > linux_downloads::LinuxDropLedger::kMaxFiles)
      return Fail(std::move(done), "ERR_FILE_DROP", "Dropped file names are required");
    std::vector<std::string> names;
    for (const auto& name : values[0].GetList()) {
      if (!name.is_string() || name.GetString().empty() || name.GetString().size() > kMaxDropName)
        return Fail(std::move(done), "ERR_FILE_DROP", "Dropped file names are invalid");
      names.push_back(name.GetString());
    }
    const auto paths = shell->drops().Take(names, NowMs());
    if (!paths) return Fail(std::move(done), "ERR_NO_FILE_DROP", "No matching desktop file drop is pending");
    base::ListValue urls;
    for (const auto& path : *paths) urls.Append(downloads::FileUrl(path));
    return std::move(done).Run(true, base::Value(std::move(urls)));
  }
  const bool reveal = method == "ArdiaSystem.revealFile";
  const auto path = PathArgument(values, 1);
  if (!path) return Fail(std::move(done), "ERR_FILE_PATH", "An absolute file path is required");
  if (!downloads::PathListed(*path, DownloadTargets(profile, !reveal)))
    return Fail(std::move(done), "ERR_FILE_NOT_ALLOWED", reveal ? "Only downloaded files can be shown"
                                                                : "Only finished downloads can be used");
  if (method == "ArdiaSystem.openFile") return Open(profile, *path, std::move(done));
  if (reveal) {
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, kFileReads,
        base::BindOnce(&CheckReveal, *path),
        base::BindOnce([](base::WeakPtr<LinuxShellWebView> shell, std::string path, Done done, RevealCheck check) {
          if (!shell || !shell->GetBrowserContext())
            return Fail(std::move(done), "ERR_WINDOW_CLOSED", "The window closed");
          if (check.backend == downloads::RevealBackend::kUnavailable)
            return Fail(std::move(done), "ERR_REVEAL_UNAVAILABLE", "No desktop file manager or xdg-open is available");
          if (!check.exists) return Fail(std::move(done), "ERR_FILE_MISSING", "The file no longer exists");
          platform_util::ShowItemInFolder(Profile::FromBrowserContext(shell->GetBrowserContext()), base::FilePath(path));
          std::move(done).Run(true, base::Value(true));
        }, weak, *path, std::move(done)));
    return;
  }
  if (method == "ArdiaSystem.moveToTrash") {
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, kFileWrites,
        base::BindOnce(&Trash, *path),
        base::BindOnce([](Done done, downloads::TrashOutcome outcome) {
          if (!outcome.ok) return Fail(std::move(done), outcome.code, outcome.message);
          std::move(done).Run(true, base::Value(true));
        }, std::move(done)));
    return;
  }
  if (method == "ArdiaFileDrag.begin") {
    base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, kFileReads,
        base::BindOnce(&RegularFile, *path),
        base::BindOnce([](base::WeakPtr<LinuxShellWebView> shell, std::string path, Done done, bool regular) {
          if (!regular) return Fail(std::move(done), "ERR_FILE_MISSING", "The download is no longer a file");
          views::Widget* widget = shell ? shell->GetWidget() : nullptr;
          if (!widget) return Fail(std::move(done), "ERR_WINDOW_CLOSED", "The window closed");
          if (widget->dragged_view()) return Fail(std::move(done), "ERR_DRAG_ACTIVE", "A drag is already in progress");
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
              base::BindOnce(&StartDrag, shell, std::move(path)));
          std::move(done).Run(true, base::Value(true));
        }, weak, *path, std::move(done)));
    return;
  }
  Fail(std::move(done), "ERR_UNKNOWN_CALL", "Unsupported file operation");
}

}
