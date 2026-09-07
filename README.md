# Universal Mapped Binary Format

**UMBF** is an extensible binary container format for storing arbitrary typed binary data.

## File Layout
| Magic | Header | Segment Table Size | Payload Size | Payload | Segment Table |
|-------|--------|--------------------|--------------|---------|---------------|

### Magic

| Size    | Description       |
| ------: | ----------------- |
| 4 bytes | UMBF magic number |

### File header

| Size    | Field            | Description                |
| ------: | ---------------- | -------------------------- |
| 4 bytes | `vendor_sign`    | Vendor signature           |
| 4 bytes | `vendor_version` | Vendor format version      |
| 4 bytes | `spec_version`   | UMBF specification version |
| 4 bytes | `type_sign`      | File format signature      |


### Segment Table

The Segment Table is a contiguous array of segment descriptors.

A standalone UMBF file stores its table internally and then the segment table size must be nonzero. If the table size is zero, the file contains only an append-only payload or segment table is provided externally.

#### Segment Descriptor

Each segment descriptor defines the location, encoded size, block count, encoding, and decoded checksum of a segment in the payload.

| Size    | Field         | Description                                             |
| ------: | ------------- | ------------------------------------------------------- |
| 8 bytes | `offset`      | Segment offset relative to the beginning of the payload |
| 8 bytes | `size`        | Encoded segment size                                    |
| 4 bytes | `block_count` | Number of blocks in the decoded segment                 |
| 4 bytes | `signature`   | Segment encoding signature                              |
| 4 bytes | `checksum`    | CRC32C of the decoded segment bytes                     |

Segment checksums allow a reader to validate only the segment it requested.

### Block Structure

Each block is encoded as:

| Size        | Field       | Description    |
| ----------: | ----------- | -------------- |
| 4 bytes     | `signature` | Type signature |
| 4 bytes     | `tag`       | Meta tag       |
| 8 bytes     | `size`      | Payload size   |
| Bytes array | `payload`   | Payload bytes  |

## Data Flow

The writer uses file I/O, not a writable mapping. A standalone writer initially writes the header followed by invalid table and payload sizes, appends encoded segments, writes the table at the end, and then replaces both size fields with
their actual values.

```text
Segment Descriptor
        │
        ▼
Encoded Segment
        │
        ▼
 Segment Codec
        │
        ▼
Decoded Segment Bytes
        │
        ▼
Block Descriptor
        │
        ▼
    Block Stream
        │
        ▼
Deserialized Block
```

## Built-in constants
### UMBF Specification

| Value      | Description       |
|------------|-------------------|
| 0xCA9FB393 | UMBF magic number |
| 0xBC037D   | UMBF vendor ID    |


### Format Signatures

| Signature  | Format Type    | Description          |
| ---------- | -------------- | -------------------- |
| 0x00000000 | none           | Undefined format     |
| 0x0A5B6E8F | image          | Image asset          |
| 0xB5F1EEB1 | scene          | Scene description    |
| 0x7A76D019 | material       | Material definition  |
| 0xC7DCA0DA | target         | Target definition    |
| 0x3500BDEA | library        | Library of assets    |
| 0xA031000C | raw            | Raw binary data      |

### Segment codec signatures

| Signature  | Codec             |
| ---------- | ----------------- |
| 0x00000000 | No encoding       |
| 0x4454535A | Zstd              |
| 0x474D495A | Image compression |

### Block Type Signatures

| Signature  | Block Type     | Description                |
|------------|----------------|----------------------------|
| 0xF82E95C8 | raw            | Generic raw binary         |
| 0x7684573F | image          | Image data                 |
| 0xA3903A92 | image_atlas    | Image atlas                |
| 0xA8D0C51E | material       | Material data              |
| 0x7EA13B42 | scene_object   | 3D scene object            |
| 0xF224B521 | mesh           | Mesh geometry              |
| 0xC441E54D | material_range | Material range assignments |
| 0x6112A229 | material_info  | Material metadata          |
| 0x0491F4E9 | target         | Target data                |
| 0x8D7824FA | library        | Asset library              |

## Extension Name Convention

| Extension | Meaning                        |
|-----------|--------------------------------|
| `.umbf`   | Default generic UMBF container |
| `.umia`   | UMBF Image Asset               |
| `.umsa`   | UMBF Scene Asset               |
| `.umm`    | UMBF Material                  |
| `.umconf` | UMBF Configuration             |


## Related Tools
### Built-in scripts

Scripts enumerated below are placed in the `scripts` directory.
* **sign_request.py** generates random block/format signatures in hex format.

### External repositories

- [umbf-convert](https://repos.wusikijeronii.me/app3d/umbf-convert)
- [umbf-viewer](https://repos.wusikijeronii.me/app3d/umbf-viewer)

## Building

### Supported compilers:
- GNU GCC
- Clang

### Supported OS:
- Linux
- Microsoft Windows

### Cmake options:
- `USE_ASAN`: Enable address sanitizer

### Bundled submodules
The following dependencies are included as git submodules and must be checked out when cloning:

- [acbt](https://repos.wusikijeronii.me/app3d/acbt)
- [acul](https://repos.wusikijeronii.me/app3d/acul)
- [amal](https://repos.wusikijeronii.me/app3d/amal)

## License
This project is licensed under the [MIT License](LICENSE).

## Contacts
For any questions or feedback, you can reach out via [email](mailto:wusikijeronii@gmail.com) or open a new issue.
