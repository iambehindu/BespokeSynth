/*
  ==============================================================================

    Prefab.h
    Created: 25 Sep 2016 10:14:15am
    Author:  Ryan Challinor

  ==============================================================================
*/

#ifndef PREFAB_H_INCLUDED
#define PREFAB_H_INCLUDED

#include "IDrawableModule.h"
#include "ClickButton.h"
#include "ModuleContainer.h"

class PatchCableSource;

class Prefab : public IDrawableModule, public IButtonListener
{
public:
   Prefab();
   ~Prefab();
   static IDrawableModule* Create() { return new Prefab(); }
   
   string GetTitleLabel() override { return "prefab"; }
   void CreateUIControls() override;
   
   ModuleContainer* GetContainer() override { return &mModuleContainer; }
   
   void Poll() override;
   
   void ButtonClicked(ClickButton* button) override;
   
   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   
   //IPatchable
   void PostRepatch(PatchCableSource* cableSource) override;
   
private:
   //IDrawableModule
   void DrawModule() override;
   bool Enabled() const override { return mEnabled; }
   void GetModuleDimensions(int& width, int& height) override;
   void OnClicked(int x, int y, bool right) override;
   
   void SavePrefab(string savePath);
   void LoadPrefab(string loadPath);
   
   PatchCableSource* mModuleCable;
   ClickButton* mSaveButton;
   ClickButton* mLoadButton;
   ModuleContainer mModuleContainer;
};



#endif  // PREFAB_H_INCLUDED
