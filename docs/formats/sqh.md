# `.sqh` resource archives

`res0.sqh` (386 MB, 16,383 files) and `res1.sqh` (2.8 MB, 983 files) hold every
game asset. The engine class that reads them is `GSquishFile` (RTTI). Despite
the name, the payloads are **not compressed**: files such as `.wav` (RIFF),
`.mp3` (LAME) and `.gsc` (plain text) are stored as-is.

Reader: `src/formats/sqh.cpp`. Tests: `tests/test_sqh.cpp`.

## Layout

```
u32le  tableSize            size of the directory table, including this field
byte   table[tableSize-4]   directory tree (below)
byte   payload[]            file contents, packed back to back in table order
```

Directory node:

```
u8     nameLen              length of name including its NUL
char   name[nameLen]        lower case, NUL terminated
u32be  span                 bytes from the end of this field to the next sibling;
                            0xFFFFFFFF = this is the last sibling
-- file nodes only --
u32be  offset               absolute offset of the payload
u32be  size
```

For a file `span` is 8 (it skips the offset/size pair). For a directory it is
the byte length of its children; a directory with `span` 0 is empty.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Table size is the leading u32le | proven | Equals the first payload offset in both archives; tests check it |
| Sibling walk with `span`, -1 = last | proven | Lookup routine `FUN_0048fb20` skips `1 + nameLen + 4 + span` on mismatch and returns not-found when `span < 0` |
| offset/size are big-endian | proven | Same routine's caller `FUN_0048e7d0` assembles both from bytes MSB-first and hands them to the `GSquishFile` constructor |
| Payloads are contiguous and fill the file | proven | Test: sorted entries have no gaps and end exactly at the file size |
| Enumeration rule "a name with a `.` is a file" | proven equivalent | The engine never enumerates; it only looks paths up. The test resolves all 17,366 enumerated paths through a port of the engine's lookup and gets identical offset/size for each |
| Lookup is case-insensitive | likely | Engine lower-cases the path (`FUN_00510620`, which calls a CRT routine that looks like `_strlwr`) and folds `/` to `\`; all stored names are lower case |
| `.tga` requests are served from `.gim` entries | likely | `FUN_0048e7d0` swaps a trailing `tga` for `gim` before the archive lookup, and retries as a loose file on disk if not found |
| Archives are mounted from `*.sqh` in the install folder | proven | `FUN_0048ed20` enumerates `*.sqh`; mount entry `FUN_0048df30` |
| `res1` overrides `res0` | guessed | 410 paths exist in both (197 differ). Mount order is a `std::map` keyed on the first four name bytes, but the search order hasn't been traced to the end. Check with a side-by-side run |
| res0 holds exactly 16,383 (2^14-1) files | observed | Could be a tool limit; unknown |
