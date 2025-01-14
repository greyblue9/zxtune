/**
 *
 * @file
 *
 * @brief  Data location internal factories
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// library includes
#include <core/data_location.h>

namespace ZXTune
{
  //! @param coreParams Parameters for plugins processing
  //! @param data Source data to be processed
  //! @param subpath Subpath in source data to be resolved
  //! @return Object if path is valid. No object elsewhere
  DataLocation::Ptr CreateLocation(Binary::Container::Ptr data);

  DataLocation::Ptr CreateNestedLocation(DataLocation::Ptr parent, Binary::Container::Ptr subData,
                                         const String& subPlugin, const String& subPath);
  DataLocation::Ptr CreateLocation(Binary::Container::Ptr data, const String& plugin, const String& path);
}  // namespace ZXTune
