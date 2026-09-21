#!/usr/bin/env python3
"""Upload packages to apt.flussonic.com: a signed PUT of every file given.

The repository's own client asks first whether the file is there and takes
"there but different" for done, so a package rebuilt under the same version
never replaces the one on the server. This puts unconditionally; the server
overwrites and re-indexes the channel.

usage: REPOSITORY_SECRET=... upload.py CHANNEL FILE...   (CHANNEL: master, binary)
"""
import hashlib
import http.client
import os
import sys
import time

HOST = "apt.flussonic.com"
PROTOCOL_VERSION = "2"


def put(channel, path, secret):
    name = os.path.basename(path)
    with open(path, "rb") as f:
        data = f.read()
    sha1 = hashlib.sha1(data).hexdigest()
    stamp = str(time.time())
    digest = hashlib.sha1(f"{name}:{stamp}:{secret}:{sha1}".encode()).hexdigest()
    conn = http.client.HTTPConnection(HOST, timeout=120)
    conn.request("PUT", f"/__upload__/repo/{channel}/{name}", data, {
        "Content-SHA1": sha1,
        "X-Upload-Version": PROTOCOL_VERSION,
        "Timestamp": stamp,
        "Digest": digest,
    })
    reply = conn.getresponse()
    body = reply.read().decode(errors="replace").strip()
    print(f"{channel}/{name}: {reply.status} {body[:120]}")
    return reply.status == 200


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    secret = os.environ["REPOSITORY_SECRET"]
    ok = all([put(sys.argv[1], p, secret) for p in sys.argv[2:]])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
