#!/usr/bin/env sh
# vroom-entrypoint — copy the ConfigMap-mounted config into the location
# vroom-express actually reads (its WORKDIR / config.yml). Setting
# VROOM_CONFIG isn't enough — vroom-express ignores it and resolves
# ./config.yml relative to its working directory. Documented in
# handoff/vroom-fork-env.md (ConfigMap mounted at /conf/config.yml).

set -eu

if [ -f /conf/config.yml ]; then
  # Source ConfigMap file is read-only; write the copy into the
  # vroom-express working directory so its default loader finds it.
  cp /conf/config.yml /opt/vroom-express/config.yml
fi

exec "$@"
