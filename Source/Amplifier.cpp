//
//  Amplifier.cpp
//  modularSynth
//
//  Created by Ryan Challinor on 7/13/13.
//
//

#include "Amplifier.h"
#include "ModularSynth.h"
#include "Profiler.h"

Amplifier::Amplifier()
: IAudioProcessor(gBufferSize)
, mBoost(0)
, mBoostSlider(nullptr)
{
}

void Amplifier::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mBoostSlider = new FloatSlider(this,"boost",5,2,110,15,&mBoost,1,4);
}

Amplifier::~Amplifier()
{
}

void Amplifier::Process(double time)
{
   Profiler profiler("Amplifier");

   if (!mEnabled)
      return;
   
   ComputeSliders(0);
   SyncBuffers();
   
   if (GetTarget())
   {
      ChannelBuffer* out = GetTarget()->GetBuffer();
      for (int ch=0; ch<GetBuffer()->NumActiveChannels(); ++ch)
      {
         Mult(GetBuffer()->GetChannel(ch), mBoost*mBoost, out->BufferSize());
         Add(out->GetChannel(ch), GetBuffer()->GetChannel(ch), out->BufferSize());
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch),GetBuffer()->BufferSize(), ch);
      }
   }
   
   GetBuffer()->Reset();
}

void Amplifier::DrawModule()
{

   
   if (Minimized() || IsVisible() == false)
      return;
   
   mBoostSlider->Draw();
}

void Amplifier::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("boost", moduleInfo, 1, mBoostSlider);

   SetUpFromSaveData();
}

void Amplifier::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   SetBoost(mModuleSaveData.GetFloat("boost"));
}



