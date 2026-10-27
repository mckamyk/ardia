#include "fixtures/page_state.h"
#include "ardia/linux/ac_linux_page_lease.h"
#include "ardia/linux/ac_linux_security.h"
#include "ardia/linux/ac_linux_history_zoom.h"
#include "ardia/linux/ac_linux_theme.h"
#include "ardia/linux/ac_linux_page_state.h"
#include <climits>
#include <limits>

using namespace ardiacore;

namespace ardiacore {
class LinuxPageLeaseTestPeer {
 public:
  static void Sequence(LinuxPageLease& lease,uint64_t value) { lease.sequence_=value; }
  static void Epoch(LinuxPageLease& lease,uint64_t value) { lease.document_epoch_=value; }
};
}

struct Harness {
  Profile profile;
  views::Widget window;
  BrowserWindowInterface browser{&profile};
  content::WebContents shell{&profile}, page{&profile};
  std::vector<base::DictValue> deliveries;
  int snapshots=0;
  std::unique_ptr<LinuxPageMountAuthority> authority;
  std::unique_ptr<LinuxPageLease> lease;
  Harness() {
    browser.strip.tabs.push_back(&page);
    authority=std::make_unique<LinuxPageMountAuthority>(&browser,window.GetWeakPtr(),&page,&shell,LinuxPageIdentity{7,"tab","claim",2,3,4,5});
    lease=std::make_unique<LinuxPageLease>(authority->GetWeakPtr(),[this](std::string module,std::string id,base::DictValue payload) {
      assert(module=="ArdiaWebView" && id=="7"); deliveries.push_back(std::move(payload));
    },[this](LinuxPageRequest request,std::string) { assert(request.Valid()); ++snapshots; });
  }
  void Boundary() { content::Page primary;page.Notify([&](auto* observer) { observer->PrimaryPageChanged(primary); }); }
  void Start(content::NavigationHandle& handle) { page.Notify([&](auto* observer) { observer->DidStartNavigation(&handle); }); }
  void Finish(content::NavigationHandle& handle) { page.Notify([&](auto* observer) { observer->DidFinishNavigation(&handle); }); }
};

base::ListValue Arg(double n) { return base::ListValue().Append(n); }
std::string Text(const base::DictValue& value,const char* key) { auto* result=value.FindString(key);assert(result);return *result; }
double Number(const base::DictValue& value,const char* key) { auto result=value.FindDouble(key);assert(result);return *result; }

void Authority() {
  Harness h;auto request=h.lease->Capture();assert(request && request->Valid());
  h.browser.strip.tabs.clear();assert(!request->Valid());h.browser.strip.tabs.push_back(&h.page);
  ui::BaseWindow other;h.browser.window=&other;assert(!request->Valid());h.browser.window=&h.browser.own_window;
  Profile other_profile;h.page.profile=&other_profile;assert(!request->Valid());h.page.profile=&h.profile;
  h.browser.profile=&other_profile;assert(!request->Valid());h.browser.profile=&h.profile;
  content::RenderFrameHost cached_shell;h.shell.primary=&cached_shell;assert(!request->Valid());h.shell.primary=&h.shell.frame;
  h.shell.frame.NewDocument();assert(!request->Valid());
  Harness retired;auto old=retired.lease->Capture();retired.authority->Invalidate();assert(!old->Valid());
  auto new_authority=std::make_unique<LinuxPageMountAuthority>(&retired.browser,retired.window.GetWeakPtr(),&retired.page,&retired.shell,LinuxPageIdentity{7,"tab","claim",2,3,4,5});
  assert(new_authority->Valid() && !old->Valid());
  Harness gone;auto delayed=gone.lease->Capture();gone.page.Notify([](auto* o) { o->WebContentsDestroyed(); });assert(!delayed->Valid());
  Harness window_gone;
  auto widget=std::make_unique<views::Widget>();
  LinuxPageMountAuthority window_authority(&window_gone.browser,widget->GetWeakPtr(),&window_gone.page,&window_gone.shell,{7,"key","claim",1,1,1,1});
  assert(window_authority.Valid());widget.reset();assert(!window_authority.Valid());
  auto destroyed_profile=std::make_unique<Profile>();content::WebContents detached_page(destroyed_profile.get()),detached_shell(destroyed_profile.get());BrowserWindowInterface detached_browser(destroyed_profile.get());detached_browser.strip.tabs.push_back(&detached_page);
  LinuxPageMountAuthority profile_authority(&detached_browser,window_gone.window.GetWeakPtr(),&detached_page,&detached_shell,{7,"key","claim",1,1,1,1});assert(profile_authority.Valid());destroyed_profile.reset();assert(!profile_authority.Valid());
  Harness missing;auto pending=missing.lease->Capture();missing.authority.reset();assert(!pending->Valid());
  Profile p;content::WebContents page(&p),shell(&p);auto browser=std::make_unique<BrowserWindowInterface>(&p);browser->strip.tabs.push_back(&page);
  LinuxPageMountAuthority authority(browser.get(),h.window.GetWeakPtr(),&page,&shell,{7,"key","a",1,1,1,1});assert(authority.Valid());browser.reset();assert(!authority.Valid());
}

void Documents() {
  Harness h;auto first=h.lease->Capture();content::RenderFrameHost next;h.page.primary=&next;
  assert(first->document.AsRenderFrameHostIfValid() && !first->Valid());h.Boundary();assert(!first->Valid());
  auto second=h.lease->Capture();assert(second && second->document_epoch==2);
  h.page.primary=&h.page.frame;h.Boundary();assert(!second->Valid() && !first->Valid());assert(h.lease->Capture()->document_epoch==3);
  auto restored=h.lease->Capture();h.page.frame.NewDocument();assert(!restored->Valid());h.Boundary();assert(h.lease->Capture());
  auto same=h.lease->Capture();content::NavigationHandle handle;handle.same=true;handle.committed=true;h.Start(handle);h.Finish(handle);assert(same->Valid());
}

void Provisional() {
  Harness pending_mount;pending_mount.page.controller.pending=&pending_mount.page.entry;
  LinuxPageLease initial(pending_mount.authority->GetWeakPtr(),[](std::string,std::string,base::DictValue) {},LinuxPageLease::SnapshotRequest());
  assert(!initial.Capture());auto initial_request=initial.CapturePresentation();assert(initial_request && initial.CreationState(*initial_request,base::ListValue()));
  LinuxSecurity initial_security(initial.GetWeakPtr());LinuxTheme initial_theme(initial.GetWeakPtr(),[] { return base::DictValue(); });
  assert(initial_security.web_contents()==&pending_mount.page && initial_theme.web_contents()==&pending_mount.page);
  assert(Text(initial_security.Snapshot(),"level")=="none" && initial_theme.Snapshot().Find("themeColor")->is_none());
  content::NavigationHandle canceled;pending_mount.Finish(canceled);assert(initial.Capture());
  Harness h;auto before=h.lease->Capture();content::NavigationHandle a;h.Start(a);
  assert(!before->Valid() && !h.lease->Capture() && Text(h.deliveries.back(),"reason")=="provisional");
  a.same=true;h.Finish(a);assert(h.snapshots==1 && h.lease->Capture()->document_epoch==before->document_epoch && !before->Valid());
  auto restored=h.lease->Capture();assert(restored->Valid());
  content::NavigationHandle old,newer;newer.id=2;h.Start(old);h.Start(newer);h.Finish(old);assert(!h.lease->Capture());h.Finish(newer);assert(h.lease->Capture());
  content::NavigationHandle commit;h.Start(commit);h.page.frame.NewDocument();commit.committed=true;h.Boundary();h.Finish(commit);assert(h.lease->Capture()->document_epoch==2);
}

void Overlapping() {
  Harness h;content::NavigationHandle first,second;second.id=2;
  h.Start(first);assert(!h.lease->Capture());
  h.Start(second);h.Finish(second);assert(!h.lease->Capture() && h.snapshots==0);
  h.Finish(first);assert(h.lease->Capture() && h.snapshots==1);
  content::NavigationHandle third,fourth;third.id=3;fourth.id=4;
  h.Start(third);h.Start(fourth);h.Finish(third);h.Finish(third);assert(!h.lease->Capture() && h.snapshots==1);
  h.Finish(fourth);assert(h.lease->Capture() && h.snapshots==2);
  content::NavigationHandle commit,stray;commit.id=5;stray.id=6;
  h.Start(commit);h.Start(stray);h.page.frame.NewDocument();commit.committed=true;h.Boundary();h.Finish(commit);
  assert(h.lease->Capture());h.Finish(stray);assert(h.lease->Capture() && h.snapshots==3);
  Harness pending;pending.page.controller.pending=&pending.page.entry;
  LinuxPageLease mounted(pending.authority->GetWeakPtr(),[](std::string,std::string,base::DictValue) {},LinuxPageLease::SnapshotRequest());
  assert(!mounted.Capture());content::NavigationHandle other;other.id=9;
  pending.Start(other);pending.Finish(other);assert(!mounted.Capture());
  content::NavigationHandle original;original.id=10;pending.Finish(original);assert(mounted.Capture());
}

void CommittedSecurity() {
  Harness h;LinuxSecurity security(h.lease->GetWeakPtr());
  h.page.security.connection_info_initialized=true;h.page.security.certificate=std::make_shared<net::X509Certificate>();
  content::NavigationEntry pending;pending.url=GURL("https://pending.test/");h.page.controller.visible=&pending;
  auto info=security.Snapshot();assert(Text(info,"url")=="https://example.test/" && Text(info,"level")=="none" && !info.Find("certificate"));
  pending.url=GURL("http://pending.test/");assert(Text(security.Snapshot(),"url")=="https://example.test/");
  h.page.controller.visible=nullptr;assert(Text(security.Snapshot(),"level")=="secure" && Text(security.Snapshot(),"url")=="https://example.test/");
  h.page.entry.url=GURL("http://plain.test/");h.page.controller.visible=&pending;pending.url=GURL("https://pending.test/");
  assert(Text(security.Snapshot(),"level")=="insecure" && Text(security.Snapshot(),"url")=="http://plain.test/");
}

void Transport() {
  Harness h;auto created=h.lease->CreationState(*h.lease->Capture(),base::ListValue());assert(created);
  auto* snapshot=created->FindDict("snapshot");assert(Number(*snapshot,"watermark")==1);
  auto request=h.lease->Capture();assert(h.lease->Emit(*request,"security",base::Value()));
  auto& message=h.deliveries.back();assert(Text(message,"claim")=="claim" && Text(message,"rendererGeneration")=="2" && Text(message,"bindingGeneration")=="3");
  assert(Number(message,"sequence")==2);h.Boundary();assert(Number(h.deliveries.back(),"sequence")==3 && Text(h.deliveries.back(),"documentEpoch")=="2");
  assert(h.lease->EmitSnapshot(*h.lease->Capture(),base::ListValue(),"document"));assert(Number(h.deliveries.back(),"sequence")==4);
  assert(!h.lease->Emit(*request,"security",base::Value()));
  assert(!h.lease->EmitSnapshot(*request,base::ListValue(),"restore"));
  Harness overflow;LinuxPageLeaseTestPeer::Sequence(*overflow.lease,9007199254740990ULL);auto last=overflow.lease->Capture();assert(overflow.lease->Emit(*last,"focus",base::Value()));assert(Number(overflow.deliveries.back(),"sequence")==9007199254740991.0);assert(!overflow.lease->Emit(*last,"focus",base::Value()) && !last->Valid());
  Harness epoch_overflow;LinuxPageLeaseTestPeer::Epoch(*epoch_overflow.lease,std::numeric_limits<uint64_t>::max());epoch_overflow.Boundary();assert(!epoch_overflow.lease->Capture());
  h.lease->Reset();assert(!h.lease->EmitSnapshot(*request,base::ListValue(),"restore"));
}

void Security() {
  Harness h;LinuxSecurity security(h.lease->GetWeakPtr());
  auto level=[&] { return Text(security.Snapshot(),"level"); };
  assert(level()=="none");
  h.page.security.connection_info_initialized=true;assert(level()=="none");
  h.page.security.certificate=std::make_shared<net::X509Certificate>();assert(level()=="secure");
  h.page.security.connection_status=net::SSL_CONNECTION_VERSION_TLS1_3;
  auto info=security.Snapshot();assert(Text(info,"protocol")=="TLS 1.3");assert(Number(*info.FindDict("certificate"),"validFrom")==1700000000123.0);
  assert(Number(*info.FindDict("certificate"),"validUntil")==1900000000456.0 && Number(*info.FindDict("certificate"),"chainLength")==3);
  assert(Text(*info.FindDict("certificate"),"serialNumber")=="0102FF");
  h.page.security.cert_status=net::CERT_STATUS_DATE_INVALID;assert(level()=="certificateError");
  h.page.security.cert_status=net::CERT_STATUS_NO_REVOCATION_MECHANISM;assert(level()=="secure");
  h.page.security.displayed_mixed_content=true;assert(level()=="mixed");assert(Text(security.Snapshot(),"mixedContent")=="displayed");
  h.page.security.ran_mixed_content=true;assert(Text(security.Snapshot(),"mixedContent")=="ran");
  h.page.security.cert_status=net::CERT_STATUS_COMMON_NAME_INVALID;assert(level()=="certificateError");
  h.page.security={};h.page.entry.url=GURL("http://example.test/");assert(level()=="insecure" && !security.Snapshot().Find("certificate"));
  for (const auto* local:{"file:///tmp/test","chrome://settings/","chrome-untrusted://test/","devtools://test/"}) { h.page.entry.url=GURL(local);assert(level()=="local"); }
  for (const auto* unknown:{"about:blank","data:text/html,test","blob:https://example.test/1","custom:test","https://example.test/"}) { h.page.entry.url=GURL(unknown);assert(level()=="none"); }
  h.page.controller.entry=nullptr;assert(level()=="none");h.page.controller.entry=&h.page.entry;
  h.page.security.connection_info_initialized=true;h.page.security.certificate=std::make_shared<net::X509Certificate>();h.page.entry.initial=true;assert(level()=="none");h.page.entry.initial=false;
  h.page.Notify([](auto* o) { o->DidChangeVisibleSecurityState(); });assert(Text(h.deliveries.back().FindDict("event")->Find("data")->GetDict(),"level")=="secure");
  content::NavigationHandle provisional;h.Start(provisional);assert(!security.Emit() && !security.Snapshot().Find("certificate"));h.Finish(provisional);assert(level()=="secure");
  security.Reset();assert(!security.Emit() && level()=="none");
}

void History() {
  Harness h;zoom::ZoomController controller;h.page.zoom=&controller;LinuxHistoryZoom zoom(h.lease->GetWeakPtr());
  for (double bad:{1.5,static_cast<double>(INT_MAX)+1,static_cast<double>(INT_MIN)-1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) assert(!zoom.GoToOffset(Arg(bad)));
  assert(!zoom.GoToOffset(base::ListValue()) && !zoom.GoToOffset(base::ListValue().Append("2")) && !zoom.GoToOffset(base::ListValue().Append(true)) && !zoom.GoToOffset(base::ListValue().Append(1).Append(2)));
  assert(zoom.GoToOffset(Arg(0)) && h.page.controller.checks==0);
  assert(zoom.GoToOffset(Arg(INT_MIN)) && zoom.GoToOffset(Arg(INT_MAX)) && h.page.controller.goes==0);
  assert(zoom.GoToOffset(Arg(-2)) && h.page.controller.last==-2 && h.page.controller.current==0);
  assert(zoom.GoToOffset(Arg(3)) && h.page.controller.current==3);
  controller.level=2;controller.fallback=1;auto info=zoom.Snapshot();assert(info && std::abs(Number(*info,"zoom")-1.44)<1e-12 && !info->Find("isDefault")->GetBool());
  assert(zoom.ZoomStep(Arg(0)) && controller.level==1 && zoom.Snapshot()->Find("isDefault")->GetBool());
  assert(zoom.ZoomStep(Arg(1)) && controller.level==2);assert(!zoom.ZoomStep(Arg(2)));
}

void HistoryBounds() {
  Harness h;zoom::ZoomController controller;h.page.zoom=&controller;LinuxHistoryZoom zoom(h.lease->GetWeakPtr());
  for (int current:{-1,0,2,INT_MAX-2}) {
    h.page.controller.current=current;
    h.page.controller.size=current<0?0:INT_MAX;
    assert(zoom.GoToOffset(Arg(INT_MIN)) && zoom.GoToOffset(Arg(INT_MAX)));
    assert(h.page.controller.current==current && h.page.controller.goes==0 && h.page.controller.checks==0);
  }
  h.page.controller.current=INT_MAX-2;
  assert(zoom.GoToOffset(Arg(1)) && h.page.controller.current==INT_MAX-1 && h.page.controller.checks==1);
}

void Domain() {
  auto request=PrepareLinuxZoomDomain("sites.zoomLevels",base::DictValue());assert(request && request->service=="ac_zoom_list" && request->args.empty());
  auto set=PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host","EXAMPLE.TEST").Set("zoom",1.44));assert(set && Text(set->args,"host")=="example.test" && std::abs(Number(set->args,"level")-2)<1e-12 && !set->args.Find("zoom"));
  for(const auto* host:{"","https://test/","a/b","a@b","a b","a:123", "a:", "a\\b"}) assert(!PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host",host).Set("zoom",1)));
  assert(!PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host",std::string(254,'a')).Set("zoom",1)));
  for(double bad:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) assert(!PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host","a").Set("zoom",bad)));
  assert(!PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host","a").Set("zoom",1).Set("tab",7)));
  auto factors=CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue().Set("levels",base::DictValue().Set("EXAMPLE.TEST",2.0).Set("other.test",0.5)));assert(factors && Number(factors->GetDict(),"example.test")==1.44 && Number(factors->GetDict(),"other.test")==1.10);
  assert(CompleteLinuxZoomDomain("sites.setZoom",base::DictValue())->is_none());
  assert(!CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue()));
  auto skipped=CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue().Set("levels",base::DictValue().Set("a",std::numeric_limits<double>::infinity()).Set("ok.test",2.0)));
  assert(skipped && skipped->GetDict().size()==1 && Number(skipped->GetDict(),"ok.test")==1.44);
  auto with_file=CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue().Set("levels",base::DictValue().Set("file:///home/user/page.html",2.0).Set("example.test",-2.0)));
  assert(with_file && with_file->GetDict().size()==1 && !with_file->GetDict().Find("file:///home/user/page.html"));
  assert(Number(with_file->GetDict(),"example.test")==0.69);
  auto tiny=CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue().Set("levels",base::DictValue().Set("tiny.test",-40.0)));
  assert(tiny && tiny->GetDict().empty());
  assert(!CompleteLinuxZoomDomain("sites.zoomLevels",base::DictValue().Set("levels",base::DictValue().Set("a","2"))));
  for(double bad:{0.24,5.01,1000.,1e-9}) assert(!PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host","a").Set("zoom",bad)));
  for(double good:{0.25,1.,5.}) assert(PrepareLinuxZoomDomain("sites.setZoom",base::DictValue().Set("host","a").Set("zoom",good)));
}

void Zoom() {
  Harness h;auto controller=std::make_unique<zoom::ZoomController>();h.page.zoom=controller.get();LinuxHistoryZoom zoom(h.lease->GetWeakPtr());assert(controller->observers.size()==1);
  controller->scale_one=false;h.page.view.widget.provider.value.page_scale_factor=1.75;assert(Number(*zoom.Snapshot(),"pinchScale")==1.75);
  h.page.Notify([](auto* o) { o->OnPageScaleFactorChanged(2.5); });assert(Number(*zoom.Snapshot(),"pinchScale")==2.5);
  h.page.zoom=nullptr;controller.reset();assert(!zoom.Snapshot());zoom.Reset();
  zoom::ZoomController next;h.page.zoom=&next;LinuxHistoryZoom rebound(h.lease->GetWeakPtr());assert(rebound.Emit());rebound.Reset();assert(next.observers.empty());
}

void Theme() {
  Harness h;LinuxTheme theme(h.lease->GetWeakPtr(),[] { return base::DictValue().Set("url","https://example.test/"); });
  assert(theme.Snapshot().Find("themeColor")->is_none() && theme.Snapshot().Find("themeColorSource")->is_none());
  h.page.theme=0xff12abef;assert(Text(theme.Snapshot(),"themeColor")=="#12abef" && Text(theme.Snapshot(),"themeColorSource")=="meta");
  h.page.Notify([](auto* o) { o->DidChangeThemeColor(); });assert(Text(h.deliveries.back().FindDict("event")->Find("data")->GetDict(),"themeColor")=="#12abef");
  h.page.theme.reset();assert(theme.Emit());assert(h.deliveries.back().FindDict("event")->Find("data")->GetDict().Find("themeColor")->is_none());
  h.page.theme=0xffff0000;content::NavigationHandle a;h.Start(a);assert(!theme.Emit() && theme.Snapshot().Find("themeColor")->is_none());h.Finish(a);assert(theme.Emit());theme.Reset();assert(!theme.Emit());
}

void Reentrant() {
  Harness h;
  std::unique_ptr<LinuxSecurity> security;
  std::unique_ptr<LinuxPageLease> lease;
  lease=std::make_unique<LinuxPageLease>(h.authority->GetWeakPtr(),[&](std::string,std::string,base::DictValue) { security.reset();lease.reset(); },LinuxPageLease::SnapshotRequest());
  security=std::make_unique<LinuxSecurity>(lease->GetWeakPtr());assert(security->Emit());assert(!security && !lease);
  std::unique_ptr<LinuxTheme> theme;
  theme=std::make_unique<LinuxTheme>(h.lease->GetWeakPtr(),[&] { theme.reset();return base::DictValue(); });assert(!theme->Emit() && !theme);
  std::unique_ptr<LinuxPageLease> boundary;
  boundary=std::make_unique<LinuxPageLease>(h.authority->GetWeakPtr(),[](std::string,std::string,base::DictValue) {},[&](LinuxPageRequest,std::string) { boundary.reset(); });h.Boundary();assert(!boundary);
  std::unique_ptr<LinuxPageLease> reset_lease;
  reset_lease=std::make_unique<LinuxPageLease>(h.authority->GetWeakPtr(),[&](std::string,std::string,base::DictValue) { reset_lease.reset(); },LinuxPageLease::SnapshotRequest());h.Boundary();assert(!reset_lease);
  zoom::ZoomController controller;h.page.zoom=&controller;
  std::unique_ptr<LinuxHistoryZoom> zoom_helper;
  LinuxPageLease zoom_lease(h.authority->GetWeakPtr(),[&](std::string,std::string,base::DictValue) { zoom_helper.reset(); },LinuxPageLease::SnapshotRequest());
  zoom_helper=std::make_unique<LinuxHistoryZoom>(zoom_lease.GetWeakPtr());assert(zoom_helper->ZoomStep(Arg(1)));assert(!zoom_helper && controller.observers.empty());
}

struct Mount {
  Profile profile;
  views::Widget window;
  BrowserWindowInterface browser{&profile};
  content::WebContents shell{&profile}, page{&profile};
  zoom::ZoomController controller;
  std::vector<base::DictValue> deliveries;
  int navigations=0;
  std::function<void()> on_delivery;
  std::unique_ptr<LinuxPageState> state;
  explicit Mount(uint64_t binding=3) {
    browser.strip.tabs.push_back(&page);page.zoom=&controller;
    state=std::make_unique<LinuxPageState>(&browser,window.GetWeakPtr(),&page,&shell,LinuxPageIdentity{7,"tab","claim",2,binding,1,9},
      [this](std::string module,std::string id,base::DictValue payload) { assert(module=="ArdiaWebView" && id=="7");deliveries.push_back(std::move(payload));if(on_delivery) on_delivery(); },
      [this] { ++navigations;return base::DictValue().Set("url",page.entry.url.spec()).Set("title","T").Set("canGoBack",true).Set("canGoForward",false).Set("isLoading",false); });
  }
  void Boundary() { content::Page primary;page.Notify([&](auto* observer) { observer->PrimaryPageChanged(primary); }); }
  void Start(content::NavigationHandle& handle) { page.Notify([&](auto* observer) { observer->DidStartNavigation(&handle); }); }
  void Finish(content::NavigationHandle& handle) { page.Notify([&](auto* observer) { observer->DidFinishNavigation(&handle); }); }
  const base::DictValue& Last() { assert(!deliveries.empty());return deliveries.back(); }
  std::string LastName() { return Text(*Last().FindDict("event"),"name"); }
  const base::DictValue& LastData() { return Last().FindDict("event")->Find("data")->GetDict(); }
};

const base::DictValue& EventAt(const base::DictValue& holder,size_t index,const char* name) {
  const auto& list=holder.Find("events")->GetList();assert(index<list.size());
  const auto& event=list[index].GetDict();assert(Text(event,"name")==name);return event.Find("data")->GetDict();
}

void Creation() {
  Mount m;m.page.theme=0xff102030;m.page.security.connection_info_initialized=true;m.page.security.certificate=std::make_shared<net::X509Certificate>();m.controller.level=1;
  auto created=m.state->CreationState();assert(created && m.deliveries.empty());
  const auto& lease=*created->FindDict("lease");assert(Text(lease,"claim")=="claim" && Text(lease,"rendererGeneration")=="2" && Text(lease,"bindingGeneration")=="3" && Text(lease,"documentEpoch")=="1");
  const auto& snapshot=*created->FindDict("snapshot");assert(Number(snapshot,"watermark")==Number(lease,"sequence") && snapshot.Find("events")->GetList().size()==3);
  const auto& navigation=EventAt(snapshot,0,"navigation");assert(Text(navigation,"url")=="https://example.test/" && Text(navigation,"themeColor")=="#102030" && Text(navigation,"themeColorSource")=="meta");
  assert(Text(EventAt(snapshot,1,"security"),"level")=="secure");
  assert(std::abs(Number(EventAt(snapshot,2,"zoom"),"zoom")-1.2)<1e-12 && !EventAt(snapshot,2,"zoom").Find("isDefault")->GetBool());
  for (auto* observer:std::vector<zoom::ZoomObserver*>(m.controller.observers)) observer->OnZoomControllerDestroyed(&m.controller);
  auto without_zoom=m.state->CreationState();assert(without_zoom->FindDict("snapshot")->Find("events")->GetList().size()==2 && m.controller.observers.empty());
  Mount provisional;content::NavigationHandle start;provisional.Start(start);auto cleared=provisional.state->CreationState();assert(cleared);
  const auto& cleared_navigation=EventAt(*cleared->FindDict("snapshot"),0,"navigation");assert(cleared_navigation.Find("themeColor")->is_none());
  assert(Text(EventAt(*cleared->FindDict("snapshot"),1,"security"),"level")=="none");
  Mount gone;gone.browser.strip.tabs.clear();assert(!gone.state->CreationState());
  Mount retired;retired.state->Retire();assert(!retired.state->CreationState() && !retired.state->EmitNavigation());
}

void Boundaries() {
  Mount m;m.state->CreationState();auto old=m.state->Capture();assert(old);
  m.page.entry.url=GURL("http://plain.test/");m.page.frame.NewDocument();m.Boundary();
  assert(m.deliveries.size()==2);
  assert(Text(m.deliveries[0],"type")=="pageLifecycle" && Text(m.deliveries[0],"action")=="reset" && Text(m.deliveries[0],"reason")=="document" && Text(m.deliveries[0],"documentEpoch")=="2");
  assert(Text(m.deliveries[1],"action")=="snapshot" && Text(m.deliveries[1],"documentEpoch")=="2" && Number(m.deliveries[1],"sequence")>Number(m.deliveries[0],"sequence"));
  assert(Text(m.deliveries[1],"reason")=="document" && m.deliveries[1].Find("events")->GetList().size()==3);
  assert(Text(EventAt(m.deliveries[1],1,"security"),"level")=="insecure" && Text(EventAt(m.deliveries[1],0,"navigation"),"url")=="http://plain.test/");
  assert(!old->Valid() && !EmitLinuxPageEvent(*old,"find",base::Value()) && m.deliveries.size()==2);
  auto current=m.state->Capture();assert(EmitLinuxPageEvent(*current,"find",base::Value(base::DictValue().Set("count",1))) && m.LastName()=="find" && Text(m.Last(),"documentEpoch")=="2");
  content::NavigationHandle pending;m.Start(pending);assert(Text(m.Last(),"reason")=="provisional");
  const auto before=m.deliveries.size();
  assert(!m.state->EmitDocument("favicon",base::Value()) && !EmitLinuxPageEvent(*current,"find",base::Value()) && m.deliveries.size()==before);
  assert(m.state->EmitNavigation() && m.LastName()=="navigation" && m.LastData().Find("themeColor")->is_none());
  assert(m.state->EmitPresentation("progress",base::Value(base::DictValue().Set("progress",0.5))) && m.LastName()=="progress");
  assert(m.state->SecurityInfo().is_none());
  const auto restoring=m.deliveries.size();m.Finish(pending);assert(m.deliveries.size()==restoring+2);
  assert(Text(m.deliveries[restoring],"action")=="snapshot" && Text(m.deliveries[restoring],"documentEpoch")=="2" && m.LastName()=="zoom");
  assert(Text(m.deliveries[restoring],"reason")=="restore");
  assert(!m.state->SecurityInfo().is_none() && Text(m.state->SecurityInfo().GetDict(),"level")=="insecure");
  double last=0;for(const auto& delivery:m.deliveries) { assert(Number(delivery,"sequence")>last);last=Number(delivery,"sequence"); }
}

void Favicons() {
  Mount m;m.page.favicons.push_back(std::make_shared<blink::mojom::FaviconURL>(blink::mojom::FaviconURL{GURL("https://example.test/a.ico")}));
  m.page.favicons.push_back(std::make_shared<blink::mojom::FaviconURL>(blink::mojom::FaviconURL{GURL("https://example.test/b.png")}));
  auto created=m.state->CreationState();const auto& snapshot=*created->FindDict("snapshot");
  const auto& favicon=EventAt(snapshot,3,"favicon");assert(Text(favicon,"url")=="https://example.test/a.ico");
  assert(favicon.Find("urls")->GetList().size()==2 && favicon.Find("urls")->GetList()[1].GetString()=="https://example.test/b.png");
  content::NavigationHandle pending;m.Start(pending);
  assert(!m.state->EmitDocument("favicon",base::Value()));
  m.Finish(pending);
  bool resent=false;for(const auto& delivery:m.deliveries) if(Text(delivery,"type")=="pageLifecycle" && delivery.FindString("reason") && Text(delivery,"reason")=="restore") { EventAt(delivery,3,"favicon");resent=true; }
  assert(resent);
  Mount empty;assert(empty.state->CreationState()->FindDict("snapshot")->Find("events")->GetList().size()==3);
}

void Commands() {
  Mount m;m.page.controller.current=3;m.page.controller.size=4;
  assert(m.state->GoToOffset(Arg(-2)) && m.page.controller.current==1);
  assert(m.state->GoToOffset(Arg(2)) && m.page.controller.current==3);
  assert(m.state->GoToOffset(Arg(-3)) && m.page.controller.current==0);
  assert(m.state->GoToOffset(Arg(-2)) && m.page.controller.current==0 && m.page.controller.goes==3);
  assert(!m.state->GoToOffset(Arg(1.5)) && !m.state->GoToOffset(base::ListValue()));
  content::NavigationHandle pending;m.Start(pending);
  assert(m.state->GoToOffset(Arg(2)) && m.page.controller.current==2);
  assert(m.state->ZoomStep(Arg(1)) && m.controller.level==1);
  m.Finish(pending);
  const auto zooms=m.deliveries.size();
  assert(m.state->ZoomStep(Arg(1)) && m.controller.level==2 && m.deliveries.size()==zooms+1 && m.LastName()=="zoom");
  assert(std::abs(Number(m.LastData(),"zoom")-1.44)<1e-12 && !m.LastData().Find("isDefault")->GetBool() && Text(m.LastData(),"host")=="example.test");
  assert(m.state->ZoomStep(Arg(0)) && m.controller.level==0 && m.LastData().Find("isDefault")->GetBool() && Number(m.LastData(),"zoom")==1);
  assert(!m.state->ZoomStep(Arg(-2)));
  m.state->Retire();assert(!m.state->GoToOffset(Arg(-1)) && !m.state->ZoomStep(Arg(1)) && m.controller.observers.empty());
}

void Closing() {
  Mount m;m.state->CreationState();m.browser.strip.tabs.clear();
  assert(!m.state->EmitNavigation() && !m.state->EmitPresentation("progress",base::Value()));
  assert(m.state->EmitClosed() && m.LastName()=="windowClose" && Text(m.Last(),"claim")=="claim");
  assert(!m.state->EmitClosed() && m.deliveries.size()==1);
  Mount retired;retired.state->Retire();assert(!retired.state->EmitClosed() && retired.deliveries.empty());
  Mount destroyed;auto* page=&destroyed.page;destroyed.state->CreationState();
  page->Notify([](auto* o) { o->WebContentsDestroyed(); });assert(!destroyed.state->EmitClosed() && destroyed.deliveries.empty());
}

void Rebinding() {
  Mount first(3);auto first_request=first.state->Capture();first.state->Retire();
  assert(!first_request->Valid() && !EmitLinuxPageEvent(*first_request,"find",base::Value()));
  first.state=std::make_unique<LinuxPageState>(&first.browser,first.window.GetWeakPtr(),&first.page,&first.shell,LinuxPageIdentity{7,"tab","claim",2,4,1,9},
      [&first](std::string,std::string,base::DictValue payload) { first.deliveries.push_back(std::move(payload)); },[] { return base::DictValue(); });
  assert(!first_request->Valid());auto created=first.state->CreationState();assert(Text(*created->FindDict("lease"),"bindingGeneration")=="4");
  assert(first.controller.observers.size()==1);
  Mount reentrant;reentrant.state->CreationState();reentrant.on_delivery=[&] { reentrant.state.reset(); };
  reentrant.page.frame.NewDocument();reentrant.Boundary();assert(!reentrant.state && reentrant.deliveries.size()==1);
  Mount snapshot_reentrant;snapshot_reentrant.state->CreationState();int seen=0;snapshot_reentrant.on_delivery=[&] { if(++seen==2) snapshot_reentrant.state.reset(); };
  snapshot_reentrant.page.frame.NewDocument();snapshot_reentrant.Boundary();assert(!snapshot_reentrant.state && snapshot_reentrant.deliveries.size()==2 && snapshot_reentrant.controller.observers.empty());
  Mount navigation_reentrant;bool destroy=false;
  navigation_reentrant.state=std::make_unique<LinuxPageState>(&navigation_reentrant.browser,navigation_reentrant.window.GetWeakPtr(),&navigation_reentrant.page,&navigation_reentrant.shell,LinuxPageIdentity{7,"tab","claim",2,5,1,9},
      [](std::string,std::string,base::DictValue) {},[&] { if (destroy) navigation_reentrant.state.reset();return base::DictValue(); });
  destroy=true;assert(!navigation_reentrant.state->EmitNavigation() && !navigation_reentrant.state);
}

int main(int argc,char** argv) {
  assert(argc==2);std::string mode=argv[1];
  if(mode=="authority") Authority();else if(mode=="documents") Documents();else if(mode=="provisional") Provisional();else if(mode=="transport") Transport();else if(mode=="security") Security();else if(mode=="history") History();else if(mode=="history-bounds") HistoryBounds();else if(mode=="domain") Domain();else if(mode=="zoom") Zoom();else if(mode=="theme") Theme();else if(mode=="reentrant") Reentrant();else if(mode=="overlapping") Overlapping();else if(mode=="committed-security") CommittedSecurity();else if(mode=="creation") Creation();else if(mode=="boundaries") Boundaries();else if(mode=="commands") Commands();else if(mode=="favicons") Favicons();else if(mode=="closing") Closing();else if(mode=="rebinding") Rebinding();else return 2;
}
