/**
 *
 * @file
 *
 * @brief  libopenmpt support plugin
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "core/plugins/player_plugins_registrator.h"
#include "core/plugins/players/plugin.h"
// common includes
#include <contract.h>
#include <make_ptr.h>
// library includes
#include <binary/format_factories.h>
#include <core/core_parameters.h>
#include <core/plugin_attrs.h>
#include <debug/log.h>
#include <formats/chiptune/container.h>
#include <module/players/properties_helper.h>
#include <module/track_information.h>
#include <module/track_state.h>
#include <parameters/tracking_helper.h>
#include <sound/loop.h>
#include <strings/trim.h>
#include <time/duration.h>
// std includes
#include <utility>
// boost includes
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
// 3rdparty includes
#define BUILDING_STATIC
#include <3rdparty/openmpt/libopenmpt/libopenmpt.hpp>

namespace Module::Mpt
{
  const Debug::Stream Dbg("Core::OpenMPT");

  using ModulePtr = std::shared_ptr<openmpt::module>;

  Time::Milliseconds ToDuration(double seconds)
  {
    return Time::Milliseconds(seconds * Time::Milliseconds::PER_SECOND);
  }

  class Information : public Module::TrackInformation
  {
  public:
    typedef std::shared_ptr<const Information> Ptr;

    Information(ModulePtr track)
      : Track(std::move(track))
    {}

    Time::Milliseconds Duration() const override
    {
      return ToDuration(Track->get_duration_seconds());
    }

    Time::Milliseconds LoopDuration() const override
    {
      return Duration();  // TODO
    }

    uint_t PositionsCount() const override
    {
      return Track->get_num_orders();
    }

    uint_t LoopPosition() const override
    {
      return 0;  // TODO
    }

    uint_t ChannelsCount() const override
    {
      return Track->get_num_channels();
    }

  private:
    const ModulePtr Track;
  };

  class TrackState : public Module::TrackState
  {
  public:
    using Ptr = std::shared_ptr<TrackState>;

    explicit TrackState(ModulePtr track)
      : Track(std::move(track))
    {}

    Time::AtMillisecond At() const override
    {
      return Time::AtMillisecond() + ToDuration(Track->get_position_seconds() - LastLoopStart);
    }

    Time::Milliseconds Total() const override
    {
      return ToDuration(Track->get_position_seconds());
    }

    uint_t LoopCount() const override
    {
      return LoopsDone;
    }

    uint_t Position() const override
    {
      return Track->get_current_order();
    }

    uint_t Pattern() const override
    {
      return Track->get_current_pattern();
    }

    uint_t Line() const override
    {
      return Track->get_current_row();
    }

    uint_t Tempo() const override
    {
      return Track->get_current_tempo();
    }

    uint_t Quirk() const override
    {
      return 0;
    }

    uint_t Channels() const override
    {
      return Track->get_current_playing_channels();
    }

    void Looped()
    {
      LastLoopStart = Track->get_position_seconds();
      ++LoopsDone;
    }

    void Reset()
    {
      LastLoopStart = {};
      LoopsDone = 0;
    }

  private:
    const ModulePtr Track;
    double LastLoopStart = 0;
    uint_t LoopsDone = 0;
  };

  class Renderer : public Module::Renderer
  {
  public:
    Renderer(ModulePtr track, uint_t samplerate, Parameters::Accessor::Ptr params)
      : Track(std::move(track))
      , State(MakePtr<TrackState>(Track))
      , Params(std::move(params))
      , SoundFreq(samplerate)
    {}

    Module::State::Ptr GetState() const override
    {
      return State;
    }

    Sound::Chunk Render(const Sound::LoopParameters& looped) override
    {
      static_assert(Sound::Sample::CHANNELS == 2, "Incompatible sound channels count");
      static_assert(Sound::Sample::BITS == 16, "Incompatible sound bits count");
      static_assert(Sound::Sample::MID == 0, "Incompatible sound sample type");
      static_assert(sizeof(Sound::Sample) == 4, "Incompatible sound sample size");

      ApplyParameters();
      const auto samples = SoundFreq / 10;  // TODO
      Sound::Chunk chunk(samples);
      while (State->LoopCount() == 0 || looped(State->LoopCount()))
      {
        const auto done = Track->read_interleaved_stereo(SoundFreq, samples, safe_ptr_cast<int16_t*>(chunk.data()));
        if (done != samples)
        {
          State->Looped();
        }
        if (done)
        {
          chunk.resize(done);
          return chunk;
        }
      }
      return {};
    }

    void Reset() override
    {
      Params.Reset();
      SetPosition({});
    }

    void SetPosition(Time::AtMillisecond request) override
    {
      Track->set_position_seconds(double(request.Get()) / request.PER_SECOND);
      State->Reset();
    }

  private:
    void ApplyParameters()
    {
      if (Params.IsChanged())
      {
        Parameters::IntType val = Parameters::ZXTune::Core::DAC::INTERPOLATION_DEFAULT;
        Params->FindValue(Parameters::ZXTune::Core::DAC::INTERPOLATION, val);
        // cubic interpolation vs windowed sinc with 8 taps
        const int interpolation = val != Parameters::ZXTune::Core::DAC::INTERPOLATION_NO ? 8 : 3;
        Track->set_render_param(openmpt::module::render_param::RENDER_INTERPOLATIONFILTER_LENGTH, interpolation);
      }
    }

  private:
    const ModulePtr Track;
    const TrackState::Ptr State;
    Parameters::TrackingHelper<Parameters::Accessor> Params;
    const uint_t SoundFreq;
  };

  class Holder : public Module::Holder
  {
  public:
    Holder(ModulePtr track, Parameters::Accessor::Ptr props)
      : Track(std::move(track))
      , Info(MakePtr<Information>(Track))
      , Properties(std::move(props))
    {}

    Module::Information::Ptr GetModuleInformation() const override
    {
      return Info;
    }

    Parameters::Accessor::Ptr GetModuleProperties() const override
    {
      return Properties;
    }

    Renderer::Ptr CreateRenderer(uint_t samplerate, Parameters::Accessor::Ptr params) const override
    {
      Require(!!Track);  // TODO
      return MakePtr<Renderer>(std::move(Track), samplerate, std::move(params));
    }

  private:
    ModulePtr Track;
    const Information::Ptr Info;
    const Parameters::Accessor::Ptr Properties;
  };

  struct PluginDescription
  {
    const char* const Id;
    const StringView Format;
    const char* const Description;
  };

  class Decoder : public Formats::Chiptune::Decoder
  {
  public:
    explicit Decoder(const PluginDescription& desc)
      : Desc(desc)
      , Fmt(Binary::CreateMatchOnlyFormat(Desc.Format))
    {}

    String GetDescription() const override
    {
      return Desc.Description;
    }

    Binary::Format::Ptr GetFormat() const override
    {
      return Fmt;
    }

    bool Check(Binary::View rawData) const override
    {
      return Fmt->Match(rawData);
    }

    Formats::Chiptune::Container::Ptr Decode(const Binary::Container& rawData) const override
    {
      if (Check(rawData))
      {
        const auto size = rawData.Size();
        auto data = rawData.GetSubcontainer(0, size);
        return Formats::Chiptune::CreateCalculatingCrcContainer(std::move(data), 0, size);
      }
      return {};
    }

  private:
    const PluginDescription& Desc;
    const Binary::Format::Ptr Fmt;
  };

  String DecodeString(String str)
  {
    const auto out = Strings::TrimSpaces(str);
    return out == str ? str : out.to_string();
  }

  class LogStub : public std::ostream
  {
  public:
    LogStub()
    {
      setstate(std::ios_base::badbit);
    }
  };

  static LogStub LOG;

  void FillMetadata(const openmpt::module& module, PropertiesHelper& props)
  {
    props.SetTitle(DecodeString(module.get_metadata("title")));
    props.SetAuthor(DecodeString(module.get_metadata("artist")));
    const auto tracker = DecodeString(module.get_metadata("tracker"));
    if (!tracker.empty())
    {
      props.SetProgram(tracker);
    }
    else
    {
      props.SetProgram(DecodeString(module.get_metadata("type_long")));
    }
    props.SetDate(DecodeString(module.get_metadata("date")));
    props.SetComment(DecodeString(module.get_metadata("message_raw")));
    {
      const auto metadata = module.get_metadata("message_heuristic");
      Strings::Array strings;
      using namespace boost::algorithm;
      split(strings, metadata, is_any_of("\r\n"), token_compress_on);
      if (!strings.empty())
      {
        props.SetStrings(strings);
      }
    }
  }

  class Factory : public Module::ExternalParsingFactory
  {
  public:
    explicit Factory(const PluginDescription& desc)
      : Desc(desc)
    {
      Controls.emplace("play.at_end", "continue");
    }

    Module::Holder::Ptr CreateModule(const Parameters::Accessor& /*params*/,
                                     const Formats::Chiptune::Container& container,
                                     Parameters::Container::Ptr properties) const override
    {
      try
      {
        // TODO: specify type filter
        auto track = ModulePtr(
            new openmpt::module(static_cast<const uint8_t*>(container.Start()), container.Size(), LOG, Controls));

        // play all subsongs
        track->select_subsong(-1);

        PropertiesHelper props(*properties);
        FillMetadata(*track, props);

        return MakePtr<Holder>(std::move(track), std::move(properties));
      }
      catch (const std::exception& e)
      {
        Dbg("Failed to create OpenMPT module: {}", e.what());
      }
      return {};
    }

  private:
    const PluginDescription& Desc;
    std::map<std::string, std::string> Controls;
  };

  // clang-format off
  const PluginDescription PLUGINS[] =
  {
    {
      "XM"
      ,
      "'E'x't'e'n'd'e'd' 'M'o'd'u'l'e':' "_sv
      ,
      "FastTracker II"
      //, "XM"
    },
    {
      "IT"
      ,
      "'I | 't"
      "'M | 'p"
      "'P | 'm"
      "'M | '."
      ""_sv
      ,
      "Impulse Tracker"
      //, "IT"
    },
    {
      "S3M"
      ,
      "?{28}"    // title
      "?"        // eof
      "10"       // file type
      "??"       // reserved
      "?{10}"    // sizes, flags
      "01|02 00" // version
      "'S'C'R'M"
      ""_sv
      ,
      "ScreamTracker 3"
      //, "S3M"
    },
    {
      "STM"
      ,
      "?{20}"    // songname
      "20-7e{8}" // trackername
      "02|1a"    // eof
      "02"       // type=module
      "02"       // major
      "00|0a|14|15" // minor
      "?"        // tempo
      "01-40"    // num patterns
      "00-40|58" // global volume or placeholder
      ""_sv
      ,
      "ScreamTracker 2"
      //, STM
    },
    {
      "MED"
      ,
      "'M'M'D '0-'3" // signature
      ""_sv
      ,
      "OctaMED / MED Soundstudio"
      //, MED
    },
    {
      "MTM"
      ,
      "'M'T'M" // signature
      "00-1f"  // version
      "?{20}"  // songName
      "??"     // numTracks
      "?"      // lastPattern
      "00-7f"  // lastOrder
      "??"     // commentSize
      "? ?"    // numSamples, attribute
      "00-40"  // beatsPerTrack
      "01-20"  // numChannels
      ""_sv
      ,
      "MultiTracker"
      //, MTM
    },
    {
      "MDL"
      ,
      "'D'M'D'L" // signature
      "00-1f"    // version
      ""_sv
      ,
      "Digitrakker"
      //, "MDL"
    },
    {
      "DBM"
      ,
      "'D'B'M'0" // signagure
      "00-03"    // trkVerHi
      ""_sv
      ,
      "DigiBooster Pro"
      //, "DBM"
    },
    {
      "FAR"
      ,
      "'F'A'R fe"  // signature
      "?{40}"      // songName
      "0d0a1a"     // eof
      ""_sv
      ,
      "Farandole Composer"
      //, "FAR"
    },
    {
      "AMS"
      ,
      "'E'x't'r'e'm'e"
      "?"  // versionLow
      "01" // versionHigh
      ""_sv
      ,
      "Extreme's Tracker"
      //, "AMS"
    },
    {
      "AMS"
      ,
      "'A'M'S'h'd'r 1a"_sv
      ,
      "Velvet Studio"
      //, "AMS2"
    },
    {
      "OKT"
      ,
      "'O'K'T'A'S'O'N'G" // signature
      "(20-7f){4}"  // iff id
      ""_sv
      ,
      "Oktalyzer"
      //, "OKT"
    },
    {
      "PTM"
      ,
      "?{28}"    // songname
      "1a"       // dosEOF
      "? 00-02"  // version
      "?"        // reserved
      "? 00-01"  // numOrders 0..256
      "01-ff 00" // numSamples 1..255
      "01-80 00" // numPatterns 1..128
      "01-20 00" // numChannels 1..32
      "00 00 ??" // flags, reserved
      "'P'T'M'F" // magic
      ""_sv
      ,
      "PolyTracker"
      //, "PTM"
    },
    {
      "ULT"
      ,
      "'M'A'S'_'U'T'r'a'c'k'_'V'0'0"
      "'1-'4"
      ""_sv
      ,
      "UltraTracker"
      //, "ULT"
    },
    {
      "DMF"
      ,
      "'D'D'M'F"  // signature
      "01-0a"     // version
      ""_sv
      ,
      "X-Tracker"
      //, "DMF"
    },
    {
      "DSM"
      ,
      // "RIFF ???? DSMF SONG"
      // no examples for second variant with "DSMF 0000/RIFF ????" 
      "'R'I'F'F ???? 'D'S'M'F"
      // song header should be located here
      "'S'O'N'G" //id
      "????"     //size
      "?{28}"    // title
      "??"       // version
      "??"       // flags
      "??"       // order pos
      "00-80 00" // restart pos <= 128
      "00-80 00" // num orders <= 128
      "??"       // num samples
      "? 00-01"  // num patterns <= 256
      "00-10 00" // num channels <= 16
      ""_sv
      ,
      "Digital Sound Interface Kit RIFF"
      //, "DSM"
    },
    {
      "AMF"
      ,
      "'A'S'Y'L'U'M' 'M'u's'i'c' 'F'o'r'm'a't' 'V'1'.'0 00" // signature
      "? ?"   // speed, tempo
      "01-3f" // numSamples
      ""_sv
      ,
      "ASYLUM Music Format"
      //, "AMF_Asylum"
    },
    {
      "AMF"
      ,
      "'A'M'F"  // signature
      "08-0e"   // version
      "?{32}"   // title
      "? ? ?"   // samples, orders, tracks
      "00-20"   // channels
      ""_sv
      ,
      "DSMI / Digital Sound And Music Interface"
      //, "AMF_DSMI"
    },
    {
      "PSM"
      ,
      "'P'S'M' " // signature
      "????"     // fileSize
      "'F'I'L'E" // fileInfoID
      ""_sv
      ,
      "Epic MegaGames MASI"
      //, "PSM"
    },
    {
      "PSM"
      ,
      "'P'S'M fe"  // formatID
      "?{59}"      // songTitle
      "1a"         // lineEnd
      "%xxxxxx00"  // songType
      "01|10"      // formatVersion
      "00"         // patternVersion
      "? ? ?"      // speed, tempo, masterVolume
      "?? ?? ?? ??" // length, orders, patterns, samples
      "? 00-01"    // channelsPlay
      "? 00-01"    // channelsReal
      ""_sv
      ,
      "Epic MegaGames MASI (Old Version)"
      //, "PSM16"
    },
    {
      "MT2"
      ,
      "'M'T'2'0"  // signature
      "????"      // userID
      "? 02"      // version 0x200..0x2ff
      "?{32}"     // trackeName
      "?{64}"     // songName
      "? 00-01"   // numOrders <= 256
      "??"        // restartPos
      "??"        // numPatterns
      "01-40 00"  // numChannels
      ""_sv
      ,
      "Mad Tracker 2.xx"
      //, "MT2"
    },
    // ITP not supported due to external files
    {
      "GDM"
      ,
      "'G'D'M fe"  // magic
      "?{32}"      // songTitle
      "?{32}"      // musician
      "0d 0a 1a"   // dosEOF
      "'G'M'F'S"   // magic2
      "01 00"      // format major, minor
      "?? ? ?"     // trackerId, tracker major, minor
      "?{32}"      // pan map
      "? ? ?"      // master vol, tempo, bpm
      "01-09 00"   // originalFormat
      ""_sv
      ,
      "BWSB Soundsystem"
      //, "GDM"
    },
    {
      "IMF"
      ,
      "?{32}"   // title
      "? 00-01" // ordNum
      "??"      // patNum
      "? 00-01" // insNum 256?
      "?? ?{8}" // flags, unused
      "? ? ? ?" // tempo, bpm, master, amp
      "?{8}"    // unused2
      "'I'M'1'0" // signature
      "(?{15} 00-02){32}"  // channels
      ""_sv
      ,
      "Imago Orpheus"
      //, "IMF"
    },
    {
      "DBM"
      ,
      "'D'I'G'I' 'B'o'o's't'e'r' 'm'o'd'u'l'e 00"
      "?{4} ?" // version + int
      "01-08"  // numChannels
      ""_sv
      ,
      "Digi Booster"
      //, "DIGI"
    },
    {
      "DTM"
      ,
      "'D'.'T'." // magic
      "00 00 00 0e-ff" // headerSize
      "00"             // type
      ""_sv
      ,
      "Digital Tracker / Digital Home Studio"
      //, "DTM"
    },
    {
      "PLM"
      ,
      "'P'L'M 1a" // signature
      "60-ff"     // header size 96+
      "10"        // version
      "?{48}"     // song name
      "01-20"     // num channels 1..32
      ""_sv
      ,
      "Disorder Tracker 2"
      //, "PLM"
    },
    {
      "J2B"
      ,
      "'R'I'F'F ????"
      "'A'M 'F|'  'F|' "
      ""_sv
      ,
      "Galaxy Sound System"
      //, "AM"
    },
    {
      "MOD"
      ,
      "'F'O'R'M"
      "????"
      "'M'O'D'L"
      ""_sv
      ,
      "ProTracker 3.6"
      //, "PT36"
    },
    // no examples for MUS_KM
    {
      "FMT"
      ,
      "'F'M'T'r'a'c'k'e'r 01 01" // magic
      "?{20}" // trackerName
      "?{32}" // songName
      "(?{8} ?{8} %000000xx{3}){8}" // channels
      ""_sv
      ,
      "Davey W Taylor's FM Tracker"
      //, "FMT"
    },
    {
      "SFX"
      ,
      "(00 00-02 ?? ){15}" // samples offsets up to 131072 BE
      "'S 'O 'N 'G" // magic
      ""_sv
      ,
      "SoundFX 1.x"
      //, "SFX"
    },
    {
      "SFX"
      ,
      "(00 00-02 ?? ){31}" // samples offsets up to 131072 BE
      "'S 'O '3 '1" // magic
      ""_sv
      ,
      "SoundFX 2.0 / MultiMedia Sound"
      //, "SFX"
    },
    {
      "STP"
      ,
      "'S'T'P'3"
      "00 00-02" // be version
      "01-80"    // orders 1..128
      "? ?{128}" // patterns length, order list
      "?? ??"    // speed, speed frac
      "?? ??"    // timer count, flags
      "????"     // reserved
      "00 32"    // midi count == 50
      ""_sv
      ,
      "Soundtracker Pro II"
      //, "STP"
    },
    {
      "MOD"
      ,
      "?{1080}" // skip
      "('M      |'P|'N|'L|'F|'N|'O   |'C   |'M|'8|'F   |'F|'E|'1-'9|'0-'9|'T)"
      "('.|'!|'&|'A|'S|'A|'E|'.|'C|'K|'D   |00   |'A   |'L|'X|'C   |'0-'9|'D)"
      "('K      |'T|'M|'R|'S|'T|'T   |'8|'6|00   |'0   |'T|'O|'H   |'C   |'Z)"
      "('.|'!   |'T|'S|'D|'T|'.|'A   |'1   |00   |'4-'8|'4-'9|'N   |'H|'N|'4-'9)"
      ""_sv
      ,
      "Generic MOD-compatible"
      //, "MOD"
    },
    {
      "MOD"
      ,
      "?{1464}"
      "'M'T'N"
      "00"
      ""_sv
      ,
      "MnemoTroN SoundTracker (MOD-compatible)"
      //, "ICE"
    },
    {
      "MOD"
      ,
      "?{1464}"
      "'I'T'1'0"
      ""_sv
      ,
      "Ice Tracker (MOD-compatible)"
      //, "ICE"
    },
    {
      "669"
      ,
      "'i|'J 'f|'N" // magic
      "?{108}"      // message
      "01-40"       // samples
      "01-80"       // patterns
      "00-7f"       // restart pos
      "(00-7f|fe|ff){128}" // orders
      "?{128}"      // tempoList
      "(00-3f){128}"// breaks
      ""_sv
      ,
      "669 Composer / UNIS 669"
      //, "669"
    },
    {
      "C67"
      ,
      "01-0f"  // speed 1..15
      "?"      // restart pos
      "(?{12} 00){32}" // samples names
      "("
       "00000000" // unknown = 0 
       "? ? 0000" // length < 0x10000
       "???? ????" // loops
      "){32}" // samples
      "(?{12} 00){32}" // instrument names
      "("
       "0x ? ? ? ? %000000xx ? ? ? ? %000000xx"
      "){32}" // instruments
      ""_sv
      ,
      "Composer 670"
      //, "C67"
    },
    {
      "MO3"
      ,
      "'M'O'3" // signature
      "00-05"  // version
      ""_sv
      ,
      "Un4seen MO3"
      //, "MO3"
    },
    {
      "MOD"
      ,
      "(00|08|20-7f){20}"  //name
      "("                  //instruments
       "?{22}"             // name
       "00-7f?"            // BE size
       "00"                // finetune
       "00-40"             // volume
       "??"                // BE loop start
       "00-7f?"            // BE loop size
      "){15}"
      "00-80"           //len
      "00-dc"           //restart
      "(00-3f){128}"    //order
      ""_sv
      ,
      "Ultimate Soundtracker / etc (MOD Compatible)"
      //, "M15"
    },
    {
      "DSYM"
      ,
      "020113131412010b"  //magic
      "00-01"             //version
      "01-08"             //channels
      "? 00-10"           //le orders up to 4096
      "? 00-10"           //le tracks up to 4096
      ""_sv
      ,
      "Digital Symphony"
      //, "DSYM"
    },
    {
      "SYMMOD"
      ,
      "'S'y'm'M"          //magic
      "00000001"          //be version==1
      ""_sv
      ,
      "Symphonie"
      //, "SYMMOD"
    }
  };
  // clang-format on
}  // namespace Module::Mpt

namespace ZXTune
{
  void RegisterMPTPlugins(PlayerPluginsRegistrator& registrator)
  {
    const uint_t CAPS = Capabilities::Module::Type::TRACK | Capabilities::Module::Device::DAC;
    for (const auto& desc : Module::Mpt::PLUGINS)
    {
      auto decoder = MakePtr<Module::Mpt::Decoder>(desc);
      auto factory = MakePtr<Module::Mpt::Factory>(desc);
      auto plugin = CreatePlayerPlugin(desc.Id, CAPS, std::move(decoder), std::move(factory));
      registrator.RegisterPlugin(std::move(plugin));
    }
  }
}  // namespace ZXTune
