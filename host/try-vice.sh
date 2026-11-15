#!/bin/sh
# The browser in VICE, on a folder of the PC:  sh host/try-vice.sh FOLDER   (see host/try-vice.py)
exec python3 "$(dirname "$0")/try-vice.py" "$@"
