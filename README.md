# Grape

Grape is a Geometry Dash bot for recording, replaying, rendering, frame stepping, trajectory tools, and gameplay assists.

This project is a modified version of [Silicate](https://git.puppy.lgbt/silicate/silicate). It remains licensed under [GPLv3](LICENSE). You may sell GPL software, but distributed builds must follow the license, including the corresponding-source requirements.

## Build

The public CI builds Android, macOS, and iOS packages. The Windows configuration is under `pc/`.

```sh
export GEODE_SDK=/path/to/GeodeSDK
cmake -S pc -B build -G Ninja
cmake --build build
```

## Corresponding source

Create the source archive for a built release with:

```sh
GEODE_SDK=/path/to/GeodeSDK scripts/make-source-bundle.sh build output/grape-source.tar.gz
```

Ship that archive and its generated SHA-256 file with the matching binary.
See [NOTICE](NOTICE) for provenance and third-party licensing information.
