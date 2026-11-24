#!/usr/bin/env bash
# Start the nats-servers the test suite runs against, with throwaway TLS certificates.
#
#   tests/ci/start-servers.sh <work-dir>
#
# Needs nats-server (or $NATS_SERVER) and openssl. Servers:
#   4222  plain, JetStream enabled            (integration group)
#   4223  user "alice" / password "secret"    (tls group: auth)
#   4443  TLS, certificate issued by intermediate A
#   4444  TLS, certificate issued by intermediate B
# Prints NAME=value lines for the test environment: append them to $GITHUB_ENV, or
#   eval "$(tests/ci/start-servers.sh /tmp/natscpp-servers | sed 's/^/export /')"
# Stop the servers with tests/ci/stop-servers.sh <work-dir>.
set -euo pipefail

work=${1:?usage: start-servers.sh <work-dir>}
nats_server=${NATS_SERVER:-nats-server}
mkdir -p "$work"
work=$(cd "$work" && pwd)
certs=$work/certs
mkdir -p "$certs" "$certs/ca-path" "$work/js"

# --- Certificates: root -> intermediates A and B -> one server cert each, plus an unrelated root.
(
  cd "$certs"
  cat > ca.ext <<'EXT'
basicConstraints=critical,CA:true
keyUsage=critical,keyCertSign,cRLSign
EXT
  cat > leaf.ext <<'EXT'
basicConstraints=critical,CA:false
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=IP:127.0.0.1,DNS:localhost
EXT
  selfsigned() {  # name CN
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$1.key" -out "$1.pem" -days 2 -subj "/CN=$2" \
      -addext basicConstraints=critical,CA:true -addext keyUsage=critical,keyCertSign,cRLSign 2>/dev/null
  }
  issue() {  # name CN issuer extfile
    openssl req -newkey rsa:2048 -nodes -keyout "$1.key" -out "$1.csr" -subj "/CN=$2" 2>/dev/null
    openssl x509 -req -in "$1.csr" -CA "$3.pem" -CAkey "$3.key" -CAcreateserial -out "$1.pem" -days 2 \
      -extfile "$4" 2>/dev/null
  }
  selfsigned root natscpp-test-root
  selfsigned other-root natscpp-test-other-root
  for i in A B; do
    issue "int$i" "natscpp-test-int-$i" root ca.ext
    issue "srv$i" localhost "int$i" leaf.ext
    cat "srv$i.pem" "int$i.pem" > "srv$i-chain.pem"  # server sends its intermediate
    openssl verify -CAfile root.pem -untrusted "int$i.pem" "srv$i.pem" >/dev/null
  done
  cp root.pem ca-path/
  openssl rehash ca-path
)

cat > "$work/auth.conf" <<'CONF'
authorization {
  users = [ { user: "alice", password: "secret" } ]
}
CONF

start() {  # name args...
  local name=$1; shift
  nohup "$nats_server" "$@" > "$work/$name.log" 2>&1 &
  echo $! >> "$work/pids"
}
: > "$work/pids"
start js   -a 127.0.0.1 -p 4222 -js -sd "$work/js"
start auth -a 127.0.0.1 -p 4223 -c "$work/auth.conf"
start tlsA -a 127.0.0.1 -p 4443 --tls --tlscert "$certs/srvA-chain.pem" --tlskey "$certs/srvA.key"
start tlsB -a 127.0.0.1 -p 4444 --tls --tlscert "$certs/srvB-chain.pem" --tlskey "$certs/srvB.key"

for port in 4222 4223 4443 4444; do
  for _ in $(seq 1 100); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then continue 2; fi
    sleep 0.1
  done
  echo "nats-server on port $port did not start; logs in $work" >&2
  cat "$work"/*.log >&2
  exit 1
done

echo "NATSCPP_TEST_TLS_DIR=$certs"
echo "NATSCPP_TEST_TLS_URL_A=tls://localhost:4443"
echo "NATSCPP_TEST_TLS_URL_B=tls://localhost:4444"
echo "NATSCPP_TEST_AUTH_URL=nats://127.0.0.1:4223"
