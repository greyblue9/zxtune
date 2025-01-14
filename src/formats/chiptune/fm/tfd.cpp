/**
 *
 * @file
 *
 * @brief  TurboFM Dump support implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "formats/chiptune/fm/tfd.h"
#include "formats/chiptune/container.h"
// common includes
#include <make_ptr.h>
// library includes
#include <binary/format_factories.h>
#include <binary/input_stream.h>
#include <strings/encoding.h>
#include <strings/trim.h>
// std includes
#include <array>

namespace Formats::Chiptune
{
  namespace TFD
  {
    const Char DESCRIPTION[] = "TurboFM Dump";

    enum
    {
      BEGIN_FRAME = 0xff,
      SKIP_FRAMES = 0xfe,
      SELECT_SECOND_CHIP = 0xfd,
      SELECT_FIRST_CHIP = 0xfc,
      FINISH = 0xfb,
      LOOP_MARKER = 0xfa
    };

    const std::size_t MIN_SIZE = 4 + 3 + 1;  // header + 3 empty strings + finish marker
    const std::size_t MAX_STRING_SIZE = 64;
    const std::size_t MAX_COMMENT_SIZE = 384;

    const std::size_t MIN_FRAMES = 150;  //~3sec

    typedef std::array<uint8_t, 4> SignatureType;

    const SignatureType SIGNATURE = {{'T', 'F', 'M', 'D'}};

    class StubBuilder : public Builder
    {
    public:
      void SetTitle(const String& /*title*/) override {}
      void SetAuthor(const String& /*author*/) override {}
      void SetComment(const String& /*comment*/) override {}

      void BeginFrames(uint_t /*count*/) override {}
      void SelectChip(uint_t /*idx*/) override {}
      void SetLoop() override {}
      void SetRegister(uint_t /*idx*/, uint_t /*val*/) override {}
    };

    bool FastCheck(Binary::View rawData)
    {
      if (rawData.Size() < MIN_SIZE)
      {
        return false;
      }
      return 0 == std::memcmp(rawData.Start(), SIGNATURE.data(), SIGNATURE.size());
    }

    const auto FORMAT = "'T'F'M'D"_sv;

    class Decoder : public Formats::Chiptune::Decoder
    {
    public:
      Decoder()
        : Format(Binary::CreateFormat(FORMAT, MIN_SIZE))
      {}

      String GetDescription() const override
      {
        return DESCRIPTION;
      }

      Binary::Format::Ptr GetFormat() const override
      {
        return Format;
      }

      bool Check(Binary::View rawData) const override
      {
        return FastCheck(rawData);
      }

      Formats::Chiptune::Container::Ptr Decode(const Binary::Container& rawData) const override
      {
        Builder& stub = GetStubBuilder();
        return Parse(rawData, stub);
      }

    private:
      const Binary::Format::Ptr Format;
    };

    String DecodeString(StringView str)
    {
      return Strings::ToAutoUtf8(Strings::TrimSpaces(str));
    }

    Formats::Chiptune::Container::Ptr Parse(const Binary::Container& data, Builder& target)
    {
      if (!FastCheck(data))
      {
        return Formats::Chiptune::Container::Ptr();
      }
      try
      {
        Binary::InputStream stream(data);
        stream.Read<SignatureType>();
        target.SetTitle(DecodeString(stream.ReadCString(MAX_STRING_SIZE)));
        target.SetAuthor(DecodeString(stream.ReadCString(MAX_STRING_SIZE)));
        target.SetComment(DecodeString(stream.ReadCString(MAX_COMMENT_SIZE)));

        const std::size_t fixedOffset = stream.GetPosition();
        std::size_t totalFrames = 0;
        for (;;)
        {
          const auto val = stream.ReadByte();
          if (val == FINISH)
          {
            break;
          }
          switch (val)
          {
          case BEGIN_FRAME:
            ++totalFrames;
            target.BeginFrames(1);
            break;
          case SKIP_FRAMES:
          {
            const uint_t frames = 3 + stream.ReadByte();
            totalFrames += frames;
            target.BeginFrames(frames);
          }
          break;
          case SELECT_SECOND_CHIP:
            target.SelectChip(1);
            break;
          case SELECT_FIRST_CHIP:
            target.SelectChip(0);
            break;
          case LOOP_MARKER:
            target.SetLoop();
            break;
          default:
            target.SetRegister(val, stream.ReadByte());
            break;
          }
        }
        Require(totalFrames >= MIN_FRAMES);
        const std::size_t usedSize = stream.GetPosition();
        return CreateCalculatingCrcContainer(stream.GetReadContainer(), fixedOffset, usedSize - fixedOffset);
      }
      catch (const std::exception&)
      {
        return Formats::Chiptune::Container::Ptr();
      }
    }

    Builder& GetStubBuilder()
    {
      static StubBuilder stub;
      return stub;
    }
  }  // namespace TFD

  Decoder::Ptr CreateTFDDecoder()
  {
    return MakePtr<TFD::Decoder>();
  }
}  // namespace Formats::Chiptune
