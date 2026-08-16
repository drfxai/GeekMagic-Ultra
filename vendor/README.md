# vendor/

Third-party material, kept verbatim. Nothing in here is part of DrFX Ultra OS
and nothing in here is built by this repository.

## stock-firmware/

GeekMagic's own firmware for the SmallTV Ultra, version 9.0.50 — the image the
device shipped with.

| File | What it is |
|---|---|
| `FW-Smalltv-Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.bin` | the stock image, 505,200 bytes |
| `FW-Smalltv-Ultra-V9.0.50/md5sum.txt` | its checksum, as published |
| `FW-Smalltv-Ultra-V9.0.50.zip` | the same image as originally distributed |
| `update_history.txt` | GeekMagic's release notes for 9.0.x |

**Why it is here.** Installing DrFX Ultra OS replaces the stock firmware
completely and erases the photo album. This is the way back. Keeping it in the
repository means recovery does not depend on a third-party download page still
being up years from now.

**To go back to stock**, flash `FW-Smalltv-Ultra-V9.0.50.bin` the same way you
flashed DrFX Ultra OS: open the device's settings page, go to
**Admin → Firmware update**, and upload it. Verify the checksum first (below).
If the device will not boot far enough to serve that page, flash over UART with
`esptool` instead.

Verify before flashing:

```bash
md5sum vendor/stock-firmware/FW-Smalltv-Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.bin
# expected: a5fe50093ada3c2637225bbf306a62c3
```

The `.zip` and the extracted `.bin` are the same image. Both are kept because the
checksum published by GeekMagic refers to the extracted file while the zip is
what their download actually hands you — having both means either can be checked
against what you downloaded yourself.

**Ownership.** These files are GeekMagic's, redistributed here only so that a
device flashed with this project can be returned to stock. They are not covered
by this repository's MIT licence. If GeekMagic would rather they were not
mirrored here, open an issue and they will be removed.
