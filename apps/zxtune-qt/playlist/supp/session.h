/**
 *
 * @file
 *
 * @brief Sessions support interface
 *
 * @author vitamin.caig@gmail.com
 *
 **/

#pragma once

// local includes
#include "container.h"

namespace Playlist
{
  class Session
  {
  public:
    typedef std::shared_ptr<Session> Ptr;

    virtual ~Session() = default;

    virtual bool Empty() const = 0;
    virtual void Load(Container::Ptr container) = 0;
    virtual void Save(Controller::Iterator::Ptr it) = 0;

    // creator
    static Ptr Create();
  };
}  // namespace Playlist
