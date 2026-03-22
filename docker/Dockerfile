# =============================================================================
# Stage 1: Build C++ CUDA engine
# =============================================================================
FROM nvidia/cuda:12.6.3-devel-ubuntu22.04 AS engine-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Placeholder: when engine/ exists, copy and build here
RUN echo '#!/bin/bash\necho "snora-engine stub - not yet implemented"\nsleep infinity' > /usr/local/bin/snora-engine \
    && chmod +x /usr/local/bin/snora-engine

# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM nvidia/cuda:12.6.3-runtime-ubuntu22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    tini \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

COPY --from=engine-builder /usr/local/bin/snora-engine /usr/local/bin/snora-engine

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci --omit=dev

COPY src/ ./src/
COPY tsconfig.json ./

RUN npx tsc

COPY docker/start.sh /usr/local/bin/start.sh
RUN chmod +x /usr/local/bin/start.sh

RUN mkdir -p /assets/sounds

EXPOSE 8080

VOLUME ["/assets/sounds"]

ENTRYPOINT ["tini", "--"]
CMD ["/usr/local/bin/start.sh"]
