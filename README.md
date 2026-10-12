# MarioKartPSP — Performance LOD update

This source package is based on the supplied graphics/gameplay fix. This iteration focuses on reducing the rendering cost on PSP hardware:

- Distant rival karts skip small sub-meshes while retaining the main kart silhouette.
- Distant rival drivers and shadows are not rendered; close-range drivers remain visible, including during collisions.
- Distant particle effects are culled.
- AI boost flames use one layer and AI star effects use fewer spark meshes; the player's effects remain full quality.
- Wheel animation is limited to closer rivals.

The changes are distance-based rendering optimizations. They do not alter race physics or item logic. The project has not been built with PSPDEV or tested on a PSP in this environment; run GitHub Actions to verify compilation and test the resulting EBOOT.PBP on hardware.

## Repository root
- `main.c`
- `char_models.h`
- `kart_models.h`
- `CMakeLists.txt`
- `Makefile`
- `assets_pack.zip`
- `.github/workflows/main.yml`
