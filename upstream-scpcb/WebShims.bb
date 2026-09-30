;WebShims.bb - stubs for native/platform features unavailable (or intentionally disabled) on the web build.
;Steam and Discord Rich Presence are both gated off via defaults.ini ("enable steam"=0, "enable discord rich presence"=0),
;but blitzcc still needs real symbols for every call site since the gating is a runtime check, not a compile-time one.

Function Steam_Init%()
	Return 1
End Function

Function Steam_RestartAppIfNecessary%(appID%)
	Return 0
End Function

Function Steam_Update()
End Function

Function Steam_Shutdown()
End Function

Function Steam_Achieve%(ID$)
	Return 0
End Function

Function Steam_GetGameLanguage$()
	Return "english"
End Function

Function Steam_GetOverlayState%()
	Return 0
End Function

Function Steam_SetOverlayNotificationPosition(pos%)
End Function

Function Steam_SetRichPresence%(key$, value$)
	Return 0
End Function

Function Steam_PublishItem(name$, desc$, path$, imgPath$)
End Function

Function Steam_UpdateItem(fileid$, name$, desc$, path$, imgPath$, changelog$, updateTags%)
End Function

Function Steam_QueryUpdateItemStatus%()
	Return 0
End Function

Function Steam_ClearItemTags()
End Function

Function Steam_AddItemTag(tag$)
End Function

Function Steam_GetPublishedItemID$()
	Return ""
End Function

Function Steam_LoadSubscribedItems()
End Function

Function Steam_GetSubscribedItemCount%()
	Return 0
End Function

Function Steam_GetSubscribedItemID$(id%)
	Return ""
End Function

Function Steam_GetSubscribedItemPath$(id%)
	Return ""
End Function

Function Steam_OpenOnScreenKeyboard(mode%, x%, y%, width%, height%)
End Function

Function Steam_CloseOnScreenKeyboard()
End Function

Function BlitzcordCreateCore%(id$)
	Return 1
End Function

Function BlitzcordRunCallbacks()
End Function

Function BlitzcordSetActivityDetails(details$)
End Function

Function BlitzcordClearActivity()
End Function

Function BlitzcordUpdateActivity()
End Function

Function BlitzcordSetSmallImage(image$)
End Function

Function BlitzcordSetSmallText(text$)
End Function

Function BlitzcordSetLargeImage(image$)
End Function

Function BlitzcordSetLargeText(text$)
End Function

Function BlitzcordSetTimestampStart(timestamp$)
End Function

Function BlitzcordGetCurrentTimestamp$()
	Return "0"
End Function

Function api_GetActiveWindow%()
	Return 1
End Function

Function api_GetFocus%()
	Return 1
End Function

Function api_MessageBox%(hwnd%, lpText$, lpCaption$, wType%)
	DebugLog "[MessageBox] " + lpCaption$ + ": " + lpText$
	Return 0
End Function
