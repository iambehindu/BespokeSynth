/*
  ==============================================================================

    ChordDisplayer.cpp
    Created: 27 Mar 2018 9:23:27pm
    Author:  Ryan Challinor

  ==============================================================================
*/

#include "ChordDisplayer.h"
#include "SynthGlobals.h"
#include "Scale.h"

ChordDisplayer::ChordDisplayer()
{
}

void ChordDisplayer::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;
   
   list<int> notes = mNoteOutput.GetHeldNotes();
   
   if (notes.size() > 2)
   {
      std::vector<int> chord{ std::begin(notes), std::end(notes) };
      DrawText(TheScale->GetChordDatabase().GetChordName(chord), 4, 14);
   }
}

void ChordDisplayer::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   PlayNoteOutput(time, pitch, velocity, voiceIdx, modulation);
}

void ChordDisplayer::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   
   SetUpFromSaveData();
}

void ChordDisplayer::SetUpFromSaveData()
{
   SetUpPatchCables(mModuleSaveData.GetString("target"));
}
