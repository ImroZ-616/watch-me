#!/usr/bin/env python3
"""
============================================================
  ESP32 Smartwatch — Laptop Companion Script
  Pipeline:
    ESP32 mic → BT → [this script] → Whisper STT
                                   → Ollama LLM
                                   → pyttsx3 / gTTS TTS
                                   → BT → ESP32 speaker
============================================================
  Requirements (pip install):
    pyserial bluetooth-socket  (or pybluez)
    openai-whisper
    ollama
    pyttsx3          (offline, fast)   ← default
    gTTS             (online, natural) ← optional fallback
    numpy sounddevice soundfile        (audio helpers)

  Adjust CONFIG below, then run:
    python3 watch_companion.py
============================================================
"""

import bluetooth          # pybluez
import struct
import time
import threading
import queue
import sys
import os
import tempfile
import wave
import logging

import numpy as np
import soundfile as sf

import whisper            # openai-whisper
import ollama             # ollama Python SDK

# TTS — try pyttsx3 (offline), fall back to gTTS (online)
try:
    import pyttsx3
    TTS_ENGINE = "pyttsx3"
except ImportError:
    try:
        from gtts import gTTS
        TTS_ENGINE = "gtts"
    except ImportError:
        TTS_ENGINE = None
        print("[WARN] No TTS engine found. Install pyttsx3 or gTTS.")

# ─── USER CONFIGURATION ──────────────────────────────────────
CONFIG = {
    # Bluetooth device name as shown by the ESP32
    "bt_device_name":   "ESP32-Watch",
    # RFCOMM channel (1 almost always works)
    "bt_channel":       1,
    # Whisper model size: "tiny", "base", "small", "medium", "large"
    "whisper_model":    "base",
    # Ollama model tag (must be pulled: `ollama pull mistral`)
    "ollama_model":     "mistral",
    # System prompt for the LLM
    "system_prompt":    (
        "You are a helpful, concise voice assistant built into a smartwatch. "
        "Keep replies short — ideally 1-3 sentences. Avoid markdown."
    ),
    # Audio format from ESP32 (must match firmware)
    "sample_rate":      16000,
    "sample_width":     2,       # bytes (16-bit PCM)
    "channels":         1,
    # Volume scaling for TTS output sent back (1.0 = no change)
    "tts_volume":       1.0,
    # TTS sample rate to resample to before sending (match ESP32 playback)
    "tts_target_sr":    16000,
    # pyttsx3 speech rate (words per minute)
    "pyttsx3_rate":     175,
    # Show debug logs
    "debug":            True,
}

# ─── Stream framing constants (must match firmware) ──────────
START_MAGIC = bytes([0xAA, 0xBB, 0xCC, 0xDD])
END_MAGIC   = bytes([0xFF, 0xFF, 0xFF, 0xFF])

# ─── Logging ─────────────────────────────────────────────────
logging.basicConfig(
    level=logging.DEBUG if CONFIG["debug"] else logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("watch")


# ═════════════════════════════════════════════════════════════
#  Bluetooth helpers
# ═════════════════════════════════════════════════════════════

def find_device(name: str) -> str | None:
    """Scan for a nearby Bluetooth device by name and return its MAC."""
    log.info("Scanning for Bluetooth device '%s'...", name)
    nearby = bluetooth.discover_devices(duration=8, lookup_names=True)
    for addr, dev_name in nearby:
        if dev_name == name:
            log.info("Found %s at %s", dev_name, addr)
            return addr
    return None


def connect_bt(device_name: str, channel: int) -> bluetooth.BluetoothSocket:
    """Scan, find and connect to the ESP32 watch."""
    while True:
        addr = find_device(device_name)
        if addr:
            try:
                sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
                sock.connect((addr, channel))
                sock.settimeout(None)   # blocking reads
                log.info("Connected to %s (%s)", device_name, addr)
                return sock
            except bluetooth.BluetoothError as e:
                log.warning("Connection failed: %s. Retrying in 3s…", e)
        else:
            log.warning("Device not found. Retrying in 5s…")
        time.sleep(5)


def bt_recv_exact(sock: bluetooth.BluetoothSocket, n: int) -> bytes:
    """Receive exactly n bytes from the socket."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("BT socket closed")
        buf += chunk
    return buf


# ═════════════════════════════════════════════════════════════
#  Audio capture from BT stream
# ═════════════════════════════════════════════════════════════

def capture_audio_stream(sock: bluetooth.BluetoothSocket) -> bytes:
    """
    Read raw PCM bytes from the BT socket.
    The ESP32 sends:
        [START_MAGIC 4B] ... PCM data ... [END_MAGIC 4B]
    Returns the raw PCM bytes (16-bit signed, mono, 16 kHz).
    """
    log.info("Waiting for audio stream from watch…")

    # Wait for start magic (scan byte by byte to handle any junk)
    window = b"\x00" * 4
    while window != START_MAGIC:
        b = bt_recv_exact(sock, 1)
        window = window[1:] + b

    log.info("Audio stream started, capturing…")

    pcm_data = bytearray()
    end_len = len(END_MAGIC)

    while True:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("BT socket closed during stream")
        pcm_data.extend(chunk)

        # Check if end magic appears near the tail
        if len(pcm_data) >= end_len:
            tail = bytes(pcm_data[-end_len:])
            if tail == END_MAGIC:
                pcm_data = pcm_data[:-end_len]
                break

    log.info("Captured %d bytes of PCM audio (%.2f s)",
             len(pcm_data),
             len(pcm_data) / (CONFIG["sample_rate"] *
                              CONFIG["sample_width"] *
                              CONFIG["channels"]))
    return bytes(pcm_data)


# ═════════════════════════════════════════════════════════════
#  Whisper transcription
# ═════════════════════════════════════════════════════════════

_whisper_model = None  # lazy load

def get_whisper_model():
    global _whisper_model
    if _whisper_model is None:
        log.info("Loading Whisper model '%s'…", CONFIG["whisper_model"])
        _whisper_model = whisper.load_model(CONFIG["whisper_model"])
        log.info("Whisper model loaded.")
    return _whisper_model


def transcribe(pcm_bytes: bytes) -> str:
    """Convert raw PCM bytes → float32 numpy array → Whisper transcript."""
    # Convert to numpy int16, then normalise to float32 [-1, 1]
    audio_np = np.frombuffer(pcm_bytes, dtype=np.int16).astype(np.float32)
    audio_np /= 32768.0

    model = get_whisper_model()
    log.info("Transcribing audio…")
    result = model.transcribe(audio_np, language="en", fp16=False)
    text = result["text"].strip()
    log.info("Transcript: %s", text)
    return text


# ═════════════════════════════════════════════════════════════
#  Ollama LLM
# ═════════════════════════════════════════════════════════════

_conversation_history: list[dict] = []

def query_ollama(user_text: str) -> str:
    """Send text to Ollama and return the assistant reply."""
    _conversation_history.append({"role": "user", "content": user_text})

    log.info("Querying Ollama model '%s'…", CONFIG["ollama_model"])
    response = ollama.chat(
        model=CONFIG["ollama_model"],
        messages=[
            {"role": "system", "content": CONFIG["system_prompt"]},
            *_conversation_history,
        ]
    )
    reply = response["message"]["content"].strip()
    _conversation_history.append({"role": "assistant", "content": reply})
    log.info("LLM reply: %s", reply)
    return reply


# ═════════════════════════════════════════════════════════════
#  Text-to-Speech → raw PCM bytes
# ═════════════════════════════════════════════════════════════

def tts_to_pcm(text: str) -> bytes:
    """
    Convert text to speech and return raw 16-bit signed PCM bytes
    at CONFIG['tts_target_sr'] Hz, mono.
    """
    if TTS_ENGINE is None:
        log.error("No TTS engine available!")
        return b""

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp_path = tmp.name

    try:
        if TTS_ENGINE == "pyttsx3":
            _tts_pyttsx3(text, tmp_path)
        else:
            _tts_gtts(text, tmp_path)

        # Load and resample if needed
        audio, sr = sf.read(tmp_path, dtype="int16")

        # Convert to mono
        if audio.ndim > 1:
            audio = audio.mean(axis=1).astype(np.int16)

        # Resample to target rate
        target_sr = CONFIG["tts_target_sr"]
        if sr != target_sr:
            import scipy.signal as signal
            num_samples = int(len(audio) * target_sr / sr)
            audio = signal.resample(audio.astype(np.float32), num_samples)
            audio = np.clip(audio, -32768, 32767).astype(np.int16)

        # Apply volume scaling
        vol = CONFIG["tts_volume"]
        if vol != 1.0:
            audio = np.clip(audio.astype(np.float32) * vol,
                            -32768, 32767).astype(np.int16)

        pcm_bytes = audio.tobytes()
        log.info("TTS generated %d bytes (%.2f s)",
                 len(pcm_bytes),
                 len(pcm_bytes) / (target_sr * 2))
        return pcm_bytes

    finally:
        os.unlink(tmp_path)


def _tts_pyttsx3(text: str, out_path: str):
    engine = pyttsx3.init()
    engine.setProperty("rate", CONFIG["pyttsx3_rate"])
    engine.save_to_file(text, out_path)
    engine.runAndWait()


def _tts_gtts(text: str, out_path: str):
    # gTTS saves MP3; convert to WAV via soundfile/ffmpeg
    import subprocess
    mp3_path = out_path.replace(".wav", ".mp3")
    tts = gTTS(text=text, lang="en", slow=False)
    tts.save(mp3_path)
    # Use ffmpeg to convert to WAV (must be installed)
    subprocess.run(
        ["ffmpeg", "-y", "-i", mp3_path, out_path],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    os.unlink(mp3_path)


# ═════════════════════════════════════════════════════════════
#  Send audio back to watch over Bluetooth
# ═════════════════════════════════════════════════════════════

def send_audio_bt(sock: bluetooth.BluetoothSocket, pcm_bytes: bytes):
    """Stream raw PCM bytes back to the watch in chunks."""
    CHUNK = 512
    total = len(pcm_bytes)
    sent  = 0
    log.info("Sending %d bytes of TTS audio to watch…", total)

    while sent < total:
        end = min(sent + CHUNK, total)
        sock.send(pcm_bytes[sent:end])
        sent = end

    # Small silence pad at the end so the ESP32 rx timeout fires cleanly
    sock.send(b"\x00" * CHUNK)
    log.info("Audio transmission complete.")


# ═════════════════════════════════════════════════════════════
#  Main pipeline
# ═════════════════════════════════════════════════════════════

def run_pipeline(sock: bluetooth.BluetoothSocket):
    """One full PTT cycle: capture → STT → LLM → TTS → playback."""
    # 1. Capture mic audio from watch
    pcm = capture_audio_stream(sock)
    if len(pcm) < 3200:   # < 0.1 s — probably noise, skip
        log.warning("Audio too short, ignoring.")
        return

    # 2. Transcribe
    user_text = transcribe(pcm)
    if not user_text:
        log.warning("Empty transcription, skipping LLM.")
        return

    # 3. LLM
    reply_text = query_ollama(user_text)

    # 4. TTS
    reply_pcm = tts_to_pcm(reply_text)
    if not reply_pcm:
        return

    # 5. Send back to watch
    send_audio_bt(sock, reply_pcm)


def main():
    print("=" * 60)
    print("  ESP32 Smartwatch Companion")
    print("=" * 60)

    # Pre-load Whisper so first query isn't slow
    get_whisper_model()

    sock = connect_bt(CONFIG["bt_device_name"], CONFIG["bt_channel"])

    print("\n[READY] Watching for PTT events. Ctrl-C to quit.\n")

    try:
        while True:
            try:
                run_pipeline(sock)
            except ConnectionError as e:
                log.error("Connection lost: %s. Reconnecting…", e)
                try:
                    sock.close()
                except Exception:
                    pass
                sock = connect_bt(CONFIG["bt_device_name"],
                                  CONFIG["bt_channel"])
    except KeyboardInterrupt:
        print("\n[EXIT] Goodbye.")
        sock.close()
        sys.exit(0)


if __name__ == "__main__":
    main()
