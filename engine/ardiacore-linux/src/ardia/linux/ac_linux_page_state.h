#ifndef ARDIA_LINUX_AC_LINUX_PAGE_STATE_H_
#define ARDIA_LINUX_AC_LINUX_PAGE_STATE_H_

#include <memory>
#include <optional>
#include <string>

#include "ardia/linux/ac_linux_history_zoom.h"
#include "ardia/linux/ac_linux_page_lease.h"
#include "ardia/linux/ac_linux_security.h"
#include "ardia/linux/ac_linux_theme.h"

namespace ardiacore {

bool EmitLinuxPageEvent(const LinuxPageRequest& request, std::string name, base::Value data);

class LinuxPageState {
 public:
  using Navigation = base::RepeatingCallback<base::DictValue()>;
  LinuxPageState(BrowserWindowInterface* browser, base::WeakPtr<views::Widget> window,
                 content::WebContents* contents, content::WebContents* shell,
                 LinuxPageIdentity identity, LinuxPageLease::Sink sink, Navigation navigation);
  ~LinuxPageState();
  LinuxPageState(const LinuxPageState&) = delete;
  LinuxPageState& operator=(const LinuxPageState&) = delete;
  const LinuxPageIdentity& identity() const { return authority_->identity; }
  std::optional<LinuxPageRequest> Capture();
  std::optional<base::DictValue> CreationState();
  bool EmitDocument(std::string name, base::Value data);
  bool EmitPresentation(std::string name, base::Value data);
  bool EmitNavigation();
  bool EmitClosed();
  base::Value SecurityInfo();
  bool GoToOffset(const base::ListValue& args);
  bool ZoomStep(const base::ListValue& args);
  void Retire();

 private:
  std::optional<base::DictValue> NavigationState();
  std::optional<base::ListValue> SnapshotEvents();
  void Snapshot(LinuxPageRequest request, std::string reason);
  Navigation navigation_;
  std::unique_ptr<LinuxPageMountAuthority> authority_;
  std::unique_ptr<LinuxPageLease> lease_;
  std::unique_ptr<LinuxSecurity> security_;
  std::unique_ptr<LinuxHistoryZoom> history_zoom_;
  std::unique_ptr<LinuxTheme> theme_;
  bool retired_ = false;
  base::WeakPtrFactory<LinuxPageState> weak_factory_{this};
};

}

#endif
