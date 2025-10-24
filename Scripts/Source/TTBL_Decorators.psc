scriptname TTBL_Decorators

;==========================================================================
; Decorator Registration
;==========================================================================

; Registers all template decorators with the SkyrimNet API
; These decorators provide dynamic data for use in LLM templates
Function RegisterDecorators() global
    SkyrimNetApi.RegisterDecorator("ttbl_get_body_desc", "TTBL_Decorators", "GetBodyDescription")
EndFunction

string Function GetBodyDescription(Actor npc) global
    string description
    string clothed = TTBL_JData.GetClothedBodyDesc(GetPresetName(npc), GetWeightKey(npc))
    string naked = TTBL_JData.GetNakedBodyDesc(GetPresetName(npc), GetWeightKey(npc))
    return "{\"isnaked\": "+isNaked(npc)+",\"clothed\": \"" + clothed + "\", \"naked\": \"" + naked + "\", \"tngdesc\": \""+GetTngInfo(npc)+"\"}"
EndFunction

string Function GetPresetName(Actor npc) global
    string obodyActorPresetKey = "obody_" + npc.GetFormID() + "_preset"
	return StorageUtil.GetStringValue(none, obodyActorPresetKey, missing = "")
EndFunction

string Function GetWeightKey(Actor npc) global
    float weight = npc.GetActorBase().GetWeight()
    string weightKey

    if(weight <= 33)
        weightKey = "0"
    elseif(weight >= 66)
        weightKey = "100"
    else
        weightKey = "50"
    endif

    return weightKey
EndFunction

string Function GetTngInfo(actor npc) global
    if(!TTBL_JData.HasTng() || !IsTngRevealing(npc))
        return ""
    endif
	
    Armor addon = TNG_PapyrusUtil.GetActorAddon(npc)
    if(addon == none)
        return ""
    endif
    
    int size = TNG_PapyrusUtil.GetActorSize(npc)        
    string sizeDesc = ""

    if(size == 0)
        sizeDesc = "tiny"
    elseif(size == 1)
        sizeDesc = "small"
    elseif(size == 2)
        sizeDesc = "average"
    elseif(size == 3)
        sizeDesc = "large"
    elseif(size == 4)
        sizeDesc = "enormous"
    endif

    string addonDesc = TTBL_JData.GetTngDescription(addon)

    string res = "cock is "+sizeDesc
    if(addonDesc)
        res += " with a " + addonDesc + " appearance."
    else
        res += "."
    endif

    return res
EndFunction

bool Function IsNaked(Actor npc) global
    return npc.GetWornForm(0x4) == none
EndFunction

bool Function IsTngRevealing(Actor npc) global
    Form arm = npc.GetWornForm(0x4)
    return IsNaked(npc) || arm.HasKeywordString("TNG_Revealing")
EndFunction