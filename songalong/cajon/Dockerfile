# Cajon parts for SongAlong: after a song is ready, this pulls its drums out (Demucs, PyTorch on
# the CPU) and finds the kick, snare and hi-hat hits (cajon/drums.ts, bundled by
# scripts/build-prep.mjs into dist/server.mjs). Runs on a 4-core container; see
# worker/cajonContainer.ts.
FROM python:3.13-slim-bookworm

RUN apt-get update \
  && apt-get install -y --no-install-recommends ffmpeg ca-certificates curl unzip \
  && rm -rf /var/lib/apt/lists/*

# PyTorch's CPU build first (the default one carries GPU libraries, gigabytes of them), then
# Demucs and the model itself (81 MB), so nothing is downloaded when a song is worked on
RUN pip install --no-cache-dir --index-url https://download.pytorch.org/whl/cpu torch==2.14.1 torchaudio==2.11.0 \
  && pip install --no-cache-dir demucs==4.0.1 soundfile==0.14.0 scipy==1.18.1 \
  && python -c "from demucs.pretrained import get_model; get_model('htdemucs')"

# Node runs the service; yt-dlp (with deno, which it needs for YouTube) gets the audio
COPY --from=node:22-bookworm-slim /usr/local/bin/node /usr/local/bin/node
RUN curl -fsSL -o /usr/local/bin/yt-dlp https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux \
  && chmod +x /usr/local/bin/yt-dlp \
  && curl -fsSL -o /tmp/deno.zip https://github.com/denoland/deno/releases/latest/download/deno-x86_64-unknown-linux-gnu.zip \
  && unzip -o /tmp/deno.zip -d /usr/local/bin \
  && rm /tmp/deno.zip

WORKDIR /app
COPY separate.py ./separate.py
COPY dist/server.mjs ./server.mjs
ENV PORT=8080
EXPOSE 8080
CMD ["node", "server.mjs"]
