Scriptname TTBL_MainController extends Quest

Event OnInit()
    Maintenance()
EndEvent

Function Maintenance()
    TTBL_JData.ImportStaticData()
    TTBL_JData.CheckHasTng()
    TTBL_Decorators.RegisterDecorators()
EndFunction

 
