# Field proximity sounds

Bake `route-beacon.bin` from `ffrs_beacon_twotone.wav` with `--lower-rear`. The rear pulses are pitched down two semitones before applying the directional HRTF.

`field-npc.wav` and `field-interactable.wav` are the canonical 24 kHz mono effects from the user's Echos of Gaia FFIX project. Their original sound identities are `FL_GFX_UI_SciFi_Interface_04.wav` and `FL_GFX_UI_Game_Interface_29.wav`.

Resample each WAV to 48 kHz PCM16 mono before passing it to `tools/build-route-beacon.py`. Use `--angle-step 10 --max-ms 1200` and the pinned 64-bit Steam Audio 4.6.1 `phonon.dll`. Give each output the corresponding `.bin` filename. The banks are baked into the reader and use the same HRTF as the route beacon.

The four `field-wall-*.wav` files are original synthesized tones by Claude Teammate. They follow Cyber Sleuth's pitch order and use original waveforms at 48 kHz PCM16 mono. Each is exactly half a second long. Bake each with `--angle-step 10 --max-ms 1200 --loop` so the HRTF output wraps without a gap.

| Bank | SHA-256 |
| --- | --- |
| `field-npc.bin` | `d774a018846e764ef576963ebc0753342eacc704fe7dabe7390ee81192288729` |
| `field-interactable.bin` | `d20417fd8507097a02161ca6aad29ebcb0bed06183c916d7c50621304a448a94` |
| `field-wall-up.bin` | `d9fb6a5dc012a22ba74edb7bfabfaa6356ed5233ef3f7590d148cb29a0557934` |
| `field-wall-right.bin` | `0aa861d3ee951094302ee7abb51a3805968e6a8d7dfdc256020f31b267fc9289` |
| `field-wall-down.bin` | `de644cbb22aae1099ea86463164aa23769c4ad5bae1911951b26e1addab51d09` |
| `field-wall-left.bin` | `4752e6011d5caea914ed6857e4908bd005b3f6ecba35651f3ea33e39acb6d5e7` |
