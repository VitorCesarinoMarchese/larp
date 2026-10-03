# Run authenticated sessions over Tailscale

Milestone 12 keeps Tailscale routing and adds a shared-key session layer for
Linux senders and receivers. Both processes must use the same private key.
Video and adaptive feedback are encrypted and authenticated. The receiver
also requires the expected sender's IPv4 address.

## Create and copy a key

Build both machines with libsodium 1.0.18 or newer, then create a key:

```sh
./build-release/larp-keygen ./session.key
scp ./session.key receiver-host:/path/to/session.key
ssh receiver-host 'chmod 600 /path/to/session.key'
```

`larp-keygen` creates a random 32-byte file with mode `0600` and refuses to
overwrite an existing file. Keep each copy owned by the account that runs
L.A.R.P. Key loading rejects symlinks, nonregular files, wrong lengths, and
group or other permission bits. Do not commit keys or include their bytes
in logs. Use a different key for each sender and receiver pair.

## Send encrypted video

On the receiver, replace the example addresses with the two Tailscale IPv4
addresses and start:

```sh
./build-release/larp-client --view-h264 192.0.2.20 5000 20 \
  --key-file /path/to/session.key --peer 192.0.2.10
```

On the sender, start synthetic video:

```sh
./build-release/larp-host --h264-synthetic 192.0.2.20 5000 450 30 \
  --adaptive 2000 --key-file ./session.key --bind 192.0.2.10
```

`--bind` selects the sender's source IPv4 address. It is useful when several
network interfaces are present. The same key options work with synthetic-byte,
raw capture, H.264 capture, and snapshot modes. Live capture still requires
the existing portal selection. No new portal selection is needed for
synthetic video or headless tests.

The sender waits up to five seconds for initial authentication and exits with
an error if it fails. Secure receivers accept no plaintext fallback. A
receiver requires both `--key-file` and `--peer`; neither option is inferred
from the first packet.

To recover from a process restart, keep the other process running and restart
the stopped process with the same address, receiver port, and key. The sender
detects missing heartbeat replies within about one second and negotiates a
fresh session. Recovery continues within the sender's existing frame or time
limit. Frames produced during reconnection are discarded rather than queued.
A restarted sender can replace an idle session after one second without
authenticated media.

The receiver resets incomplete frame state and its H.264 decoder for a fresh
session, while preserving cumulative statistics. Adaptive senders preserve
their current bitrate and establish a fresh feedback baseline.

Secure commands print `Sessions`, `Security rejected`, and `Unsent`.
`Sessions` counts authenticated session generations. `Security rejected`
counts envelopes rejected before application processing. `Unsent` counts
application datagram attempts withheld while no authenticated session exists,
including adaptive probes. Sender packet and encoded-frame counters still
include attempts made during recovery. They are not confirmed delivery counts.

## Verify while the screen is locked

Run the production session integration test:

```sh
ctest --test-dir build-release -R '^secure-sessions$' --output-on-failure
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize \
  -R '^secure-sessions$' --output-on-failure
```

The test uses SDL's dummy display when rendering is enabled, or decoded
snapshots otherwise. It checks tampering, forged high counters, duplicates,
bounded reordering, retired-session replay, wrong keys and peer addresses,
key-file validation, sender and receiver restarts, and a controlled network
blackhole. A synthetic-byte check also verifies heartbeat processing between
one-second frame intervals. No desktop image is captured or displayed.

For two-machine acceptance, build the same source on both machines and run:

```sh
python3 benchmarks/secure_tailscale.py build-release \
  --remote receiver-host --remote-build /path/to/build-release \
  --local-ip LOCAL_TAILSCALE_IP --remote-ip REMOTE_TAILSCALE_IP
```

This check creates an ephemeral key, copies it through SSH, and removes both
copies on completion. It restarts a local receiver while the remote sender
continues, then restarts a local sender while the remote receiver continues.
Both directions use synthetic video and dummy displays. It requires decoded
recovery, zero corruption, presentation of every decoded frame, and fresh
feedback after receiver restart. Run it on otherwise idle machines.

The [protocol reference](protocol.md#authenticated-session-envelope) describes
the wire format and cryptographic limits. This shared-key prototype has no
forward secrecy or independent security audit. A compromised key exposes
recorded sessions and permits impersonation of either peer. Tailscale access
policy and secure key distribution remain part of deployment.

See [the acceptance results](session-verification.md) for the verified builds,
restart measurements, and remaining milestones.
