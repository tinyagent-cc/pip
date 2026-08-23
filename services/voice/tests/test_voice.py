"""Unit tests for the voice service. Piper is never loaded: `_load_voice` is
replaced by a fake whose `synthesize` yields one 22050-sample sine chunk."""

import numpy as np
import pytest
from fastapi.testclient import TestClient

import voice as voice_mod

NATIVE = 22050
SINE = (
    np.sin(2 * np.pi * 220 * np.arange(NATIVE) / NATIVE) * 12000
).astype(np.int16)


class FakeChunk:
    def __init__(self, samples, sample_rate=NATIVE):
        self.audio_int16_array = samples
        self.sample_rate = sample_rate
        self.sample_width = 2
        self.sample_channels = 1


class FakeVoice:
    def __init__(self, chunks=None):
        self.chunks = chunks if chunks is not None else [FakeChunk(SINE)]
        self.calls = []

    def synthesize(self, text, syn_config=None, include_alignments=False):
        self.calls.append((text, syn_config))
        yield from self.chunks


@pytest.fixture
def fake_voice(monkeypatch):
    fake = FakeVoice()
    monkeypatch.setattr(voice_mod, "_voice", None)
    monkeypatch.setattr(voice_mod, "_load_voice", lambda: fake)
    return fake


@pytest.fixture
def client(fake_voice):
    return TestClient(voice_mod.app)


# --- /tts --------------------------------------------------------------------


def expected_samples(n_in=NATIVE, rate=None):
    rate = voice_mod.VOICE_RATE if rate is None else rate
    return int(round(n_in * voice_mod.OUT_RATE / (NATIVE * rate)))


def test_tts_returns_16k_pcm_with_the_contract_headers(client):
    r = client.post("/tts", json={"text": "Hello, I am Pip."})
    assert r.status_code == 200
    assert r.headers["content-type"] == "application/octet-stream"
    assert r.headers["x-sample-rate"] == "16000"

    want = expected_samples()  # 22050 -> 16000 at rate 1.12 => 14286 samples
    assert want == 14286
    assert abs(len(r.content) // 2 - want) <= 2
    assert len(r.content) % 2 == 0
    assert abs(int(r.headers["x-duration-ms"]) - 893) <= 2


def test_tts_body_is_little_endian_int16_and_not_silence(client):
    r = client.post("/tts", json={"text": "Hello"})
    pcm = np.frombuffer(r.content, dtype="<i2")
    assert pcm.dtype == np.dtype("<i2")
    assert int(np.abs(pcm).max()) > 1000
    assert int(np.abs(pcm).max()) <= 32767


def test_tts_passes_the_text_through_to_piper(client, fake_voice):
    client.post("/tts", json={"text": "  Say this  "})
    assert fake_voice.calls[0][0] == "Say this"


def test_tts_concatenates_multiple_chunks(monkeypatch, fake_voice):
    fake_voice.chunks = [FakeChunk(SINE), FakeChunk(SINE)]
    client = TestClient(voice_mod.app)
    r = client.post("/tts", json={"text": "two chunks"})
    assert abs(len(r.content) // 2 - expected_samples(2 * NATIVE)) <= 2


def test_tts_empty_text_is_400(client):
    for body in ({"text": ""}, {"text": "   "}, {}):
        r = client.post("/tts", json=body)
        assert r.status_code == 400, body
        assert "error" in r.json()


def test_tts_over_300_chars_is_413(client):
    r = client.post("/tts", json={"text": "a" * 301})
    assert r.status_code == 413
    r = client.post("/tts", json={"text": "a" * 300})
    assert r.status_code == 200


def test_tts_accepts_a_body_without_a_json_content_type(client):
    r = client.post("/tts", content=b'{"text":"Hello"}')
    assert r.status_code == 200
    assert len(r.content) > 0


def test_tts_piper_failure_is_503(client, fake_voice):
    def boom(text, syn_config=None, include_alignments=False):
        raise RuntimeError("onnx exploded")

    fake_voice.synthesize = boom
    r = client.post("/tts", json={"text": "hi"})
    assert r.status_code == 503
    assert "error" in r.json()


# --- resampling --------------------------------------------------------------


def test_resample_rate_1_is_a_plain_downsample():
    monkey = voice_mod.VOICE_RATE
    try:
        voice_mod.VOICE_RATE = 1.0
        out = voice_mod.resample(SINE, NATIVE)
        assert abs(out.size - 16000) <= 1
    finally:
        voice_mod.VOICE_RATE = monkey


def test_resample_above_1_shortens_the_audio():
    slow = voice_mod.resample(SINE, NATIVE)
    assert slow.size < 16000  # 1.12 plays it faster, so fewer output samples
    assert slow.dtype == np.int16


def test_resample_of_nothing_is_nothing():
    assert voice_mod.resample(np.zeros(0, dtype=np.int16), NATIVE).size == 0


# --- /health -----------------------------------------------------------------


def test_health_shape(client):
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {
        "ok": True,
        "voice": "en_US-lessac-medium",
        "sample_rate": 16000,
    }
