#include "chrome/browser/ardia/ac_engine.h"

AC_ENGINE_CALL(ac_cli_control) {
  ardia::Call call(profile_dir, args_json, reply, context);
  if (!call) {
    return;
  }
  call.TakeReply().Error("CLI token authentication is unavailable on Linux");
}
