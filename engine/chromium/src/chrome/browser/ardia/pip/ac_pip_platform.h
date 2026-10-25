// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_PIP_AC_PIP_PLATFORM_H_
#define CHROME_BROWSER_ARDIA_PIP_AC_PIP_PLATFORM_H_

#include <optional>

#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

std::optional<gfx::Rect> LoadArdiaPictureInPictureBounds();
void SaveArdiaPictureInPictureBounds(const gfx::Rect& bounds);

void FadeOutArdiaPictureInPicture(gfx::NativeWindow window,
                                      base::TimeDelta duration);
void ResetArdiaPictureInPictureFade(gfx::NativeWindow window);

#endif
