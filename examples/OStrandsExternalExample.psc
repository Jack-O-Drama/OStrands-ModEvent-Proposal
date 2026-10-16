Scriptname OStrandsExternalExample extends ReferenceAlias
; Minimal prototype caller, GPL-3.0.
; Attach to a forced PlayerRef alias in a running test quest.
; Uses the proposed EBF_OStrands ModEvent interface.
; Illustrative example; not game-tested.

Bool _requestEnabled = False
Float _interval = 4.0

Event OnInit()
    InitializeClient()
EndEvent

Event OnPlayerLoadGame()
    InitializeClient()
EndEvent

Function InitializeClient()
    ; The native request is not saved. Default this example to off after load.
    _requestEnabled = False
    UnregisterForUpdate()
    RegisterForModEvent("EBF_OStrands_Status", "OnExternalStatus")
    Ping()
EndFunction

Function Ping()
    Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "ping", 0.0)
EndFunction

Function StartExternal(Float intervalSeconds = 4.0)
    If intervalSeconds <= 0.0
        StopExternal()
        Return
    EndIf
    _interval = intervalSeconds
    _requestEnabled = True
    UnregisterForUpdate()
    Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "start", _interval)
    RegisterForSingleUpdate(2.0)
EndFunction

Function StopExternal()
    _requestEnabled = False
    UnregisterForUpdate()
    Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "stop", 0.0)
EndFunction

Event OnUpdate()
    If _requestEnabled
        ; A real controller re-evaluates arousal/clothing here before renewing.
        Game.GetPlayer().SendModEvent("EBF_OStrands_Request", "start", _interval)
        RegisterForSingleUpdate(2.0)
    EndIf
EndEvent

Event OnExternalStatus(String eventName, String status, Float apiVersion, Form sender)
    If sender != Game.GetPlayer()
        Return
    EndIf
    Debug.Trace("[OStrands example] API=" + apiVersion + " status=" + status)
EndEvent
