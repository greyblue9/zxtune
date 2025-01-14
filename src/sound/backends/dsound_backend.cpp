/**
 *
 * @file
 *
 * @brief  DirectSound backend implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "sound/backends/backend_impl.h"
#include "sound/backends/dsound.h"
#include "sound/backends/gates/dsound_api.h"
#include "sound/backends/l10n.h"
#include "sound/backends/storage.h"
#include "sound/backends/volume_control.h"
// common includes
#include <error_tools.h>
#include <make_ptr.h>
// library includes
#include <debug/log.h>
#include <math/numeric.h>
#include <sound/backend_attrs.h>
#include <sound/backends_parameters.h>
#include <sound/render_params.h>
#include <strings/encoding.h>
// std includes
#include <thread>
// boost includes
#include <boost/range/size.hpp>

#define FILE_TAG BCBCECCC

namespace Sound::DirectSound
{
  const Debug::Stream Dbg("Sound::Backend::DirectSound");

  const uint_t CAPABILITIES = CAP_TYPE_SYSTEM | CAP_FEAT_HWVOLUME;

  const uint_t LATENCY_MIN = 20;
  const uint_t LATENCY_MAX = 10000;

  HWND GetWindowHandle()
  {
    if (HWND res = ::GetForegroundWindow())
    {
      return res;
    }
    return ::GetDesktopWindow();
  }

  void CheckWin32Error(HRESULT res, Error::LocationRef loc)
  {
    if (FAILED(res))
    {
      throw MakeFormattedError(loc, translate("Error in DirectSound backend: {}."), res);
    }
  }

  void ReleaseRef(IUnknown* iface)
  {
    if (iface)
    {
      iface->Release();
    }
  }

  String Guid2String(LPGUID guid)
  {
    if (!guid)
    {
      return String();
    }
    OLECHAR strGuid[39] = {0};
    if (const int chars = ::StringFromGUID2(*guid, strGuid, boost::size(strGuid)))
    {
      return String(strGuid, strGuid + chars - 1);
    }
    else
    {
      return String();
    }
  }

  std::unique_ptr<GUID> String2Guid(const String& str)
  {
    if (str.empty())
    {
      return std::unique_ptr<GUID>();
    }
    std::vector<OLECHAR> strGuid(str.begin(), str.end());
    strGuid.push_back(0);
    std::unique_ptr<GUID> res(new GUID);
    CheckWin32Error(::CLSIDFromString(strGuid.data(), res.get()), THIS_LINE);
    return res;
  }

  typedef std::shared_ptr<IDirectSound> DirectSoundPtr;
  typedef std::shared_ptr<IDirectSoundBuffer> DirectSoundBufferPtr;

  DirectSoundPtr OpenDevice(Api& api, const String& device)
  {
    Dbg("OpenDevice({})", device);
    IDirectSound* raw = 0;
    const std::unique_ptr<GUID> deviceUuid = String2Guid(device);
    CheckWin32Error(api.DirectSoundCreate(deviceUuid.get(), &raw, NULL), THIS_LINE);
    const DirectSoundPtr result = DirectSoundPtr(raw, &ReleaseRef);
    CheckWin32Error(result->SetCooperativeLevel(GetWindowHandle(), DSSCL_PRIORITY), THIS_LINE);
    Dbg("Opened");
    return result;
  }

  DirectSoundBufferPtr CreateSecondaryBuffer(DirectSoundPtr device, uint_t sampleRate, uint_t bufferInMs)
  {
    Dbg("CreateSecondaryBuffer");
    WAVEFORMATEX format;
    std::memset(&format, 0, sizeof(format));
    format.cbSize = sizeof(format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = Sample::CHANNELS;
    format.nSamplesPerSec = static_cast< ::DWORD>(sampleRate);
    format.nBlockAlign = sizeof(Sample);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.wBitsPerSample = Sample::BITS;

    DSBUFFERDESC buffer;
    std::memset(&buffer, 0, sizeof(buffer));
    buffer.dwSize = sizeof(buffer);
    buffer.dwFlags = DSBCAPS_GLOBALFOCUS;
    buffer.dwBufferBytes = format.nAvgBytesPerSec * bufferInMs / 1000;
    buffer.lpwfxFormat = &format;

    LPDIRECTSOUNDBUFFER rawSecondary = 0;
    CheckWin32Error(device->CreateSoundBuffer(&buffer, &rawSecondary, NULL), THIS_LINE);
    assert(rawSecondary);
    const std::shared_ptr<IDirectSoundBuffer> secondary(rawSecondary, &ReleaseRef);
    Dbg("Created");
    return secondary;
  }

  DirectSoundBufferPtr CreatePrimaryBuffer(DirectSoundPtr device)
  {
    Dbg("CreatePrimaryBuffer");
    DSBUFFERDESC buffer;
    std::memset(&buffer, 0, sizeof(buffer));
    buffer.dwSize = sizeof(buffer);
    buffer.dwFlags = DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_PRIMARYBUFFER;

    LPDIRECTSOUNDBUFFER rawPrimary = 0;
    CheckWin32Error(device->CreateSoundBuffer(&buffer, &rawPrimary, NULL), THIS_LINE);
    assert(rawPrimary);
    const std::shared_ptr<IDirectSoundBuffer> primary(rawPrimary, &ReleaseRef);
    Dbg("Created");
    return primary;
  }

  class StreamBuffer
  {
  public:
    typedef std::shared_ptr<StreamBuffer> Ptr;

    explicit StreamBuffer(DirectSoundBufferPtr buff)
      : Buff(buff)
      , SleepPeriod(1)
      , BuffSize(0)
      , Cursor(0)
    {
      DSBCAPS caps;
      std::memset(&caps, 0, sizeof(caps));
      caps.dwSize = sizeof(caps);
      CheckWin32Error(Buff->GetCaps(&caps), THIS_LINE);
      BuffSize = caps.dwBufferBytes;
      Dbg("Using cycle buffer size {}", BuffSize);
      CheckWin32Error(Buff->Play(0, 0, DSBPLAY_LOOPING), THIS_LINE);
    }

    void Add(const Chunk& buffer)
    {
      std::size_t inputSize = buffer.size() * sizeof(buffer.front());
      const uint8_t* inputStart = safe_ptr_cast<const uint8_t*>(buffer.data());

      while (inputSize)
      {
        const std::size_t srcSize = std::min(inputSize, BuffSize / 2);
        if (!WaitForFree(srcSize))
        {
          return;
        }

        LPVOID part1Data = 0, part2Data = 0;
        DWORD part1Size = 0, part2Size = 0;

        for (;;)
        {
          const HRESULT res = Buff->Lock(Cursor, srcSize, &part1Data, &part1Size, &part2Data, &part2Size, 0);
          if (DSERR_BUFFERLOST == res)
          {
            Dbg("Buffer lost. Retry to lock");
            Buff->Restore();
            continue;
          }
          CheckWin32Error(res, THIS_LINE);
          break;
        }
        assert(srcSize == part1Size + part2Size);
        std::memcpy(part1Data, inputStart, part1Size);
        if (part2Data)
        {
          std::memcpy(part2Data, inputStart + part1Size, part2Size);
        }
        Cursor += srcSize;
        Cursor %= BuffSize;
        CheckWin32Error(Buff->Unlock(part1Data, part1Size, part2Data, part2Size), THIS_LINE);
        inputSize -= srcSize;
        inputStart += srcSize;
      }
    }

    void Pause()
    {
      LPVOID part1Data = 0, part2Data = 0;
      DWORD part1Size = 0, part2Size = 0;
      for (;;)
      {
        const HRESULT res = Buff->Lock(0, 0, &part1Data, &part1Size, &part2Data, &part2Size, DSBLOCK_ENTIREBUFFER);
        if (DSERR_BUFFERLOST == res)
        {
          Dbg("Buffer lost. Retry to lock");
          Buff->Restore();
          continue;
        }
        CheckWin32Error(res, THIS_LINE);
        break;
      }
      std::memset(part1Data, 0, part1Size);
      if (part2Data)
      {
        std::memset(part2Data, 0, part2Size);
      }
      CheckWin32Error(Buff->Unlock(part1Data, part1Size, part2Data, part2Size), THIS_LINE);
    }

    void Stop()
    {
      CheckWin32Error(Buff->Stop(), THIS_LINE);
      while (IsPlaying())
      {
        Wait();
      }
    }

  private:
    bool WaitForFree(std::size_t srcSize) const
    {
      while (GetBytesToWrite() < srcSize)
      {
        if (!IsPlaying())
        {
          return false;
        }
        Wait();
      }
      return true;
    }

    std::size_t GetBytesToWrite() const
    {
      DWORD playCursor = 0;
      CheckWin32Error(Buff->GetCurrentPosition(&playCursor, NULL), THIS_LINE);
      if (Cursor > playCursor)  // go ahead
      {
        return BuffSize - (Cursor - playCursor);
      }
      else  // overlap
      {
        return playCursor - Cursor;
      }
    }

    bool IsPlaying() const
    {
      DWORD status = 0;
      CheckWin32Error(Buff->GetStatus(&status), THIS_LINE);
      return 0 != (status & DSBSTATUS_PLAYING);
    }

    void Wait() const
    {
      std::this_thread::sleep_for(SleepPeriod);
    }

  private:
    const DirectSoundBufferPtr Buff;
    const std::chrono::milliseconds SleepPeriod;
    std::size_t BuffSize;
    std::size_t Cursor;
  };

  // in centidecibell
  // use simple scale method due to less error in forward and backward conversion
  Gain::Type AttenuationToGain(int_t cdB)
  {
    return Gain::Type(1) - Gain::Type(cdB, int(DSBVOLUME_MIN));
  }

  int_t GainToAttenuation(Gain::Type level)
  {
    return ((Gain::Type(1) - level) * int(DSBVOLUME_MIN)).Round();
  }

  class VolumeControl : public Sound::VolumeControl
  {
  public:
    // buffer depends of device
    VolumeControl(DirectSoundPtr device, DirectSoundBufferPtr buffer)
      : Device(device)
      , Buffer(buffer)
    {}

    virtual Gain GetVolume() const
    {
      const VolPan vols = GetVolumeImpl();
      static_assert(Sample::CHANNELS == 2, "Incompatible sound channels count");
      // in hundredths of a decibel
      const int_t attLeft = vols.first - (vols.second > 0 ? vols.second : 0);
      const int_t attRight = vols.first - (vols.second < 0 ? -vols.second : 0);
      const Gain volume(AttenuationToGain(attLeft), AttenuationToGain(attRight));
      Dbg("GetVolume(vol={} pan={})", vols.first, vols.second);
      return volume;
    }

    virtual void SetVolume(const Gain& volume)
    {
      if (!volume.IsNormalized())
      {
        throw Error(THIS_LINE, translate("Failed to set volume: gain is out of range."));
      }
      const int_t attLeft = GainToAttenuation(volume.Left());
      const int_t attRight = GainToAttenuation(volume.Right());
      const LONG vol = std::max(attLeft, attRight);
      // pan is negative for left
      const LONG pan = attLeft < vol ? vol - attLeft : vol - attRight;
      Dbg("SetVolume(vol={} pan={})", vol, pan);
      SetVolumeImpl(VolPan(vol, pan));
    }

  private:
    typedef std::pair<LONG, LONG> VolPan;

    VolPan GetVolumeImpl() const
    {
      VolPan res;
      CheckWin32Error(Buffer->GetVolume(&res.first), THIS_LINE);
      CheckWin32Error(Buffer->GetPan(&res.second), THIS_LINE);
      return res;
    }

    void SetVolumeImpl(const VolPan& vols) const
    {
      CheckWin32Error(Buffer->SetVolume(vols.first), THIS_LINE);
      CheckWin32Error(Buffer->SetPan(vols.second), THIS_LINE);
    }

  private:
    const DirectSoundPtr Device;
    const DirectSoundBufferPtr Buffer;
  };

  class BackendParameters
  {
  public:
    explicit BackendParameters(const Parameters::Accessor& accessor)
      : Accessor(accessor)
    {}

    String GetDevice() const
    {
      Parameters::StringType device;
      Accessor.FindValue(Parameters::ZXTune::Sound::Backends::DirectSound::DEVICE, device);
      return device;
    }

    uint_t GetLatency() const
    {
      Parameters::IntType latency = Parameters::ZXTune::Sound::Backends::DirectSound::LATENCY_DEFAULT;
      if (Accessor.FindValue(Parameters::ZXTune::Sound::Backends::DirectSound::LATENCY, latency)
          && !Math::InRange<Parameters::IntType>(latency, LATENCY_MIN, LATENCY_MAX))
      {
        throw MakeFormattedError(THIS_LINE,
                                 translate("DirectSound backend error: latency ({0}) is out of range ({1}..{2})."),
                                 static_cast<int_t>(latency), LATENCY_MIN, LATENCY_MAX);
      }
      return static_cast<uint_t>(latency);
    }

  private:
    const Parameters::Accessor& Accessor;
  };

  class BackendWorker : public Sound::BackendWorker
  {
  public:
    BackendWorker(Api::Ptr api, Parameters::Accessor::Ptr params)
      : DsApi(api)
      , BackendParams(params)
      , RenderingParameters(RenderParameters::Create(BackendParams))
    {}

    virtual void Startup()
    {
      Dbg("Starting");
      Objects = OpenDevices();
      Dbg("Started");
    }

    virtual void Shutdown()
    {
      Dbg("Stopping");
      Objects = DSObjects();
      Dbg("Stopped");
    }

    virtual void Pause()
    {
      Objects.Stream->Pause();
    }

    virtual void Resume() {}

    virtual void FrameStart(const Module::State& /*state*/) {}

    virtual void FrameFinish(Chunk buffer)
    {
      /*

       From http://msdn.microsoft.com/en-us/library/windows/desktop/dd797880(v=vs.85).aspx :

       For 8-bit PCM data, each sample is represented by a single unsigned data byte.
       For 16-bit PCM data, each sample is represented by a 16-bit signed value.
      */
      if (Sample::BITS == 16)
      {
        buffer.ToS16();
      }
      else
      {
        buffer.ToU8();
      }
      Objects.Stream->Add(buffer);
    }

    VolumeControl::Ptr GetVolumeControl() const
    {
      return CreateVolumeControlDelegate(Objects.Volume);
    }

  private:
    struct DSObjects
    {
      DirectSoundPtr Device;
      StreamBuffer::Ptr Stream;
      VolumeControl::Ptr Volume;

      DSObjects& operator=(const DSObjects& rh)
      {
        if (Stream)
        {
          Stream->Stop();
        }
        Volume.reset();
        Stream.reset();
        Device.reset();
        Device = rh.Device;
        Stream = rh.Stream;
        Volume = rh.Volume;
        return *this;
      }
    };

    DSObjects OpenDevices()
    {
      const BackendParameters params(*BackendParams);
      const String device = params.GetDevice();
      DSObjects res;
      res.Device = OpenDevice(*DsApi, device);
      const uint_t latency = params.GetLatency();
      const DirectSoundBufferPtr buffer = CreateSecondaryBuffer(res.Device, RenderingParameters->SoundFreq(), latency);
      res.Stream = MakePtr<StreamBuffer>(buffer);
      const DirectSoundBufferPtr primary = CreatePrimaryBuffer(res.Device);
      res.Volume = MakePtr<VolumeControl>(res.Device, primary);
      return res;
    }

  private:
    const Api::Ptr DsApi;
    const Parameters::Accessor::Ptr BackendParams;
    const RenderParameters::Ptr RenderingParameters;
    DSObjects Objects;
  };

  class BackendWorkerFactory : public Sound::BackendWorkerFactory
  {
  public:
    explicit BackendWorkerFactory(Api::Ptr api)
      : DsApi(api)
    {}

    virtual BackendWorker::Ptr CreateWorker(Parameters::Accessor::Ptr params, Module::Holder::Ptr /*holder*/) const
    {
      return MakePtr<BackendWorker>(DsApi, params);
    }

  private:
    const Api::Ptr DsApi;
  };

  class DirectSoundDevice : public Device
  {
  public:
    DirectSoundDevice(const String& id, const String& name)
      : IdValue(id)
      , NameValue(name)
    {}

    virtual String Id() const
    {
      return IdValue;
    }

    virtual String Name() const
    {
      return NameValue;
    }

  private:
    const String IdValue;
    const String NameValue;
  };

  class DevicesIterator : public Device::Iterator
  {
  public:
    explicit DevicesIterator(Api::Ptr api)
      : Current(Devices.begin())
    {
      if (DS_OK != api->DirectSoundEnumerateW(&EnumerateDevicesCallback, &Devices))
      {
        Dbg("Failed to enumerate devices. Skip backend.");
        Current = Devices.end();
      }
      else
      {
        Dbg("Detected {} devices to output.", Devices.size());
        Current = Devices.begin();
      }
    }

    virtual bool IsValid() const
    {
      return Current != Devices.end();
    }

    virtual Device::Ptr Get() const
    {
      return IsValid() ? MakePtr<DirectSoundDevice>(Current->first, Current->second) : Device::Ptr();
    }

    virtual void Next()
    {
      if (IsValid())
      {
        ++Current;
      }
    }

  private:
    static BOOL CALLBACK EnumerateDevicesCallback(LPGUID guid, LPCWSTR descr, LPCWSTR module, LPVOID param)
    {
      static_assert(sizeof(*descr) == sizeof(uint16_t), "Char size mismatch");
      const auto& id = Guid2String(guid);
      const auto& name = Strings::Utf16ToUtf8(safe_ptr_cast<const uint16_t*>(descr));
      const auto& mod = Strings::Utf16ToUtf8(safe_ptr_cast<const uint16_t*>(module));
      Dbg("Detected device '{}' (uuid={} module='{}')", name, id, mod);
      DevicesArray& devices = *static_cast<DevicesArray*>(param);
      devices.push_back(IdAndName(id, name));
      return TRUE;
    }

  private:
    typedef std::pair<String, String> IdAndName;
    typedef std::vector<IdAndName> DevicesArray;
    DevicesArray Devices;
    DevicesArray::const_iterator Current;
  };
}  // namespace Sound::DirectSound

namespace Sound
{
  void RegisterDirectSoundBackend(BackendsStorage& storage)
  {
    try
    {
      const DirectSound::Api::Ptr api = DirectSound::LoadDynamicApi();
      if (DirectSound::DevicesIterator(api).IsValid())
      {
        const BackendWorkerFactory::Ptr factory = MakePtr<DirectSound::BackendWorkerFactory>(api);
        storage.Register(DirectSound::BACKEND_ID, DirectSound::BACKEND_DESCRIPTION, DirectSound::CAPABILITIES, factory);
      }
      else
      {
        throw Error(THIS_LINE, translate("No suitable output devices found"));
      }
    }
    catch (const Error& e)
    {
      storage.Register(DirectSound::BACKEND_ID, DirectSound::BACKEND_DESCRIPTION, DirectSound::CAPABILITIES, e);
    }
  }

  namespace DirectSound
  {
    Device::Iterator::Ptr EnumerateDevices()
    {
      try
      {
        const Api::Ptr api = LoadDynamicApi();
        return MakePtr<DevicesIterator>(api);
      }
      catch (const Error& e)
      {
        Dbg("{}", e.ToString());
        return Device::Iterator::CreateStub();
      }
    }
  }  // namespace DirectSound
}  // namespace Sound

#undef FILE_TAG
