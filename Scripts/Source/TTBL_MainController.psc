Scriptname TTBL_MainController extends Quest

Event OnInit()
    Maintenance()
EndEvent

Function Maintenance()
    string[] tngRacialGroups = TNG_PapyrusUtil.GetRgNames()
    ; tng addon index is offset by 2 for mcm menu options
    int tngOffset = 2
    int i = 0
    Race playerRace = Game.GetPlayer().GetRace()
    
    while(i < tngRacialGroups.Length)
        string[] rgAddons = TNG_PapyrusUtil.GetRgAddons(i)
        int addonIndex = TNG_PapyrusUtil.GetRgAddon(i) + tngOffset
        string tngRaceName = Race.GetRace(tngRacialGroups[i]).GetName()
        StorageUtil.SetStringValue(none, "TTLB_TNG_RacialGroup_" + tngRaceName + "_Addon", rgAddons[addonIndex])
        StorageUtil.SetFloatValue(none, "TTLB_TNG_RacialGroup_" + tngRaceName + "_Multiplier", TNG_PapyrusUtil.GetRgMult(i))
        i += 1
    endwhile

    string[] tngAddonsM = TNG_PapyrusUtil.GetAllPossibleAddons(false)
    i = 0
    while(i < tngAddonsM.Length)
        StorageUtil.StringListAdd(none, "TTLB_TNG_MaleAddons", tngAddonsM[i], false)
        i += 1
    endwhile

    string[] tngAddonsF = TNG_PapyrusUtil.GetAllPossibleAddons(true)
    i = 0
    while(i < tngAddonsF.Length)
        StorageUtil.StringListAdd(none, "TTLB_TNG_FemaleAddons", tngAddonsF[i], false)
        i += 1
    endwhile
EndFunction

 
