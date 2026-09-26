#!/usr/bin/env python3
"""Words saved from the reader's dictionary reach AnkiWeb through a sync.

With the local sync server as AnkiWeb: a device posts two saved words beside
its (empty) review payload, the bridge acknowledges them, adds them as notes
in a deck per language, pushes, chooses the new deck for the reader and ships
it built. A second post of the same word adds nothing; a malformed batch is
refused whole.

Run: .venv/bin/python tests/test_words.py
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(REPO / "tools_local" / "study"))
sys.path.insert(0, str(HERE))
from portguard import assert_alive, popen_group, reap, require_free_port  # noqa: E402

PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996")) + 3
USER, PW = "mario", "words-pw"
ENDPOINT = f"http://127.0.0.1:{PORT}/"

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


def wait_port(port, timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = socket.socket()
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            s.close()
            return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("sync server never opened its port")


def saved_word(word, dictionary, entry, context=""):
    head = f"word: {word}\ndictionary: {dictionary}\nlanguage: {dictionary[:2]}\nformat: html\n"
    if context:
        head += f"context: {context}\n"
    return (head + "\n" + entry).encode("utf-8")


MOOI = saved_word(
    "mooi",
    "nl-ru",
    '<div><font class="grammar">adjective</font></div>1. prettig om te zien<div>красивый</div>',
    "Wat een mooi meisje.",
)
HUS = saved_word(
    "hús", "is-ru", '<p>Существительное, средний род.</p><ol><li><a href="bword://дом">дом</a></li></ol>'
)


def payload(words, decks=()):
    header = json.dumps({"decks": list(decks), "words": [{"file": f, "len": len(b)} for f, b in words]}).encode()
    return struct.pack("<I", len(header)) + header + b"".join(b for _, b in words)


async def wait_job(web, dev, job):
    for _ in range(900):
        await asyncio.sleep(0.1)
        r = await web.get("/api/sync/status", headers=dev, params={"job": job})
        if r.json()["status"] in ("done", "error", "frozen"):
            return r.json()
    return r.json()


async def run(tmp):
    import httpx
    from anki.collection import Collection
    from cryptography.fernet import Fernet

    from bridge import decks as decks_mod
    from bridge import store as store_mod
    from bridge.app import app

    decks_mod.TOOLS = REPO / "tools_local" / "study"

    desktop = Collection(str(tmp / "desktop.anki2"))
    auth = desktop.sync_login(USER, PW, ENDPOINT)
    nt = desktop.models.by_name("Basic")
    note = desktop.new_note(nt)
    note["Front"], note["Back"] = "uno", "UNO"
    desktop.add_note(note, desktop.decks.get_current_id())
    out = desktop.sync_collection(auth, sync_media=False)
    if out.required:
        desktop.full_upload_or_download(auth=auth, server_usn=out.server_media_usn, upload=True)

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="https://bridge") as web:
        r = await web.post("/login", data={"username": USER, "password": PW})
        cookie = r.cookies.get("bridge_session")
        session = json.loads(Fernet(os.environ["BRIDGE_FERNET_KEY"].encode()).decrypt(cookie.encode()))
        pair = (await web.post("/api/pair/start")).json()
        await web.post("/api/pair/claim", data={"code": pair["code"], "csrf": session["csrf"]})
        token = (await web.get("/api/pair/poll", params={"pollToken": pair["pollToken"]})).json()["deviceToken"]
        dev = {"Authorization": f"Bearer {token}"}
        await web.post("/api/decks/choose", headers=dev, json={"decks": ["Default"]})

        # --- A batch the bridge cannot take is refused whole, and keeps nothing.
        bad = payload([("nl/mooi.txt", MOOI), ("../../state.json", HUS)])
        r = await web.post("/api/sync", headers=dev, content=bad)
        ok(r.status_code == 400, f"a path outside the inbox must be refused, got {r.status_code}")
        st = store_mod.UserStore(store_mod.uid_for(USER))
        ok(not (st.root / "words.jsonl").exists(), "and nothing of the batch is kept")

        # --- Two words, two languages.
        r = await web.post("/api/sync", headers=dev, content=payload([("nl/mooi.txt", MOOI), ("is/hús.txt", HUS)]))
        body = r.json()
        ok(body.get("wordsAccepted") == ["nl/mooi.txt", "is/hús.txt"],
           "the bridge names the words it now holds, so the reader can delete them")
        status = await wait_job(web, dev, body["job"])
        ok(status["status"] == "done", f"the sync should finish, got {status}")
        ok(status["summary"].get("words") == 2, "both words become notes")
        ok(not (st.root / "words.jsonl").exists(), "the words are dropped once the push is confirmed")

        chosen = (await web.get("/api/decks", headers=dev)).json()["chosen"]
        ok("Dictionary::Dutch" in chosen and "Dictionary::Icelandic" in chosen,
           "each language's deck is chosen for the reader without being asked")
        built = {m["deck"] for m in status["summary"]["manifests"]}
        ok("Dictionary::Dutch" in built, "and built on the same sync, so the words come back as cards")

        # --- AnkiWeb has them: a fresh pull on the desktop sees the notes.
        out = desktop.sync_collection(auth, sync_media=False)
        if out.required:
            desktop.full_upload_or_download(auth=auth, server_usn=out.server_media_usn, upload=False)
        nids = desktop.find_notes('"deck:Dictionary::Dutch"')
        ok(len(nids) == 1, "the Dutch word reached AnkiWeb")
        if nids:
            n = desktop.get_note(nids[0])
            ok(n["Word"] == "mooi" and n["Meaning"] == "красивый", "with the card-sized meaning")
            ok(n["Sentence"] == "Wat een mooi meisje.", "and the sentence it was read in")
        ok(len(desktop.find_notes('"deck:Dictionary::Icelandic"')) == 1, "the Icelandic word too")

        # --- The same word again: the reader deleted it, then saved it anew.
        r = await web.post("/api/sync", headers=dev, content=payload([("nl/mooi.txt", MOOI)]))
        status = await wait_job(web, dev, r.json()["job"])
        ok(status["summary"].get("words") == 0, "a word already in its deck is not added twice")

        # --- A sync with no words is the old wire, unchanged.
        header = json.dumps({"decks": []}).encode()
        r = await web.post("/api/sync", headers=dev, content=struct.pack("<I", len(header)) + header)
        ok(r.json().get("wordsAccepted") == [], "a payload without words is still accepted")
        await wait_job(web, dev, r.json()["job"])

    desktop.close()


def main():
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bridge-words-"))
    os.environ["BRIDGE_DATA"] = str(tmp / "data")
    from cryptography.fernet import Fernet

    os.environ["BRIDGE_FERNET_KEY"] = Fernet.generate_key().decode()
    os.environ["BRIDGE_ALLOWLIST"] = USER
    os.environ["BRIDGE_ANKIWEB_ENDPOINT"] = ENDPOINT
    server = None
    try:
        env = dict(
            os.environ,
            SYNC_USER1=f"{USER}:{PW}",
            SYNC_BASE=str(tmp / "server"),
            SYNC_HOST="127.0.0.1",
            SYNC_PORT=str(PORT),
        )
        require_free_port(PORT, "the sync server")
        server = popen_group(
            [sys.executable, "-m", "anki.syncserver"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
        )
        wait_port(PORT)
        assert_alive(server, "the sync server")
        asyncio.run(run(tmp))
        print(f"{checks} checks, {failures} failed")
        sys.exit(1 if failures else 0)
    finally:
        reap(server)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
