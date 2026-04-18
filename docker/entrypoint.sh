#!/usr/bin/env sh
# vroom-entrypoint — mount /conf/config.yml if the consumer provides one,
# otherwise run with vroom-express defaults. Matches the config mount path
# documented in handoff/vroom-fork-env.md (ConfigMap at /conf/config.yml).

set -eu

if [ -f /conf/config.yml ]; then
  export VROOM_CONFIG=/conf/config.yml
fi

exec "$@"
