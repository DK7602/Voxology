# Song prep for SongAlong: yt-dlp (+ deno, which yt-dlp needs for YouTube) and ffmpeg, plus the
# bundled prep server (scripts/build-prep.mjs writes dist/server.mjs before deploy).
FROM node:22-bookworm-slim

RUN apt-get update \
  && apt-get install -y --no-install-recommends ffmpeg ca-certificates curl unzip \
  && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /usr/local/bin/yt-dlp https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux \
  && chmod +x /usr/local/bin/yt-dlp \
  && curl -fsSL -o /tmp/deno.zip https://github.com/denoland/deno/releases/latest/download/deno-x86_64-unknown-linux-gnu.zip \
  && unzip -o /tmp/deno.zip -d /usr/local/bin \
  && rm /tmp/deno.zip

WORKDIR /app
COPY dist/server.mjs ./server.mjs
ENV PORT=8080
EXPOSE 8080
CMD ["node", "server.mjs"]
