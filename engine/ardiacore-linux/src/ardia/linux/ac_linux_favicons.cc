#include "ardia/linux/ac_linux_favicons.h"

#include <map>
#include <utility>
#include <vector>

#include "ardia/linux/ac_linux_favicon_policy.h"
#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/image_fetcher/image_decoder_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/image_fetcher_impl.h"
#include "components/image_fetcher/core/request_metadata.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace ardiacore {
namespace {

namespace policy = linux_favicons;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("ardia_linux_favicon_fetch", R"(
      semantics {
        sender: "Ardia favicons"
        description:
          "Downloads the favicon of a page Ardia shows without a live tab "
          "(a restored or unloaded tab, an essential or a pinned row) when "
          "its icon is not in the favicon cache."
        trigger: "The sidebar, essentials or tab strip list a page whose icon is missing."
        data: "None: no cookies or credentials are sent."
        destination: WEBSITE
        internal {
          contacts { owners: "//ardia/linux/OWNERS" }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-10-27"
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Not implemented."
      })");

class PendingReplies {
 public:
  uint64_t Add(LinuxFaviconReply reply) {
    const uint64_t key = ++next_;
    replies_[key] = std::move(reply);
    return key;
  }
  void Finish(uint64_t key, base::Value value) {
    auto found = replies_.find(key);
    if (found == replies_.end()) return;
    auto reply = std::move(found->second);
    replies_.erase(found);
    std::move(reply).Run(std::move(value));
  }

 private:
  uint64_t next_ = 0;
  std::map<uint64_t, LinuxFaviconReply> replies_;
};

PendingReplies& PageDownloads() {
  static base::NoDestructor<PendingReplies> replies;
  return *replies;
}

void ExpireAfter(base::OnceClosure expire) {
  if (!base::SequencedTaskRunner::HasCurrentDefault()) return;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, std::move(expire), base::Seconds(policy::kTimeoutSeconds));
}

}

base::Value LinuxFaviconImage(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) return base::Value();
  const auto [width, height] = policy::Fit(bitmap.width(), bitmap.height());
  if (width <= 0 || height <= 0) return base::Value();
  const SkBitmap scaled = width == bitmap.width() && height == bitmap.height()
      ? bitmap
      : skia::ImageOperations::Resize(bitmap, skia::ImageOperations::RESIZE_BEST, width, height);
  if (scaled.drawsNothing()) return base::Value();
  const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(scaled, false);
  if (!png || png->empty()) return base::Value();
  return base::Value(base::DictValue()
      .Set("uri", "data:image/png;base64," + base::Base64Encode(*png))
      .Set("width", scaled.width()).Set("height", scaled.height()));
}

void DownloadLinuxFavicon(content::WebContents* contents, const GURL& url,
                          base::RepeatingCallback<bool()> current, LinuxFaviconReply reply) {
  if (!contents || !url.is_valid() || !policy::Allowed(url.spec(), policy::Source::kPage)) {
    return std::move(reply).Run(base::Value());
  }
  const uint64_t key = PageDownloads().Add(std::move(reply));
  ExpireAfter(base::BindOnce([](uint64_t key) { PageDownloads().Finish(key, base::Value()); }, key));
  contents->DownloadImage(
      url, true, gfx::Size(policy::kPixels, policy::kPixels), policy::kPixels, false,
      base::BindOnce([](uint64_t key, base::RepeatingCallback<bool()> current, int, int,
                        const GURL&, const std::vector<SkBitmap>& bitmaps,
                        const std::vector<gfx::Size>&) {
        if (!current || !current.Run()) return PageDownloads().Finish(key, base::Value());
        std::vector<std::pair<int, int>> sizes;
        for (const auto& bitmap : bitmaps) sizes.emplace_back(bitmap.width(), bitmap.height());
        const int best = policy::Pick(sizes);
        PageDownloads().Finish(key, best < 0 ? base::Value() : LinuxFaviconImage(bitmaps[best]));
      }, key, std::move(current)));
}

LinuxFaviconFetcher::LinuxFaviconFetcher(Profile* profile)
    : profile_(profile ? profile->GetWeakPtr() : nullptr) {
  if (!profile || profile->IsOffTheRecord()) return;
  fetcher_ = std::make_unique<image_fetcher::ImageFetcherImpl>(
      std::make_unique<ImageDecoderImpl>(),
      profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess());
}

LinuxFaviconFetcher::~LinuxFaviconFetcher() = default;

void LinuxFaviconFetcher::Fetch(const GURL& url, LinuxFaviconReply reply) {
  if (!fetcher_ || !profile_ || !url.is_valid() ||
      !policy::Allowed(url.spec(), policy::Source::kNetwork)) {
    return std::move(reply).Run(base::Value());
  }
  const uint64_t key = PageDownloads().Add(std::move(reply));
  ExpireAfter(base::BindOnce([](uint64_t key) { PageDownloads().Finish(key, base::Value()); }, key));
  image_fetcher::ImageFetcherParams params(kTrafficAnnotation, "ArdiaLinuxFavicons");
  params.set_max_download_size(policy::kMaxBytes);
  params.set_frame_size(gfx::Size(policy::kPixels, policy::kPixels));
  fetcher_->FetchImage(
      url,
      base::BindOnce([](uint64_t key, const gfx::Image& image,
                        const image_fetcher::RequestMetadata&) {
        PageDownloads().Finish(key, image.IsEmpty() ? base::Value() : LinuxFaviconImage(image.AsBitmap()));
      }, key),
      std::move(params));
}

}
