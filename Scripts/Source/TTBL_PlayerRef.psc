Scriptname TTBL_PlayerRef extends ReferenceAlias

Event OnPlayerLoadGame()
    MiscUtil.PrintConsole("!!!!!!!!!!!!!!!")
    TTBL_MainController mainController = self.GetOwningQuest() as TTBL_MainController
    mainController.Maintenance()
EndEvent
