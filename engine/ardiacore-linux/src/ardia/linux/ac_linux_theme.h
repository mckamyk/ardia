#ifndef ARDIA_LINUX_AC_LINUX_THEME_H_
#define ARDIA_LINUX_AC_LINUX_THEME_H_

#include "ardia/linux/ac_linux_page_lease.h"

namespace ardiacore {

class LinuxTheme : public content::WebContentsObserver {
 public:
  using NavigationSnapshot = base::RepeatingCallback<base::DictValue()>;
  LinuxTheme(base::WeakPtr<LinuxPageLease> lease, NavigationSnapshot navigation);
  ~LinuxTheme() override;
  base::DictValue Snapshot() const;
  bool Emit();
  void Reset();

 private:
  void DidChangeThemeColor() override;
  void WebContentsDestroyed() override;
  base::WeakPtr<LinuxPageLease> lease_;
  NavigationSnapshot navigation_;
  base::WeakPtrFactory<LinuxTheme> weak_factory_{this};
};

}

#endif
