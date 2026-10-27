#ifndef ARDIA_LINUX_AC_LINUX_FAVICONS_H_
#define ARDIA_LINUX_AC_LINUX_FAVICONS_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"

class GURL;
class Profile;
class SkBitmap;
namespace content { class WebContents; }
namespace image_fetcher { class ImageFetcher; }

namespace ardiacore {

using LinuxFaviconReply = base::OnceCallback<void(base::Value)>;

base::Value LinuxFaviconImage(const SkBitmap& bitmap);

void DownloadLinuxFavicon(content::WebContents* contents, const GURL& url,
                          base::RepeatingCallback<bool()> current, LinuxFaviconReply reply);

class LinuxFaviconFetcher {
 public:
  explicit LinuxFaviconFetcher(Profile* profile);
  ~LinuxFaviconFetcher();
  LinuxFaviconFetcher(const LinuxFaviconFetcher&) = delete;
  LinuxFaviconFetcher& operator=(const LinuxFaviconFetcher&) = delete;
  Profile* profile() const { return profile_.get(); }
  void Fetch(const GURL& url, LinuxFaviconReply reply);

 private:
  base::WeakPtr<Profile> profile_;
  std::unique_ptr<image_fetcher::ImageFetcher> fetcher_;
};

}

#endif
