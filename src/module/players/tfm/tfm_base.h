/**
 *
 * @file
 *
 * @brief  TFM-based chiptunes support
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// local includes
#include "module/players/tfm/tfm_chiptune.h"
// library includes
#include <module/renderer.h>

namespace Module
{
  namespace TFM
  {
    Renderer::Ptr CreateRenderer(Time::Microseconds frameDuration, DataIterator::Ptr iterator,
                                 Devices::TFM::Chip::Ptr device);
  }
}  // namespace Module
