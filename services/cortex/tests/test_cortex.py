"""Unit tests for the cortex service. No hardware, no network: every
subprocess call and every HTTP call to the VLM is mocked."""

import base64
import json
import subprocess

import httpx
import pytest
from fastapi.testclient import TestClient

import cortex


# --- helpers -----------------------------------------------------------------


class FakeCompleted:
    def __init__(self, returncode=0, stdout="", stderr=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


def arg_after(cmd, flag):
    return cmd[cmd.index(flag) + 1]


def location_of(cmd):
    for part in cmd:
        if part.startswith("location="):
            return part.split("=", 1)[1]
    raise AssertionError(f"no location= in {cmd}")


JPEG = b"\xff\xd8\xff\xe0" + b"x" * 40000 + b"\xff\xd9"


@pytest.fixture
def client(monkeypatch):
    monkeypatch.setattr(cortex, "_bin_ok", lambda path: True)
    monkeypatch.setattr(cortex, "_camera_present", lambda: True)
    return TestClient(cortex.app)


class FakeHttp:
    """Stands in for the httpx.Client the cortex opens for the VLM. Patched in
    as `cortex._http_client`, because patching `httpx.Client` itself would break
    Starlette's TestClient, which subclasses it."""

    def __init__(self, on_post=None, on_get=None):
        self.on_post = on_post
        self.on_get = on_get

    def __call__(self, timeout):
        return self

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def post(self, url, **kwargs):
        assert self.on_post is not None, f"unexpected POST {url}"
        return self.on_post(url, **kwargs)

    def get(self, url, **kwargs):
        assert self.on_get is not None, f"unexpected GET {url}"
        return self.on_get(url, **kwargs)


def response(status, **kwargs):
    return httpx.Response(status, request=httpx.Request("POST", "http://vlm"), **kwargs)


def fake_runner(handlers):
    """handlers: dict of program basename -> callable(cmd) -> FakeCompleted"""

    def run(cmd, **kwargs):
        prog = cmd[0].rsplit("/", 1)[-1]
        if prog not in handlers:
            raise AssertionError(f"unexpected subprocess: {cmd}")
        return handlers[prog](cmd)

    return run


def good_arecord(cmd):
    with open(cmd[-1], "wb") as fh:
        fh.write(b"RIFF" + b"\0" * 100)
    return FakeCompleted(0)


def good_gst(cmd):
    with open(location_of(cmd), "wb") as fh:
        fh.write(JPEG)
    return FakeCompleted(0)


# --- /listen -----------------------------------------------------------------


def test_listen_happy_path(client, monkeypatch):
    seen = {}

    def whisper(cmd):
        seen["model"] = arg_after(cmd, "-m")
        seen["wav"] = arg_after(cmd, "-f")
        return FakeCompleted(0, stdout="\n  Hello   Pip,\n how are you?  \n")

    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"arecord": good_arecord, "whisper-cli": whisper}),
    )
    r = client.post("/listen", json={"seconds": 3})
    assert r.status_code == 200
    body = r.json()
    assert body["text"] == "Hello Pip, how are you?"
    assert body["lang"] == "en"          # the .en model never auto-detects
    assert isinstance(body["ms"], int) and body["ms"] >= 0
    assert seen["model"] == cortex.WHISPER_MODEL
    assert seen["wav"].endswith(".wav")


def test_listen_seconds_are_clamped(client, monkeypatch):
    seen = {}

    def arecord(cmd):
        seen["d"] = arg_after(cmd, "-d")
        return good_arecord(cmd)

    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner(
            {"arecord": arecord, "whisper-cli": lambda c: FakeCompleted(0, "ok")}
        ),
    )
    client.post("/listen", json={"seconds": 99})
    assert seen["d"] == "10"
    client.post("/listen", json={"seconds": 0})
    assert seen["d"] == "1"


def test_listen_blank_audio_becomes_empty_text(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner(
            {
                "arecord": good_arecord,
                "whisper-cli": lambda c: FakeCompleted(
                    0, stdout="[BLANK_AUDIO]\n (silence)\n [ Music ]\n"
                ),
            }
        ),
    )
    r = client.post("/listen", json={"seconds": 4})
    assert r.status_code == 200
    assert r.json()["text"] == ""


def test_listen_arecord_failure_is_503(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner(
            {"arecord": lambda c: FakeCompleted(1, stderr="No such device")}
        ),
    )
    r = client.post("/listen", json={"seconds": 2})
    assert r.status_code == 503
    assert "error" in r.json()


def test_listen_missing_whisper_binary_is_503(client, monkeypatch):
    monkeypatch.setattr(cortex, "_bin_ok", lambda path: "whisper" not in path)
    r = client.post("/listen", json={"seconds": 2})
    assert r.status_code == 503
    assert "whisper" in r.json()["error"].lower()


def test_listen_whisper_timeout_is_503(client, monkeypatch):
    def whisper(cmd):
        raise subprocess.TimeoutExpired(cmd, 30)

    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"arecord": good_arecord, "whisper-cli": whisper}),
    )
    r = client.post("/listen", json={"seconds": 2})
    assert r.status_code == 503


def test_listen_accepts_a_body_without_json_content_type(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner(
            {"arecord": good_arecord, "whisper-cli": lambda c: FakeCompleted(0, "hi")}
        ),
    )
    r = client.post("/listen", content=b'{"seconds":2}')
    assert r.status_code == 200
    assert r.json()["text"] == "hi"


# --- /see --------------------------------------------------------------------


def _vlm_ok(captured):
    def post(url, **kwargs):
        captured["url"] = url
        captured["json"] = kwargs.get("json")
        return response(
            200, json={"choices": [{"message": {"content": "  A tidy desk.\n"}}]}
        )

    return post


def test_see_happy_path_builds_a_data_uri(client, monkeypatch):
    captured = {}
    monkeypatch.setattr(
        cortex.subprocess, "run", fake_runner({"gst-launch-1.0": good_gst})
    )
    monkeypatch.setattr(cortex, "_http_client", FakeHttp(on_post=_vlm_ok(captured)))

    r = client.post("/see", json={"question": "What do you see?"})
    assert r.status_code == 200
    body = r.json()
    assert body["text"] == "A tidy desk."
    assert isinstance(body["ms"], int)

    assert captured["url"].endswith("/v1/chat/completions")
    content = captured["json"]["messages"][0]["content"]
    assert content[0] == {"type": "text", "text": "What do you see?"}
    url = content[1]["image_url"]["url"]
    assert url.startswith("data:image/jpeg;base64,")
    assert base64.b64decode(url.split(",", 1)[1]) == JPEG
    assert captured["json"]["max_tokens"] == 80
    assert captured["json"]["temperature"] == pytest.approx(0.2)


def test_see_retries_when_the_first_frame_is_too_small(client, monkeypatch):
    calls = []

    def gst(cmd):
        calls.append(cmd)
        if len(calls) == 1:
            with open(location_of(cmd), "wb") as fh:
                fh.write(b"tiny")
            return FakeCompleted(0)
        # second attempt: multifilesink pattern, write three frames
        pattern = location_of(cmd)
        for i in range(3):
            with open(pattern % i, "wb") as fh:
                fh.write(JPEG + bytes([i]))
        return FakeCompleted(0)

    monkeypatch.setattr(cortex.subprocess, "run", fake_runner({"gst-launch-1.0": gst}))
    captured = {}
    monkeypatch.setattr(cortex, "_http_client", FakeHttp(on_post=_vlm_ok(captured)))

    r = client.post("/see", json={"question": "hi"})
    assert r.status_code == 200
    assert len(calls) == 2
    url = captured["json"]["messages"][0]["content"][1]["image_url"]["url"]
    assert base64.b64decode(url.split(",", 1)[1]) == JPEG + bytes([2])


def test_see_camera_failure_is_503(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"gst-launch-1.0": lambda c: FakeCompleted(1, stderr="busy")}),
    )
    r = client.post("/see", json={"question": "hi"})
    assert r.status_code == 503
    assert "error" in r.json()


def test_see_vlm_500_is_503(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess, "run", fake_runner({"gst-launch-1.0": good_gst})
    )

    monkeypatch.setattr(
        cortex,
        "_http_client",
        FakeHttp(on_post=lambda url, **kw: response(500, text="boom")),
    )
    r = client.post("/see", json={"question": "hi"})
    assert r.status_code == 503
    assert "vlm" in r.json()["error"].lower()


def test_see_vlm_unreachable_is_503(client, monkeypatch):
    monkeypatch.setattr(
        cortex.subprocess, "run", fake_runner({"gst-launch-1.0": good_gst})
    )

    def boom(url, **kwargs):
        raise httpx.ConnectError("refused")

    monkeypatch.setattr(cortex, "_http_client", FakeHttp(on_post=boom))
    r = client.post("/see", json={"question": "hi"})
    assert r.status_code == 503


def test_see_missing_camera_device_is_503(client, monkeypatch):
    monkeypatch.setattr(cortex, "_camera_present", lambda: False)
    r = client.post("/see", json={"question": "hi"})
    assert r.status_code == 503
    assert "camera" in r.json()["error"].lower()


# --- /health -----------------------------------------------------------------


def test_health_shape_all_up(client, monkeypatch):
    monkeypatch.setattr(cortex, "_camera_present", lambda: True)
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"arecord": lambda c: FakeCompleted(0, "card 2: BRIO")}),
    )
    monkeypatch.setattr(
        cortex,
        "_http_client",
        FakeHttp(on_get=lambda url, **kw: response(200, json={"status": "ok"})),
    )
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {
        "ok": True,
        "whisper": True,
        "vlm": True,
        "camera": True,
        "mic": True,
        "search": cortex._search_available(),
    }


def test_health_reports_parts_down_but_stays_ok(client, monkeypatch):
    monkeypatch.setattr(cortex, "_bin_ok", lambda path: False)
    monkeypatch.setattr(cortex, "_camera_present", lambda: False)
    monkeypatch.setattr(
        cortex.subprocess, "run", fake_runner({"arecord": lambda c: FakeCompleted(1)})
    )

    def get(url, **kw):
        raise httpx.ConnectError("refused")

    monkeypatch.setattr(cortex, "_http_client", FakeHttp(on_get=get))
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {
        "ok": True,
        "whisper": False,
        "vlm": False,
        "camera": False,
        "mic": False,
        "search": cortex._search_available(),
    }


def test_health_keys_are_exactly_the_contract(client, monkeypatch):
    monkeypatch.setattr(cortex, "_camera_present", lambda: True)
    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"arecord": lambda c: FakeCompleted(0, "card 2: BRIO")}),
    )
    monkeypatch.setattr(
        cortex,
        "_http_client",
        FakeHttp(on_get=lambda url, **kw: response(200, json={})),
    )
    keys = set(client.get("/health").json())
    assert keys == {"ok", "whisper", "vlm", "camera", "mic", "search"}


# --- text cleaning -----------------------------------------------------------


@pytest.mark.parametrize(
    "raw,want",
    [
        ("  hello  world \n", "hello world"),
        ("[BLANK_AUDIO]", ""),
        ("[ Silence ]", ""),
        ("(door creaks)", ""),
        ("[BLANK_AUDIO]\nTurn on the light.", "Turn on the light."),
        ("", ""),
        ("*whispering* ok", "ok"),
    ],
)
def test_clean_transcript(raw, want):
    assert cortex.clean_transcript(raw) == want


# --- language detection ------------------------------------------------------


def test_parse_lang_reads_whisper_stderr():
    assert cortex.parse_lang("whisper_full: auto-detected language: fr (p = 0.94)") == "fr"
    assert cortex.parse_lang("no such line") == ""
    assert cortex.parse_lang("") == ""


def test_multilingual_listen_uses_auto_and_reports_the_language(client, monkeypatch):
    monkeypatch.setattr(cortex, "WHISPER_MULTILINGUAL", True)
    seen = {}

    def whisper(cmd):
        seen["lang"] = arg_after(cmd, "-l")
        return FakeCompleted(
            0, stdout="Bonjour Pip.",
            stderr="auto-detected language: fr (p = 0.91)",
        )

    monkeypatch.setattr(
        cortex.subprocess,
        "run",
        fake_runner({"arecord": good_arecord, "whisper-cli": whisper}),
    )
    r = client.post("/listen", json={"seconds": 3})
    assert r.status_code == 200
    assert r.json()["text"] == "Bonjour Pip."
    assert r.json()["lang"] == "fr"
    assert seen["lang"] == "auto"


# --- /search -----------------------------------------------------------------


class FakeDDGS:
    hits = [
        {"title": "A", "body": "first", "href": "https://a"},
        {"title": "B", "body": "second", "href": "https://b"},
    ]
    raise_exc = None
    seen = {}

    def __init__(self, timeout=None):
        FakeDDGS.seen["timeout"] = timeout

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False

    def text(self, query, max_results=4):
        FakeDDGS.seen["query"] = query
        FakeDDGS.seen["max_results"] = max_results
        if FakeDDGS.raise_exc:
            raise FakeDDGS.raise_exc
        return list(FakeDDGS.hits)


@pytest.fixture
def fake_ddgs(monkeypatch):
    import sys as _sys
    import types as _types

    mod = _types.ModuleType("ddgs")
    mod.DDGS = FakeDDGS
    monkeypatch.setitem(_sys.modules, "ddgs", mod)
    FakeDDGS.raise_exc = None
    FakeDDGS.seen = {}
    return FakeDDGS


def test_search_happy_path(client, fake_ddgs):
    r = client.post("/search", json={"query": "red hat", "max_results": 2})
    assert r.status_code == 200
    body = r.json()
    assert body["results"] == [
        {"title": "A", "snippet": "first", "url": "https://a"},
        {"title": "B", "snippet": "second", "url": "https://b"},
    ]
    assert fake_ddgs.seen["query"] == "red hat"
    assert fake_ddgs.seen["max_results"] == 2


def test_search_empty_query_is_400(client, fake_ddgs):
    assert client.post("/search", json={}).status_code == 400


def test_search_failure_is_503(client, fake_ddgs):
    fake_ddgs.raise_exc = RuntimeError("rate limited")
    r = client.post("/search", json={"query": "x"})
    assert r.status_code == 503
    assert "rate limited" in r.json()["error"]
