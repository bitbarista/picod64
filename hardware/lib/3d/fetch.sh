#!/bin/bash
# Fetch the makers' 3D models into hardware/lib/3d/. They are NOT in the repo: none of the three makers
# allows redistribution (THIRD-PARTY.md). The footprints already point here, placed by hand
# (gen/gen_footprints.py MODELS), so the files only need to arrive under these names.
#   bash hardware/lib/3d/fetch.sh
set -e; cd "$(dirname "$0")"; T=$(mktemp -d)
ok() { echo "$2  $1" | sha256sum -c --quiet - && echo "ok  $1"; }
[ -f Lumberg_0105_06.igs ] || { curl -fsSL -o $T/l.gz https://downloads.lumberg.com/3dmodelle/lumberg_0105_06.igs.gz
                                 gunzip -c $T/l.gz > Lumberg_0105_06.igs; }
ok Lumberg_0105_06.igs 8b23a30675778d97a75e49dcddfc5df319e1249cdbfc157e99e07d564d84ab65
[ -f Alps_EC11E-05SW.step ] || { curl -fsSL -o $T/a.zip https://tech.alpsalpine.com/cms.media/product_3dcad_ec11e15244g1_en_9e13dba9a5.zip
                                  unzip -p $T/a.zip EC11E-05SW.STEP > Alps_EC11E-05SW.step; }
ok Alps_EC11E-05SW.step c5e05e6ce6dcd486fd79ff67869c7a32e2baa8a1f69a2a737dc472d8f4ff43fb
if [ -f GCT_MEM2075-00-140-01-A.step ]; then ok GCT_MEM2075-00-140-01-A.step ddc0a35e7410ed7cb9487e6e8790dd91f749b34d66351cf77ce35fb6859e7cff || true
else echo "GCT_MEM2075-00-140-01-A.step: download by hand (gct.co, part MEM2075-00-140-01-A, STEP; needs a login) and save it here"; fi
rm -rf $T
