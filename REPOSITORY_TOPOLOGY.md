# Recommended Repository Topology

```text
umicom-framework
    portable user-space platform
         ^
         |
Umicom application modules
    thin product compositions

umicom-os
    installable distribution product
    GNU Linux-libre production profile
    recovery/images/packages/security/installation
         |
         | optional future pin for explicit experimental profile
         v
umicom-kernel
    independent original native-kernel source
```

Never add `umicom-kernel` as a submodule of Framework or Applications.

After the first native-kernel integration milestone, `umicom-os` may pin it under a clearly experimental path, for example:

```text
umicom-os/
    experimental/
        umicom-kernel/   -> submodule
```

Do not silently replace the existing `kernel/` Linux configuration area.
