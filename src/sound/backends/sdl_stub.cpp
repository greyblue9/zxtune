/**
 *
 * @file
 *
 * @brief  SDL backend stub
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "sound/backends/sdl.h"
#include "sound/backends/storage.h"
// library includes
#include <sound/backend_attrs.h>

namespace Sound
{
  void RegisterSdlBackend(BackendsStorage& storage)
  {
    storage.Register(Sdl::BACKEND_ID, Sdl::BACKEND_DESCRIPTION, CAP_TYPE_SYSTEM);
  }
}  // namespace Sound
