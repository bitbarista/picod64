# 3D models (not in the repo)

The KiCad footprints use the makers' own 3D models for J1/J2 (Lumberg 0105 06), J3 (GCT MEM2075) and SW1 (Alps EC11E). None of the three makers allows the models to be redistributed ([`THIRD-PARTY.md`](../../../THIRD-PARTY.md)), so they are not committed.

Run `bash hardware/lib/3d/fetch.sh`. It downloads the Lumberg and Alps models and checks them against the files the board was designed with (SHA-256). The GCT model needs a login, so you download it by hand; the script says how. Without these models the board is unchanged, and only the 3D view and renders are missing those parts.
