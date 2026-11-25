Scriptname BodyLanguage Hidden

; ============================================================================
; BodyLanguage Public Papyrus API
; Works standalone (no SkyrimNet required) or alongside SkyrimNet.
; All queries are backed by BodyLanguage.dll's 15-second per-actor cache.
; ============================================================================

; --- Version & Status ---

; Returns the BodyLanguage API version (returns 0 if BodyLanguage.dll is not loaded).
Int Function GetApiVersion() global native

; Returns true if SkyrimNet.dll is detected and BodyLanguage decorators are active.
Bool Function IsSkyrimNetActive() global native

; --- Actor State & Height ---

; Returns true if the actor is not wearing armor in slot 32 (Body) or slot 49.
Bool Function IsNaked(Actor akActor) global native

; Returns true if the actor is excluded via BodyLanguage.ini or is a child.
Bool Function IsExcluded(Actor akActor) global native

; Returns "werewolf", "vampire_lord", "werebear", or "" if not in beast form.
String Function GetBeastForm(Actor akActor) global native

; Returns the actor's effective height scale (1.0 = 182.88 cm / 6 ft).
Float Function GetHeightScale(Actor akActor) global native

; Returns the actor's estimated standing height in centimeters.
Float Function GetEstimatedHeightCm(Actor akActor) global native

; --- Body Part Size Tiers ---

; Generic body part tier lookup.
; Valid asPart values (case-insensitive):
;   "frame", "shoulders", "chest", "breasts", "waist", "belly",
;   "butt", "thighs", "arms", "condition"
String Function GetBodyPartTier(Actor akActor, String asPart) global native

; Female: "flat", "small", "average", "large", "huge", "impossible" | Male: ""
String Function GetBreastSize(Actor akActor) global native

; Female: "flat", "small", "average", "large", "large_muscular", "huge", "huge_muscular"
; Male:   "flat", "firm", "large", "huge", "huge_muscular"
String Function GetButtSize(Actor akActor) global native

; Female: "thin", "lean", "average", "thick", "muscular", "colossal", "colossal_muscular"
; Male:   "spindly", "athletic", "pillars", "immense", "immense_soft"
String Function GetThighSize(Actor akActor) global native

; Female: "tiny", "slim", "standard", "soft", "thick"
; Male:   "ripped", "tapered", "straight", "thick", "potbellied", "distended"
String Function GetWaistSize(Actor akActor) global native

; Female: "ripped", "toned", "flat", "soft", "chubby", "large", "huge",
;         "pregnant_early", "pregnant_mid", "pregnant_full"
; Male:   same as midsection ("ripped", "tapered", "straight", "thick", "potbellied", "distended")
String Function GetBellySize(Actor akActor) global native

; Female: "thin", "soft", "average", "toned", "thick", "muscular", "colossal"
; Male:   "thin", "athletic", "brawny", "colossal"
String Function GetArmSize(Actor akActor) global native

; Female: "small", "slim", "average", "broad", "massive"
; Male:   "slight", "compact", "average", "heavy", "massive"
String Function GetFrameSize(Actor akActor) global native

; Female: "small", "slim", "average", "broad", "massive"
; Male:   "narrow", "average", "broad", "expansive"
String Function GetShoulderBreadth(Actor akActor) global native

; Female: same as GetBreastSize | Male: "flat", "average", "broad", "sculpted", "heavyset"
String Function GetChestSize(Actor akActor) global native

; Overall physique archetype (e.g. "slender", "curvy", "athletic", "muscular", "plump", "shredded", ...)
String Function GetPhysiqueCondition(Actor akActor) global native

; --- Real-World Measurements (cm) & Anthropometric Ratios ---

; Returns real-world circumference/width in centimeters (scaled by actor height).
; Valid asPart values (case-insensitive):
;   "shoulders", "chest", "underbust", "waist", "belly", "hips", "thigh", "calf", "arm", "height"
Float Function GetBodyMeasurementCm(Actor akActor, String asPart) global native

; Returns a dimensionless anthropometric ratio or signal.
; Valid asMetric values (case-insensitive):
;   "bmi", "shoulder_to_height", "v_taper", "whr", "bust_to_underbust",
;   "hips_to_height", "thigh_girth", "calf_girth", "arm_girth",
;   "belly_drop", "flank_flare", "geo_tone", "tex_tone", "tone", "pregnancy"
Float Function GetBodyRatio(Actor akActor, String asMetric) global native

; Returns isolated localized pregnancy belly score in [0.0, 1.0].
Float Function GetPregnancyScore(Actor akActor) global native

; Returns combined muscularity vs. body fat tone index in [-1.0, +1.0].
Float Function GetToneIndex(Actor akActor) global native

; --- Genitals (TNG / SOS, when naked) ---

Bool Function HasGenitals(Actor akActor) global native

; Returns "XS", "S", "M", "L", "XL", "XXL", "XXXL", or "" if none/clothed.
String Function GetGenitalSize(Actor akActor) global native

; Returns "flaccid", "hard", or "" if none/clothed.
String Function GetGenitalState(Actor akActor) global native

; Returns the resolved TNG/SOS addon key, or "" if none/clothed.
String Function GetGenitalAddon(Actor akActor) global native

; Returns effective genital length in centimeters (0.0 if none/clothed).
Float Function GetGenitalLengthCm(Actor akActor) global native

; Returns effective genital girth in centimeters (0.0 if none/clothed).
Float Function GetGenitalGirthCm(Actor akActor) global native

; --- Natural-Language Descriptions & JSON ---

String Function GetFullBodyDescription(Actor akActor) global native
String Function GetShortBodyDescription(Actor akActor) global native

; Returns complete JSON with state, measurements_cm, measurements_in, ratios, parts, and genitals.
String Function GetBodyDataJson(Actor akActor) global native

; Returns compact JSON: {"valid": bool, "isFemale": bool, "breasts": "...", "butt": "...", "thighs": "..."}
String Function GetBodyPartsJson(Actor akActor) global native

; Returns compact JSON: {"hasGenitals": bool, "state": "...", "size": "...", "lengthCm": ..., "girthCm": ..., "addon": "..."}
String Function GetGenitalsJson(Actor akActor) global native

; --- Cache Management ---

; Invalidates the 15s cache for a single actor (pass None to clear the entire cache).
Function InvalidateActorCache(Actor akActor) global native
