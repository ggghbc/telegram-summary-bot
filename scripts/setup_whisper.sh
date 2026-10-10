#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"
MODELS_DIR="$PROJECT_ROOT/models"

mkdir -p "$BIN_DIR" "$MODELS_DIR"

MODEL_NAME="${1:-base}"

echo "=== Setting up local Whisper for Telegram Summary Bot ==="

# 1. Download model if not present
MODEL_FILE="$MODELS_DIR/ggml-${MODEL_NAME}.bin"
if [ ! -f "$MODEL_FILE" ]; then
    echo "Downloading ggml-${MODEL_NAME}.bin from Hugging Face..."
    curl -L --progress-bar -o "$MODEL_FILE" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-${MODEL_NAME}.bin"
    echo "Model saved to $MODEL_FILE"
else
    echo "Model already exists: $MODEL_FILE"
fi

# 2. Build whisper-cli if not present
if [ ! -x "$BIN_DIR/whisper-cli" ]; then
    echo "Building whisper-cli from source..."
    TMP_BUILD_DIR="$(mktemp -d)"
    git clone --depth 1 https://github.com/ggerganov/whisper.cpp "$TMP_BUILD_DIR"
    cmake -B "$TMP_BUILD_DIR/build" -S "$TMP_BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DWHISPER_BUILD_TESTS=OFF \
        -DWHISPER_BUILD_EXAMPLES=ON
    cmake --build "$TMP_BUILD_DIR/build" --target whisper-cli -j$(nproc || echo 2)
    cp "$TMP_BUILD_DIR/build/bin/whisper-cli" "$BIN_DIR/whisper-cli"
    chmod +x "$BIN_DIR/whisper-cli"
    rm -rf "$TMP_BUILD_DIR"
    echo "whisper-cli built and installed to $BIN_DIR/whisper-cli"
else
    echo "whisper-cli already exists: $BIN_DIR/whisper-cli"
fi

echo "=== Local Whisper setup completed successfully! ==="
