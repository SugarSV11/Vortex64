# Texture filters (GLideN64 texture enhancement)

GLideN64 is built from the untouched submodule `Source/GLideN64`. Three files of its texture enhancement library (GLideNHQ) are
replaced by patched copies in `Source/GLideN64-linux/patched/`; each starts with a note that says what changed. The submodule is not edited.

| file | change |
|---|---|
| `TxCache.cpp` | the cache evicts the entry that **owns** the list node, not any entry with the same checksum (use after free that corrupted the heap, seen with 5xBRZ/6xBRZ whose textures fill the 100 MB cache) |
| `TxFilter.cpp` | the bands of rows that the threads share are filtered **with the rows around them** (see below); threads only for textures of 128x128 or more |
| `TxUtil.cpp` | a platform that cannot tell its number of cores has one; `VORTEX_TXFILTER_THREADS` (tests only) sets the number of threads |

## Seams between bands (every filter, not only xBRZ)

Upstream cut a texture in bands of rows for its threads and filtered each band as a separate image: the rows at each cut were taken for
the border of the picture and came out different from the same rows of the whole image (a line of different pixels at every cut, repeated by every
tile of a repeating texture). It affected **every** enhancement and filter (`txfilter-seams-original-test` fails for all of them).

Every output row of these filters depends on a few source rows above and below it (the "halo"):

| filter | rows each side | strategy |
|---|---|---|
| xBRZ 2x-6x | up to 3 (library) | slices of the whole image (`xbrz::scale` with `yFirst`/`yLast`): no work repeated |
| 2x | 1 | band + halo |
| 2xSaI | 2 | band + halo |
| HQ2x, HQ2xS, LQ2x, LQ2xS, HQ4x | 1 | band + halo |
| smooth 1-4, sharp 1-2 | 1 | band + halo |
| deposterization | 2 (two vertical passes of one row) | added to the halo of the filter after it; xBRZ after deposterization uses band + halo |

Each thread filters its band plus the halo (the true border of the picture where the band touches it), keeps the rows of its band, so the result
is that of the whole image row for row. The halo is rounded up to a multiple of 4 rows. If a thread cannot get its memory, the texture is filtered whole.

Threads cost more than the filtering for small textures (measured with `txfilter-seams-test --bench`: up to 6x slower for 16x16, 2x for 64x64; they pay
off from about 256x256), so they are used from 128x128 up.

## Memory

Two buffers of `maxTextureSize^2 * 4` bytes (at most 4096: 64 MB each, allocated once), the source is never larger than `max / scale`. Per thread: the
band with its halo, scaled (`rows * width * scale^2 * 4`), and two scratch buffers of the band for deposterization.

## Known upstream behaviour kept as is

With an enhancement **and** deposterization **and** a smooth/sharp filter, the second pass (smooth/sharp, on the enlarged image) deposterizes again
(`DEPOSTERIZE` stays in the options). It is how the whole-image path has always worked; the threaded path gives the same result.

## Tests

* `txfilter-seams-test`: 51840 comparisons of every enhancement x (no filter, smooth 1-4, sharp 1-2) x deposterization, 9 sizes, 8 patterns made to show
  a seam, with 2, 3, 5 and 8 threads against one thread (the whole image). The original TxFilter fails it.
* `txcache-test`: the cache under AddressSanitizer with a cache much smaller than the textures; xBRZ slices against `xbrz::scale` of the whole image.
  `txcache-original-test` (the submodule's cache) must fail.
