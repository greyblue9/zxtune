/**
 *
 * @file
 *
 * @brief  Location open and resolving implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "core/src/location.h"
// common includes
#include <make_ptr.h>
// library includes
#include <module/attributes.h>

namespace ZXTune
{
  Analysis::Path::Ptr CreateEmptyPath()
  {
    static const auto instance = Analysis::ParsePath({}, Module::SUBPATH_DELIMITER);
    return instance;
  }

  Analysis::Path::Ptr CreateEmptyPluginsChain()
  {
    static const auto instance = Analysis::ParsePath({}, Module::CONTAINERS_DELIMITER);
    return instance;
  }

  class UnresolvedLocation : public DataLocation
  {
  public:
    explicit UnresolvedLocation(Binary::Container::Ptr data)
      : Data(std::move(data))
    {}

    Binary::Container::Ptr GetData() const override
    {
      return Data;
    }

    Analysis::Path::Ptr GetPath() const override
    {
      return CreateEmptyPath();
    }

    Analysis::Path::Ptr GetPluginsChain() const override
    {
      return CreateEmptyPluginsChain();
    }

  private:
    const Binary::Container::Ptr Data;
  };

  class GeneratedLocation : public DataLocation
  {
  public:
    GeneratedLocation(Binary::Container::Ptr data, const String& plugin, const String& path)
      : Data(std::move(data))
      , Path(Analysis::ParsePath(path, Module::SUBPATH_DELIMITER))
      , Plugins(Analysis::ParsePath(plugin, Module::CONTAINERS_DELIMITER))
    {}

    Binary::Container::Ptr GetData() const override
    {
      return Data;
    }

    Analysis::Path::Ptr GetPath() const override
    {
      return Path;
    }

    Analysis::Path::Ptr GetPluginsChain() const override
    {
      return Plugins;
    }

  private:
    const Binary::Container::Ptr Data;
    const Analysis::Path::Ptr Path;
    const Analysis::Path::Ptr Plugins;
  };
}  // namespace ZXTune

namespace ZXTune
{
  DataLocation::Ptr CreateLocation(Binary::Container::Ptr data)
  {
    return MakePtr<UnresolvedLocation>(std::move(data));
  }

  DataLocation::Ptr CreateLocation(Binary::Container::Ptr data, const String& plugin, const String& path)
  {
    return MakePtr<GeneratedLocation>(std::move(data), plugin, path);
  }
}  // namespace ZXTune

#undef FILE_TAG
