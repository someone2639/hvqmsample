# Modern HVQM Sample
Modification/Partial rewrite of the N64SDK `hvqmsample` demo.

## Goals
 - Decode faster than the original demo (This repo can already playback at 24fps on console without breaking a sweat)
 - Allow the easiest possible integration into other N64 games and ROM Hacks
 - Have both video playback functionality and "render-to-texture" functionality
 - (_maybe_ in the far future) reimplement `libhvqm2`

## Why not just use MPEG-1 or -2?
If _you_ want MPEG in your N64 game, you can just use [ultrampeg](https://github.com/devwizard64/ultra_mpeg) (libultra) or the built-in support in libdragon.

HVQM is in a weird spot. MPEG encodes at basically real time through `ffmpeg` from any source format, while HVQM gets encoded at potentially tens of seconds per frame (somebody _please_ make `hvqm2enc.exe` faster), and requires a fully uncompressed AVI input file (which basically only VirtualDub2 supports). The average HVQM video's size is also probably worse than what can be achieved with modern tech.

HVQM uses a proprietary RSP accelerator microcode, which is annoying (if at all possible) to integrate into existing task schedulers, since it writes directly to the framebuffer in playback mode. The CPU-side library is also not source-available, which means any performance the original developers left on the table is locked behind a research project's level of reverse engineering or reimplementation.

Despite all this, HVQM is actually the most compatible N64 video format. It works on many popular emulators and real hardware at full resolution and at basically full speed, therefore it's worth using.