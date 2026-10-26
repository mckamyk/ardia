#include "chrome/browser/ardia/pip/ac_pip_platform.h"

#include "base/check.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/scoped_layer_animation_settings.h"

namespace {

constexpr char kBoundsKey[] = "ardia-picture-in-picture";

PrefService* WindowPrefs() {
  content::WebContents* contents =
      PictureInPictureWindowManager::GetInstance()->GetWebContents();
  if (!contents) {
    return nullptr;
  }
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  PrefService* service = profile->GetPrefs();
  return service->FindPreference(prefs::kAppWindowPlacement) ? service : nullptr;
}

}

std::optional<gfx::Rect> LoadArdiaPictureInPictureBounds() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  PrefService* service = WindowPrefs();
  if (!service) {
    return std::nullopt;
  }
  const base::ListValue* list =
      service->GetDict(prefs::kAppWindowPlacement).FindList(kBoundsKey);
  if (!list || list->size() != 4) {
    return std::nullopt;
  }
  for (const base::Value& value : *list) {
    if (!value.is_int()) {
      return std::nullopt;
    }
  }
  gfx::Rect bounds((*list)[0].GetInt(), (*list)[1].GetInt(), (*list)[2].GetInt(),
                   (*list)[3].GetInt());
  return bounds.IsEmpty() ? std::nullopt : std::make_optional(bounds);
}

void SaveArdiaPictureInPictureBounds(const gfx::Rect& bounds) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  PrefService* service = WindowPrefs();
  if (!service || bounds.IsEmpty()) {
    return;
  }
  ScopedDictPrefUpdate placement(service, prefs::kAppWindowPlacement);
  placement->Set(kBoundsKey, base::ListValue()
                                 .Append(bounds.x())
                                 .Append(bounds.y())
                                 .Append(bounds.width())
                                 .Append(bounds.height()));
}

void FadeOutArdiaPictureInPicture(gfx::NativeWindow window,
                                 base::TimeDelta duration) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (!window || !window->layer()) {
    return;
  }
  ui::Layer* layer = window->layer();
  ui::ScopedLayerAnimationSettings animation(layer->GetAnimator());
  animation.SetTransitionDuration(duration);
  animation.SetTweenType(gfx::Tween::EASE_OUT);
  animation.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  layer->SetOpacity(0.0f);
}

void ResetArdiaPictureInPictureFade(gfx::NativeWindow window) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (!window || !window->layer()) {
    return;
  }
  window->layer()->GetAnimator()->StopAnimatingProperty(
      ui::LayerAnimationElement::OPACITY);
  window->layer()->SetOpacity(1.0f);
}
