/**
 *
 * @file
 *
 * @brief  Plugins parameters names
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// library includes
#include <core/core_parameters.h>

namespace Parameters
{
  namespace ZXTune
  {
    namespace Core
    {
      //! @brief Plugins-related parameters namespace
      namespace Plugins
      {
        //! @brief Parameters#ZXTune#Core#Plugins namespace prefix
        const auto PREFIX = Core::PREFIX + "plugins"_id;

        //@{
        //! @name Default song duration in seconds (if not specified exactly) for all types (can be overriden)

        //! Default value (3min)
        const IntType DEFAULT_DURATION_DEFAULT = 3 * 60;
        //! Parameter name for type
        const auto DEFAULT_DURATION = PREFIX + "default_duration"_id;
        //@}

        //! @brief RAW scaner parameters namespace
        namespace Raw
        {
          //! @brief Parameters#ZXTune#Core#Plugins#Raw namespace prefix
          const auto PREFIX = Plugins::PREFIX + "raw"_id;

          //@{
          //! @name Perform double analysis of plain data containers

          //! Parameter name
          const auto PLAIN_DOUBLE_ANALYSIS = PREFIX + "plain_double_analysis"_id;
          //@}

          //@{
          //! @name Minimal scan size parameter

          //! Default value
          const IntType MIN_SIZE_DEFAULT = 128;
          //! Parameter name
          const auto MIN_SIZE = PREFIX + "min_size"_id;
          //@}
        }  // namespace Raw

        //! @brief HRIP container parameters namespace
        namespace Hrip
        {
          //! @brief Parameters#ZXTune#Core#Plugins#Hrip namespace prefix
          const auto PREFIX = Plugins::PREFIX + "hrip"_id;

          //! @brief Ignore corrupted blocks
          //! @details 1 if do so
          const auto IGNORE_CORRUPTED = PREFIX + "ignore_corrupted"_id;
        }  // namespace Hrip

        //! @brief SID container/player parameters namespace
        namespace SID
        {
          //! @brief Parameters#ZXTune#Core#Plugins#SID namespace prefix
          const auto PREFIX = Plugins::PREFIX + "sid"_id;

          //@{
          //! @name ROMs content

          //! 8192 bytes
          const auto KERNAL = PREFIX + "kernal"_id;
          //! 8192 bytes
          const auto BASIC = PREFIX + "basic"_id;
          //! 4096 bytes
          const auto CHARGEN = PREFIX + "chargen"_id;
          //@}
        }  // namespace SID

        //! @brief ZIP container parameters namespace
        namespace Zip
        {
          //! @brief Parameters#ZXTune#Core#Plugins#Zip namespace prefix
          const auto PREFIX = Plugins::PREFIX + "zip"_id;

          //@{
          //! @name Maximal file size to be depacked in Mb

          //! Default value
          const IntType MAX_DEPACKED_FILE_SIZE_MB_DEFAULT = 32;
          //! Parameter name
          const auto MAX_DEPACKED_FILE_SIZE_MB = PREFIX + "max_depacked_size_mb"_id;
          //@}
        }  // namespace Zip
      }    // namespace Plugins
    }      // namespace Core
  }        // namespace ZXTune
}  // namespace Parameters
