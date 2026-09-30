Type Difficulty
	Field name$, localName$
	Field description$
	Field permaDeath%
	Field aggressiveNPCs
	Field saveType%
	Field otherFactors%
	
	Field r%
	Field g%
	Field b%
	
	Field customizable%
End Type

Global Difficulties.Difficulty[4]

Global SelectedDifficulty.Difficulty

Const SAFE=0, EUCLID=1, KETER=2, CUSTOM=3

Const SAVEANYWHERE = 0, SAVEONQUIT=1, SAVEONSCREENS=2

Const EASY = 0, NORMAL = 1, HARD = 2

difficulties[SAFE] = New Difficulty
difficulties[SAFE]\name = "Safe"
difficulties[SAFE]\localName = I_Loc\Difficulty_Safe
difficulties[SAFE]\description = I_Loc\Difficulty_SafeDesc
difficulties[SAFE]\permaDeath = False
difficulties[SAFE]\aggressiveNPCs = False
difficulties[SAFE]\saveType = SAVEANYWHERE
difficulties[SAFE]\otherFactors = EASY
difficulties[SAFE]\r = 120
difficulties[SAFE]\g = 150
difficulties[SAFE]\b = 50

difficulties[EUCLID] = New Difficulty
difficulties[EUCLID]\name = "Euclid"
difficulties[EUCLID]\localName = I_Loc\Difficulty_Euclid
difficulties[EUCLID]\description = I_Loc\Difficulty_EuclidDesc
difficulties[EUCLID]\permaDeath = False
difficulties[EUCLID]\aggressiveNPCs = False
difficulties[EUCLID]\saveType = SAVEONSCREENS
difficulties[EUCLID]\otherFactors = NORMAL
difficulties[EUCLID]\r = 200
difficulties[EUCLID]\g = 200
difficulties[EUCLID]\b = 0

difficulties[KETER] = New Difficulty
difficulties[KETER]\name = "Keter"
difficulties[KETER]\localName = I_Loc\Difficulty_Keter
difficulties[KETER]\description = I_Loc\Difficulty_KeterDesc
difficulties[KETER]\permaDeath = True
difficulties[KETER]\aggressiveNPCs = True
difficulties[KETER]\saveType = SAVEONQUIT
difficulties[KETER]\otherFactors = HARD
difficulties[KETER]\r = 200
difficulties[KETER]\g = 0
difficulties[KETER]\b = 0

difficulties[CUSTOM] = New Difficulty
difficulties[CUSTOM]\name = "Esoteric"
difficulties[CUSTOM]\localName = I_Loc\Difficulty_Esoteric
difficulties[CUSTOM]\permaDeath = False
difficulties[CUSTOM]\aggressiveNPCs = True
difficulties[CUSTOM]\saveType = SAVEANYWHERE
difficulties[CUSTOM]\customizable = True
difficulties[CUSTOM]\otherFactors = EASY
difficulties[CUSTOM]\r = 255
difficulties[CUSTOM]\g = 255
difficulties[CUSTOM]\b = 255

SelectedDifficulty = difficulties[SAFE]
;~IDEal Editor Parameters:
;~F#0
;~C#Blitz3D