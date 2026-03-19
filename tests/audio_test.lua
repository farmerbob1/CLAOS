-- CLAOS Audio Test Script
-- Tests AC97 sound driver

print("=== AC97 Audio Test ===")
print("")

-- Check status
local st = claos.audio.status()
print("Volume: " .. st.volume .. "%")
print("Playing: " .. tostring(st.playing))
print("")

-- Play a test tone (non-blocking)
print("Playing A4 (440 Hz) for 2 seconds...")
claos.audio.beep(440, 2000)

print("")
print("Tone started! Use claos.audio.stop() to stop.")
print("Try: claos.audio.beep(1000, 1000) for 1kHz")
print("     claos.audio.volume(50) to lower volume")
print("")
print("=== Test complete ===")
