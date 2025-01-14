/**
 *
 * @file
 *
 * @brief  ProSoundMaker chiptune factory
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// local includes
#include "module/players/aym/aym_factory.h"

namespace Module
{
  namespace ProSoundMaker
  {
    AYM::Factory::Ptr CreateFactory();
  }
}  // namespace Module
