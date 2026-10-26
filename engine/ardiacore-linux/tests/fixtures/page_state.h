#ifndef ARDIA_TEST_PAGE_STATE_H_
#define ARDIA_TEST_PAGE_STATE_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace base {
class DictValue;
class ListValue;
class Value {
 public:
  Value() = default;
  Value(const char* v) : data_(std::string(v)) {}
  Value(std::string v) : data_(std::move(v)) {}
  Value(std::string_view v) : data_(std::string(v)) {}
  Value(bool v) : data_(v) {}
  Value(int v) : data_(v) {}
  Value(double v) : data_(v) {}
  Value(DictValue v);
  Value(ListValue v);
  Value(Value&&) = default;
  Value& operator=(Value&&) = default;
  Value(const Value&) = delete;
  Value& operator=(const Value&) = delete;
  bool is_int() const { return std::holds_alternative<int>(data_); }
  bool is_double() const { return std::holds_alternative<double>(data_); }
  bool is_none() const { return std::holds_alternative<std::monostate>(data_); }
  double GetDouble() const { return is_int() ? std::get<int>(data_) : std::get<double>(data_); }
  const std::string& GetString() const { return std::get<std::string>(data_); }
  bool GetBool() const { return std::get<bool>(data_); }
  DictValue& GetDict() const { return *std::get<std::shared_ptr<DictValue>>(data_); }
  ListValue& GetList() const { return *std::get<std::shared_ptr<ListValue>>(data_); }
  bool is_dict() const { return std::holds_alternative<std::shared_ptr<DictValue>>(data_); }
  Value Clone() const;
 private:
  std::variant<std::monostate, bool, int, double, std::string, std::shared_ptr<DictValue>, std::shared_ptr<ListValue>> data_;
};
class DictValue {
 public:
  DictValue() = default;
  DictValue(DictValue&&) = default;
  DictValue& operator=(DictValue&&) = default;
  DictValue(const DictValue&) = delete;
  template<class T> Value* Set(std::string key, T v) & { auto entry=data_.insert_or_assign(std::move(key), Value(std::move(v))); return &entry.first->second; }
  template<class T> DictValue&& Set(std::string key, T v) && { Set(std::move(key), std::move(v)); return std::move(*this); }
  Value* Find(std::string_view key) const { auto it=data_.find(std::string(key)); return it==data_.end()?nullptr:const_cast<Value*>(&it->second); }
  const std::string* FindString(std::string_view key) const { auto* v=Find(key); if (!v) return nullptr; try { return &v->GetString(); } catch (...) { return nullptr; } }
  std::optional<double> FindDouble(std::string_view key) const { auto* v=Find(key); return v && (v->is_int() || v->is_double()) ? std::optional<double>(v->GetDouble()):std::nullopt; }
  const DictValue* FindDict(std::string_view key) const { auto* v=Find(key); return v && v->is_dict() ? &v->GetDict():nullptr; }
  void Merge(DictValue v) { for (auto& [key,value] : v.data_) data_.insert_or_assign(key,std::move(value)); }
  DictValue Clone() const { DictValue v; for (const auto& [k,x]:data_) v.Set(k,x.Clone()); return v; }
  bool empty() const { return data_.empty(); }
  size_t size() const { return data_.size(); }
  const auto& storage() const { return data_; }
  class Iterator {
   public:
    using Inner=std::map<std::string,Value>::const_iterator;
    explicit Iterator(Inner it): it_(it) {}
    auto operator*() const { return std::pair<const std::string&,const Value&>(it_->first,it_->second); }
    Iterator& operator++() { ++it_; return *this; }
    bool operator!=(const Iterator& v) const { return it_!=v.it_; }
   private: Inner it_;
  };
  auto begin() const { return Iterator(data_.begin()); }
  auto end() const { return Iterator(data_.end()); }
 private: std::map<std::string,Value> data_;
};
class ListValue {
 public:
  ListValue() = default;
  ListValue(ListValue&&) = default;
  ListValue& operator=(ListValue&&) = default;
  ListValue(const ListValue&) = delete;
  template<class T> ListValue& Append(T v) & { data_.emplace_back(std::move(v)); return *this; }
  template<class T> ListValue&& Append(T v) && { Append(std::move(v)); return std::move(*this); }
  const Value& operator[](size_t i) const { return data_[i]; }
  size_t size() const { return data_.size(); }
  ListValue Clone() const { ListValue v; for(const auto& x:data_) v.Append(x.Clone()); return v; }
 private: std::vector<Value> data_;
};
inline Value::Value(DictValue v):data_(std::make_shared<DictValue>(std::move(v))) {}
inline Value::Value(ListValue v):data_(std::make_shared<ListValue>(std::move(v))) {}
inline Value Value::Clone() const {
  return std::visit([](const auto& v)->Value { using T=std::decay_t<decltype(v)>; if constexpr(std::is_same_v<T,std::monostate>) return Value(); else if constexpr(std::is_same_v<T,std::shared_ptr<DictValue>> || std::is_same_v<T,std::shared_ptr<ListValue>>) return Value(v->Clone()); else return Value(v); },data_);
}
template<class T> class WeakPtr {
 public:
  WeakPtr() = default;
  WeakPtr(std::nullptr_t) {}
  WeakPtr(T* ptr, std::weak_ptr<int> live):ptr_(ptr),live_(live) {}
  T* get() const { return live_.expired()?nullptr:ptr_; }
  T* operator->() const { return get(); }
  explicit operator bool() const { return get(); }
  void reset() { live_.reset(); ptr_=nullptr; }
 private: T* ptr_=nullptr; std::weak_ptr<int> live_;
};
template<class T> class WeakPtrFactory {
 public:
  explicit WeakPtrFactory(T* ptr):ptr_(ptr) {}
  WeakPtr<T> GetWeakPtr() { return WeakPtr<T>(ptr_,live_); }
  void InvalidateWeakPtrs() { live_=std::make_shared<int>(0); }
 private: T* ptr_; std::shared_ptr<int> live_=std::make_shared<int>(0);
};
template<class> class RepeatingCallback;
template<class R,class... A> class RepeatingCallback<R(A...)> {
 public:
  RepeatingCallback() = default;
  template<class F> RepeatingCallback(F f): fn_(std::move(f)) {}
  R Run(A... args) const { return fn_(std::forward<A>(args)...); }
  explicit operator bool() const { return bool(fn_); }
  void Reset() { fn_={}; }
 private: std::function<R(A...)> fn_;
};
class CheckedObserver {};
template<class... Args> std::string StringPrintf(const char* format, Args... args) { char out[128]; std::snprintf(out,sizeof(out),format,args...); return out; }
template<class T> std::string HexEncode(const T& bytes) { std::string result; for (auto c:bytes) result+=StringPrintf("%02X",static_cast<unsigned char>(c)); return result; }
struct Time { double value=0; double InMillisecondsFSinceUnixEpoch() const { return value; } };
}

class GURL {
 public:
  GURL() = default;
  explicit GURL(std::string value):value_(std::move(value)) {
    auto colon=value_.find(':'); if (colon==std::string::npos) return; scheme_=value_.substr(0,colon);
    if (value_.substr(colon,3)=="://") { auto start=colon+3; auto end=value_.find('/',start); host_=value_.substr(start,end-start); std::transform(host_.begin(),host_.end(),host_.begin(),::tolower); }
  }
  bool is_valid() const { return !scheme_.empty() && value_.find(' ')==std::string::npos; }
  const std::string& spec() const { return value_; }
  std::string_view host() const { return host_; }
  bool has_port() const { return host_.find(':')!=std::string::npos; }
  bool SchemeIs(std::string_view scheme) const { return scheme_==scheme; }
  bool SchemeIsHTTPOrHTTPS() const { return SchemeIs("http") || SchemeIs("https"); }
 private: std::string value_,scheme_,host_;
};
namespace url { struct Origin { GURL value; static Origin Create(GURL url) { return {url}; } std::string Serialize() const { return std::string(value.SchemeIs("https")?"https://":"http://")+std::string(value.host()); } }; }
namespace ui { class BaseWindow {}; }
namespace views { class Widget { public: base::WeakPtr<Widget> GetWeakPtr() { return weak_.GetWeakPtr(); } private: base::WeakPtrFactory<Widget> weak_{this}; }; }
class Profile {
 public: base::WeakPtr<Profile> GetWeakPtr() { return weak_.GetWeakPtr(); }
 private: base::WeakPtrFactory<Profile> weak_{this};
};
namespace net {
using CertStatus=uint32_t;
using SHA256HashValue=std::array<unsigned char,32>;
struct CertPrincipal { std::string common_name, country_name; std::vector<std::string> organization_names; std::string GetDisplayName() const { return common_name; } };
struct X509Certificate {
  CertPrincipal subject_,issuer_;
  base::Time start{1700000000123},end{1900000000456};
  std::vector<unsigned char> serial{1,2,255};
  std::vector<int> intermediates{1,2};
  const CertPrincipal& subject() const { return subject_; }
  const CertPrincipal& issuer() const { return issuer_; }
  base::Time valid_start() const { return start; }
  base::Time valid_expiry() const { return end; }
  auto serial_number() const { return std::span<const unsigned char>(serial); }
  const auto& intermediate_buffers() const { return intermediates; }
  const void* cert_buffer() const { return this; }
  static SHA256HashValue CalculateFingerprint256(const void*) { return {}; }
};
constexpr CertStatus CERT_STATUS_COMMON_NAME_INVALID=1,CERT_STATUS_DATE_INVALID=2,CERT_STATUS_AUTHORITY_INVALID=4,CERT_STATUS_REVOKED=32,CERT_STATUS_INVALID=128,CERT_STATUS_WEAK_SIGNATURE_ALGORITHM=256,CERT_STATUS_NON_UNIQUE_NAME=1024,CERT_STATUS_WEAK_KEY=2048,CERT_STATUS_PINNED_KEY_MISSING=8192,CERT_STATUS_NAME_CONSTRAINT_VIOLATION=16384,CERT_STATUS_VALIDITY_TOO_LONG=32768,CERT_STATUS_CERTIFICATE_TRANSPARENCY_REQUIRED=1<<24,CERT_STATUS_IS_EV=1<<16,CERT_STATUS_NO_REVOCATION_MECHANISM=8,CERT_STATUS_UNABLE_TO_CHECK_REVOCATION=16,CERT_STATUS_ALL_ERRORS=0xFF00FFFF;
enum {SSL_CONNECTION_VERSION_TLS1=1,SSL_CONNECTION_VERSION_TLS1_1,SSL_CONNECTION_VERSION_TLS1_2,SSL_CONNECTION_VERSION_TLS1_3,SSL_CONNECTION_VERSION_QUIC};
inline int SSLConnectionStatusToVersion(int value) { return value; }
}
namespace security_state {
struct VisibleSecurityState {
  bool connection_info_initialized=false;
  std::shared_ptr<net::X509Certificate> certificate;
  net::CertStatus cert_status=0;
  int connection_status=0;
  bool ran_mixed_content=false, displayed_mixed_content=false, ran_content_with_cert_errors=false, displayed_content_with_cert_errors=false, contained_mixed_form=false,is_error_page=false;
};
}
namespace content {
class WebContents;
class RenderFrameHost;
struct WeakDocumentPtr {
  base::WeakPtr<RenderFrameHost> value;
  RenderFrameHost* AsRenderFrameHostIfValid() const { return value.get(); }
};
struct RenderFrameHost {
  bool live=true;
  WeakDocumentPtr GetWeakDocumentPtr() { return {document.GetWeakPtr()}; }
  bool IsRenderFrameLive() const { return live; }
  void NewDocument() { document.InvalidateWeakPtrs(); }
  base::WeakPtrFactory<RenderFrameHost> document{this};
};
struct NavigationEntry { GURL url{"https://example.test/"}; bool initial=false; GURL GetURL() const { return url; } bool IsInitialEntry() const { return initial; } };
struct NavigationController {
  NavigationEntry* entry=nullptr;
  NavigationEntry* pending=nullptr;
  NavigationEntry* GetPendingEntry() { return pending; }
  int current=2, size=4, last=0, checks=0, goes=0;
  NavigationEntry* GetVisibleEntry() { return entry; }
  int GetCurrentEntryIndex() { return current; }
  int GetEntryCount() { return size; }
  bool CanGoToOffset(int n) { ++checks; return current+n>=0 && current+n<size; }
  void GoToOffset(int n) { ++goes;last=n;current+=n; }
};
struct NavigationHandle {
  bool primary=true,same=false,committed=false; int64_t id=1;
  bool IsInPrimaryMainFrame() const { return primary; }
  bool IsSameDocument() const { return same; }
  bool HasCommitted() const { return committed; }
  int64_t GetNavigationId() const { return id; }
};
struct Page {};
class WebContentsObserver {
 public:
  explicit WebContentsObserver(WebContents* contents=nullptr);
  virtual ~WebContentsObserver();
  void Observe(WebContents* contents);
  WebContents* web_contents() const { return contents_; }
  virtual void PrimaryPageChanged(Page&) {}
  virtual void DidStartNavigation(NavigationHandle*) {}
  virtual void DidFinishNavigation(NavigationHandle*) {}
  virtual void WebContentsDestroyed() {}
  virtual void DidChangeVisibleSecurityState() {}
  virtual void DidChangeThemeColor() {}
  virtual void OnPageScaleFactorChanged(float) {}
 private: WebContents* contents_=nullptr;
};
struct Metadata { float page_scale_factor=1; };
struct MetadataProvider { Metadata value; const Metadata& LastRenderFrameMetadata() { return value; } };
struct RenderWidgetHostImpl { MetadataProvider provider; static RenderWidgetHostImpl* From(RenderWidgetHostImpl* widget) { return widget; } MetadataProvider* render_frame_metadata_provider() { return &provider; } };
struct RenderWidgetHostView { RenderWidgetHostImpl widget; RenderWidgetHostImpl* GetRenderWidgetHost() { return &widget; } };
class WebContents {
 public:
  explicit WebContents(Profile* p):profile(p) {}
  ~WebContents() { auto copy=observers; for(auto* o:copy) if (std::find(observers.begin(),observers.end(),o)!=observers.end()) o->WebContentsDestroyed(); }
  base::WeakPtr<WebContents> GetWeakPtr() { return weak.GetWeakPtr(); }
  RenderFrameHost* GetPrimaryMainFrame() const { return primary; }
  Profile* GetBrowserContext() const { return profile; }
  NavigationController& GetController() { return controller; }
  GURL GetLastCommittedURL() const { return committed; }
  std::optional<uint32_t> GetThemeColor() { return theme; }
  RenderWidgetHostView* GetRenderWidgetHostView() { return &view; }
  template<class F> void Notify(F fn) { auto copy=observers; for(auto* o:copy) if(std::find(observers.begin(),observers.end(),o)!=observers.end()) fn(o); }
  Profile* profile;
  RenderFrameHost frame;
  RenderFrameHost* primary=&frame;
  NavigationEntry entry;
  NavigationController controller{&entry};
  GURL committed{"https://example.test/"};
  std::optional<uint32_t> theme;
  RenderWidgetHostView view;
  security_state::VisibleSecurityState security;
  void* zoom=nullptr;
  std::vector<WebContentsObserver*> observers;
 private: base::WeakPtrFactory<WebContents> weak{this};
};
inline WebContentsObserver::WebContentsObserver(WebContents* contents) { Observe(contents); }
inline WebContentsObserver::~WebContentsObserver() { Observe(nullptr); }
inline void WebContentsObserver::Observe(WebContents* contents) { if(contents_) std::erase(contents_->observers,this);contents_=contents; if(contents_) contents_->observers.push_back(this); }
enum PageZoom { PAGE_ZOOM_OUT=-1,PAGE_ZOOM_RESET=0,PAGE_ZOOM_IN=1 };
}
class TabStripModel {
 public: std::vector<content::WebContents*> tabs;
  int GetIndexOfWebContents(content::WebContents* contents) { auto it=std::find(tabs.begin(),tabs.end(),contents); return it==tabs.end()?-1:it-tabs.begin(); }
};
class BrowserWindowInterface {
 public:
  explicit BrowserWindowInterface(Profile* p):profile(p) {}
  base::WeakPtr<BrowserWindowInterface> GetWeakPtr() { return weak.GetWeakPtr(); }
  Profile* GetProfile() { return profile; }
  ui::BaseWindow* GetWindow() { return window; }
  TabStripModel* GetTabStripModel() { return &strip; }
  Profile* profile;
  ui::BaseWindow own_window;
  ui::BaseWindow* window=&own_window;
  TabStripModel strip;
 private: base::WeakPtrFactory<BrowserWindowInterface> weak{this};
};
namespace security_state { inline std::unique_ptr<VisibleSecurityState> GetVisibleSecurityState(content::WebContents* contents) { return std::make_unique<VisibleSecurityState>(contents->security); } }
namespace blink { inline double ZoomLevelToZoomFactor(double level) { return std::pow(1.2,level); } inline double ZoomFactorToZoomLevel(double factor) { return std::log(factor)/std::log(1.2); } }
namespace zoom {
class ZoomObserver;
class ZoomController {
 public:
  struct ZoomChangedEventData {};
  static ZoomController* FromWebContents(content::WebContents* contents) { return static_cast<ZoomController*>(contents->zoom); }
  void AddObserver(ZoomObserver* observer) { observers.push_back(observer); }
  void RemoveObserver(ZoomObserver* observer) { std::erase(observers,observer); }
  double GetZoomLevel() const { return level; }
  bool IsAtDefaultZoom() const { return level==fallback; }
  bool PageScaleFactorIsOne() const { return scale_one; }
  double level=0,fallback=0;bool scale_one=true;
  std::vector<ZoomObserver*> observers;
  ~ZoomController();
};
class ZoomObserver : public base::CheckedObserver {
 public: virtual void OnZoomControllerDestroyed(ZoomController*)=0; virtual void OnZoomChanged(const ZoomController::ZoomChangedEventData&) {}
};
inline ZoomController::~ZoomController() { auto copy=observers; for(auto* o:copy) o->OnZoomControllerDestroyed(this); assert(observers.empty()); }
struct PageZoom { static void Zoom(content::WebContents* contents,content::PageZoom step) { auto* c=ZoomController::FromWebContents(contents); c->level=step==content::PAGE_ZOOM_RESET?c->fallback:c->level+static_cast<int>(step); auto copy=c->observers;for(auto* o:copy) if(std::find(c->observers.begin(),c->observers.end(),o)!=c->observers.end()) o->OnZoomChanged({}); } };
}
inline unsigned SkColorGetR(uint32_t c) { return (c>>16)&255; }
inline unsigned SkColorGetG(uint32_t c) { return (c>>8)&255; }
inline unsigned SkColorGetB(uint32_t c) { return c&255; }

#endif
