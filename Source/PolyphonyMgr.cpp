//
//  PolyphonyMgr.cpp
//  additiveSynth
//
//  Created by Ryan Challinor on 11/20/12.
//
//

#include "PolyphonyMgr.h"
#include "IMidiVoice.h"
#include "FMVoice.h"
#include "KarplusStrongVoice.h"
#include "SingleOscillatorVoice.h"
#include "SampleVoice.h"
#include "SynthGlobals.h"
#include "Profiler.h"

PolyphonyMgr::PolyphonyMgr(IDrawableModule* owner)
   : mAllowStealing(true)
   , mLastVoice(-1)
   , mFadeOutBufferPos(0)
   , mOwner(owner)
   , mFadeOutBuffer(kVoiceFadeSamples)
   , mFadeOutWorkBuffer(kVoiceFadeSamples)
{
}

PolyphonyMgr::~PolyphonyMgr()
{
   for (int i=0; i<kNumVoices; ++i)
      delete mVoices[i].mVoice;
}

void PolyphonyMgr::Init(VoiceType type, IVoiceParams* params)
{
   if (type == kVoiceType_FM)
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         mVoices[i].mVoice = new FMVoice(mOwner);
         mVoices[i].mVoice->SetVoiceParams(params);
      }
   }
   else if (type == kVoiceType_Karplus)
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         mVoices[i].mVoice = new KarplusStrongVoice(mOwner);
         mVoices[i].mVoice->SetVoiceParams(params);
      }
   }
   else if (type == kVoiceType_SingleOscillator)
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         mVoices[i].mVoice = new SingleOscillatorVoice(mOwner);
         mVoices[i].mVoice->SetVoiceParams(params);
      }
   }
   else if (type == kVoiceType_Sampler)
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         mVoices[i].mVoice = new SampleVoice(mOwner);
         mVoices[i].mVoice->SetVoiceParams(params);
      }
   }
   else
   {
      assert(false);  //unsupported voice type
   }
}

void PolyphonyMgr::Start(double time, int pitch, float amount, int voiceIdx, ModulationParameters modulation)
{
   assert(voiceIdx < kNumVoices);
   
   amount = amount * amount; //increase the importance of velocity
   
   bool preserveVoice = voiceIdx != -1 &&  //we specified a voice
                        mVoices[voiceIdx].mPitch != -1; //there is a note playing from that voice

   if (voiceIdx == -1) //haven't specified a voice
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         if (mVoices[i].mPitch == pitch)
         {
            voiceIdx = i;   //reuse existing voice
            preserveVoice = true;
            break;
         }
      }
   }
   
   if (voiceIdx == -1) //need a new voice
   {
      for (int i=0; i<kNumVoices; ++i)
      {
         int check = (i + mLastVoice + 1) % 16;  //try to keep incrementing through list to allow old voices to finish
         if (mVoices[check].mPitch == -1)
         {
            voiceIdx = check;
            break;
         }
      }
   }

   if (voiceIdx == -1)   //all used
   {
      if (mAllowStealing)
      {
         double oldest = mVoices[0].mTime;
         int oldestIndex = 0;
         for (int i=1; i<kNumVoices; ++i)
         {
            if (mVoices[i].mTime < oldest)
            {
               oldest = mVoices[i].mTime;
               oldestIndex = i;
            }
         }
         voiceIdx = oldestIndex;
      }
      else
      {
         return;
      }
   }
   
   IMidiVoice* voice = mVoices[voiceIdx].mVoice;
   assert(voice);
   voice->SetPitch(pitch);
   voice->SetModulators(modulation);
   if (!preserveVoice || modulation.pan != voice->GetPan())
   {
      Clear(mWorkBuffer, kVoiceFadeSamples);
      voice->Process(time, &mFadeOutWorkBuffer);
      for (int i=0; i<kVoiceFadeSamples; ++i)
      {
         float fade = 1 - (float(i) / kVoiceFadeSamples);
         for (int ch=0; ch<mFadeOutBuffer.NumActiveChannels(); ++ch)
         {
            mFadeOutBuffer.GetChannel(ch)[(i+mFadeOutBufferPos) % kVoiceFadeSamples] += mFadeOutWorkBuffer.GetChannel(ch)[i] * fade;
         }
      }
      voice->ClearVoice();
   }
   voice->Start(time, amount);
   voice->SetPan(modulation.pan);
   mLastVoice = voiceIdx;
   
   mVoices[voiceIdx].mPitch = pitch;
   mVoices[voiceIdx].mTime = time;
}

void PolyphonyMgr::Stop(double time, int pitch)
{
   for (int i=0; i<kNumVoices; ++i)
   {
      if (mVoices[i].mPitch == pitch)
         mVoices[i].mVoice->Stop(time);
   }
}

void PolyphonyMgr::Process(double time, ChannelBuffer* out, int bufferSize)
{
   Profiler profiler("PolyphonyMgr");
   
   mFadeOutBuffer.SetNumActiveChannels(out->NumActiveChannels());
   mFadeOutWorkBuffer.SetNumActiveChannels(out->NumActiveChannels());

   for (int i=0; i<kNumVoices; ++i)
   {
      Clear(mWorkBuffer, bufferSize);
      mVoices[i].mVoice->Process(time, out);
      
      if (mVoices[i].mPitch != -1 && mVoices[i].mVoice->IsDone(time))
         mVoices[i].mPitch = -1;
   }
   
   for (int ch=0; ch<out->NumActiveChannels(); ++ch)
   {
      for (int i=0; i<bufferSize; ++i)
      {
         int fadeOutIdx = (i+mFadeOutBufferPos) % kVoiceFadeSamples;
         out->GetChannel(ch)[i] += mFadeOutBuffer.GetChannel(ch)[fadeOutIdx];
         mFadeOutBuffer.GetChannel(ch)[fadeOutIdx] = 0;
      }
   }
   
   mFadeOutBufferPos += bufferSize;
}
