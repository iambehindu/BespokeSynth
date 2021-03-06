 //
//  VSTPlugin.cpp
//  Bespoke
//
//  Created by Ryan Challinor on 1/18/16.
//
//

#include "VSTPlugin.h"
#include "OpenFrameworksPort.h"
#include "SynthGlobals.h"
#include "IAudioReceiver.h"
#include "ofxJSONElement.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "Scale.h"
#include "ModulationChain.h"
//#include "NSWindowOverlay.h"

namespace
{
   const int kGlobalModulationIdx = 16;
}

namespace VSTLookup
{
   typedef string VstDirExtPair[2];
   const int kNumVstTypes = 1;
   //const VstDirExtPair vstDirs[kNumVstTypes] =
   //                                 {{"/Library/Audio/Plug-Ins/VST3","vst3"},
   //                                  {"/Library/Audio/Plug-Ins/VST","vst"}};
   const VstDirExtPair vstDirs[kNumVstTypes] = {{"/Library/Audio/Plug-Ins/VST","vst"}};
   
   void GetAvailableVSTs(vector<string>& vsts)
   {
      for (int i=0; i<kNumVstTypes; ++i)
      {
         const VstDirExtPair& pair = vstDirs[i];
         string dirPath = pair[0];
         string ext = pair[1];
         DirectoryIterator dir(File(dirPath), true, "*."+ext, File::findFilesAndDirectories);
         while(dir.next())
         {
            File file = dir.getFile();
            vsts.push_back(file.getRelativePathFrom(File(dirPath)).toStdString());
         }
      }
   }
   
   void FillVSTList(DropdownList* list)
   {
      assert(list);
      vector<string> vsts;
      GetAvailableVSTs(vsts);
      for (int i=0; i<vsts.size(); ++i)
         list->AddLabel(vsts[i].c_str(), i);
   }
   
   string GetVSTPath(string vstName)
   {
      for (int i=0; i<kNumVstTypes; ++i)
      {
         const VstDirExtPair& pair = vstDirs[i];
         string dirPath = pair[0];
         string ext = pair[1];
         if (ofIsStringInString(vstName, ext))
            return dirPath+"/"+vstName;
      }
      return "";
   }
}

VSTPlugin::VSTPlugin()
: IAudioProcessor(gBufferSize)
, mVol(1)
, mVolSlider(nullptr)
, mPlugin(nullptr)
, mChannel(1)
, mPitchBendRange(2)
, mModwheelCC(1)  //or 74 in Multidimensional Polyphonic Expression (MPE) spec
, mUseVoiceAsChannel(false)
, mProgramChangeSelector(nullptr)
, mProgramChange(0)
, mOpenEditorButton(nullptr)
//, mWindowOverlay(nullptr)
, mDisplayMode(kDisplayMode_Sliders)
{
   mFormatManager.addDefaultFormats();
   
   mChannelModulations.resize(kGlobalModulationIdx+1);
}

void VSTPlugin::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   mVolSlider = new FloatSlider(this,"vol",3,3,80,15,&mVol,0,1);
   mProgramChangeSelector = new DropdownList(this,"program change",100,3,&mProgramChange);
   mOpenEditorButton = new ClickButton(this, "open", 150, 3);
   
   for (int i=0; i<128; ++i)
      mProgramChangeSelector->AddLabel(ofToString(i), i);
   
   if (mPlugin)
   {
      CreateParameterSliders();
   }
   
   //const auto* editor = mPlugin->createEditor();
}

VSTPlugin::~VSTPlugin()
{
}

void VSTPlugin::Exit()
{
   if (mWindow)
   {
      VSTWindow* window = mWindow.release();
      delete window;
   }
}

string VSTPlugin::GetTitleLabel()
{
   if (mPlugin)
      return "vst: "+mPlugin->getName().toStdString();
   return "vst";
}

void VSTPlugin::SetVST(string vstName)
{
   mModuleSaveData.SetString("vst", vstName);
   string path = VSTLookup::GetVSTPath(vstName);
   
   if (mPlugin != nullptr && mPlugin->getPluginDescription().fileOrIdentifier == path)
      return;  //this VST is already loaded! we're all set
   
   if (mPlugin != nullptr && mWindow != nullptr)
   {
      VSTWindow* window = mWindow.release();
      delete window;
      //delete mWindowOverlay;
      //mWindowOverlay = nullptr;
   }
   
   juce::PluginDescription desc;
   desc.fileOrIdentifier = path;
   desc.uid = 0;
   
   juce::String errorMessage;
   mVSTMutex.lock();
   for (int i=0; i<mFormatManager.getNumFormats(); ++i)
   {
      if (mFormatManager.getFormat(i)->fileMightContainThisPluginType(path))
         mPlugin = mFormatManager.getFormat(i)->createInstanceFromDescription(desc, gSampleRate, gBufferSize);
   }
   if (mPlugin != nullptr)
   {
      mPlugin->prepareToPlay(gSampleRate, gBufferSize);
      mPlugin->setPlayHead(&mPlayhead);
      mNumInputs = CLAMP(mPlugin->getTotalNumInputChannels(), 1, 4);
      mNumOutputs = CLAMP(mPlugin->getTotalNumOutputChannels(), 1, 4);
      ofLog() << "vst inputs: " << mNumInputs << "  vst outputs: " << mNumOutputs;
   }
   mVSTMutex.unlock();
   
   if (mPlugin != nullptr)
      CreateParameterSliders();
}

void VSTPlugin::CreateParameterSliders()
{
   assert(mPlugin);
   
   for (auto& slider : mParameterSliders)
   {
      slider.mSlider->SetShowing(false);
      RemoveUIControl(slider.mSlider);
      slider.mSlider->Delete();
   }
   mParameterSliders.clear();
   
   if (mPlugin->getNumParameters() <= 100)
   {
      mParameterSliders.resize(mPlugin->getNumParameters());
      for (int i=0; i<mPlugin->getNumParameters(); ++i)
      {
         mParameterSliders[i].mValue = mPlugin->getParameter(i);
         juce::String name = mPlugin->getParameterName(i);
         string label(name.getCharPointer());
         try
         {
            int append = 0;
            while (FindUIControl(label.c_str()))
            {
               ++append;
               label = name.toStdString() + ofToString(append);
            }
         }
         catch(UnknownUIControlException& e)
         {
            
         }
         mParameterSliders[i].mSlider = new FloatSlider(this, label.c_str(), 3, 35, 200, 15, &mParameterSliders[i].mValue, 0, 1);
         if (i > 0)
         {
            const int kRows = 20;
            if (i % kRows == 0)
               mParameterSliders[i].mSlider->PositionTo(mParameterSliders[i-kRows].mSlider, kAnchor_Right);
            else
               mParameterSliders[i].mSlider->PositionTo(mParameterSliders[i-1].mSlider, kAnchor_Below);
         }
         mParameterSliders[i].mParameterIndex = i;
      }
   }
}

void VSTPlugin::Poll()
{
   if (mDisplayMode == kDisplayMode_Sliders)
   {
      for (int i=0; i<mParameterSliders.size(); ++i)
      {
         mParameterSliders[i].mValue = mPlugin->getParameter(mParameterSliders[i].mParameterIndex);
      }
   }
}

void VSTPlugin::Process(double time)
{
   Profiler profiler("VSTPlugin");
   
   int inputChannels = MAX(2, mNumInputs);
   GetBuffer()->SetNumActiveChannels(inputChannels);
   
   ComputeSliders(0);
   SyncBuffers();
   
   int bufferSize = GetBuffer()->BufferSize();
   assert(bufferSize == gBufferSize);
   
   juce::AudioBuffer<float> buffer(inputChannels, bufferSize);
   for (int i=0; i<inputChannels; ++i)
      buffer.copyFrom(i, 0, GetBuffer()->GetChannel(MIN(i,GetBuffer()->NumActiveChannels()-1)), GetBuffer()->BufferSize());
   
   if (mEnabled && mPlugin != nullptr)
   {
      mVSTMutex.lock();
      {
         const juce::ScopedLock lock(mMidiInputLock);
         
         for (int i=0; i<mChannelModulations.size(); ++i)
         {
            ChannelModulations& mod = mChannelModulations[i];
            int channel = i + 1;
            if (i == kGlobalModulationIdx)
               channel = 1;
            
            if (mUseVoiceAsChannel == false)
               channel = mChannel;
            
            float bend = mod.mModulation.pitchBend ? mod.mModulation.pitchBend->GetValue(0) : 0;
            if (bend != mod.mLastPitchBend)
            {
               mod.mLastPitchBend = bend;
               mMidiBuffer.addEvent(juce::MidiMessage::pitchWheel(channel, (int)ofMap(bend,-mPitchBendRange,mPitchBendRange,0,16383,K(clamp))), 0);
            }
            float modWheel = mod.mModulation.modWheel ? mod.mModulation.modWheel->GetValue(0) : 0;
            if (modWheel != mod.mLastModWheel)
            {
               mod.mLastModWheel = modWheel;
               mMidiBuffer.addEvent(juce::MidiMessage::controllerEvent(channel, mModwheelCC, ofClamp(modWheel * 127,0,127)), 0);
            }
            float pressure = mod.mModulation.pressure ? mod.mModulation.pressure->GetValue(0) : 0;
            if (pressure != mod.mLastPressure)
            {
               mod.mLastPressure = pressure;
               mMidiBuffer.addEvent(juce::MidiMessage::channelPressureChange(channel, ofClamp(pressure*127,0,127)), 0);
            }
         }
         
         mPlugin->processBlock(buffer, mMidiBuffer);
         
         mMidiBuffer.clear();
      }
      mVSTMutex.unlock();
   
      GetBuffer()->Clear();
      for (int ch=0; ch < buffer.getNumChannels(); ++ch)
      {
         int outputChannel = MIN(ch,GetBuffer()->NumActiveChannels()-1);
         for (int sampleIndex=0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
         {
            GetBuffer()->GetChannel(outputChannel)[sampleIndex] += buffer.getSample(ch, sampleIndex) * mVol;
         }
         if (GetTarget())
            Add(GetTarget()->GetBuffer()->GetChannel(outputChannel), GetBuffer()->GetChannel(outputChannel), bufferSize);
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(outputChannel), bufferSize, outputChannel);
      }
   }
   else
   {
      //bypass
      for (int ch=0; ch<GetBuffer()->NumActiveChannels(); ++ch)
      {
         if (GetTarget())
            Add(GetTarget()->GetBuffer()->GetChannel(ch), GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize());
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch),GetBuffer()->BufferSize(), ch);
      }
   }

   GetBuffer()->Clear();
}

void VSTPlugin::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   if (mPlugin == nullptr)
      return;
   
   if (pitch < 0 || pitch > 127)
      return;
   
   int channel = voiceIdx + 1;
   if (voiceIdx == -1)
      channel = 1;
   
   const juce::ScopedLock lock(mMidiInputLock);
   
   if (velocity > 0)
   {
      mMidiBuffer.addEvent(juce::MidiMessage::noteOn(mUseVoiceAsChannel ? channel : mChannel, pitch, (uint8)velocity), 0);
      //ofLog() << "+ vst note on: " << (mUseVoiceAsChannel ? channel : mChannel) << " " << pitch << " " << (uint8)velocity;
   }
   else
   {
      mMidiBuffer.addEvent(juce::MidiMessage::noteOff(mUseVoiceAsChannel ? channel : mChannel, pitch), 0);
      //ofLog() << "- vst note off: " << (mUseVoiceAsChannel ? channel : mChannel) << " " << pitch;
   }
   
   int modIdx = voiceIdx;
   if (voiceIdx == -1)
      modIdx = kGlobalModulationIdx;
   
   mChannelModulations[modIdx].mModulation = modulation;
}

void VSTPlugin::SendCC(int control, int value, int voiceIdx /*=-1*/)
{
   if (mPlugin == nullptr)
      return;
   
   if (control < 0 || control > 127)
      return;
   
   int channel = voiceIdx + 1;
   if (voiceIdx == -1)
      channel = 1;
   
   const juce::ScopedLock lock(mMidiInputLock);
   
   mMidiBuffer.addEvent(juce::MidiMessage::controllerEvent((mUseVoiceAsChannel ? channel : mChannel), control, (uint8)value), 0);
}

void VSTPlugin::SetEnabled(bool enabled)
{
   mEnabled = enabled;
}

void VSTPlugin::PreDrawModule()
{
   /*if (mDisplayMode == kDisplayMode_PluginOverlay && mWindowOverlay)
   {
      mWindowOverlay->GetDimensions(mOverlayWidth, mOverlayHeight);
      if (mWindow)
      {
         mOverlayWidth = 500;
         mOverlayHeight = 500;
         float contentMult = gDrawScale;
         float width = mOverlayWidth * contentMult;
         float height = mOverlayHeight * contentMult;
         mWindow->setSize(width, height);
      }
      mWindowOverlay->UpdatePosition(this);
   }*/
}

void VSTPlugin::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;
   
   if (mPlugin)
      DrawText(mPlugin->getName().toStdString(), 3, 32);
   else
      DrawText("no plugin loaded", 3, 32);
   
   mVolSlider->Draw();
   mProgramChangeSelector->Draw();
   mOpenEditorButton->Draw();
   
   if (mDisplayMode == kDisplayMode_Sliders)
   {
      for (auto& slider : mParameterSliders)
      {
         if (slider.mSlider)
            slider.mSlider->Draw();
      }
   }
}

void VSTPlugin::GetModuleDimensions(int& width, int& height)
{
   if (mDisplayMode == kDisplayMode_PluginOverlay)
   {
      /*if (mWindowOverlay)
      {
         width = mOverlayWidth;
         height = mOverlayHeight+20;
      }
      else
      {*/
         width = 206;
         height = 40;
      //}
   }
   else
   {
      width = 206;
      height = 40;
      for (auto slider : mParameterSliders)
      {
         if (slider.mSlider)
         {
            width = MAX(width, slider.mSlider->GetRect(true).x + slider.mSlider->GetRect(true).width + 3);
            height = MAX(height, slider.mSlider->GetRect(true).y + slider.mSlider->GetRect(true).height + 3);
         }
      }
   }
}

void VSTPlugin::OnVSTWindowClosed()
{
   mWindow.release();
}

void VSTPlugin::DropdownUpdated(DropdownList* list, int oldVal)
{
   if (list == mProgramChangeSelector)
   {
      mMidiBuffer.addEvent(juce::MidiMessage::programChange(1, mProgramChange), 0);
   }
}

void VSTPlugin::FloatSliderUpdated(FloatSlider* slider, float oldVal)
{
   for (int i=0; i<mParameterSliders.size(); ++i)
   {
      if (mParameterSliders[i].mSlider == slider)
      {
         mPlugin->setParameter(mParameterSliders[i].mParameterIndex, mParameterSliders[i].mValue);
      }
   }
}

void VSTPlugin::IntSliderUpdated(IntSlider* slider, int oldVal)
{
   
}

void VSTPlugin::CheckboxUpdated(Checkbox* checkbox)
{
}

void VSTPlugin::ButtonClicked(ClickButton* button)
{
   if (button == mOpenEditorButton)
   {
      if (mPlugin != nullptr)
      {
         if (mWindow == nullptr)
            mWindow = VSTWindow::CreateWindow(this, VSTWindow::Normal);
         mWindow->toFront (true);
      }
      
      //if (mWindow->GetNSViewComponent())
      //   mWindowOverlay = new NSWindowOverlay(mWindow->GetNSViewComponent()->getView());
   }
}

void VSTPlugin::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("vst", moduleInfo, "", VSTLookup::FillVSTList);
   
   mModuleSaveData.LoadString("target", moduleInfo);
   
   mModuleSaveData.LoadInt("channel",moduleInfo,1,0,16);
   mModuleSaveData.LoadBool("usevoiceaschannel", moduleInfo, false);
   mModuleSaveData.LoadFloat("pitchbendrange",moduleInfo,2,1,24,K(isTextField));
   mModuleSaveData.LoadInt("modwheelcc(1or74)",moduleInfo,1,0,127,K(isTextField));
   
   SetUpFromSaveData();
}

void VSTPlugin::SetUpFromSaveData()
{
   string vstName = mModuleSaveData.GetString("vst");
   if (vstName != "")
      SetVST(vstName);
   
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   
   mChannel = mModuleSaveData.GetInt("channel");
   mUseVoiceAsChannel = mModuleSaveData.GetBool("usevoiceaschannel");
   mPitchBendRange = mModuleSaveData.GetFloat("pitchbendrange");
   mModwheelCC = mModuleSaveData.GetInt("modwheelcc(1or74)");
}

namespace
{
   const int kSaveStateRev = 0;
}

void VSTPlugin::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   
   out << kSaveStateRev;
   
   if (mPlugin)
   {
      out << true;
      juce::MemoryBlock vstState;
      mPlugin->getStateInformation(vstState);
      out << (int)vstState.getSize();
      out.WriteGeneric(vstState.getData(), vstState.getSize());
   }
   else
   {
      out << false;
   }
}

void VSTPlugin::LoadState(FileStreamIn& in)
{
   IDrawableModule::LoadState(in);
   
   int rev;
   in >> rev;
   LoadStateValidate(rev == kSaveStateRev);
   
   bool hasPlugin;
   in >> hasPlugin;
   if (hasPlugin)
   {
      assert(mPlugin != nullptr);
      int size;
      in >> size;
      char* data = new char[size];
      in.ReadGeneric(data, size);
      mPlugin->setStateInformation(data, size);
   }
}
