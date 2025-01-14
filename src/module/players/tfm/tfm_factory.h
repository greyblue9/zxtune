/**
 *
 * @file
 *
 * @brief  TFM-based modules factory
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// local includes
#include "module/players/tfm/tfm_chiptune.h"
// library includes
#include <binary/container.h>
#include <parameters/container.h>

namespace Module
{
  namespace TFM
  {
    class Factory
    {
    public:
      typedef std::shared_ptr<const Factory> Ptr;
      virtual ~Factory() = default;

      virtual Chiptune::Ptr CreateChiptune(const Binary::Container& data,
                                           Parameters::Container::Ptr properties) const = 0;
    };
  }  // namespace TFM
}  // namespace Module
