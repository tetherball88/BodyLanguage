scriptname TTBL_JData

import TTBL_JCDomain

;/
  Returns the namespace key for MARAS data in JContainers.
/;
string Function GetNamespaceKey() global
    return ".TT_BodyLanguage"
EndFunction

;/
  Returns the static key for MARAS static data in JContainers.
/;
string Function GetStaticKey() global
    return GetNamespaceKey() + ".static"
EndFunction

;/
  Returns the key for initial data in JContainers.
/;
string Function GetInitialDataKey() global
    return GetStaticKey() + ".initialData"
EndFunction

;/ ==============================
   SECTION: Object Top level
============================== /;
;/
  Clears all MARAS data from JContainers.
/;
Function Clear() global
    JDB_solveObjSetter(GetNamespaceKey(), JMap_object())
    ImportStaticData()
EndFunction

;/
  Exports MARAS data to a JSON file.
/;
Function ExportData() global
    JValue_writeToFile(JDB_solveObj(GetNamespaceKey()), JContainers.userDirectory() + "BodyLanguage/store.json")
EndFunction

Function CheckHasTng() global
    int hasTng = 0
    if(Game.GetModByName("TheNewGentleman.esp") != 255)
        hasTng = 1
    endif
    JValue_solveIntSetter(JDB_solveObj(GetNamespaceKey()), ".hasTng", hasTng, true)
EndFunction

bool Function HasTng() global
    return JValue_solveInt(JDB_solveObj(GetNamespaceKey()), ".hasTng") == 1
EndFunction

;/
  Imports MARAS data from a JSON file.
/;
Function ImportData() global
    int jObj = JValue_readFromFile(JContainers.userDirectory() + "BodyLanguage/store.json")
    JDB_solveObjSetter(GetNamespaceKey(), jObj)
EndFunction

;/
  Imports static data (initialData.json) into JContainers.
/;
Function ImportStaticData() global
    int JRoot = JDB_solveObj(GetNamespaceKey())
    if(JRoot == 0)
        JDB_solveObjSetter(GetNamespaceKey(), JMap_object())
    endif
    int JBodyDescriptions = LoadMultipleFiles("Data/SKSE/Plugins/BodyLanguage/bodyDescriptions")
    JDB_solveObjSetter(GetStaticKey() + ".initialData.bodyDescriptions", JBodyDescriptions, true)

    int JTngDescriptions = LoadMultipleFiles("Data/SKSE/Plugins/BodyLanguage/tngDescriptions", true)
    JDB_solveObjSetter(GetStaticKey() + ".initialData.tngDescriptions", JTngDescriptions, true)
EndFunction

int Function LoadMultipleFiles(string folderPath, bool isFormMap = false) global
    int JTarget
    if(isFormMap)
        JTarget = JFormMap_object()
    else
        JTarget = JMap_object()
    endif
    int JFilesMap = JValue_readFromDirectory(folderPath)
    string nextKey = JMap_nextKey(JFilesMap)
    while(nextKey)
      if(isFormMap)
          JFormMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      else
          JMap_addPairs(JTarget, JMap_getObj(JFilesMap, nextKey), true)
      endif
          nextKey = JMap_nextKey(JFilesMap, nextKey)
    endwhile
    JValue_release(JFilesMap)
    return JTarget
EndFunction

string Function GetBodyDesc(string presetName, string weight, string type) global
    int JInitialData = JDB_solveObj(GetStaticKey() + ".initialData.bodyDescriptions")
    int jPreset = JMap_getObj(JInitialData, presetName)
    int jWeight = JMap_getObj(jPreset, weight)
    string desc = JMap_getStr(jWeight, type)
    return desc
EndFunction

string Function GetClothedBodyDesc(string presetName, string weight) global
    return GetBodyDesc(presetName, weight, "clothed")
EndFunction

string Function GetNakedBodyDesc(string presetName, string weight) global
    return GetBodyDesc(presetName, weight, "naked")
EndFunction

string Function GetTngDescription(Armor addon) global
    int JTngDescriptions = JDB_solveObj(GetStaticKey() + ".initialData.tngDescriptions")
    return JFormMap_getStr(JTngDescriptions, addon)
EndFunction