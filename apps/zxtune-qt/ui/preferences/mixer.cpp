/**
 *
 * @file
 *
 * @brief Single channel mixer widget implementation
 *
 * @author vitamin.caig@gmail.com
 *
 **/

// local includes
#include "mixer.h"
#include "mixer.ui.h"
// common includes
#include <contract.h>
// std includes
#include <utility>

namespace
{
  const char* CHANNEL_NAMES[] = {QT_TRANSLATE_NOOP("UI::MixerWidget", "Left"),
                                 QT_TRANSLATE_NOOP("UI::MixerWidget", "Right")};

  class MixerWidgetImpl
    : public UI::MixerWidget
    , private Ui::Mixer
  {
  public:
    MixerWidgetImpl(QWidget& parent, UI::MixerWidget::Channel chan)
      : UI::MixerWidget(parent)
      , Chan(chan)
    {
      // setup self
      setupUi(this);
      LoadName();

      Require(connect(channelValue, SIGNAL(valueChanged(int)), SIGNAL(valueChanged(int))));
    }

    void setValue(int val) override
    {
      channelValue->setValue(val);
    }

    // QWidget
    void changeEvent(QEvent* event) override
    {
      if (event && QEvent::LanguageChange == event->type())
      {
        LoadName();
      }
      UI::MixerWidget::changeEvent(event);
    }

  private:
    void LoadName()
    {
      channelName->setText(UI::MixerWidget::tr(CHANNEL_NAMES[Chan]));
    }

  private:
    const UI::MixerWidget::Channel Chan;
  };
}  // namespace

namespace UI
{
  MixerWidget::MixerWidget(QWidget& parent)
    : QWidget(&parent)
  {}

  MixerWidget* MixerWidget::Create(QWidget& parent, MixerWidget::Channel chan)
  {
    return new MixerWidgetImpl(parent, chan);
  }
}  // namespace UI

namespace
{
  using namespace Parameters;

  class MixerValueImpl : public MixerValue
  {
  public:
    MixerValueImpl(UI::MixerWidget& parent, Container& ctr, Identifier name, int defValue)
      : MixerValue(parent)
      , Storage(ctr)
      , Name(name)
    {
      IntType value = defValue;
      Storage.FindValue(Name, value);
      parent.setValue(value);
      Require(connect(&parent, SIGNAL(valueChanged(int)), SLOT(SetValue(int))));
    }

    void SetValue(int value) override
    {
      Storage.SetValue(Name, value);
    }

  private:
    Container& Storage;
    const Identifier Name;
  };
}  // namespace

namespace Parameters
{
  MixerValue::MixerValue(QObject& parent)
    : QObject(&parent)
  {}

  void MixerValue::Bind(UI::MixerWidget& mix, Container& ctr, Identifier name, int defValue)
  {
    new MixerValueImpl(mix, ctr, name, defValue);
  }
}  // namespace Parameters
