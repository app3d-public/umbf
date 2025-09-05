# Unviersal Meta Blocks Format

**UMBF** is a extensible binary container format for storing arbitrary assets together with their metadata.

## File Layout
```text
+---------+---------+----------+-----------+
| Magic   | Header  | Payload  | Checksum  |
+---------+---------+----------+-----------+
```

### Magic
Size: 4 bytes

### Header
Size: 12 bytes

| Bits | Description                |
|------|----------------------------|
| 24   | Vendor ID                  |
| 24   | Vendor Version             |
| 16   | Asset type signature       |
| 24   | UMBF specification version |
| 8    | UMBF Flags                 |

### Payload

The payload is a sequence of blocks. The first block points to the asset subtype.
Each block contains:
1. **Type Signature** — Identifies the kind of block.
2. **Block Data** — Binary data for that block.

### Checksum

Size: 4 bytes\
Algorithm: CRC32C (Castagnoli)

The file itself does not store a serialized checksum.
Instead, a checksum is computed at load time and kept in memory.\
This computed value is not used to validate the container as a whole, but it is written into certain internal blocks and later used for validation.
In particular, a Target block stores the checksum of the resource it references, allowing the fetched data to be verified against the expected value.

## Built-in constants

### UMBF Specification

| Value      | Description       |
|------------|-------------------|
| 0xCA9FB393 | UMBF magic number |
| 0xBC037D   | UMBF vendor ID    |


### Format Signatures (16-bit)

| Signature | Format Type | Description         |
|-----------|-------------|---------------------|
| 0x0000    | none        | Undefined format    |
| 0x0490    | image       | Image asset         |
| 0xD20C    | scene       | Scene description   |
| 0x78DB    | material    | Material definition |
| 0x613E    | target      | Target definition   |
| 0x1A2C    | library     | Library of assets   |
| 0x4D4D    | raw         | Raw binary data     |

### Block Type Signatures (32-bit)

| Signature  | Block Type     | Description                |
|------------|----------------|----------------------------|
| 0xF82E95C8 | raw            | Generic raw binary         |
| 0x7684573F | image          | Image data                 |
| 0xA3903A92 | image_atlas    | Image atlas                |
| 0xA8D0C51E | material       | Material data              |
| 0xB7A3EE80 | scene          | Scene data                 |
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

* **atlas_min_size.py** - estimates the minimum atlas size for a set of images based on JSON configuration.
> [!NOTE] The result is approximate, but allows quickly evaluating minimal atlas dimensions.

* **sign_request.py** generates random block/format signatures in hex format.

### External repositories

- [umbf-convert](https://git.homedatasrv.ru/app3d/umbf-convert)
- [umbf-viewer](https://git.homedatasrv.ru/app3d/umbf-viewer)

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

- [acbt](https://git.homedatasrv.ru/app3d/acbt)
- [acul](https://git.homedatasrv.ru/app3d/acul)
- [amal](https://git.homedatasrv.ru/app3d/amal)
- [rectpack2D](ttps://github.com/TeamHypersomnia/rectpack2D)

## License
This project is licensed under the [MIT License](LICENSE).

## Contacts
For any questions or feedback, you can reach out via [email](mailto:wusikijeronii@gmail.com) or open a new issue.