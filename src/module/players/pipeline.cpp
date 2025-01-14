/**
 *
 * @file
 *
 * @brief  Sound pipeline support implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// common includes
#include <make_ptr.h>
// library includes
#include <debug/log.h>
#include <module/holder.h>
#include <module/players/pipeline.h>
#include <parameters/merged_accessor.h>
#include <parameters/tracking_helper.h>
#include <sound/gainer.h>
#include <sound/render_params.h>
#include <sound/sound_parameters.h>
// std includes
#include <algorithm>

namespace Module
{
  const Debug::Stream Debug("Core::Pipeline");

  using TimeUnit = Time::Millisecond;

  Time::Duration<TimeUnit> GetDurationValue(const Parameters::Accessor& params, Parameters::Identifier name,
                                            Parameters::IntType def, Parameters::IntType precision)
  {
    auto value = def;
    params.FindValue(name, value);
    return Time::Duration<TimeUnit>::FromRatio(value, precision);
  }

  struct FadeInfo
  {
    const Time::Duration<TimeUnit> FadeIn;
    const Time::Duration<TimeUnit> FadeOut;
    const Time::Duration<TimeUnit> Duration;

    FadeInfo(Time::Duration<TimeUnit> fadeIn, Time::Duration<TimeUnit> fadeOut, Time::Duration<TimeUnit> duration)
      : FadeIn(fadeIn)
      , FadeOut(fadeOut)
      , Duration(duration)
    {
      Debug("Fading: {}+{}/{} ms", fadeIn.Get(), fadeOut.Get(), duration.Get());
    }

    bool IsValid() const
    {
      return FadeIn + FadeOut < Duration;
    }

    bool IsFadein(Time::Instant<TimeUnit> pos) const
    {
      return pos.Get() < FadeIn.Get();
    }

    bool IsFadeout(Time::Instant<TimeUnit> pos) const
    {
      return FadeOut && Duration.Get() < FadeOut.Get() + pos.Get();
    }

    Sound::Gain::Type GetFadein(Sound::Gain::Type vol, Time::Instant<TimeUnit> pos) const
    {
      return vol * pos.Get() / FadeIn.Get();
    }

    Sound::Gain::Type GetFadeout(Sound::Gain::Type vol, Time::Instant<TimeUnit> pos) const
    {
      return vol * (Duration.Get() - pos.Get()) / FadeOut.Get();
    }

    static FadeInfo Create(Time::Milliseconds duration, const Parameters::Accessor& params)
    {
      using namespace Parameters::ZXTune::Sound;
      const auto fadeIn = GetDurationValue(params, FADEIN, FADEIN_DEFAULT, FADEIN_PRECISION);
      const auto fadeOut = GetDurationValue(params, FADEOUT, FADEOUT_DEFAULT, FADEOUT_PRECISION);
      return FadeInfo(fadeIn, fadeOut, duration);
    }
  };

  // Use inlined realization due to requied Reset() method
  class SilenceDetector
  {
  public:
    bool Detected(const Sound::Chunk& in)
    {
      if (in.empty() || Limit == 0)
      {
        return false;
      }
      const auto silent = LastSample;
      if (Counter != 0 && std::all_of(in.begin(), in.end(), [silent](Sound::Sample in) { return in == silent; }))
      {
        Counter += in.size();
      }
      else
      {
        LastSample = in.back();
        // approximate counting
        Counter = std::count(in.begin(), in.end(), LastSample);
      }
      return Counter >= Limit;
    }

    void Reset()
    {
      Counter = 0;
    }

    static SilenceDetector Create(uint_t samplerate, const Parameters::Accessor& params)
    {
      using namespace Parameters::ZXTune::Sound;
      const auto duration = GetDurationValue(params, SILENCE_LIMIT, SILENCE_LIMIT_DEFAULT, SILENCE_LIMIT_PRECISION);
      const auto limit = std::size_t(samplerate) * duration.Get() / duration.PER_SECOND;
      Debug("Silence detection: {} ms ({} samples)", duration.Get(), limit);
      return SilenceDetector(limit);
    }

  private:
    explicit SilenceDetector(std::size_t limit)
      : Limit(limit)
    {}

  private:
    const std::size_t Limit;
    std::size_t Counter = 0;
    Sound::Sample LastSample;
  };

  class PipelinedRenderer : public Renderer
  {
  public:
    PipelinedRenderer(const Holder& holder, uint_t samplerate, Parameters::Accessor::Ptr params)
      : Delegate(holder.CreateRenderer(samplerate, params))
      , State(Delegate->GetState())
      , Params(std::move(params))
      , Fading(FadeInfo::Create(holder.GetModuleInformation()->Duration(), *Params))
      , Gainer(Sound::CreateGainer())
      , Silence(SilenceDetector::Create(samplerate, *Params))
    {}

    Module::State::Ptr GetState() const override
    {
      return State;
    }

    Sound::Chunk Render(const Sound::LoopParameters& loop) override
    {
      auto data = Delegate->Render(loop);
      if (Silence.Detected(data))
      {
        return {};
      }
      // Apply fading at post-rendering position to avoid absolute silence at the beginning
      Gainer->SetGain(CalculateGain(loop));
      return Gainer->Apply(std::move(data));
    }

    void Reset() override
    {
      Delegate->Reset();
      Params.Reset();
      Silence.Reset();
    }

    void SetPosition(Time::AtMillisecond position) override
    {
      Silence.Reset();
      Delegate->SetPosition(position);
    }

  private:
    Sound::Gain::Type CalculateGain(const Sound::LoopParameters& loop)
    {
      if (Params.IsChanged())
      {
        using namespace Parameters::ZXTune::Sound;
        auto val = GAIN_DEFAULT;
        Params->FindValue(GAIN, val);
        Preamp = Sound::Gain::Type(val, GAIN_PRECISION);
        Debug("Preamp: {}%", val);
      }
      if (!Fading.IsValid())
      {
        return Preamp;
      }
      const auto pos = State->At();
      if (Fading.IsFadein(pos) && !State->LoopCount())
      {
        return Fading.GetFadein(Preamp, pos);
      }
      else if (!loop.Enabled && Fading.IsFadeout(pos))
      {
        return Fading.GetFadeout(Preamp, pos);
      }
      else
      {
        return Preamp;
      }
    }

  private:
    const Renderer::Ptr Delegate;
    const Module::State::Ptr State;
    Parameters::TrackingHelper<Parameters::Accessor> Params;
    const FadeInfo Fading;
    const Sound::Gainer::Ptr Gainer;
    SilenceDetector Silence;
    Sound::Gain::Type Preamp;
  };

  Renderer::Ptr CreatePipelinedRenderer(const Holder& holder, Parameters::Accessor::Ptr globalParams)
  {
    const auto samplerate = Sound::GetSoundFrequency(*globalParams);
    return CreatePipelinedRenderer(holder, samplerate, std::move(globalParams));
  }

  Renderer::Ptr CreatePipelinedRenderer(const Holder& holder, uint_t samplerate, Parameters::Accessor::Ptr globalParams)
  {
    auto props = Parameters::CreateMergedAccessor(holder.GetModuleProperties(), std::move(globalParams));
    return MakePtr<PipelinedRenderer>(holder, samplerate, std::move(props));
  }
}  // namespace Module
