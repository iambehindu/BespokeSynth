//
//  Pumper.h
//  modularSynth
//
//  Created by Ryan Challinor on 3/16/13.
//
//

#ifndef __modularSynth__Pumper__
#define __modularSynth__Pumper__

#include <iostream>
#include "IAudioEffect.h"
#include "Checkbox.h"
#include "Slider.h"
#include "DropdownList.h"
#include "LFO.h"

class Pumper : public IAudioEffect, public IDropdownListener, public IFloatSliderListener
{
public:
   Pumper();
   virtual ~Pumper();
   
   static IAudioEffect* Create() { return new Pumper(); }
   
   string GetTitleLabel() override { return "pumper"; }
   void CreateUIControls() override;

   //IAudioEffect
   void ProcessAudio(double time, ChannelBuffer* buffer) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   float GetEffectAmount() override;
   string GetType() override { return "pumper"; }

   void DropdownUpdated(DropdownList* list, int oldVal) override;
   void CheckboxUpdated(Checkbox* checkbox) override {}
   void FloatSliderUpdated(FloatSlider* slider, float oldVal) override;

private:
   //IDrawableModule
   void DrawModule() override;
   void GetModuleDimensions(int& w, int& h) override;
   bool Enabled() const override { return mEnabled; }

   
   FloatSlider* mAmountSlider;
   float mOffset;
   FloatSlider* mOffsetSlider;
   FloatSlider* mPumpSlider;
   
   LFO mLFO;
   NoteInterval mInterval;
   DropdownList* mIntervalSelector;
   float mLastValue;
   float mAmount;
   float mPump;
};

#endif /* defined(__modularSynth__Pumper__) */

