# Validation before direct USB printing

`lj1005 validate input.zjs` validates a print file without accessing hardware.
Success reports the encoded page count, sheet count including copies, and total
decoded raster bytes. Failure returns exit code 1 with a reason.
`lj1005 usb-print input.zjs` performs the identical check on its in-memory input
before USB enumeration, device identity queries or writes. It transmits those
same bytes only after validation and the existing firmware-readiness check.

## Accepted profile

The validator accepts the project's original-1005 profile: big-endian `JZJZ`,
one complete document, one monochrome JBIG image per page, 600 dpi, simplex,
automatic paper source, normal media, RET enabled and economode disabled.
A4 and Letter may be mixed within a document. Raster dimensions plus any offsets
must fit 4960 x 7016 pixels for A4 or 5100 x 6600 for Letter. Video, raster and
JBIG dimensions must agree. Page numbers start at one and are consecutive.
The document page-count field may be zero (unspecified) or the actual count.
Document quantity is one; page copies may be 1..999.

The required record sequence is START_DOC, then one or more repetitions of
START_PAGE / JBIG_BIH / JBIG_BID(s) / END_JBIG / END_PAGE, then END_DOC.
Duplicate/missing items, unsupported tags, invalid item types, unknown records,
plane markers, nested documents and bytes after END_DOC are rejected. Item order
may vary. Empty documents are rejected.

The JBIG header must match the native encoder's single-plane, single-layer
profile, including its stripe size and coding options. Each image is decoded
with bounded dimensions; the decoded dimensions and byte count must match the
page. The final data block must have the native profile's four-byte alignment
and 16 extra zero bytes. Nonfinal data blocks are 65536 bytes. Extra compressed
images, nonzero padding and missing or excessive padding are rejected.
The profile follows the existing encoder and the pinned
[OpenPrinting reference implementation](https://github.com/OpenPrinting/foo2zjs/blob/80499ed5bf6caa2963ad337e37cfda78a80aab1e/foo2zjs.c).

## Resource limits and scope

- Input: 128 MiB per job.
- Decoded rasters: 128 MiB across the job, counted once per encoded page.
- Output: at most 1000 sheets, including page copies.
- Compressed image: 64 MiB per page; at most 65536 ZJS records per job.
- Memory: the input, one compressed image and one decoder are retained; decoded
  pages are released individually instead of collecting the entire job.

These limits bound parsing and decoding work; they are not a wall-clock timeout
or certification of arbitrary ZJS dialects. Some otherwise valid third-party
jobs use unsupported options and will be refused. `inspect` remains a record
listing tool, and `decode` remains an offline raster extraction tool; neither
command substitutes for `validate` when deciding whether to send a file.

The encoder does not automatically resize PBMs to fit the selected paper. An
oversized encoded image is refused by print validation. A successful validation
does not establish paper availability, physical output or device completion.
This change applies to direct CLI file submission. It does not buffer or change
the installed Windows spooler pipeline, which generates its own ZJS records.

## Regression checks

Protocol tests cover every truncated prefix of a valid small job, omitted and
duplicated record types, joined documents, malformed headers/items, unsupported
settings, mismatched geometry, changed JBIG headers, corrupt image data,
padding, offset boundaries, mixed media, copy limits and the decoded-raster
budget. Random raster tests exercise multi-block images. A CLI test verifies
that a signature-only file fails in both `validate` and `usb-print` with the
validation error before any device lookup is reached.

The five clean-VM capture scenarios and the repaired mixed-paper ResetDC capture
pass; the historical duplicate-document-header capture fails. The existing
encoder and its wire output are unchanged. No physical job is needed for these
checks. Dated evidence and package results are recorded in `TEST_STATUS.md`.
