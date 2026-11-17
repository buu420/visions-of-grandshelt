"""Bake directional HRTF positions for field cues using Steam Audio 4.6.1."""
import argparse
import ctypes as c
import hashlib
import math
import struct
import wave
from pathlib import Path


RATE = 48000
BLOCK = 512
SHA256 = "45bd778a10e909b926c20a1ed55f08255fd573d11c05c625bacd4d6ef368eed5"


class ContextSettings(c.Structure):
    _fields_ = [
        ("version", c.c_uint32), ("log", c.c_void_p),
        ("allocate", c.c_void_p), ("free", c.c_void_p),
        ("simd", c.c_int), ("flags", c.c_int),
    ]


class AudioSettings(c.Structure):
    _fields_ = [("rate", c.c_int), ("frame_size", c.c_int)]


class HrtfSettings(c.Structure):
    _fields_ = [
        ("type", c.c_int), ("sofa_file", c.c_void_p), ("sofa_data", c.c_void_p),
        ("sofa_size", c.c_int), ("volume", c.c_float), ("normalization", c.c_int),
    ]


class EffectSettings(c.Structure):
    _fields_ = [("hrtf", c.c_void_p)]


class Direction(c.Structure):
    _fields_ = [("x", c.c_float), ("y", c.c_float), ("z", c.c_float)]


class EffectParameters(c.Structure):
    _fields_ = [
        ("direction", Direction), ("interpolation", c.c_int),
        ("blend", c.c_float), ("hrtf", c.c_void_p), ("peak_delays", c.c_void_p),
    ]


class AudioBuffer(c.Structure):
    _fields_ = [
        ("channels", c.c_int), ("samples", c.c_int),
        ("data", c.POINTER(c.POINTER(c.c_float))),
    ]


def require_success(status, operation):
    if status != 0:
        raise RuntimeError(f"{operation} failed with Steam Audio error {status}")


class Renderer:
    def __init__(self, dll):
        with dll.open("rb") as data:
            if hashlib.file_digest(data, "sha256").hexdigest() != SHA256:
                raise RuntimeError("Unexpected Steam Audio DLL")
        self.dll = c.WinDLL(str(dll.resolve()), winmode=0x900)
        handle = c.c_void_p
        p = c.POINTER
        for name, result, args in [
            ("iplContextCreate", c.c_int, (p(ContextSettings), p(handle))),
            ("iplContextRelease", None, (p(handle),)),
            ("iplHRTFCreate", c.c_int, (handle, p(AudioSettings), p(HrtfSettings), p(handle))),
            ("iplHRTFRelease", None, (p(handle),)),
            ("iplBinauralEffectCreate", c.c_int, (handle, p(AudioSettings), p(EffectSettings), p(handle))),
            ("iplBinauralEffectRelease", None, (p(handle),)),
            ("iplBinauralEffectReset", None, (handle,)),
            ("iplBinauralEffectApply", c.c_int, (handle, p(EffectParameters), p(AudioBuffer), p(AudioBuffer))),
            ("iplBinauralEffectGetTail", c.c_int, (handle, p(AudioBuffer))),
        ]:
            function = getattr(self.dll, name)
            function.restype = result
            function.argtypes = args
        self.context, self.hrtf, self.effect = handle(), handle(), handle()
        try:
            audio = AudioSettings(RATE, BLOCK)
            require_success(self.dll.iplContextCreate(c.byref(ContextSettings(version=0x040601)), c.byref(self.context)), "Context")
            require_success(self.dll.iplHRTFCreate(self.context, c.byref(audio), c.byref(HrtfSettings(volume=1)), c.byref(self.hrtf)), "HRTF")
            require_success(self.dll.iplBinauralEffectCreate(self.context, c.byref(audio), c.byref(EffectSettings(self.hrtf)), c.byref(self.effect)), "Binaural effect")
        except BaseException:
            self.close()
            raise

    def close(self):
        for name, handle in [
            ("iplBinauralEffectRelease", self.effect),
            ("iplHRTFRelease", self.hrtf),
            ("iplContextRelease", self.context),
        ]:
            if handle:
                getattr(self.dll, name)(c.byref(handle))

    def render(self, samples, degrees):
        """Zero degrees is ahead, positive angles are to the player's right."""
        angle = math.radians(degrees)
        params = EffectParameters(Direction(math.sin(angle), 0, -math.cos(angle)), 1, 1, self.hrtf, None)
        left = (c.c_float * BLOCK)()
        right = (c.c_float * BLOCK)()
        channels = c.POINTER(c.c_float) * 2
        output_buffer = AudioBuffer(2, BLOCK, channels(left, right))
        rendered = []
        self.dll.iplBinauralEffectReset(self.effect)
        for start in range(0, len(samples), BLOCK):
            frame = (c.c_float * BLOCK)(*samples[start:start + BLOCK])
            input_buffer = AudioBuffer(1, BLOCK, (c.POINTER(c.c_float) * 1)(frame))
            state = self.dll.iplBinauralEffectApply(self.effect, c.byref(params), c.byref(input_buffer), c.byref(output_buffer))
            if state not in (0, 1):
                raise RuntimeError(f"Unexpected Steam Audio state {state}")
            rendered.extend(zip(left, right))
        for _ in range(8):
            if state == 1:
                break
            state = self.dll.iplBinauralEffectGetTail(self.effect, c.byref(output_buffer))
            rendered.extend(zip(left, right))
        if state != 1 or not all(math.isfinite(l) and math.isfinite(r) for l, r in rendered):
            raise RuntimeError("Invalid binaural output")
        return rendered

    def render_loop(self, samples, degrees):
        frames = len(samples)
        # Keep a steady-state cycle so the baked interaural delays wrap at the seam.
        repeated = self.render(samples * 4, degrees)
        loop = repeated[2 * frames:3 * frames]
        following = repeated[3 * frames:4 * frames]
        if any(max(abs(l - next_l), abs(r - next_r)) > 8e-5
               for (l, r), (next_l, next_r) in zip(loop, following, strict=True)):
            raise ValueError(f"HRTF output is not periodic at {degrees} degrees")
        return loop


def load_mono(path, max_ms):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (1, 2, RATE, "NONE"):
            raise ValueError(f"Expected 48 kHz PCM16 mono WAV: {path}")
        frames = source.getnframes()
        if not 0.05 * RATE <= frames <= max_ms * RATE / 1000:
            raise ValueError(f"Unexpected cue duration: {path}")
        return [value[0] / 32768 for value in struct.iter_unpack("<h", source.readframes(frames))]


def build_bank(renderer, source, output, step, max_ms, loop=False):
    angles = range(0, 360, step)
    dry = load_mono(source, max_ms)
    gain = 0.7
    pcm = []
    for angle in angles:
        pulse = renderer.render_loop(dry, angle) if loop else renderer.render(dry, angle)
        peak = max(abs(sample) * gain for frame in pulse for sample in frame)
        if not 0 < peak < 1:
            raise ValueError(f"Invalid peak for {angle} degrees")
        pcm.append(b"".join(struct.pack("<hh", round(l * gain * 32767), round(r * gain * 32767))
                            for l, r in pulse))
    frames = len(pcm[0]) // 4
    if any(len(pulse) != frames * 4 for pulse in pcm) or frames * 1000 >= max_ms * RATE:
        raise ValueError("Invalid or inconsistent rendered duration")

    def energy_ratio(pulse):
        left, right = zip(*struct.iter_unpack("<hh", pulse))
        return 10 * math.log10(sum(r * r for r in right) / sum(l * l for l in left))

    def correct_side(pulse, side):
        if side * energy_ratio(pulse) >= (0.1 if loop else 2.0):
            return True
        if not loop:
            return False
        # At low frequencies, the earlier arrival at the near ear can dominate level.
        left, right = zip(*struct.iter_unpack("<hh", pulse))
        lag = max(range(-48, 49), key=lambda delay: sum(
            left[(i + delay) % frames] * right[i] for i in range(0, frames, 8)))
        return side * lag > 0

    if (not correct_side(pcm[90 // step], 1) or not correct_side(pcm[270 // step], -1)
            or pcm[0] == pcm[180 // step]):
        raise ValueError("Invalid HRTF direction")
    bank = struct.pack("<4sIII", b"RLB1" if loop else b"RAB1", RATE, len(pcm), frames) + b"".join(pcm)
    output.write_bytes(bank)
    print(f"{len(pcm)} positions, {frames} stereo frames at {RATE} Hz, SHA256 {hashlib.sha256(bank).hexdigest()}")


def main():
    assets = Path(__file__).resolve().parents[1] / "assets" / "beacon"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--steam-audio", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=assets / "ffrs_beacon_twotone.wav")
    parser.add_argument("--output", type=Path, default=assets / "route-beacon.bin")
    parser.add_argument("--angle-step", type=int, choices=(5, 10), default=5)
    parser.add_argument("--max-ms", type=int, choices=(220, 1200), default=220)
    parser.add_argument("--loop", action="store_true", help="Bake a seamless repeated cue")
    args = parser.parse_args()
    renderer = Renderer(args.steam_audio)
    try:
        build_bank(renderer, args.source, args.output, args.angle_step, args.max_ms, loop=args.loop)
    finally:
        renderer.close()


if __name__ == "__main__":
    main()
