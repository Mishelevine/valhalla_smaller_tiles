# Local verification, 2026-10-28

Both images were built from `docker/Dockerfile.locate` on this Mac:

| Image | Platform | Image size reported by Docker |
| --- | --- | --- |
| `valhalla-locate:arm64` | Linux ARM64, native | 157,163,529 bytes |
| `valhalla-locate:amd64` | Linux AMD64, emulated | 135,210,259 bytes |

The tile archive was `/Users/misha/Backups/tiles/valhalla_tiles.tar`.
It contains 86,331 tiles and an `index.bin`; each test container mounted it read-only.
The native standard Valhalla CLI used the same TAR and matching Loki/cache defaults
as the reference. The TAR was not included in either image.

## Functional results

`docker/locate/test_locate.py` passed all 7 test groups on each final image:

- 30 location/cutoff combinations, including a location outside the dataset.
- The supplied request, changing cutoffs and repeating a previous request.
- Defaults for radius, reachability, cutoff and node snapping.
- A request containing two locations.
- 32 requests submitted by 8 concurrent clients.
- Rejection of all other Valhalla endpoints and an unknown endpoint.
- Malformed JSON, missing locations and invalid latitude, followed by a valid request.

Each run issued 87 HTTP requests and made 36 exact comparisons of parsed JSON
against the standard CLI. No coordinate rounding, edge sorting or tolerance was
applied in the comparison. The profile was also checked with two server workers;
the final images were tested with the default single worker.

For the supplied Moscow request with `search_cutoff: 50`, both images returned
two directed edges for OSM way `39949793`, with correlated coordinates
`55.751224, 37.618351`.

An independent Postman CLI check of the supplied JSON passed 3 assertions:
HTTP 200, one location, and nonempty road candidates. Event reporting was disabled.

The ARM64 image exits with code 1 when its TAR is not mounted. The generated
Compose configuration resolves the supplied TAR path as a read-only bind mount.
Runtime inspection confirmed a single service executable in `/usr/local/bin` and
no tile-building tools, Python bindings or full `libvalhalla` installation.
All four test containers shut down with exit code 0 on SIGTERM and were removed.
The AMD64 image was exported to `/tmp/valhalla-locate-amd64.tar`; its saved
manifest and configuration were checked to contain the Linux AMD64 image.
The final C++ files passed clang-format 11's dry-run check.

## Existing native tests

Before and after the changes, `gurka_locate` passed all 34 tests when launched
from the build directory. The current macOS compiler needed a local override
for the existing unused lambda capture at `test/gurka/test_locate.cc:350`;
the test source was not changed.

The additional `gurka_route` run passed 36 of 38 tests. `DateTimeTest.DepartAt`
and `DateTimeTest.ArriveBy` failed with missing timezone values; the existing
`build/test/data/tz.sqlite` is an empty file. These fixtures were not repaired
as part of the locate image work.

No native AMD64 server, production deployment, load test or latency benchmark was
performed. Emulated AMD64 execution establishes functional compatibility here.
