#include "ardia/linux/ac_linux_import_service.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "ardia/linux/ac_linux_import_formats.h"
#include "ardia/linux/ac_linux_import_readers.h"
#include "ardia/linux/ac_linux_profile_service.h"
#include "ardia/linux/ac_linux_profiles.h"
#include "base/callback_list.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/scoped_observation.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate_factory.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/common/chrome_paths.h"
#include "components/password_manager/core/browser/import/csv_password.h"
#include "components/password_manager/core/browser/import/import_results.h"
#include "components/password_manager/core/browser/import/password_importer.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/ui/credential_ui_entry.h"
#include "components/password_manager/core/browser/ui/saved_passwords_presenter.h"
#include "content/public/browser/web_contents.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "url/gurl.h"

namespace ardia {
namespace {

namespace li = linux_import;

using Done = base::OnceCallback<void(bool, base::Value)>;
using Emit = base::RepeatingCallback<void(base::Value)>;
using Flag = base::RefCountedData<std::atomic<bool>>;

constexpr size_t kMaxJobs = 8;
constexpr base::TaskTraits kBlocking = {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                                        base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN};

void Fail(Done done, std::string_view code, std::string_view message) {
  std::move(done).Run(false, base::Value(base::DictValue().Set("code", code).Set("message", message)));
}

double NowMs() {
  return (base::TimeTicks::Now() - base::TimeTicks()).InMillisecondsF();
}

std::string Argument(const base::ListValue& values, size_t index) {
  return index < values.size() && values[index].is_string() ? values[index].GetString() : std::string();
}

std::string JobKey(std::string_view owner, std::string_view job) {
  return std::string(owner) + "\n" + std::string(job);
}

li::Environment CurrentEnvironment() {
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  const std::optional<std::string> home = environment->GetVar("HOME");
  const std::optional<std::string> config = environment->GetVar("XDG_CONFIG_HOME");
  base::FilePath temp;
  base::GetTempDir(&temp);
  base::FilePath own;
  base::PathService::Get(chrome::DIR_USER_DATA, &own);
  return li::MakeEnvironment(home ? home->c_str() : nullptr, config ? config->c_str() : nullptr,
                             temp.value().c_str(), own.value());
}

using ProfileResolved = base::OnceCallback<void(Profile*, std::string)>;

void ResolveImportProfile(std::string_view engine_profile, ProfileResolved done) {
  const li::ProfileTarget target = li::ClassifyProfile(engine_profile);
  if (target == li::ProfileTarget::kPrivate || target == li::ProfileTarget::kInvalid) {
    return std::move(done).Run(nullptr, li::ProfileErrorCode(target));
  }
  ardiacore::ResolveLinuxProfile(
      engine_profile,
      base::BindOnce(
          [](ProfileResolved done, Profile* profile, ardiacore::linux_profiles::ProfileError error) {
            if (!profile) {
              const std::string_view code = ardiacore::linux_profiles::ProfileErrorCode(error);
              return std::move(done).Run(nullptr, code.empty() ? "ERR_PROFILE_UNAVAILABLE" : std::string(code));
            }
            if (profile->IsOffTheRecord()) return std::move(done).Run(nullptr, "ERR_PROFILE_PRIVATE");
            std::move(done).Run(profile, std::string());
          },
          std::move(done)));
}

class JobObserver : public li::ImportObserver {
 public:
  JobObserver(scoped_refptr<Flag> cancelled, scoped_refptr<base::SequencedTaskRunner> ui, Emit emit, std::string job)
      : cancelled_(std::move(cancelled)), ui_(std::move(ui)), emit_(std::move(emit)), job_(std::move(job)) {}

  bool Cancelled() const override { return cancelled_->data.load(); }

  void Progress(std::string_view kind, std::string_view phase, int processed, std::optional<int> total) override {
    base::DictValue event;
    event.Set("jobId", job_);
    event.Set("type", "progress");
    event.Set("kind", kind);
    event.Set("phase", phase);
    event.Set("processed", processed);
    if (total) event.Set("total", *total);
    ui_->PostTask(FROM_HERE, base::BindOnce(emit_, base::Value(std::move(event))));
  }

 private:
  scoped_refptr<Flag> cancelled_;
  scoped_refptr<base::SequencedTaskRunner> ui_;
  Emit emit_;
  std::string job_;
};

li::ImportOutcome RunImport(li::Environment environment, std::string browser, std::string profile,
                            std::vector<std::string> kinds, li::ImportOptions options, scoped_refptr<Flag> cancelled,
                            scoped_refptr<base::SequencedTaskRunner> ui, Emit emit, std::string job) {
  li::RemoveStaleScratch(environment.temp_root, static_cast<int64_t>(base::Time::Now().ToTimeT()), li::kStaleScratchSeconds);
  JobObserver observer(std::move(cancelled), std::move(ui), std::move(emit), std::move(job));
  return li::ReadBrowserProfile(environment, browser, profile, kinds, options, observer);
}

base::Value ReadBookmarksFile(std::string path) {
  std::string html;
  const li::FileRead read = li::ReadFileLimited(path, li::kMaxBookmarksHtmlBytes, &html);
  if (read != li::FileRead::kOk) {
    return base::Value(base::DictValue().Set("code", read == li::FileRead::kTooLarge ? "tooLarge" : "unreadable"));
  }
  li::BookmarksParse parse = li::ParseNetscapeBookmarks(html);
  if (!parse.root) return base::Value(base::DictValue().Set("code", parse.error_code).Set("message", parse.error));
  return base::Value(base::DictValue().Set("root", std::move(*parse.root)).Set("links", parse.links));
}

struct CsvRead {
  std::string code;
  std::string message;
  std::optional<li::CsvParse> parse;
};

CsvRead ReadPasswordsFile(std::string path) {
  CsvRead read;
  std::string csv;
  const li::FileRead status = li::ReadFileLimited(path, li::kMaxPasswordsCsvBytes, &csv);
  if (status != li::FileRead::kOk) {
    read.code = status == li::FileRead::kTooLarge ? "tooLarge" : "unreadable";
    read.message = "The passwords file couldn't be read";
    return read;
  }
  li::CsvParse parse = li::ParsePasswordsCsv(csv);
  li::Wipe(csv);
  if (!parse.error_code.empty()) {
    read.code = parse.error_code;
    read.message = parse.error;
    return read;
  }
  read.parse = std::move(parse);
  return read;
}

class FileChooser : public ui::SelectFileDialog::Listener {
 public:
  using Chosen = base::OnceCallback<void(std::optional<base::FilePath>)>;

  explicit FileChooser(Chosen chosen) : chosen_(std::move(chosen)) {}
  FileChooser(const FileChooser&) = delete;
  FileChooser& operator=(const FileChooser&) = delete;
  ~FileChooser() override {
    if (dialog_) dialog_->ListenerDestroyed();
  }

  void Open(content::WebContents* contents, std::string_view kind) {
    dialog_ = ui::SelectFileDialog::Create(this, std::make_unique<ChromeSelectFilePolicy>(contents));
    if (!dialog_) return Finish(std::nullopt);
    ui::SelectFileDialog::FileTypeInfo types;
    if (kind == "bookmarksHTML") types.extensions = {{FILE_PATH_LITERAL("html"), FILE_PATH_LITERAL("htm")}};
    else types.extensions = {{FILE_PATH_LITERAL("csv")}};
    types.include_all_files = true;
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                        kind == "bookmarksHTML" ? u"Choose a Bookmarks HTML File" : u"Choose a Passwords CSV File",
                        base::FilePath(), &types, 1, base::FilePath::StringType(),
                        contents ? contents->GetTopLevelNativeWindow() : gfx::NativeWindow());
  }

  void FileSelected(const ui::SelectedFileInfo& file, int) override { Finish(file.path()); }
  void FileSelectionCanceled() override { Finish(std::nullopt); }

 private:
  void Finish(std::optional<base::FilePath> path) {
    if (chosen_) std::move(chosen_).Run(std::move(path));
  }

  Chosen chosen_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

class PasswordWriter : public ProfileObserver {
 public:
  using Finished = base::OnceCallback<void(bool, base::DictValue)>;
  using Progress = base::RepeatingCallback<void(int processed, int total)>;

  PasswordWriter(Profile* profile, std::vector<li::Credential> credentials, Progress progress, Finished finished)
      : profile_(profile),
        credentials_(std::move(credentials)),
        progress_(std::move(progress)),
        finished_(std::move(finished)) {
    observation_.Observe(profile);
  }
  PasswordWriter(const PasswordWriter&) = delete;
  PasswordWriter& operator=(const PasswordWriter&) = delete;
  ~PasswordWriter() override { ForgetExisting(); }

  void OnProfileWillBeDestroyed(Profile*) override {
    weak_factory_.InvalidateWeakPtrs();
    importer_.reset();
    delegate_.reset();
    observation_.Reset();
    profile_ = nullptr;
    Fail("ERR_PROFILE_UNAVAILABLE", "The profile closed before the passwords were saved");
  }

  void Start() {
    if (!profile_) return Fail("ERR_PROFILE_UNAVAILABLE", "The profile is unavailable");
    delegate_ = extensions::PasswordsPrivateDelegateFactory::GetForBrowserContext(profile_, true);
    if (!delegate_) return Fail("ERR_PASSWORDS_UNAVAILABLE", "The password store is unavailable");
    delegate_->GetSavedPasswordsList(base::BindOnce(&PasswordWriter::Loaded, weak_factory_.GetWeakPtr()));
  }

 private:
  void Loaded(const extensions::PasswordsPrivateDelegate::UiEntries&) {
    password_manager::SavedPasswordsPresenter* presenter = delegate_->GetSavedPasswordsPresenter();
    if (!presenter) return Fail("ERR_PASSWORDS_UNAVAILABLE", "The password store is unavailable");
    for (password_manager::CredentialUIEntry& entry : presenter->GetSavedPasswords()) {
      std::string username = base::UTF16ToUTF8(entry.username);
      std::string password = base::UTF16ToUTF8(entry.password);
      existing_.insert(li::ExistingKey(entry.GetURL().spec(), username, password));
      li::Wipe(password);
      std::fill(entry.password.begin(), entry.password.end(), u'\0');
    }
    importer_ = std::make_unique<password_manager::PasswordImporter>(*presenter);
    NextChunk();
  }

  void NextChunk() {
    if (offset_ >= credentials_.size()) {
      base::DictValue counts = tally_.ToValue();
      return Complete(true, std::move(counts));
    }
    const size_t end = std::min(credentials_.size(), offset_ + li::kPasswordChunk);
    chunk_.clear();
    for (size_t i = offset_; i < end; ++i) chunk_.push_back(std::move(credentials_[i]));
    offset_ = end;
    chunk_existing_ = li::CountExisting(chunk_, existing_);
    chunk_conflicts_ = 0;
    std::vector<password_manager::CSVPassword> rows;
    rows.reserve(chunk_.size());
    for (const li::Credential& credential : chunk_) {
      const GURL url(credential.url);
      if (url.is_valid()) {
        rows.emplace_back(url, credential.username, credential.password, credential.note,
                          password_manager::CSVPassword::Status::kOK);
      } else {
        rows.emplace_back(credential.url, credential.username, credential.password, credential.note,
                          password_manager::CSVPassword::Status::kOK);
      }
    }
    importer_->Import(rows, password_manager::PasswordForm::Store::kProfileStore,
                      base::BindOnce(&PasswordWriter::Imported, weak_factory_.GetWeakPtr()));
  }

  void Imported(const password_manager::ImportResults& results) {
    if (results.status == password_manager::ImportResults::Status::CONFLICTS) {
      chunk_conflicts_ += static_cast<int>(results.displayed_entries.size());
      return importer_->ContinueImport({}, base::BindOnce(&PasswordWriter::Imported, weak_factory_.GetWeakPtr()));
    }
    if (results.status != password_manager::ImportResults::Status::SUCCESS) {
      return Fail("ERR_PASSWORDS_IMPORT", "Chromium's password importer rejected the passwords");
    }
    int invalid = 0;
    for (const password_manager::ImportEntry& entry : results.displayed_entries) {
      if (entry.status != password_manager::ImportEntry::Status::VALID &&
          entry.status != password_manager::ImportEntry::Status::CONFLICT_PROFILE &&
          entry.status != password_manager::ImportEntry::Status::CONFLICT_ACCOUNT) {
        ++invalid;
      }
    }
    tally_.AddChunk(results.number_imported, chunk_existing_, chunk_conflicts_, invalid);
    chunk_.clear();
    progress_.Run(static_cast<int>(offset_), static_cast<int>(credentials_.size()));
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PasswordWriter::NextChunk, weak_factory_.GetWeakPtr()));
  }

  void Fail(std::string_view code, std::string_view message) {
    Complete(false, base::DictValue().Set("code", code).Set("message", message));
  }

  void Complete(bool ok, base::DictValue payload) {
    credentials_.clear();
    chunk_.clear();
    ForgetExisting();
    if (finished_) std::move(finished_).Run(ok, std::move(payload));
  }

  void ForgetExisting() {
    while (!existing_.empty()) {
      auto node = existing_.extract(existing_.begin());
      li::Wipe(node.value());
    }
  }

  raw_ptr<Profile> profile_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
  std::vector<li::Credential> credentials_;
  std::vector<li::Credential> chunk_;
  std::set<std::string> existing_;
  size_t offset_ = 0;
  size_t chunk_existing_ = 0;
  int chunk_conflicts_ = 0;
  li::PasswordTally tally_;
  Progress progress_;
  Finished finished_;
  scoped_refptr<extensions::PasswordsPrivateDelegate> delegate_;
  std::unique_ptr<password_manager::PasswordImporter> importer_;
  base::WeakPtrFactory<PasswordWriter> weak_factory_{this};
};

class ImportService {
 public:
  static ImportService& Get() {
    static base::NoDestructor<ImportService> service;
    return *service;
  }

  void Invoke(LinuxImportRequester requester, const std::string& method, const base::ListValue& values, Done done) {
    vault_.Expire(NowMs());
    if (terminating_) return Fail(std::move(done), "ERR_SHUTTING_DOWN", "Ardia is quitting");
    if (method == "ArdiaImport.listBrowsers") return ListBrowsers(std::move(done));
    if (method == "ArdiaImport.startImport") return StartImport(std::move(requester), values, std::move(done));
    if (method == "ArdiaImport.cancelImport") {
      auto found = jobs_.find(JobKey(requester.owner, Argument(values, 0)));
      if (found != jobs_.end()) found->second->data.store(true);
      return std::move(done).Run(true, base::Value(found != jobs_.end()));
    }
    if (method == "ArdiaImport.writePasswords") return WritePasswords(std::move(requester), values, std::move(done));
    if (method == "ArdiaImport.discardPasswords") {
      return std::move(done).Run(true, base::Value(vault_.Discard(requester.owner, Argument(values, 0))));
    }
    if (method == "ArdiaImport.forgetUnlockedKeys") {
      vault_.DiscardOwner(requester.owner);
      return std::move(done).Run(true, base::Value());
    }
    if (method == "ArdiaImport.chooseImportFile") return Choose(std::move(requester), values, std::move(done));
    if (method == "ArdiaImport.cancelChooser") {
      auto found = choosers_.find(requester.owner);
      const bool cancelled = found != choosers_.end() && found->second.request == Argument(values, 0);
      if (cancelled) choosers_.erase(found);
      return std::move(done).Run(true, base::Value(cancelled));
    }
    if (method == "ArdiaImport.importBookmarksHTML") return ReadBookmarks(std::move(requester), values, std::move(done));
    if (method == "ArdiaImport.importPasswordsCSV") return ReadPasswords(std::move(requester), values, std::move(done));
    if (method == "ArdiaImport.listExtensions") {
      const std::string browser = Argument(values, 0);
      const std::string profile = Argument(values, 1);
      if (!li::ValidBrowserId(browser) || !li::ValidProfileId(profile)) return Fail(std::move(done), "invalid", "Invalid extension request");
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, kBlocking,
          base::BindOnce([](li::Environment environment, std::string browser, std::string profile) {
            return li::ListExtensions(environment, browser, profile);
          }, CurrentEnvironment(), browser, profile),
          base::BindOnce([](Done done, base::ListValue list) { std::move(done).Run(true, base::Value(std::move(list))); },
                         std::move(done)));
      return;
    }
    Fail(std::move(done), "unsupported", "Unsupported import method");
  }

  void WindowClosed(std::string_view owner) {
    const std::string prefix = std::string(owner) + "\n";
    for (auto it = jobs_.begin(); it != jobs_.end();) {
      if (it->first.rfind(prefix, 0) == 0) {
        it->second->data.store(true);
        it = jobs_.erase(it);
      } else {
        ++it;
      }
    }
    vault_.DiscardOwner(owner);
    chosen_.Forget(owner);
    choosers_.erase(std::string(owner));
  }

 private:
  friend class base::NoDestructor<ImportService>;

  struct OpenChooser {
    std::string request;
    std::unique_ptr<FileChooser> chooser;
  };

  ImportService()
      : terminating_subscription_(browser_shutdown::AddAppTerminatingCallback(
            base::BindOnce(&ImportService::Terminate, base::Unretained(this)))) {}

  void Terminate() {
    terminating_ = true;
    for (auto& [key, flag] : jobs_) flag->data.store(true);
    jobs_.clear();
    vault_.Clear();
    choosers_.clear();
  }

  void Store(const std::string& owner, std::vector<li::Credential> credentials, base::DictValue& reply,
             base::ListValue& failed, base::ListValue& warnings) {
    if (credentials.empty()) return;
    const int count = static_cast<int>(credentials.size());
    const std::string token = base::HexEncodeLower(base::RandBytesAsVector(16));
    if (!vault_.Put(owner, token, std::move(credentials), NowMs())) {
      failed.Append("passwords");
      warnings.Append(base::DictValue()
                          .Set("kind", "passwords")
                          .Set("code", "unavailable")
                          .Set("message", "The imported passwords couldn't be held for saving"));
      return;
    }
    reply.Set("passwords", base::DictValue().Set("token", token).Set("count", count));
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&ImportService::ExpireVault, weak_factory_.GetWeakPtr()),
        base::Milliseconds(li::kVaultLifetimeMs + 1));
  }

  void ExpireVault() { vault_.Expire(NowMs()); }

  void ListBrowsers(Done done) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking, base::BindOnce(&li::ListBrowsers, CurrentEnvironment()),
        base::BindOnce([](Done done, base::ListValue list) { std::move(done).Run(true, base::Value(std::move(list))); },
                       std::move(done)));
  }

  void StartImport(LinuxImportRequester requester, const base::ListValue& values, Done done) {
    const std::string job = Argument(values, 0);
    const std::string browser = Argument(values, 1);
    const std::string profile = Argument(values, 2);
    if (!li::ValidJobId(job) || !li::ValidBrowserId(browser) || !li::ValidProfileId(profile)) {
      return Fail(std::move(done), "invalid", "Invalid import request");
    }
    if (!li::FindBrowser(browser)) return Fail(std::move(done), "notFound", "Unknown browser");
    const std::vector<std::string> kinds =
        li::RequestedKinds(values.size() > 3 && values[3].is_list() ? &values[3].GetList() : nullptr);
    li::ImportOptions options;
    if (values.size() > 4 && values[4].is_dict()) {
      const base::DictValue& given = values[4].GetDict();
      if (std::optional<int> limit = given.FindInt("historyLimit"); limit && *limit > 0) options.history_limit = limit;
      if (std::optional<double> since = given.FindDouble("historySince"); since && *since > 0) options.history_since = since;
    }
    const std::string key = JobKey(requester.owner, job);
    if (jobs_.contains(key)) return Fail(std::move(done), "busy", "That import is already running");
    if (jobs_.size() >= kMaxJobs) return Fail(std::move(done), "busy", "Too many imports are running");
    auto cancelled = base::MakeRefCounted<Flag>(std::in_place, false);
    jobs_[key] = cancelled;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking,
        base::BindOnce(&RunImport, CurrentEnvironment(), browser, profile, kinds, options, cancelled,
                       base::SequencedTaskRunner::GetCurrentDefault(), requester.emit, job),
        base::BindOnce(&ImportService::Imported, weak_factory_.GetWeakPtr(), requester.owner, job, requester.emit));
    std::move(done).Run(true, base::Value(true));
  }

  void Imported(std::string owner, std::string job, Emit emit, li::ImportOutcome outcome) {
    auto found = jobs_.find(JobKey(owner, job));
    if (found == jobs_.end()) return;
    const bool cancelled = outcome.cancelled || found->second->data.load();
    jobs_.erase(found);
    base::DictValue event;
    event.Set("jobId", job);
    if (cancelled) {
      event.Set("type", "failed");
      event.Set("code", "cancelled");
      event.Set("message", "Import cancelled");
    } else if (!outcome.error_code.empty()) {
      event.Set("type", "failed");
      event.Set("code", outcome.error_code);
      event.Set("message", outcome.error);
    } else {
      if (li::ReplyLeaksSecret(base::Value(outcome.result.Clone()), outcome.credentials)) {
        event.Set("type", "failed");
        event.Set("code", "ERR_SECRET_IN_REPLY");
        event.Set("message", "The import result was withheld because it contained a password");
      } else {
        base::ListValue failed;
        base::ListValue warnings;
        if (base::ListValue* listed = outcome.result.FindList("failed")) failed = std::move(*listed);
        if (base::ListValue* listed = outcome.result.FindList("warnings")) warnings = std::move(*listed);
        Store(owner, std::move(outcome.credentials), outcome.result, failed, warnings);
        outcome.result.Set("failed", std::move(failed));
        outcome.result.Set("warnings", std::move(warnings));
        event.Set("type", "done");
        event.Set("result", std::move(outcome.result));
      }
    }
    outcome.credentials.clear();
    emit.Run(base::Value(std::move(event)));
  }

  void WritePasswords(LinuxImportRequester requester, const base::ListValue& values, Done done) {
    const std::string request = Argument(values, 0);
    const std::string token = Argument(values, 1);
    if (!li::ValidJobId(request) || !li::ValidToken(token)) return Fail(std::move(done), "invalid", "Invalid password write");
    const std::string engine_profile = Argument(values, 2);
    const li::ProfileTarget target = li::ClassifyProfile(engine_profile);
    if (target == li::ProfileTarget::kPrivate || target == li::ProfileTarget::kInvalid) {
      vault_.Discard(requester.owner, token);
      return Fail(std::move(done), li::ProfileErrorCode(target), "Passwords can't be imported into that profile");
    }
    if (!vault_.Contains(requester.owner, token, NowMs())) {
      return Fail(std::move(done), "expired", "The imported passwords are no longer available");
    }
    std::move(done).Run(true, base::Value(true));
    ResolveImportProfile(engine_profile, base::BindOnce(&ImportService::WriteResolved, weak_factory_.GetWeakPtr(),
                                                        requester.owner, request, token, requester.emit));
  }

  void WriteResolved(std::string owner, std::string request, std::string token, Emit emit, Profile* profile,
                     std::string code) {
    auto failed = [&](std::string_view failure, std::string_view message) {
      emit.Run(base::Value(base::DictValue()
                               .Set("jobId", request)
                               .Set("type", "failed")
                               .Set("code", failure)
                               .Set("message", message)));
    };
    if (!profile) {
      vault_.Discard(owner, token);
      return failed(code, "Passwords can't be imported into that profile");
    }
    std::optional<std::vector<li::Credential>> credentials = vault_.Take(owner, token, NowMs());
    if (!credentials) return failed("expired", "The imported passwords are no longer available");
    const int id = ++next_writer_;
    auto writer = std::make_unique<PasswordWriter>(
        profile, std::move(*credentials),
        base::BindRepeating(
            [](Emit emit, std::string request, int processed, int total) {
              emit.Run(base::Value(base::DictValue()
                                       .Set("jobId", request)
                                       .Set("type", "progress")
                                       .Set("kind", "passwords")
                                       .Set("phase", "progress")
                                       .Set("processed", processed)
                                       .Set("total", total)));
            },
            emit, request),
        base::BindOnce(&ImportService::Written, weak_factory_.GetWeakPtr(), id, request, emit));
    PasswordWriter* started = writer.get();
    writers_[id] = std::move(writer);
    started->Start();
  }

  void Written(int id, std::string request, Emit emit, bool ok, base::DictValue payload) {
    auto found = writers_.find(id);
    if (found != writers_.end()) {
      base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(found->second));
      writers_.erase(found);
    }
    payload.Set("jobId", request);
    payload.Set("type", ok ? "written" : "failed");
    emit.Run(base::Value(std::move(payload)));
  }

  void Choose(LinuxImportRequester requester, const base::ListValue& values, Done done) {
    const std::string request = Argument(values, 0);
    const std::string kind = Argument(values, 1);
    if (!li::ValidJobId(request)) return Fail(std::move(done), "invalid", "Invalid chooser request");
    if (!li::ValidChooserKind(kind)) return Fail(std::move(done), "unsupported", "That file type can't be imported on Linux");
    if (choosers_.contains(requester.owner)) return Fail(std::move(done), "busy", "A file chooser is already open");
    if (!ChromeSelectFilePolicy::FileSelectDialogsAllowed()) {
      return Fail(std::move(done), "ERR_FILE_CHOOSER_DISABLED", "File choosers are disabled by policy");
    }
    auto chooser = std::make_unique<FileChooser>(base::BindOnce(
        &ImportService::Chosen, weak_factory_.GetWeakPtr(), requester.owner, request, kind, requester.emit));
    FileChooser* opening = chooser.get();
    choosers_[requester.owner] = OpenChooser{request, std::move(chooser)};
    std::move(done).Run(true, base::Value(true));
    opening->Open(requester.contents.get(), kind);
  }

  void Chosen(std::string owner, std::string request, std::string kind, Emit emit, std::optional<base::FilePath> path) {
    auto found = choosers_.find(owner);
    if (found != choosers_.end() && found->second.request == request) {
      base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(found->second.chooser));
      choosers_.erase(found);
    }
    base::DictValue event;
    event.Set("jobId", request);
    event.Set("type", "chosen");
    if (path && li::ValidLocalFile(path->value())) {
      chosen_.Remember(owner, kind, path->value(), NowMs());
      event.Set("path", path->value());
    } else {
      event.Set("path", base::Value());
    }
    emit.Run(base::Value(std::move(event)));
  }

  bool TakeChosen(const LinuxImportRequester& requester, std::string_view kind, const std::string& path) {
    return li::ValidLocalFile(path) && chosen_.Take(requester.owner, kind, path, NowMs());
  }

  void ReadBookmarks(LinuxImportRequester requester, const base::ListValue& values, Done done) {
    const std::string path = Argument(values, 0);
    if (!TakeChosen(requester, "bookmarksHTML", path)) {
      return Fail(std::move(done), "ERR_FILE_NOT_CHOSEN", "Choose the file to import first");
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking, base::BindOnce(&ReadBookmarksFile, path),
        base::BindOnce(
            [](Done done, base::Value reply) {
              const bool ok = reply.is_dict() && !reply.GetDict().contains("code");
              std::move(done).Run(ok, std::move(reply));
            },
            std::move(done)));
  }

  void ReadPasswords(LinuxImportRequester requester, const base::ListValue& values, Done done) {
    const std::string path = Argument(values, 0);
    if (!TakeChosen(requester, "passwordsCSV", path)) {
      return Fail(std::move(done), "ERR_FILE_NOT_CHOSEN", "Choose the file to import first");
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking, base::BindOnce(&ReadPasswordsFile, path),
        base::BindOnce(&ImportService::PasswordsRead, weak_factory_.GetWeakPtr(), requester.owner, std::move(done)));
  }

  void PasswordsRead(std::string owner, Done done, CsvRead read) {
    if (!read.parse) return Fail(std::move(done), read.code, read.message);
    li::CsvParse& parse = *read.parse;
    base::DictValue reply;
    reply.Set("entries", li::CredentialSummaries(parse.credentials));
    reply.Set("rows", parse.rows);
    reply.Set("duplicates", parse.duplicates);
    reply.Set("invalid", parse.invalid);
    if (li::ReplyLeaksSecret(base::Value(reply.Clone()), parse.credentials)) {
      return Fail(std::move(done), "ERR_SECRET_IN_REPLY", "The passwords file reply was withheld");
    }
    base::ListValue failed;
    base::ListValue warnings;
    Store(owner, std::move(parse.credentials), reply, failed, warnings);
    if (!failed.empty()) return Fail(std::move(done), "unavailable", "The imported passwords couldn't be held for saving");
    std::move(done).Run(true, base::Value(std::move(reply)));
  }

  bool terminating_ = false;
  base::CallbackListSubscription terminating_subscription_;
  std::map<std::string, scoped_refptr<Flag>> jobs_;
  li::CredentialVault vault_;
  li::ChosenFiles chosen_;
  std::map<std::string, OpenChooser> choosers_;
  std::map<int, std::unique_ptr<PasswordWriter>> writers_;
  int next_writer_ = 0;
  base::WeakPtrFactory<ImportService> weak_factory_{this};
};

}

bool LinuxImportHandles(std::string_view method) {
  return std::any_of(std::begin(li::kImportCapabilities), std::end(li::kImportCapabilities),
                     [method](const char* known) { return method == known; });
}

void LinuxImportInvoke(LinuxImportRequester requester, const std::string& method, const base::ListValue& values,
                       base::OnceCallback<void(bool, base::Value)> done) {
  ImportService::Get().Invoke(std::move(requester), method, values, std::move(done));
}

void LinuxImportWindowClosed(std::string_view owner) {
  ImportService::Get().WindowClosed(owner);
}

}
