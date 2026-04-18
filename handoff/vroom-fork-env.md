# VROOM Fork — Environment & Deployment Facts

Companion to [`vroom-fork-rfc.md`](./vroom-fork-rfc.md). Read this before writing code — it's the ground truth of what our deployed VROOM actually looks like.

---

## 1. Current upstream base

| Fact | Value |
|---|---|
| Upstream repo | https://github.com/VROOM-Project/vroom |
| Our fork | https://github.com/gvoider/vroom |
| Version running in dev (2026-04-18) | **v1.13.0** (released 2023-01-31) |
| Latest upstream release | v1.15.0 (2026-03-12) |
| Latest minor before ours | v1.14.0 (2024-01-16) |

**Decision point for M0**: fork from upstream `master` (current state post-v1.15.0) or from the `v1.13.0` tag?

- **Recommended: from `master`**. This is a new fork; catching up to latest releases now costs ~2 hours, catching up later costs much more.
- Run our regression fixtures (§3) against a v1.13.0 checkout AND a master checkout of upstream before starting feature work. Confirm identical outputs on our data. If outputs diverge, chase the upstream diff before proceeding.

---

## 2. Deployed pod facts (dev K8s)

### Container image

```
docker.io/vroomvrp/vroom-docker:v1.13.0
sha256:2e417553320bf68a25f6614ba8bdc31b0b2fc1172f8c25518c4656f488031c2a
```

The upstream image is maintained at https://hub.docker.com/r/vroomvrp/vroom-docker.

### Pod resources

```yaml
resources:
  requests: { cpu: 100m, memory: 128Mi }
  limits:   { cpu: 500m, memory: 512Mi }
```

### Ports and health

- Service port: `3000/TCP`
- Health endpoint: `GET /health` (used by K8s liveness + readiness probes)
- Liveness: initialDelay 10 s, period 30 s, timeout 1 s
- Readiness: initialDelay 5 s, period 10 s, timeout 1 s

### K8s location

- Context: `dev` (also exists in `prod2` for prod)
- Namespace: `bus-vdexpress` (dev) / `bus-vdexpress-prod` (prod)
- Service name (in-cluster DNS): `vroom`
- Consumer call: `http://vroom:3000/` (see `VroomClient::SERVICE_URL`)

---

## 3. Runtime configuration — `vroom-config` ConfigMap

Mounted at `/conf/config.yml` via subPath. **The fork's default config must accept a ConfigMap-mounted config.yml in this exact schema** — we don't want to rewrite our Helm/manifests.

```yaml
cliArgs:
  geometry: true
  planmode: false
  threads: 4
  explore: 5
  limit: '1mb'
  logdir: '/..'
  logsize: '100M'
  maxlocations: 1000
  maxvehicles: 200
  override: true
  path: ''
  port: 3000
  router: 'valhalla'
  timeout: 300000
  baseurl: '/'
routingServers:
  osrm:
    car:
      host: '0.0.0.0'
      port: '5000'
  valhalla:
    auto:          { host: 'valhalla', port: '8002' }
    bicycle:       { host: 'valhalla', port: '8002' }
    pedestrian:    { host: 'valhalla', port: '8002' }
    motorcycle:    { host: 'valhalla', port: '8002' }
    motor_scooter: { host: 'valhalla', port: '8002' }
    taxi:          { host: 'valhalla', port: '8002' }
    hov:           { host: 'valhalla', port: '8002' }
    truck:         { host: 'valhalla', port: '8002' }
    bus:           { host: 'valhalla', port: '8002' }
```

**Key takeaways**:
- Router: **Valhalla** (not OSRM). Fork must preserve this.
- Profile used in practice: `auto` (see `VroomClient::PROFILE` in backend-dispatch).
- Solve timeout: 300 s (wildly permissive — our P99 is <3 s).
- Concurrency: 4 threads (matches CPU limit of 500m = half a core; 4 threads doing tight JSON work).

---

## 4. Matrix backend — Valhalla

### Location

- In-cluster DNS: `valhalla:8002`
- Upstream: https://github.com/valhalla/valhalla
- Exposed endpoint used by VROOM: `/sources_to_targets` (VROOM's matrix-building API)

### For the agent's local dev

The fork's unit tests MUST NOT depend on Valhalla being up. Supply precomputed matrices for test fixtures. For integration tests, either:

- **Option A (recommended)**: spin up Valhalla in Docker Compose alongside VROOM using a minimal Ukraine tile subset. An example stack exists at https://gis-ops.com/valhalla-part-3-building-a-time-table/ but tiles are ~1 GB.
- **Option B**: mock Valhalla with a small Python stub that returns a canned matrix. Good enough for ~90% of tests.

### Tile coverage

Production Valhalla loads Ukrainian OSM tiles. Test fixtures must use coordinates inside that coverage OR the matrix call returns empty rows (VROOM's unassigned reason becomes "no route found").

Coordinate bounding box observed in fixtures: roughly `[49.0°–51.0°, 23.0°–27.0°]` (Western Ukraine).

---

## 5. Deployment flow the fork must fit

Backend-dispatch expects VROOM at `http://vroom:3000/`. The fork can deploy as either:

### Path A — Replace the existing service (risky)

1. Build fork image with the same name pattern (`<registry>/vroom:<tag>`).
2. Update existing Deployment to point to new image.
3. Rollback = revert Deployment.

### Path B — Deploy alongside, flip consumer (recommended for early milestones)

1. Deploy fork as `vroom-fork` service in the same namespace.
2. Consumer reads `VROOM_SERVICE_URL` env var (currently hardcoded to `http://vroom:3000/`; the fork work should include a consumer PR to make this configurable).
3. Flip `VROOM_SERVICE_URL` to `http://vroom-fork:3000/` on the backend-dispatch Deployment for dev UAT.
4. Both services run concurrently; rollback = flip the env var back.

**Recommended for all milestones M1–M5**: Path B. Less disruptive, easier rollback, lets us A/B compare.

### Image registry

- Upstream Docker Hub image: `vroomvrp/vroom-docker`
- Our fork should push to: `registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork`
- Credentials for push: provided by project owner (GitLab deploy token scoped to this project).
- Pull credentials in K8s: existing `itnet-registry` imagePullSecret already grants access to this registry.

### GitHub Actions → GitLab registry

A minimal GitHub Actions workflow that builds the image and pushes to our GitLab registry:

```yaml
# .github/workflows/docker-image.yml
name: Build and push Docker image
on:
  push:
    branches: [main]
    tags: ['v*']
jobs:
  build-push:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Login to GitLab registry
        uses: docker/login-action@v3
        with:
          registry: registry.gitlab.itnet.lviv.ua
          username: ${{ secrets.GITLAB_REGISTRY_USER }}
          password: ${{ secrets.GITLAB_REGISTRY_TOKEN }}
      - name: Build and push
        uses: docker/build-push-action@v5
        with:
          context: .
          push: true
          tags: |
            registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork:${{ github.sha }}
            registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork:${{ github.ref_name }}
```

Project owner needs to create GitHub secrets `GITLAB_REGISTRY_USER` and `GITLAB_REGISTRY_TOKEN` (GitLab deploy token with `write_registry` scope).

---

## 6. What the fork MUST preserve — contract with consumer

1. **Port**: 3000.
2. **Health**: `GET /health` returns 200 when ready.
3. **Solve endpoint**: `POST /` accepting VROOM JSON.
4. **`/diff` and `/counterfactual`**: NEW endpoints added by milestones M5/M6; MUST be additive.
5. **Config schema**: the YAML at `/conf/config.yml` must continue to parse on the fork — backward-compatible additions only.
6. **Response shape**: mainline fields (`code`, `summary`, `routes`, `unassigned`) unchanged. New fields (`cost_breakdown`, structured `unassigned.reason`) are additive.

Violating any of the above requires a consumer-side PR *and* a coordinated deployment. Avoid.

---

## 7. Build and run locally

```bash
# From a fresh clone of the fork
git clone https://github.com/gvoider/vroom.git
cd vroom
git remote add upstream https://github.com/VROOM-Project/vroom.git

# Build (match upstream's process)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Quick smoke test with a fixture
cd ..
./bin/vroom -i ../busportal1/ref/handoff/vroom-fork-fixtures/problem-shipments-3.json

# Start HTTP server (as vroom-express wraps the binary):
# Use the upstream vroom-express repo for now:
# https://github.com/VROOM-Project/vroom-express
```

### Regression check against mainline

After M0 scaffolding, the fork ships a script `scripts/regression.sh`:

```bash
./scripts/regression.sh ../busportal1/ref/handoff/vroom-fork-fixtures/
```

The script:
1. Builds the fork binary.
2. Runs each `problem-*.json` through both mainline VROOM (v1.15.0 pulled as submodule) and the fork.
3. Asserts the solution is byte-identical (to within float tolerance) when no new features are exercised.
4. Fails CI if any regression.

---

## 8. Secrets and authorization

The fork does NOT need:
- Database credentials
- Our GitLab source access
- Dispatcher UI credentials

The fork DOES need:
- Read-only access to our fixtures (they're checked into this repo)
- Ability to push to our GitLab Docker registry (project owner provides deploy token)
- Ability to deploy to dev K8s for UAT (project owner runs `kubectl apply`; the agent doesn't need direct kube access)

---

## 9. Observability

Current VROOM emits logs to stdout (picked up by container logs). No metrics endpoint. No tracing.

**Fork MAY add**:
- A `GET /metrics` endpoint emitting Prometheus text (our cluster has Prometheus scraping configured; adding the scrape config is a one-line ops task).
- Solve-time histograms by problem size.
- Unassigned-shipment counters by reason code.

Observability additions are OPTIONAL and P2 unless they materially help the UAT feedback loop.
