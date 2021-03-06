//
//  NoteCanvas.cpp
//  Bespoke
//
//  Created by Ryan Challinor on 12/30/14.
//
//

#include "NoteCanvas.h"
#include "IAudioSource.h"
#include "SynthGlobals.h"
#include "DrumPlayer.h"
#include "ModularSynth.h"
#include "CanvasControls.h"
#include "Scale.h"
#include "CanvasElement.h"
#include "Profiler.h"
#include "PolyphonyMgr.h"

NoteCanvas::NoteCanvas()
: mCanvas(nullptr)
, mCanvasControls(nullptr)
, mNumMeasuresSlider(nullptr)
, mNumMeasures(1)
, mQuantizeButton(nullptr)
, mPlay(true)
, mPlayCheckbox(nullptr)
, mRecord(false)
, mRecordCheckbox(nullptr)
, mInterval(kInterval_8n)
, mIntervalSelector(nullptr)
, mScrollPartial(0)
, mFreeRecordCheckbox(nullptr)
, mFreeRecord(false)
, mFreeRecordStartMeasure(0)
, mClipButton(nullptr)
{
   TheTransport->AddAudioPoller(this);
   SetEnabled(true);
   bzero(mInputNotes, 128*sizeof(NoteCanvasElement*));
   bzero(mCurrentNotes, 128*sizeof(NoteCanvasElement*));
   mVoiceModulations.resize(kNumVoices+1);
}

void NoteCanvas::CreateUIControls()
{
   IDrawableModule::CreateUIControls();
   
   mQuantizeButton = new ClickButton(this,"quantize",160,5);
   mClipButton = new ClickButton(this,"clip",220,5);
   mPlayCheckbox = new Checkbox(this,"play",5,5,&mPlay);
   mRecordCheckbox = new Checkbox(this,"rec",50,5,&mRecord);
   mFreeRecordCheckbox = new Checkbox(this,"free rec",90,5,&mFreeRecord);
   mNumMeasuresSlider = new IntSlider(this,"measures",5,25,100,15,&mNumMeasures,1,16);
   
   mIntervalSelector = new DropdownList(this,"interval",110,25,(int*)(&mInterval));
   mIntervalSelector->AddLabel("4n", kInterval_4n);
   mIntervalSelector->AddLabel("4nt", kInterval_4nt);
   mIntervalSelector->AddLabel("8n", kInterval_8n);
   mIntervalSelector->AddLabel("8nt", kInterval_8nt);
   mIntervalSelector->AddLabel("16n", kInterval_16n);
   mIntervalSelector->AddLabel("16nt", kInterval_16nt);
   mIntervalSelector->AddLabel("32n", kInterval_32n);
   mIntervalSelector->AddLabel("64n", kInterval_64n);
   
   mCanvas = new Canvas(this, 5, 45, 390, 100, L(length,1), L(rows,128), L(cols,16), &(NoteCanvasElement::Create));
   AddUIControl(mCanvas);
   mCanvas->SetNumVisibleRows(16);
   mCanvas->SetRowOffset(60);
   mCanvasControls = new CanvasControls();
   mCanvasControls->SetCanvas(mCanvas);
   mCanvasControls->CreateUIControls();
   AddChild(mCanvasControls);
   UpdateNumColumns();
   
   mCanvas->SetListener(this);
}

NoteCanvas::~NoteCanvas()
{
   mCanvas->SetListener(nullptr);
   TheTransport->RemoveAudioPoller(this);
}

void NoteCanvas::PlayNote(double time, int pitch, int velocity, int voiceIdx, ModulationParameters modulation)
{
   mNoteOutput.PlayNote(time, pitch, velocity, voiceIdx, modulation);
   
   if (!mEnabled || !mRecord)
      return;
   
   if (mInputNotes[pitch]) //handle note-offs or retriggers
   {
      float endPos = GetCurPos();
      if (mInputNotes[pitch]->GetStart() > endPos)
         endPos += 1; //wrap
      mInputNotes[pitch]->SetEnd(endPos);
      mInputNotes[pitch] = nullptr;
   }
   
   if (velocity > 0)
   {
      if (mFreeRecord && mFreeRecordStartMeasure == -1)
         mFreeRecordStartMeasure = TheTransport->GetMeasure();
      
      float canvasPos = GetCurPos() * mCanvas->GetNumCols();
      int col = int(canvasPos + .5f); //round off
      int row = mCanvas->GetNumRows()-pitch-1;;
      NoteCanvasElement* element = static_cast<NoteCanvasElement*>(mCanvas->CreateElement(col,row));
      mInputNotes[pitch] = element;
      element->mOffset = canvasPos - element->mCol; //the rounded off part
      element->mLength = .5f;
      element->SetVelocity(velocity / 127.0f);
      element->SetVoiceIdx(voiceIdx);
      int modIdx = voiceIdx;
      if (modIdx == -1)
         modIdx = kNumVoices;
      mVoiceModulations[modIdx] = modulation;
      mCanvas->AddElement(element);
      
      mCanvas->SetRowOffset(element->mRow - mCanvas->GetNumVisibleRows()/2);
   }
}

bool NoteCanvas::FreeRecordParityMatched()
{
   int currentMeasureParity = (TheTransport->GetMeasure() % (mNumMeasures*2)) / mNumMeasures;
   int recordStartMeasureParity = (mFreeRecordStartMeasure % (mNumMeasures*2)) / mNumMeasures;
   return currentMeasureParity == recordStartMeasureParity;
}

void NoteCanvas::OnTransportAdvanced(float amount)
{
   Profiler profiler("NoteCanvas");
   
   if (mFreeRecord && mFreeRecordStartMeasure != -1)
   {
      if (TheTransport->GetMeasurePos() < amount &&
          !FreeRecordParityMatched())
      {
         int oldNumMeasures = mNumMeasures;
         while (!FreeRecordParityMatched())
            mNumMeasures *= 2;
         int shift = mFreeRecordStartMeasure % mNumMeasures - mFreeRecordStartMeasure % oldNumMeasures;
         SetNumMeasures(mNumMeasures);
         
         for (auto* element : mCanvas->GetElements())
            element->SetStart(element->GetStart() + float(shift) / mNumMeasures);
      }
   }
   
   if (!mEnabled || !mPlay)
   {
      mCanvas->SetCursorPos(-1);
      return;
   }

   float curPos = GetCurPos();
   mCanvas->SetCursorPos(curPos);
   
   Canvas::ElementMask curElements = mCanvas->GetElementMask(curPos);
   
   for (int i=0; i<MAX_CANVAS_MASK_ELEMENTS; ++i)
   {
      int pitch = MAX_CANVAS_MASK_ELEMENTS - i - 1;
      bool wasOn = mLastElements.GetBit(i) || mInputNotes[pitch];
      bool nowOn = curElements.GetBit(i) || mInputNotes[pitch];
      if (wasOn && !nowOn)
      {
         //note off
         if (mCurrentNotes[pitch])
         {
            mNoteOutput.PlayNote(gTime, pitch, 0, mCurrentNotes[pitch]->GetVoiceIdx());
            mCurrentNotes[pitch] = nullptr;
         }
      }
      if (nowOn && !wasOn)
      {
         //note on
         NoteCanvasElement* note = ((NoteCanvasElement*)mCanvas->GetElementAt(curPos, i));
         assert(note);
         mCurrentNotes[pitch] = note;
         mNoteOutput.PlayNote(gTime, pitch, note->GetVelocity()*127, note->GetVoiceIdx(), ModulationParameters(note->GetPitchBend(), note->GetModWheel(), note->GetPressure(), note->GetPan()));
      }
   }
   
   for (int pitch=0; pitch<128; ++pitch)
   {
      if (mInputNotes[pitch])
      {
         float endPos = curPos;
         if (mInputNotes[pitch]->GetStart() > endPos)
            endPos += 1; //wrap
         mInputNotes[pitch]->SetEnd(endPos);
         
         int modIdx = mInputNotes[pitch]->GetVoiceIdx();
         if (modIdx == -1)
            modIdx = kNumVoices;
         float bend = 0;
         float mod = 0;
         float pressure = 0;
         if (mVoiceModulations[modIdx].pitchBend)
            bend = mVoiceModulations[modIdx].pitchBend->GetValue(0);
         if (mVoiceModulations[modIdx].modWheel)
            mod = mVoiceModulations[modIdx].modWheel->GetValue(0);
         if (mVoiceModulations[modIdx].pressure)
            pressure = mVoiceModulations[modIdx].pressure->GetValue(0);
         mInputNotes[pitch]->WriteModulation(curPos, bend, mod, pressure, mVoiceModulations[modIdx].pan);
      }
      else if (mCurrentNotes[pitch])
      {
         mCurrentNotes[pitch]->UpdateModulation(curPos);
      }
   }
   
   mLastElements = curElements;
}

float NoteCanvas::GetCurPos() const
{
   return ((TheTransport->GetMeasure() % mNumMeasures) + TheTransport->GetMeasurePos()) / mNumMeasures;
}

void NoteCanvas::UpdateNumColumns()
{
   mCanvas->RescaleNumCols(TheTransport->CountInStandardMeasure(mInterval) * mNumMeasures);
   if (mInterval < kInterval_8n)
      mCanvas->SetMajorColumnInterval(TheTransport->CountInStandardMeasure(mInterval));
   else
      mCanvas->SetMajorColumnInterval(TheTransport->CountInStandardMeasure(mInterval) / 4);
}

void NoteCanvas::CanvasUpdated(Canvas* canvas)
{
   if (canvas == mCanvas)
   {
      
   }
}

void NoteCanvas::DrawModule()
{

   if (Minimized() || IsVisible() == false)
      return;
   
   ofPushStyle();
   ofFill();
   for (int i=0;i<mCanvas->GetNumVisibleRows();++i)
   {
      int pitch = mCanvas->GetNumRows()-mCanvas->GetRowOffset()+mCanvas->GetNumVisibleRows()-i-1;
      if (pitch%TheScale->GetTet() == TheScale->ScaleRoot()%TheScale->GetTet())
         ofSetColor(0,255,0,80);
      else if (pitch%TheScale->GetTet() == (TheScale->ScaleRoot()+7)%TheScale->GetTet())
         ofSetColor(200,150,0,80);
      else if (TheScale->IsInScale(pitch))
         ofSetColor(100,75,0,80);
      else
         continue;
      
      float boxHeight = (float(mCanvas->GetGridHeight())/mCanvas->GetNumVisibleRows());
      float y = mCanvas->GetPosition(true).y + i*boxHeight;
      ofRect(mCanvas->GetPosition(true).x,y,mCanvas->GetGridWidth(),boxHeight);
   }
   ofPopStyle();
   
   mCanvas->Draw();
   mCanvasControls->Draw();
   mQuantizeButton->Draw();
   mClipButton->Draw();
   mPlayCheckbox->Draw();
   mRecordCheckbox->Draw();
   mFreeRecordCheckbox->Draw();
   mNumMeasuresSlider->Draw();
   mIntervalSelector->Draw();
   
   if (mRecord)
   {
      ofPushStyle();
      ofSetColor(205 + 50 * (cosf(TheTransport->GetMeasurePos() * 4 * FTWO_PI)), 0, 0);
      ofSetLineWidth(4);
      ofRect(mCanvas->GetPosition(true).x, mCanvas->GetPosition(true).y, mCanvas->GetWidth(), mCanvas->GetHeight());
      ofPopStyle();
   }
}

bool NoteCanvas::MouseScrolled(int x, int y, float scrollX, float scrollY)
{
   if (GetKeyModifiers() == kModifier_Shift)
   {
      int canvasX,canvasY;
      mCanvas->GetPosition(canvasX, canvasY, true);
      ofVec2f canvasPos = ofVec2f(ofMap(x, canvasX, canvasX+mCanvas->GetWidth(), 0, 1),
                                  ofMap(y, canvasY, canvasY+mCanvas->GetHeight(), 0, 1));
      if (IsInUnitBox(canvasPos))
      {
         float zoomCenter = ofLerp(mCanvas->mStart, mCanvas->mEnd, canvasPos.x);
         float distFromStart = zoomCenter - mCanvas->mStart;
         float distFromEnd = zoomCenter - mCanvas->mEnd;
         
         distFromStart *= 1 - scrollY/100;
         distFromEnd *= 1 - scrollY/100;
         
         float slideX = (mCanvas->mEnd - mCanvas->mStart) * -scrollX/300;
         
         mCanvas->mStart = ofClamp(zoomCenter - distFromStart + slideX, 0, mNumMeasures);
         mCanvas->mEnd = ofClamp(zoomCenter - distFromEnd + slideX, 0, mNumMeasures);
         ofLog() << mCanvas->mStart << " " << mCanvas->mEnd;
         return true;
      }
   }
   else
   {
      if (x >= mCanvas->GetPosition(true).x && y >= mCanvas->GetPosition(true).y &&
          x < mCanvas->GetPosition(true).x + mCanvas->GetWidth() && y < mCanvas->GetPosition(true).y + mCanvas->GetHeight())
      {
         mScrollPartial += -scrollY;
         int scrollWhole = int(mScrollPartial);
         mScrollPartial -= scrollWhole;
         mCanvas->SetRowOffset(mCanvas->GetRowOffset()+scrollWhole);
         return true;
      }
   }
   return false;
}

namespace
{
   const float extraW = 10;
   const float extraH = 140;
}

void NoteCanvas::Resize(float w, float h)
{
   w = MAX(w - extraW, 390);
   h = MAX(h - extraH, 40);
   mCanvas->SetDimensions(w, h);
}

void NoteCanvas::GetModuleDimensions(int& width, int& height)
{
   width = mCanvas->GetWidth() + extraW;
   height = mCanvas->GetHeight() + extraH;
}

void NoteCanvas::SetNumMeasures(int numMeasures)
{
   mNumMeasures = numMeasures;
   mCanvas->SetLength(mNumMeasures);
   mCanvas->SetNumCols(TheTransport->CountInStandardMeasure(mInterval) * mNumMeasures);
   if (mInterval < kInterval_8n)
      mCanvas->SetMajorColumnInterval(TheTransport->CountInStandardMeasure(mInterval));
   else
      mCanvas->SetMajorColumnInterval(TheTransport->CountInStandardMeasure(mInterval) / 4);
   mCanvas->mStart = 0;
   mCanvas->mEnd = mNumMeasures;
}

void NoteCanvas::SetRecording(bool rec)
{
   mRecord = rec;
   
   if (mRecord)
      mPlay = true;
   
   for (int pitch=0; pitch<128; ++pitch)
      mInputNotes[pitch] = nullptr;
}

void NoteCanvas::ClipNotes()
{
   bool anyHighlighted = false;
   float earliest = FLT_MAX;
   float latest = 0;
   vector<CanvasElement*> toDelete;
   for (auto* element : mCanvas->GetElements())
   {
      if (element->GetHighlighted())
      {
         anyHighlighted = true;
         if (element->GetStart() < earliest)
            earliest = element->GetStart();
         if (element->GetEnd() > latest)
            latest = element->GetEnd();
      }
      else
      {
         toDelete.push_back(element);
      }
   }

   if (anyHighlighted)
   {
      for (auto* remove : toDelete)
         mCanvas->RemoveElement(remove);
      
      int earliestMeasure = int(earliest * mNumMeasures);
      int latestMeasure = int(latest * mNumMeasures) + 1;
      int clipStart = 0;
      int clipEnd = mNumMeasures;
      
      while (earliestMeasure - clipStart >= (clipEnd - clipStart) / 2 ||
             clipEnd - latestMeasure >= (clipEnd - clipStart) / 2)
      {
         if (earliestMeasure - clipStart >= (clipEnd - clipStart) / 2)
            clipStart += (clipEnd - clipStart) / 2;
         if (clipEnd - latestMeasure >= (clipEnd - clipStart) / 2)
            clipEnd -= (clipEnd - clipStart) / 2;
      }
      
      SetNumMeasures(clipEnd - clipStart);
      
      ofLog() << earliest << " " << latest << " " << clipStart << " " << clipEnd;
      
      int shift = -clipStart;
      
      for (auto* element : mCanvas->GetElements())
         element->SetStart(element->GetStart() + float(shift) / mNumMeasures);
   }
}

void NoteCanvas::QuantizeNotes()
{
   bool anyHighlighted = false;
   for (auto* element : mCanvas->GetElements())
   {
      if (element->GetHighlighted())
      {
         anyHighlighted = true;
         break;
      }
   }
   for (auto* element : mCanvas->GetElements())
   {
      if (anyHighlighted == false || element->GetHighlighted())
      {
         element->mCol = int(element->mCol + element->mOffset + .5f) % mCanvas->GetNumCols();
         element->mOffset = 0;
      }
   }
}

void NoteCanvas::CheckboxUpdated(Checkbox* checkbox)
{
   if (checkbox == mEnabledCheckbox)
   {
      mNoteOutput.Flush();
      for (int pitch=0; pitch<128; ++pitch)
         mInputNotes[pitch] = nullptr;
   }
   if (checkbox == mPlayCheckbox)
   {
      if (!mPlay)
      {
         mRecord = false;
         mNoteOutput.Flush();
      }
   }
   if (checkbox == mRecordCheckbox)
   {
      SetRecording(mRecord);
   }
   if (checkbox == mFreeRecordCheckbox)
   {
      if (mFreeRecord)
      {
         SetRecording(true);
         mFreeRecordStartMeasure = -1;
      }
   }
}

void NoteCanvas::ButtonClicked(ClickButton* button)
{
   if (button == mQuantizeButton)
      QuantizeNotes();
   
   if (button == mClipButton)
      ClipNotes();
}

void NoteCanvas::FloatSliderUpdated(FloatSlider* slider, float oldVal)
{
}

void NoteCanvas::IntSliderUpdated(IntSlider* slider, int oldVal)
{
   if (slider == mNumMeasuresSlider)
   {
      SetNumMeasures(mNumMeasures);
   }
}

void NoteCanvas::DropdownUpdated(DropdownList* list, int oldVal)
{
   if (list == mIntervalSelector)
   {
      UpdateNumColumns();
   }
}

void NoteCanvas::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("canvaswidth", moduleInfo, 390, 390, 99999, K(isTextField));
   mModuleSaveData.LoadFloat("canvasheight", moduleInfo, 100, 40, 99999, K(isTextField));
   
   SetUpFromSaveData();
}

void NoteCanvas::SetUpFromSaveData()
{
   SetUpPatchCables(mModuleSaveData.GetString("target"));
   mCanvas->SetDimensions(mModuleSaveData.GetFloat("canvaswidth"), mModuleSaveData.GetFloat("canvasheight"));
}

void NoteCanvas::SaveLayout(ofxJSONElement& moduleInfo)
{
   IDrawableModule::SaveLayout(moduleInfo);
   
   moduleInfo["canvaswidth"] = mCanvas->GetWidth();
   moduleInfo["canvasheight"] = mCanvas->GetHeight();
}

void NoteCanvas::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   
   mCanvas->SaveState(out);
}

void NoteCanvas::LoadState(FileStreamIn& in)
{
   IDrawableModule::LoadState(in);
   
   mCanvas->LoadState(in);
}
