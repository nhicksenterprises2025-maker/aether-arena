# Audio design and authoring

The 1.1.0 palette replaces the old mono sine/noise cues with recorded metal,
wood, stone, cloth, air and water layers. Short attacks leave room for the next
event. Ten common combat cues have three different recorded takes, rotated at
runtime with slight pitch variation. All 41 original cue IDs remain available;
20 additional takes bring the cooked sound inventory to 61.

The original 16-bar D-minor score uses recorded chamber strings, French horn,
harp, glockenspiel, timpani and restrained snare. It has two phrases, changing
voicings and dynamics, stereo placement and circular reverb. Victory, defeat,
overtime, tiebreaker, aether and core announcements share the score's instruments.
Interface clicks use quiet wood/mechanism transients and page/card movement.
Error feedback is a short double knock. The river is a low-level water bed.

Source recordings are CC0: Kenney Impact Sounds, Jan Schupke's Tinysized SFX,
and Versilian Studios VSCO 2 Community Edition. The exact 48 recordings used
are under `Assets/Source/Audio/Palette`; source URLs, original filenames, pinned
hashes and licenses are beside them. No proprietary card-battler media is used.

## Runtime mix

`URiftBattleAudioSubsystem` admits at most 26 combat voices and reserves six
voices for the interface, alongside music and ambience. Repeated same-cue
events arriving in a swarm are aggregated over 65–180 ms. Important spells,
tower destruction and match announcements can replace a lower-priority combat
voice. Crowded scenes get smooth gain headroom; announcements briefly lower
the score and it recovers. All voices route to a linked stereo look-ahead
limiter at −3 dB with 5 ms look-ahead, instantaneous peak detection and 120 ms release. Its purpose is to
catch overload transients while the authored mix keeps normal battles below it.
Independent Master, Music, SFX and UI settings continue to control live voices.

The transient MasterMix is explicitly registered with each live audio device
before playback. Runtime-created submixes do not receive the asset PostLoad
registration path. Teardown unregisters the mix and restores the original
SoundWave routes. The dynamics preset seeds both its serialized Settings and
thread-safe runtime copy before effect initialization, so creating the effect
cannot replace the limiter with UE's default compressor settings.
UE's peak detector normally applies attack smoothing. Zero attack avoids
underestimating isolated impact samples; the look-ahead delays the audible
signal while the detector responds. The threshold leaves margin for detector
release during that delay. The output is gain-controlled, without a PCM clamp.

## Reproduce and verify

The packaged game needs none of these authoring tools. For source authoring:

```powershell
python -m pip install -r Build/requirements-audio.txt
python Build/fetch_audio_sources.py
python Build/generate_audio.py
python Build/verify_audio_masters.py
```

The last command checks all master hashes, non-silence, DC, endpoint tails,
sample clipping, 4× oversampled true peak, RMS and BS.1770-4 integrated loudness.
Cues shorter than 400 ms are explicitly marked as padded to one measurement
block. It compares the original released `v1.0.0` WAVs and produces a listening
reel and timeline under `Artifacts/QA/Audio/masters-1.1.0`. Numeric measurements
are not a human listening acceptance result. The committed master report is
`Docs/QA/audio-polish-1.1.0.json`.

Use the current Editor to execute `Build/import_unreal_audio.py`. It replaces
only SoundWaves: short effects use Force Inline loading, loops use Retain on
Load, compression quality is 90, and music/ambience have high physical-voice
priority. Full asset imports through `Build/import_unreal_assets.py` also read
the same master manifest. Rebuild/cook before Shipping playback validation.

`Build/Test-UnrealAudio.ps1` tests the actual audio device, 41 base cues, all 20
variations, four bound volume settings, live mixer registration, limiter routing, reserved UI capacity,
duplicate aggregation, event priority and crowded headroom. Its deferred burst
checks that both looping beds retain physical voices across mixer buffers.
The smoke also records actual post-effect MasterMix output to `mixed-output.wav`
and a separate deliberately overloaded pair of large cues to
`limiter-overload.wav`. Both reports measure raw floating-point samples before
PCM conversion, requiring audible content, zero clipped floats and peak below
0.95. Hidden automated windows temporarily bypass UE's application-focus mute
for these captures and restore it afterward; this does not bypass player volume
settings. These recordings are implementation evidence, not ordinary gameplay
captures or subjective sound-quality approval.

The final-source Editor run under
`Artifacts/QA/Audio/polish15-editor-audio-final` passed all 49 checks and exited 0
in 22.60 seconds. Its stereo 48 kHz normal capture contains 59,392 float samples,
with peak 0.206878617 and RMS 0.047645573. The 12× overload contains 116,736
float samples, with peak 0.800180495 and RMS 0.172086378. Both contain zero
clipped floats. These values
are measured before PCM16 encoding; packaged WAV byte verification is separate.

The final Shipping run under
`Artifacts/QA/Audio/polish15-shipping-audio-final` also passed all 49 checks and
exited 0 in 11.16 seconds. Its stereo 48 kHz normal capture contains 61,440 float
samples, with peak 0.203053907 and RMS 0.051877354. The 12× overload contains
114,688 float samples, with peak 0.801191688 and RMS 0.173328368. Both contain
zero clipped floats. The tested native executable SHA256 is
`0f35c7e9f52bb6009876c5e2eef23be8d4033712d844a91f04a8c28c68aa176b`.
These are actual mixer measurements, not human listening acceptance.
