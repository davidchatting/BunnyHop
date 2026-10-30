# Research material

Third-party files consulted to establish the rabbit's protocol (see
[docs/PROTOCOL.md](../docs/PROTOCOL.md)). Kept for reference; nothing here is
built into the firmware except `bootcode.violet`, which is copied to
`firmware/data/vl/bootcode.bin`.

All files below come from **OpenJabNab** - https://github.com/OpenJabNab/OpenJabNab
(GPL-2.0), fetched 2026-10-30 from the `master` branch.

| Path here | Upstream path | What it is |
|---|---|---|
| `bootcode.violet` | `http-wrapper/ojn_local/bootcode/bootcode.violet` | Violet's original compiled bootcode (bytecode rev 21029) - **served to the rabbit, unmodified** |
| `bootcode.ojn` | `http-wrapper/ojn_local/bootcode/bootcode.ojn` | OpenJabNab's patched bootcode (not used) |
| `violet-src/*.mtl` | `bootcode/sources/` | Violet bootcode sources (Metal language), by Sylvain Huet / Violet |
| `ojn/*.cpp`, `ojn/*.h` | `server/lib/`, `server/plugins/` | OpenJabNab server: XMPP handler, auth, packets, choreography, locate, RFID, ears |

The bootcode sources are an older revision than the 21029 bytecode actually
served; ServerlessNabaztag (https://github.com/andreax79/ServerlessNabaztag)
was used as a cross-check for that revision.
