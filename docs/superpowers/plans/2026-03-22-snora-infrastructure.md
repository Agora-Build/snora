# Snora Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Docker container, Helm chart, and GitHub Actions CI/CD pipelines for deploying Snora to Kubernetes with GPU support.

**Architecture:** Multi-stage Dockerfile builds the C++ engine (stubbed for now) and bundles the Node.js services. A `start.sh` entrypoint runs both the Worker Manager and API under `tini`. Helm chart templates produce Deployment, Service, Ingress, HPA, NetworkPolicy, and PDB resources. Three GitHub Actions workflows handle CI (lint/test/build), release (push image + chart to GHCR), and GPU tests (manual dispatch).

**Tech Stack:** Docker (nvidia/cuda base), Helm 3, GitHub Actions, GHCR (OCI), tini

**Spec:** `docs/superpowers/specs/2026-03-22-snora-design.md` (Components 7, 12, 13)

**Depends on:** Node.js services (already implemented in `src/`). C++ engine is not yet built — Dockerfile uses a stub placeholder.

---

## File Structure

```
snora/
  .dockerignore                         # Exclude node_modules, .git, tests, docs (must be at repo root)
  docker/
    Dockerfile                          # Multi-stage build (CUDA + Node.js)
    start.sh                            # Entrypoint: starts Worker Manager then API
  docker-compose.yml                    # Dev/test: snora + redis
  docker-compose.test.yml               # CI integration tests: snora + redis + test-runner
  charts/
    snora/
      Chart.yaml                        # Chart metadata
      values.yaml                       # Default values
      templates/
        _helpers.tpl                    # Template helpers (fullname, labels, etc.)
        deployment.yaml                 # Pod spec with GPU resources, env vars
        service.yaml                    # ClusterIP service on port 8080
        configmap.yaml                  # Non-secret config (AGORA_APP_ID, etc.)
        secret.yaml                     # Sensitive env vars (API key, Redis password)
        ingress.yaml                    # Optional TLS ingress
        hpa.yaml                        # HPA on custom metric snora_sessions_active
        networkpolicy.yaml              # Restrict pod-to-pod traffic
        pdb.yaml                        # PodDisruptionBudget (minAvailable: 1)
  .github/
    workflows/
      ci.yml                           # PR/push: lint, test, docker build
      release.yml                      # Tag push: build+push image, package+push chart
      gpu-tests.yml                    # Manual: GPU integration tests
```

---

## Task 1: Dockerfile

**Files:**
- Create: `docker/Dockerfile`
- Create: `.dockerignore`

- [ ] **Step 1: Create .dockerignore**

Create `.dockerignore` (at repo root — Docker reads this from the build context root):
```
node_modules
.git
.github
tests
docs
dist
*.md
.claude
```

- [ ] **Step 2: Create Dockerfile**

Create `docker/Dockerfile`:
```dockerfile
# =============================================================================
# Stage 1: Build C++ CUDA engine
# =============================================================================
FROM nvidia/cuda:12.6.3-devel-ubuntu22.04 AS engine-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Placeholder: when engine/ exists, copy and build here
# COPY engine/ /build/engine/
# WORKDIR /build/engine
# RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
# For now, create a stub binary
RUN echo '#!/bin/bash\necho "snora-engine stub - not yet implemented"\nsleep infinity' > /usr/local/bin/snora-engine \
    && chmod +x /usr/local/bin/snora-engine

# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM nvidia/cuda:12.6.3-runtime-ubuntu22.04

# Install tini for proper signal handling
RUN apt-get update && apt-get install -y --no-install-recommends \
    tini \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Install Node.js 20 LTS
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Copy engine binary from build stage
COPY --from=engine-builder /usr/local/bin/snora-engine /usr/local/bin/snora-engine

# Set up Node.js app
WORKDIR /app

# Copy package files and install production deps only
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

# Copy application source
COPY src/ ./src/
COPY tsconfig.json ./

# Build TypeScript
RUN npx tsc

# Copy entrypoint
COPY docker/start.sh /usr/local/bin/start.sh
RUN chmod +x /usr/local/bin/start.sh

# Create assets volume mount point
RUN mkdir -p /assets/sounds

EXPOSE 8080

VOLUME ["/assets/sounds"]

ENTRYPOINT ["tini", "--"]
CMD ["/usr/local/bin/start.sh"]
```

- [ ] **Step 3: Commit**

```bash
git add docker/Dockerfile .dockerignore
git commit -m "feat: multi-stage Dockerfile with CUDA base and Node.js runtime

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 2: Entrypoint Script

**Files:**
- Create: `docker/start.sh`

- [ ] **Step 1: Create start.sh**

Create `docker/start.sh`:
```bash
#!/bin/bash
set -e

WORKER_PID=""
API_PID=""

# Register signal handler before spawning any processes
shutdown() {
    echo "[snora] Received shutdown signal, stopping processes..."
    [ -n "$WORKER_PID" ] && kill -TERM $WORKER_PID 2>/dev/null || true
    [ -n "$API_PID" ] && kill -TERM $API_PID 2>/dev/null || true
    [ -n "$WORKER_PID" ] && wait $WORKER_PID 2>/dev/null || true
    [ -n "$API_PID" ] && wait $API_PID 2>/dev/null || true
    echo "[snora] All processes stopped"
    exit 0
}

trap shutdown SIGTERM SIGINT

echo "[snora] Starting Worker Manager..."
node dist/src/worker/main.js &
WORKER_PID=$!

# Wait briefly for Worker Manager to initialize
sleep 1

# Verify Worker Manager is running
if ! kill -0 $WORKER_PID 2>/dev/null; then
    echo "[snora] ERROR: Worker Manager failed to start"
    exit 1
fi

echo "[snora] Starting API server..."
node dist/src/api/main.js &
API_PID=$!

echo "[snora] Both processes started (worker=$WORKER_PID, api=$API_PID)"

# Wait for either process to exit
wait -n $WORKER_PID $API_PID
EXIT_CODE=$?

echo "[snora] A process exited with code $EXIT_CODE, shutting down..."
shutdown
```

- [ ] **Step 2: Commit**

```bash
git add docker/start.sh
git commit -m "feat: container entrypoint script with signal forwarding

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 3: Docker Compose (Dev & Test)

**Files:**
- Create: `docker-compose.yml`
- Create: `docker-compose.test.yml`

- [ ] **Step 1: Create dev docker-compose.yml**

Create `docker-compose.yml`:
```yaml
services:
  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
    command: redis-server --appendonly yes
    volumes:
      - redis-data:/data

  snora:
    build:
      context: .
      dockerfile: docker/Dockerfile
    ports:
      - "8080:8080"
    environment:
      - SNORA_API_KEY=dev-api-key
      - REDIS_URL=redis://redis:6379
      - AGORA_APP_ID=dev-agora-app-id
      - ASSETS_PATH=/assets/sounds
      - MAX_CONCURRENT_SESSIONS=4
      - LOG_LEVEL=debug
    depends_on:
      - redis
    # GPU support — uncomment when nvidia-container-toolkit is installed
    # deploy:
    #   resources:
    #     reservations:
    #       devices:
    #         - driver: nvidia
    #           count: 1
    #           capabilities: [gpu]

volumes:
  redis-data:
```

- [ ] **Step 2: Create test docker-compose**

Create `docker-compose.test.yml`:
```yaml
services:
  redis:
    image: redis:7-alpine
    command: redis-server --appendonly yes

  snora:
    build:
      context: .
      dockerfile: docker/Dockerfile
    environment:
      - SNORA_API_KEY=test-api-key
      - REDIS_URL=redis://redis:6379
      - AGORA_APP_ID=test-agora-app-id
      - ASSETS_PATH=/assets/sounds
      - MAX_CONCURRENT_SESSIONS=2
      - LOG_LEVEL=info
    depends_on:
      - redis
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/health"]
      interval: 5s
      timeout: 3s
      retries: 10

  test-runner:
    build:
      context: .
      dockerfile: docker/Dockerfile
    entrypoint: ["node", "--test"]
    command: ["dist/tests/integration/session-lifecycle.test.js"]
    environment:
      - SNORA_API_URL=http://snora:8080
      - SNORA_API_KEY=test-api-key
    depends_on:
      snora:
        condition: service_healthy
```

- [ ] **Step 3: Commit**

```bash
git add docker-compose.yml docker-compose.test.yml
git commit -m "feat: Docker Compose for dev and integration testing

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 4: Helm Chart Scaffold

**Files:**
- Create: `charts/snora/Chart.yaml`
- Create: `charts/snora/values.yaml`
- Create: `charts/snora/templates/_helpers.tpl`

- [ ] **Step 1: Create Chart.yaml**

Create `charts/snora/Chart.yaml`:
```yaml
apiVersion: v2
name: snora
description: Adaptive sleep audio service with GPU-powered real-time sound generation
type: application
version: 0.1.0
appVersion: "0.1.0"
keywords:
  - audio
  - sleep
  - gpu
  - agora
maintainers:
  - name: Snora Team
```

- [ ] **Step 2: Create values.yaml**

Create `charts/snora/values.yaml`:
```yaml
replicaCount: 1

image:
  repository: ghcr.io/aerojet/snora
  tag: latest
  pullPolicy: IfNotPresent

imagePullSecrets: []

service:
  type: ClusterIP
  port: 8080

ingress:
  enabled: false
  className: ""
  annotations: {}
  hosts:
    - host: snora.example.com
      paths:
        - path: /
          pathType: Prefix
  tls: []

resources:
  limits:
    nvidia.com/gpu: 1
    memory: 8Gi
  requests:
    memory: 4Gi
    cpu: 500m

config:
  maxConcurrentSessions: 4
  port: 8080
  assetsPath: /assets/sounds
  gpuDeviceId: 0
  maxSessionDurationHours: 12
  idleTimeoutMinutes: 30
  clientDisconnectGraceMinutes: 5
  logLevel: info
  agoraAppId: ""

secrets:
  apiKey: ""
  redisPassword: ""

redis:
  url: redis://redis:6379

autoscaling:
  enabled: false
  minReplicas: 1
  maxReplicas: 10
  targetSessionsPerPod: 3

podDisruptionBudget:
  enabled: true
  minAvailable: 1

terminationGracePeriodSeconds: 45

nodeSelector: {}
tolerations: []
affinity: {}
```

- [ ] **Step 3: Create _helpers.tpl**

Create `charts/snora/templates/_helpers.tpl`:
```
{{/*
Expand the name of the chart.
*/}}
{{- define "snora.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
*/}}
{{- define "snora.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "snora.labels" -}}
helm.sh/chart: {{ include "snora.name" . }}-{{ .Chart.Version | replace "+" "_" }}
{{ include "snora.selectorLabels" . }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels
*/}}
{{- define "snora.selectorLabels" -}}
app.kubernetes.io/name: {{ include "snora.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}
```

- [ ] **Step 4: Commit**

```bash
git add charts/snora/Chart.yaml charts/snora/values.yaml charts/snora/templates/_helpers.tpl
git commit -m "feat: Helm chart scaffold with Chart.yaml, values, and helpers

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 5: Helm Deployment & Service Templates

**Files:**
- Create: `charts/snora/templates/deployment.yaml`
- Create: `charts/snora/templates/service.yaml`

- [ ] **Step 1: Create deployment.yaml**

Create `charts/snora/templates/deployment.yaml`:
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
spec:
  {{- if not .Values.autoscaling.enabled }}
  replicas: {{ .Values.replicaCount }}
  {{- end }}
  selector:
    matchLabels:
      {{- include "snora.selectorLabels" . | nindent 6 }}
  template:
    metadata:
      labels:
        {{- include "snora.selectorLabels" . | nindent 8 }}
    spec:
      {{- with .Values.imagePullSecrets }}
      imagePullSecrets:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      terminationGracePeriodSeconds: {{ .Values.terminationGracePeriodSeconds }}
      containers:
        - name: {{ .Chart.Name }}
          image: "{{ .Values.image.repository }}:{{ .Values.image.tag }}"
          imagePullPolicy: {{ .Values.image.pullPolicy }}
          ports:
            - name: http
              containerPort: {{ .Values.config.port }}
              protocol: TCP
          env:
            - name: PORT
              value: {{ .Values.config.port | quote }}
            - name: REDIS_URL
              value: {{ .Values.redis.url | quote }}
            - name: AGORA_APP_ID
              valueFrom:
                configMapKeyRef:
                  name: {{ include "snora.fullname" . }}
                  key: agora-app-id
            - name: SNORA_API_KEY
              valueFrom:
                secretKeyRef:
                  name: {{ include "snora.fullname" . }}
                  key: api-key
            - name: ASSETS_PATH
              value: {{ .Values.config.assetsPath | quote }}
            - name: MAX_CONCURRENT_SESSIONS
              value: {{ .Values.config.maxConcurrentSessions | quote }}
            - name: GPU_DEVICE_ID
              value: {{ .Values.config.gpuDeviceId | quote }}
            - name: MAX_SESSION_DURATION_HOURS
              value: {{ .Values.config.maxSessionDurationHours | quote }}
            - name: IDLE_TIMEOUT_MINUTES
              value: {{ .Values.config.idleTimeoutMinutes | quote }}
            - name: CLIENT_DISCONNECT_GRACE_MINUTES
              value: {{ .Values.config.clientDisconnectGraceMinutes | quote }}
            - name: LOG_LEVEL
              value: {{ .Values.config.logLevel | quote }}
          livenessProbe:
            httpGet:
              path: /health
              port: http
            initialDelaySeconds: 10
            periodSeconds: 15
          readinessProbe:
            httpGet:
              path: /health
              port: http
            initialDelaySeconds: 5
            periodSeconds: 5
          resources:
            {{- toYaml .Values.resources | nindent 12 }}
          volumeMounts:
            - name: sound-assets
              mountPath: {{ .Values.config.assetsPath }}
      volumes:
        - name: sound-assets
          emptyDir: {}
      {{- with .Values.nodeSelector }}
      nodeSelector:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      {{- with .Values.tolerations }}
      tolerations:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      {{- with .Values.affinity }}
      affinity:
        {{- toYaml . | nindent 8 }}
      {{- end }}
```

- [ ] **Step 2: Create service.yaml**

Create `charts/snora/templates/service.yaml`:
```yaml
apiVersion: v1
kind: Service
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
spec:
  type: {{ .Values.service.type }}
  ports:
    - port: {{ .Values.service.port }}
      targetPort: http
      protocol: TCP
      name: http
  selector:
    {{- include "snora.selectorLabels" . | nindent 4 }}
```

- [ ] **Step 3: Commit**

```bash
git add charts/snora/templates/deployment.yaml charts/snora/templates/service.yaml
git commit -m "feat: Helm deployment and service templates

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 6: Helm ConfigMap, Secret, Ingress

**Files:**
- Create: `charts/snora/templates/configmap.yaml`
- Create: `charts/snora/templates/secret.yaml`
- Create: `charts/snora/templates/ingress.yaml`

- [ ] **Step 1: Create configmap.yaml**

Create `charts/snora/templates/configmap.yaml`:
```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
data:
  agora-app-id: {{ .Values.config.agoraAppId | quote }}
```

- [ ] **Step 2: Create secret.yaml**

Create `charts/snora/templates/secret.yaml`:
```yaml
apiVersion: v1
kind: Secret
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
type: Opaque
data:
  api-key: {{ .Values.secrets.apiKey | b64enc | quote }}
  {{- if .Values.secrets.redisPassword }}
  redis-password: {{ .Values.secrets.redisPassword | b64enc | quote }}
  {{- end }}
```

- [ ] **Step 3: Create ingress.yaml**

Create `charts/snora/templates/ingress.yaml`:
```yaml
{{- if .Values.ingress.enabled -}}
apiVersion: networking.k8s.io/v1
kind: Ingress
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
  {{- with .Values.ingress.annotations }}
  annotations:
    {{- toYaml . | nindent 4 }}
  {{- end }}
spec:
  {{- if .Values.ingress.className }}
  ingressClassName: {{ .Values.ingress.className }}
  {{- end }}
  {{- if .Values.ingress.tls }}
  tls:
    {{- range .Values.ingress.tls }}
    - hosts:
        {{- range .hosts }}
        - {{ . | quote }}
        {{- end }}
      secretName: {{ .secretName }}
    {{- end }}
  {{- end }}
  rules:
    {{- range .Values.ingress.hosts }}
    - host: {{ .host | quote }}
      http:
        paths:
          {{- range .paths }}
          - path: {{ .path }}
            pathType: {{ .pathType }}
            backend:
              service:
                name: {{ include "snora.fullname" $ }}
                port:
                  name: http
          {{- end }}
    {{- end }}
{{- end }}
```

- [ ] **Step 4: Commit**

```bash
git add charts/snora/templates/configmap.yaml charts/snora/templates/secret.yaml charts/snora/templates/ingress.yaml
git commit -m "feat: Helm configmap, secret, and ingress templates

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 7: Helm HPA, NetworkPolicy, PDB

**Files:**
- Create: `charts/snora/templates/hpa.yaml`
- Create: `charts/snora/templates/networkpolicy.yaml`
- Create: `charts/snora/templates/pdb.yaml`

- [ ] **Step 1: Create hpa.yaml**

Create `charts/snora/templates/hpa.yaml`:
```yaml
{{- if .Values.autoscaling.enabled }}
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: {{ include "snora.fullname" . }}
  minReplicas: {{ .Values.autoscaling.minReplicas }}
  maxReplicas: {{ .Values.autoscaling.maxReplicas }}
  behavior:
    scaleDown:
      stabilizationWindowSeconds: 300
  metrics:
    - type: Pods
      pods:
        metric:
          name: snora_sessions_active
        target:
          type: AverageValue
          averageValue: {{ .Values.autoscaling.targetSessionsPerPod | quote }}
{{- end }}
```

- [ ] **Step 2: Create networkpolicy.yaml**

Create `charts/snora/templates/networkpolicy.yaml`:
```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
spec:
  podSelector:
    matchLabels:
      {{- include "snora.selectorLabels" . | nindent 6 }}
  policyTypes:
    - Ingress
    - Egress
  ingress:
    # Allow traffic from ingress controller
    - ports:
        - port: {{ .Values.config.port }}
          protocol: TCP
  egress:
    # Allow DNS
    - ports:
        - port: 53
          protocol: UDP
        - port: 53
          protocol: TCP
    # Allow Redis
    - ports:
        - port: 6379
          protocol: TCP
    # Allow Agora (external TCP for signaling + UDP for audio transport)
    - ports:
        - port: 443
          protocol: TCP
        - port: 8443
          protocol: TCP
        - protocol: UDP
      to:
        - ipBlock:
            cidr: 0.0.0.0/0
            except:
              - 10.0.0.0/8
              - 172.16.0.0/12
              - 192.168.0.0/16
```

- [ ] **Step 3: Create pdb.yaml**

Create `charts/snora/templates/pdb.yaml`:
```yaml
{{- if .Values.podDisruptionBudget.enabled }}
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata:
  name: {{ include "snora.fullname" . }}
  labels:
    {{- include "snora.labels" . | nindent 4 }}
spec:
  {{- if gt (int .Values.replicaCount) 1 }}
  minAvailable: {{ .Values.podDisruptionBudget.minAvailable }}
  {{- else }}
  maxUnavailable: 1
  {{- end }}
  selector:
    matchLabels:
      {{- include "snora.selectorLabels" . | nindent 6 }}
{{- end }}
```

- [ ] **Step 4: Commit**

```bash
git add charts/snora/templates/hpa.yaml charts/snora/templates/networkpolicy.yaml charts/snora/templates/pdb.yaml
git commit -m "feat: Helm HPA, network policy, and pod disruption budget

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 8: Helm Chart Validation

- [ ] **Step 1: Install helm if not available**

```bash
which helm || curl https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3 | bash
```

- [ ] **Step 2: Lint the chart**

```bash
helm lint charts/snora/ --set secrets.apiKey=test --set config.agoraAppId=test
```
Expected: `1 chart(s) linted, 0 chart(s) failed`

- [ ] **Step 3: Template render test**

```bash
helm template test-release charts/snora/ \
  --set secrets.apiKey=test-key \
  --set config.agoraAppId=test-app \
  --set ingress.enabled=true \
  --set autoscaling.enabled=true \
  > /dev/null && echo "Template renders OK"
```
Expected: No errors

- [ ] **Step 4: Fix any issues and commit if needed**

```bash
git add -A
git commit -m "fix: resolve Helm chart lint/template issues

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 9: GitHub Actions CI Workflow

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create CI workflow**

Create `.github/workflows/ci.yml`:
```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

permissions:
  contents: read

jobs:
  lint-node:
    name: Lint (Node.js)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20
          cache: npm
      - run: npm ci
      - run: npm run lint

  test-node:
    name: Test (Node.js)
    runs-on: ubuntu-latest
    services:
      redis:
        image: redis:7-alpine
        ports:
          - 6379:6379
        options: >-
          --health-cmd "redis-cli ping"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20
          cache: npm
      - run: npm ci
      - run: npm test
        env:
          REDIS_URL: redis://localhost:6379

  # TODO: Uncomment when C++ engine exists (snora-cuda-engine plan)
  # lint-cpp:
  #   name: Lint (C++)
  #   runs-on: ubuntu-latest
  #   steps:
  #     - uses: actions/checkout@v4
  #     - run: sudo apt-get install -y clang-format
  #     - run: find engine/ -name '*.cpp' -o -name '*.h' -o -name '*.cu' | xargs clang-format --dry-run --Werror

  # test-cpp:
  #   name: Test (C++ CPU mode)
  #   runs-on: ubuntu-latest
  #   steps:
  #     - uses: actions/checkout@v4
  #     - run: sudo apt-get install -y cmake build-essential
  #     - run: cd engine && cmake -B build -DSNORA_CPU_MODE=ON && cmake --build build
  #     - run: cd engine/build && ctest --output-on-failure

  lint-helm:
    name: Lint (Helm)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: azure/setup-helm@v4
      - run: helm lint charts/snora/ --set secrets.apiKey=ci --set config.agoraAppId=ci

  docker-build:
    name: Docker Build
    runs-on: ubuntu-latest
    needs: [lint-node, test-node]
    steps:
      - uses: actions/checkout@v4
      - uses: docker/setup-buildx-action@v3
      - uses: docker/build-push-action@v6
        with:
          context: .
          file: docker/Dockerfile
          push: false
          tags: snora:ci-${{ github.sha }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "feat: GitHub Actions CI workflow (lint, test, docker build)

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 10: GitHub Actions Release Workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Create release workflow**

Create `.github/workflows/release.yml`:
```yaml
name: Release

on:
  push:
    tags:
      - "v*"

permissions:
  contents: read
  packages: write

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository }}

jobs:
  docker-release:
    name: Build & Push Docker Image
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: docker/setup-buildx-action@v3

      - uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Extract version from tag
        id: version
        run: echo "VERSION=${GITHUB_REF_NAME#v}" >> $GITHUB_OUTPUT

      - uses: docker/build-push-action@v6
        with:
          context: .
          file: docker/Dockerfile
          push: true
          tags: |
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ github.ref_name }}
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:latest
          cache-from: type=gha
          cache-to: type=gha,mode=max

  helm-release:
    name: Package & Push Helm Chart
    runs-on: ubuntu-latest
    needs: docker-release
    steps:
      - uses: actions/checkout@v4

      - uses: azure/setup-helm@v4

      - name: Login to GHCR for Helm
        run: echo "${{ secrets.GITHUB_TOKEN }}" | helm registry login ${{ env.REGISTRY }} -u ${{ github.actor }} --password-stdin

      - name: Sync chart version with git tag
        run: |
          VERSION=${GITHUB_REF_NAME#v}
          sed -i "s/^version:.*/version: $VERSION/" charts/snora/Chart.yaml
          sed -i "s/^appVersion:.*/appVersion: \"$VERSION\"/" charts/snora/Chart.yaml

      - name: Package chart
        run: helm package charts/snora/

      - name: Push chart to GHCR
        run: |
          CHART_FILE=$(ls snora-*.tgz)
          helm push "$CHART_FILE" oci://${{ env.REGISTRY }}/${{ github.repository_owner }}/charts
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "feat: GitHub Actions release workflow (Docker + Helm to GHCR)

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 11: GitHub Actions GPU Tests Workflow

**Files:**
- Create: `.github/workflows/gpu-tests.yml`

- [ ] **Step 1: Create GPU tests workflow**

Create `.github/workflows/gpu-tests.yml`:
```yaml
name: GPU Tests

on:
  workflow_dispatch:
    inputs:
      image_tag:
        description: "Docker image tag to test"
        required: false
        default: "latest"

permissions:
  contents: read
  packages: read

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository }}

jobs:
  gpu-smoke-test:
    name: GPU Integration Test
    runs-on: [self-hosted, gpu]
    steps:
      - uses: actions/checkout@v4

      - name: Login to GHCR
        run: echo "${{ secrets.GITHUB_TOKEN }}" | docker login ${{ env.REGISTRY }} -u ${{ github.actor }} --password-stdin

      - name: Run smoke test
        run: |
          docker compose -f docker-compose.yml up -d redis
          sleep 2

          docker run --rm --gpus all \
            --network host \
            -e SNORA_API_KEY=gpu-test-key \
            -e REDIS_URL=redis://localhost:6379 \
            -e AGORA_APP_ID=gpu-test-app \
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ inputs.image_tag }} &

          sleep 5

          # Smoke test: create session, check health
          curl -sf http://localhost:8080/health | jq .
          curl -sf -X POST http://localhost:8080/sessions \
            -H "Content-Type: application/json" \
            -H "X-API-Key: gpu-test-key" \
            -d '{"user_id":"gpu-test","agora":{"token":"t","channel":"c"},"initial_state":{"mood":"calm","heart_rate":70,"hrv":50,"respiration_rate":14,"stress_level":0.3},"preferences":{"soundscape":"rain","volume":0.7}}' \
            | jq .

          echo "GPU smoke test passed"

      - name: Cleanup
        if: always()
        run: docker compose -f docker-compose.yml down -v
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/gpu-tests.yml
git commit -m "feat: GitHub Actions GPU test workflow (manual dispatch)

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Task 12: Verify Everything

- [ ] **Step 1: Verify Helm chart**

```bash
helm lint charts/snora/ --set secrets.apiKey=test --set config.agoraAppId=test
helm template test charts/snora/ --set secrets.apiKey=test --set config.agoraAppId=test > /dev/null
```
Expected: Both pass

- [ ] **Step 2: Verify Docker build**

```bash
docker build -f docker/Dockerfile -t snora:test .
```
Expected: Build succeeds

- [ ] **Step 3: Verify docker-compose up**

```bash
docker compose up -d
sleep 3
curl -sf http://localhost:8080/health | jq .
docker compose down -v
```
Expected: Health check returns `{"ok":true,...}`

- [ ] **Step 4: Verify all Node.js tests still pass**

```bash
npx vitest run
```
Expected: 59 tests pass

- [ ] **Step 5: Final commit if fixes needed**

```bash
git add -A
git commit -m "fix: infrastructure verification fixes

Co-Authored-By: 🤖 Built with SMT <smt@agora.build>"
```

---

## Next Plan

After this infrastructure plan, one more plan remains:

- **`2026-03-22-snora-cuda-engine.md`** — C++ CUDA audio engine: IPC socket server, audio pipeline (noise gen, spectral tilt, binaural beats, nature loops, mixer), Agora SDK integration, CPU fallback mode, GoogleTest unit tests, reference audio tests.
