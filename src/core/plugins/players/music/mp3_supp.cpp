/**
 *
 * @file
 *
 * @brief  MP3 support plugin
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "core/plugins/player_plugins_registrator.h"
#include "core/plugins/players/plugin.h"
// common includes
#include <contract.h>
#include <error_tools.h>
#include <make_ptr.h>
// library includes
#include <core/plugin_attrs.h>
#include <debug/log.h>
#include <formats/chiptune/decoders.h>
#include <formats/chiptune/music/mp3.h>
#include <module/players/properties_helper.h>
#include <module/players/properties_meta.h>
#include <module/players/streaming.h>
#include <sound/resampler.h>
// 3rdparty
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NONSTANDARD_BUT_LOGICAL
#include <3rdparty/minimp3/minimp3.h>

#define FILE_TAG 04123EA8

namespace Module::Mp3
{
  const Debug::Stream Dbg("Core::Mp3Supp");

  const auto SEEK_PRECISION = Time::Milliseconds(2000);

  struct SeekPoint
  {
    Time::AtMillisecond Start;
    uint_t Offset;
    uint_t Frames = 1;

    SeekPoint(Time::AtMillisecond start, uint_t offset)
      : Start(start)
      , Offset(offset)
    {}

    bool operator<(Time::AtMillisecond pos) const
    {
      return Start < pos;
    }
  };

  struct Model
  {
    using RWPtr = std::shared_ptr<Model>;
    using Ptr = std::shared_ptr<const Model>;

    std::vector<SeekPoint> Lookup;
    Time::Microseconds Duration;
    Binary::Data::Ptr Content;
  };

  struct FrameSound
  {
    uint_t Frequency = 0;
    Sound::Chunk Data;

    FrameSound() = default;
    FrameSound(const FrameSound&) = delete;
    FrameSound& operator=(const FrameSound&) = delete;
    FrameSound(FrameSound&& rh) noexcept  // = default
      : Frequency(rh.Frequency)
      , Data(std::move(rh.Data))
    {}
    FrameSound& operator=(FrameSound&& rh) noexcept
    {
      Frequency = rh.Frequency;
      Data = std::move(rh.Data);
      return *this;
    }

    Sound::Sample::Type* GetTarget()
    {
      Data.resize(MINIMP3_MAX_SAMPLES_PER_FRAME);
      return safe_ptr_cast<Sound::Sample::Type*>(Data.data());
    }

    void Finalize(uint_t resultSamples, const mp3dec_frame_info_t& info)
    {
      if (1 == info.channels)
      {
        const auto pcm = GetTarget();
        for (std::size_t idx = resultSamples; idx != 0; --idx)
        {
          const auto mono = pcm[idx - 1];
          Data[idx - 1] = Sound::Sample(mono, mono);
        }
      }
      Data.resize(resultSamples);
      Frequency = info.hz;
    }
  };

  class Mp3Tune
  {
  public:
    explicit Mp3Tune(Model::Ptr data)
      : Data(std::move(data))
    {
      Reset();
      static_assert(Sound::Sample::CHANNELS == 2, "Incompatible sound channels count");
      static_assert(Sound::Sample::BITS == 16, "Incompatible sound sample bits count");
      static_assert(Sound::Sample::MID == 0, "Incompatible sound sample type");
    }

    void Reset()
    {
      ::mp3dec_init(&Decoder);
      Offset = Data->Lookup.front().Offset;
    }

    bool IsEnd() const
    {
      return Offset == Data->Content->Size();
    }

    FrameSound RenderNextFrame()
    {
      const auto total = Data->Content->Size();
      FrameSound result;
      mp3dec_frame_info_t info;
      while (Offset < total)
      {
        const auto resultSamples = ::mp3dec_decode_frame(&Decoder,
                                                         static_cast<const uint8_t*>(Data->Content->Start()) + Offset,
                                                         int(total - Offset), result.GetTarget(), &info);
        Offset += info.frame_bytes;
        result.Finalize(resultSamples, info);
        if (resultSamples || !info.frame_bytes)
        {
          break;
        }
      }
      return result;
    }

    // Returns real position
    Time::AtMicrosecond Seek(Time::AtMillisecond request)
    {
      Reset();
      auto lookup = std::lower_bound(Data->Lookup.begin(), Data->Lookup.end(), request);
      for (uint_t preFrames = 0; lookup != Data->Lookup.begin() && preFrames < 10;)
      {
        --lookup;
        preFrames += lookup->Frames;
      }
      const auto* raw = static_cast<const uint8_t*>(Data->Content->Start());
      auto offset = lookup->Offset;
      auto size = Data->Content->Size() - offset;
      const Time::AtMicrosecond target = request;
      Time::AtMicrosecond pos = lookup->Start;
      while (pos < target)
      {
        mp3dec_frame_info_t info;
        if (const auto samples = ::mp3dec_decode_frame(&Decoder, raw + offset, int(size), nullptr, &info))
        {
          offset += info.frame_bytes;
          size -= info.frame_bytes;
          pos += Time::Microseconds::FromRatio(samples, info.hz);
        }
        else if (!info.frame_bytes)
        {
          Dbg("Failed to decode frame for seek @0x{:08x}", offset);
          break;
        }
      }
      Offset = offset;
      return pos;
    }

  private:
    const Model::Ptr Data;
    mp3dec_t Decoder;
    std::size_t Offset = 0;
  };

  class MultiFreqResampler
  {
  public:
    explicit MultiFreqResampler(uint_t samplerate)
      : TargetFreq(samplerate)
    {}

    Sound::Chunk Apply(FrameSound frame)
    {
      if (frame.Data.empty())
      {
        return {};
      }
      else if (frame.Frequency == TargetFreq)
      {
        return std::move(frame.Data);
      }
      else
      {
        return GetTarget(frame.Frequency).Apply(std::move(frame.Data));
      }
    }

  private:
    Sound::Converter& GetTarget(uint_t freq)
    {
      for (const auto& resampled : Resamplers)
      {
        if (freq == resampled.first)
        {
          return *resampled.second;
        }
      }
      const auto res = Sound::CreateResampler(freq, TargetFreq);
      Resamplers.emplace_back(freq, res);
      return *res;
    }

  private:
    uint_t TargetFreq;
    std::vector<std::pair<uint_t, Sound::Converter::Ptr> > Resamplers;
  };

  class Renderer : public Module::Renderer
  {
  public:
    Renderer(Model::Ptr data, uint_t samplerate)
      : Tune(data)
      , State(MakePtr<TimedState>(data->Duration))
      , Target(samplerate)
    {}

    Module::State::Ptr GetState() const override
    {
      return State;
    }

    Sound::Chunk Render(const Sound::LoopParameters& looped) override
    {
      if (!State->IsValid())
      {
        return {};
      }
      auto frame = Tune.RenderNextFrame();
      if (frame.Data.empty())
      {
        State->Consume({}, looped);
        if (State->IsValid())
        {
          Tune.Reset();
          frame = Tune.RenderNextFrame();
        }
      }
      const auto rendered = Time::Microseconds::FromRatio(frame.Data.size(), frame.Frequency);
      const auto loops = State->LoopCount();
      State->Consume(rendered, looped);
      if (loops != State->LoopCount())
      {
        Tune.Reset();
      }
      return Target.Apply(std::move(frame));
    }

    void Reset() override
    {
      Tune.Reset();
      State->Reset();
    }

    void SetPosition(Time::AtMillisecond request) override
    {
      State->Seek(request);
      const auto realPos = Tune.Seek(State->At());
      State->Seek(realPos);
    }

  private:
    Mp3Tune Tune;
    const TimedState::Ptr State;
    MultiFreqResampler Target;
  };

  class Holder : public Module::Holder
  {
  public:
    Holder(Model::Ptr data, Parameters::Accessor::Ptr props)
      : Data(std::move(data))
      , Properties(std::move(props))
    {}

    Module::Information::Ptr GetModuleInformation() const override
    {
      return CreateTimedInfo(Data->Duration.CastTo<Time::Millisecond>());
    }

    Parameters::Accessor::Ptr GetModuleProperties() const override
    {
      return Properties;
    }

    Renderer::Ptr CreateRenderer(uint_t samplerate, Parameters::Accessor::Ptr /*params*/) const override
    {
      return MakePtr<Renderer>(Data, samplerate);
    }

  private:
    const Model::Ptr Data;
    const Parameters::Accessor::Ptr Properties;
  };

  static const auto MIN_DURATION = Time::Seconds(1);

  class DataBuilder : public Formats::Chiptune::Mp3::Builder
  {
  public:
    explicit DataBuilder(PropertiesHelper& props)
      : Data(MakeRWPtr<Model>())
      , Properties(props)
      , Meta(props)
    {}

    Formats::Chiptune::MetaBuilder& GetMetaBuilder() override
    {
      return Meta;
    }

    void AddFrame(const Formats::Chiptune::Mp3::Frame& frame) override
    {
      const auto pos = Time::AtMillisecond() + Data->Duration.CastTo<Time::Millisecond>();
      if (Data->Lookup.empty() || Data->Lookup.back().Start + SEEK_PRECISION < pos)
      {
        Data->Lookup.emplace_back(pos, frame.Location.Offset);
      }
      else
      {
        Data->Lookup.back().Frames++;
      }
      Data->Duration += Time::Microseconds::FromRatio(frame.Properties.SamplesCount, frame.Properties.Samplerate);
    }

    void SetContent(Binary::Data::Ptr data)
    {
      Data->Content = std::move(data);
    }

    Model::Ptr GetResult()
    {
      if (Data->Duration < MIN_DURATION)
      {
        return {};
      }
      else
      {
        Dbg("Built {} seek points", Data->Lookup.size());
        Data->Lookup.shrink_to_fit();
        return Data;
      }
    }

  private:
    const Model::RWPtr Data;
    PropertiesHelper& Properties;
    MetaProperties Meta;
  };

  class Factory : public Module::Factory
  {
  public:
    Module::Holder::Ptr CreateModule(const Parameters::Accessor& /*params*/, const Binary::Container& rawData,
                                     Parameters::Container::Ptr properties) const override
    {
      try
      {
        PropertiesHelper props(*properties);
        DataBuilder dataBuilder(props);
        if (const auto container = Formats::Chiptune::Mp3::Parse(rawData, dataBuilder))
        {
          if (auto data = dataBuilder.GetResult())
          {
            props.SetSource(*container);
            dataBuilder.SetContent(container);
            return MakePtr<Holder>(std::move(data), std::move(properties));
          }
        }
      }
      catch (const std::exception& e)
      {
        Dbg("Failed to create MP3: {}", e.what());
      }
      return {};
    }
  };
}  // namespace Module::Mp3

namespace ZXTune
{
  void RegisterMP3Plugin(PlayerPluginsRegistrator& registrator)
  {
    const Char ID[] = {'M', 'P', '3', 0};
    const uint_t CAPS = Capabilities::Module::Type::STREAM | Capabilities::Module::Device::DAC;

    const auto decoder = Formats::Chiptune::CreateMP3Decoder();
    const auto factory = MakePtr<Module::Mp3::Factory>();
    const PlayerPlugin::Ptr plugin = CreatePlayerPlugin(ID, CAPS, decoder, factory);
    registrator.RegisterPlugin(plugin);
  }
}  // namespace ZXTune

#undef FILE_TAG
