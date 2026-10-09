# Multi-stage build for ultra-small and fast production container
FROM alpine:3.20 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    curl-dev \
    sqlite-dev \
    ca-certificates

WORKDIR /app

# Copy source tree
COPY . .

# Build with Release optimizations
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target telegram_summary_bot

# Production runtime image
FROM alpine:3.20

RUN apk add --no-cache \
    libcurl \
    sqlite-libs \
    libstdc++ \
    libgcc \
    ca-certificates

WORKDIR /app

# Copy binary from builder
COPY --from=builder /app/build/telegram-summary-bot /app/telegram-summary-bot
COPY config.example.json /app/config.example.json
COPY .env.example /app/.env.example

# Volume for persistent SQLite storage
VOLUME ["/app/data"]

ENV DB_PATH=/app/data/messages.db

ENTRYPOINT ["/app/telegram-summary-bot"]
