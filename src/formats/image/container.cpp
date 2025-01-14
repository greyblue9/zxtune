/**
 *
 * @file
 *
 * @brief  Image container helpers implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "formats/image/container.h"
// common includes
#include <make_ptr.h>
// library includes
#include <binary/container_base.h>
#include <binary/container_factories.h>
// std includes
#include <cassert>

namespace Formats
{
  namespace Image
  {
    class ImageContainer : public Binary::BaseContainer<Container>
    {
    public:
      ImageContainer(Binary::Container::Ptr delegate, std::size_t origSize)
        : BaseContainer(std::move(delegate))
        , OrigSize(origSize)
      {}

      std::size_t OriginalSize() const override
      {
        return OrigSize;
      }

    private:
      const std::size_t OrigSize;
    };

    Container::Ptr CreateContainer(Binary::Container::Ptr data, std::size_t origSize)
    {
      return origSize && data && data->Size() ? MakePtr<ImageContainer>(std::move(data), origSize) : Container::Ptr();
    }

    Container::Ptr CreateContainer(std::unique_ptr<Binary::Dump> data, std::size_t origSize)
    {
      auto container = Binary::CreateContainer(std::move(data));
      return CreateContainer(std::move(container), origSize);
    }
  }  // namespace Image
}  // namespace Formats
