#include "ardia/linux/ac_linux_suggest.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ardia/linux/ac_linux_suggest_policy.h"
#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace ardia::linux_suggest {
namespace {

namespace policy = ardia::linux_suggest_policy;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("ardia_linux_search_suggestions", R"(
      semantics {
        sender: "Ardia command bar"
        description:
          "Asks the default search engine for query completions while the "
          "user types in the Ardia command bar."
        trigger: "Typing a search in the command bar with search suggestions on."
        data: "The typed text. No cookies or credentials are sent."
        destination: OTHER
        destination_other: "The built-in search engine the user chose."
      }
      policy {
        cookies_allowed: NO
        setting: "Settings > Search Engine > Search suggestions."
        policy_exception_justification: "Not implemented."
      })");

int g_in_flight = 0;

class SuggestionFetch : public ProfileObserver {
 public:
  SuggestionFetch(Profile* profile, SuggestionReply reply)
      : reply_(std::move(reply)) {
    ++g_in_flight;
    observation_.Observe(profile);
  }
  SuggestionFetch(const SuggestionFetch&) = delete;
  SuggestionFetch& operator=(const SuggestionFetch&) = delete;
  ~SuggestionFetch() override { --g_in_flight; }

  void Load(Profile* profile, const GURL& url) {
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url;
    request->method = "GET";
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->redirect_mode = network::mojom::RedirectMode::kError;
    request->load_flags = net::LOAD_DISABLE_CACHE;
    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               kTrafficAnnotation);
    loader_->SetTimeoutDuration(base::Seconds(policy::kTimeoutSeconds));
    loader_->DownloadToString(
        profile->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess()
            .get(),
        base::BindOnce(&SuggestionFetch::OnLoaded, base::Unretained(this)),
        policy::kMaxReplyBytes);
  }

 private:
  void OnLoaded(std::optional<std::string> body) {
    const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
    const int status =
        head && head->headers ? head->headers->response_code() : 0;
    const bool ok = loader_->NetError() == net::OK && body &&
                    policy::ReplyAcceptable(status, *body);
    Finish(ok ? std::move(body) : std::nullopt);
  }

  void OnProfileWillBeDestroyed(Profile* profile) override {
    Finish(std::nullopt);
  }

  void Finish(std::optional<std::string> body) {
    SuggestionReply reply = std::move(reply_);
    delete this;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(reply), std::move(body)));
  }

  SuggestionReply reply_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
};

}

bool CanFetchSuggestions(Profile* profile, const std::string& url) {
  if (!profile || profile->IsOffTheRecord() ||
      !policy::HasCapacity(g_in_flight) || !policy::RequestAllowed(url)) {
    return false;
  }
  const GURL target(url);
  return target.is_valid() && target.SchemeIs(url::kHttpsScheme) &&
         policy::RequestAllowed(target.spec());
}

void FetchSuggestions(Profile* profile, const std::string& url,
                      SuggestionReply reply) {
  if (!CanFetchSuggestions(profile, url)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(reply), std::nullopt));
    return;
  }
  (new SuggestionFetch(profile, std::move(reply)))->Load(profile, GURL(url));
}

}
