# Locate-only Valhalla

This image is built from this fork with the 2 / 0.5 / 0.125 degree tile hierarchy.
It serves `/locate` using Valhalla's existing parser, correlation and JSON serializer.
The standard Valhalla build remains available when `ENABLE_LOCATE_ONLY` is off.

The runtime contains one service executable and its shared dependencies. It excludes
the routing workers, Odin, Meili, Skadi, tile-building tools and language bindings.
Loki retains the Dijkstra helper used by its reachability code. The supplied profile
sets reachability to zero, so that graph expansion is skipped.

## Build and run on ARM64

Run these commands from the repository root:

```sh
docker buildx build --platform linux/arm64 --load -f docker/Dockerfile.locate -t valhalla-locate:arm64 .
docker run --rm --name valhalla-locate-arm64 --platform linux/arm64 -p 8002:8002 --mount type=bind,source=/Users/misha/Backups/tiles/valhalla_tiles.tar,target=/custom_files/valhalla_tiles.tar,readonly valhalla-locate:arm64
```

Send the example request from another terminal:

```sh
curl --fail-with-body -H 'Content-Type: application/json' --data-binary @docker/locate/request.json http://localhost:8002/locate
```

Only `locations[].search_cutoff` needs to change in the example. It is in meters.
If omitted, the configured default is 100. Radius and minimum reachability default
to zero, as does node snapping tolerance. Connectivity indexing is disabled.

The three cache flags are false as requested. This selects a separate FlatTileCache
for each worker; it does not disable caching. The default worker count is one.
To change it, append `/etc/valhalla/valhalla.json 2` after the image name.

The TAR is mounted read-only and must contain uncompressed `.gph` tiles from this
fork, preferably with the `index.bin` created by `valhalla_build_extract`. A TAR of
`.gph.gz` files or a `.tar.gz` archive cannot be used as a memory-mapped extract.
The service exits with an error if the extract cannot be loaded.

## Build for the AMD64 server

```sh
docker buildx build --platform linux/amd64 --load -f docker/Dockerfile.locate -t valhalla-locate:amd64 .
```

On ARM hosts this build uses emulation. Build success and functional checks under
emulation do not measure native AMD64 performance.

To transfer the image without a registry:

```sh
docker save -o /tmp/valhalla-locate-amd64.tar valhalla-locate:amd64
```

On the server, load the image and bind the server's actual tile archive path:

```sh
docker load -i /tmp/valhalla-locate-amd64.tar
```

Alternatively, build both platforms and publish a manifest to your own registry
with `docker buildx build --platform linux/amd64,linux/arm64 --push` and a registry
tag. Both variants use the same Dockerfile and configuration.

## Compose

Copy `compose.yaml` into the destination project. It uses an existing local image;
it has no build context and does not require Valhalla source files. Pulling images
from a registry is disabled. Load the exported AMD64 image on the server first:

```sh
docker load -i valhalla-locate-amd64.tar
docker compose up -d
```

The defaults are image `valhalla-locate:amd64` and a tile archive named
`valhalla_tiles.tar` beside the copied Compose file. Set `VALHALLA_TILE_EXTRACT`
to use an archive elsewhere. Optionally copy `.env.example` to `.env` beside
`compose.yaml` and adjust the image and archive path there.

For the existing ARM64 image and tile archive on this Mac, run from the directory
containing the copied Compose file:

```sh
VALHALLA_IMAGE=valhalla-locate:arm64 VALHALLA_TILE_EXTRACT=/Users/misha/Backups/tiles/valhalla_tiles.tar docker compose up -d
```

## Verification

```sh
python3 docker/locate/test_locate.py http://localhost:8002
```

For response comparison, generate a standard config using this repository's
`scripts/valhalla_build_config`, pointing to the same TAR and matching the image's
cache and Loki defaults. Then pass `--reference-binary /absolute/path/to/valhalla_service`
and `--reference-config /absolute/path/to/config.json` to the test script.
The script compares parsed JSON, including coordinate and edge order, without
normalizing differences. The regional checks use Moscow, Kyiv, Yekaterinburg and
Vladivostok; the supplied Russia/Ukraine archive is needed for the Moscow assertion.

The checks cover different cutoffs, default values, repeated and concurrent calls,
multiple locations, missing roads, invalid requests and disabled endpoints.
They are functional checks, not a latency or throughput benchmark.
