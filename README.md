Stripped down VBA-M port based on VBA-M Nx, itself based on VBA-M with some speed related changes.
This is mostly for the RS-97 but it can also work with the GCW0.

=======

# Visual Boy Advance M NX

**WIP**

This is going to be the sucessor to https://github.com/RSDuck/vba-next-switch, but is currently not in a useable state.

Based upon https://github.com/visualboyadvance-m/visualboyadvance-m

## Building

[Devkitpro](https://devkitpro.org/) and libnx, freetype and zlib are required for building.

```
pacman -S switch-freetype
pacman -S switch-zlib

git clone https://github.com/RSDuck/vba-m-nx.git

cd vba-m-nx

make
```
