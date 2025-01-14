/**
 *
 * @file
 *
 * @brief  Song length database interface
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// library includes
#include <time/duration.h>

namespace Module::Sid
{
  typedef Time::Milliseconds TimeType;

  TimeType GetSongLength(const char* md5digest, uint_t idx);
}  // namespace Module::Sid
