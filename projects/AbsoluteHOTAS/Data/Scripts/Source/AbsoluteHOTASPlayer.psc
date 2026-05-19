Scriptname AbsoluteHOTASPlayer extends Quest

bool initialized = false
bool isPiloting = false
int stateTimerId = 6001
float statePollSeconds = 0.5
GlobalVariable Property AbsoluteHOTAS_IsPiloting Auto Const

Event OnQuestInit()
    InitializeAbsoluteHOTAS("OnQuestInit")
EndEvent

Event OnQuestStarted()
    InitializeAbsoluteHOTAS("OnQuestStarted")
EndEvent

Function InitializeAbsoluteHOTAS(string sourceEvent)
    if initialized
        Debug.Trace("AbsoluteHOTAS: pilot-state quest already initialized from " + sourceEvent)
        Debug.TraceUser("AbsoluteHOTAS", "QUEST_ALREADY_READY source=" + sourceEvent)
        return
    endif

    initialized = true
    Actor player = Game.GetPlayer()
    RegisterForRemoteEvent(player, "OnSit")
    RegisterForRemoteEvent(player, "OnGetUp")
    Debug.OpenUserLog("AbsoluteHOTAS")
    Debug.TraceUser("AbsoluteHOTAS", "QUEST_READY source=" + sourceEvent)
    Debug.Trace("AbsoluteHOTAS: pilot-state quest initialized from " + sourceEvent)
    EvaluatePilotState(sourceEvent, true)
    StartTimer(statePollSeconds, stateTimerId)
EndFunction

Event Actor.OnSit(Actor akSender, ObjectReference akFurniture)
    if akSender != Game.GetPlayer()
        return
    endif

    EvaluatePilotState("OnSit")
EndEvent

Event Actor.OnGetUp(Actor akSender, ObjectReference akFurniture)
    if akSender != Game.GetPlayer()
        return
    endif

    EvaluatePilotState("OnGetUp")
EndEvent

Event OnTimer(int aiTimerID)
    if aiTimerID == stateTimerId
        EvaluatePilotState("OnTimer", false)
        StartTimer(statePollSeconds, stateTimerId)
    endif
EndEvent

Function EvaluatePilotState(string sourceEvent, bool forceLog = false)
    Actor player = Game.GetPlayer()
    ObjectReference seatRef = player.GetFurnitureUsing()
    int sitState = player.GetSitState()
    SpaceshipReference pilotedShip = player.GetSpaceship()
    bool desiredPiloting = pilotedShip != None

    if desiredPiloting == isPiloting && !forceLog
        return
    endif

    isPiloting = desiredPiloting
    if isPiloting
        Debug.TraceUser("AbsoluteHOTAS", "NATIVE_CALL_BEGIN SetPilotStateTrue source=" + sourceEvent)
        AbsoluteHOTAS.SetPilotStateTrue()
        Debug.TraceUser("AbsoluteHOTAS", "NATIVE_CALL_END SetPilotStateTrue source=" + sourceEvent)
    else
        Debug.TraceUser("AbsoluteHOTAS", "NATIVE_CALL_BEGIN SetPilotStateFalse source=" + sourceEvent)
        AbsoluteHOTAS.SetPilotStateFalse()
        Debug.TraceUser("AbsoluteHOTAS", "NATIVE_CALL_END SetPilotStateFalse source=" + sourceEvent)
    endif

    if AbsoluteHOTAS_IsPiloting != None
        if isPiloting
            AbsoluteHOTAS_IsPiloting.SetValue(1.0)
        else
            AbsoluteHOTAS_IsPiloting.SetValue(0.0)
        endif
    endif

    if isPiloting
        Debug.Trace("AbsoluteHOTAS PILOT_STATE=1 source=" + sourceEvent + " sitState=" + sitState + " furniture=" + seatRef + " ship=" + pilotedShip)
        Debug.TraceUser("AbsoluteHOTAS", "PILOT_STATE=1 source=" + sourceEvent + " sitState=" + sitState + " furniture=" + seatRef + " ship=" + pilotedShip)
    else
        Debug.Trace("AbsoluteHOTAS PILOT_STATE=0 source=" + sourceEvent + " sitState=" + sitState + " furniture=" + seatRef)
        Debug.TraceUser("AbsoluteHOTAS", "PILOT_STATE=0 source=" + sourceEvent + " sitState=" + sitState + " furniture=" + seatRef)
    endif
EndFunction
