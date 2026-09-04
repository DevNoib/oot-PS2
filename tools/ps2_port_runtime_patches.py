#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import psp_port_runtime_patches as psp


def resolve_manifest(elf_path: Path, manifest_path: Path, output: Path, base_elf_path: Path | None = None) -> None:
    elf = psp.ElfObject(elf_path)
    image_base = psp.linked_image_base(elf)
    objects = psp.linked_objects_by_file(elf)
    objects_by_name: dict[str, list[object]] = {}

    for (_source_file, name), candidate in objects.items():
        bucket = objects_by_name.setdefault(name, [])
        if not any(existing.value == candidate.value and existing.size == candidate.size for existing in bucket):
            bucket.append(candidate)

    data = manifest_path.read_bytes()
    if data[:4] != b"OPR2":
        raise ValueError("invalid runtime patch manifest")

    count, permutation_count = struct.unpack_from("<II", data, 4)
    cursor = 12
    permutation_data = data[cursor : cursor + permutation_count * 8]
    cursor += len(permutation_data)
    records: list[bytes] = []
    texture_symbols_by_source: dict[str, set[str]] = {}

    for _ in range(count):
        source_len, symbol_len, source_vrom, size, payload_size, compressed_size, reloc_count = struct.unpack_from(
            "<HHIIIII", data, cursor
        )
        cursor += 24
        source_file = data[cursor : cursor + source_len].decode("utf-8")
        cursor += source_len
        symbol_name = data[cursor : cursor + symbol_len].decode("utf-8")
        cursor += symbol_len
        compressed = data[cursor : cursor + compressed_size]
        cursor += compressed_size
        relocation_data = data[cursor : cursor + reloc_count * 8]
        cursor += len(relocation_data)

        symbol = objects.get((source_file, symbol_name))
        if symbol is None:
            symbol = objects.get((Path(source_file).name, symbol_name))
        if symbol is None:
            candidates = [candidate for candidate in objects_by_name.get(symbol_name, []) if candidate.size == size]
            if len(candidates) == 1:
                symbol = candidates[0]
        if symbol is None:
            raise ValueError(f"clean ELF is missing {source_file}:{symbol_name}")
        if symbol.size != size:
            raise ValueError(f"clean ELF size mismatch for {source_file}:{symbol_name}")

        texture_symbols = texture_symbols_by_source.get(source_file)
        if texture_symbols is None:
            source_path = psp.ROOT / source_file
            source_text = source_path.read_text() if source_path.is_file() else ""
            texture_symbols = {match.group("name") for match in psp.U64_SOURCE_DECL_RE.finditer(source_text)}
            texture_symbols_by_source[source_file] = texture_symbols
        is_texture_words = (symbol_name in texture_symbols) or (psp.TEXTURE_WORD_SYMBOL_RE.search(symbol_name) is not None)
        patch_flags = psp.RUNTIME_PATCH_TEXTURE_WORDS if is_texture_words else 0

        reference_relocations = list(struct.iter_unpack("<II", relocation_data))
        relocation_offsets = [offset for offset, _target in reference_relocations]
        clean_symbol_data = psp.linked_symbol_bytes(elf, symbol)
        adjustments: list[int] = []

        for offset, reference_pointer_value in reference_relocations:
            clean_pointer_value = struct.unpack_from("<I", clean_symbol_data, offset)[0]
            adjustment = (clean_pointer_value - image_base) - reference_pointer_value
            if not -(1 << 31) <= adjustment < (1 << 31):
                raise ValueError(f"runtime relocation adjustment is too large for {source_file}:{symbol_name}")
            adjustments.append(adjustment)

        destination_offset = symbol.value - image_base
        if not 0 <= destination_offset < (1 << 32):
            raise ValueError(f"runtime patch destination is outside the linked image for {source_file}:{symbol_name}")

        records.append(
            struct.pack(
                "<IIIIIII",
                destination_offset,
                source_vrom,
                size,
                payload_size,
                compressed_size,
                reloc_count,
                patch_flags,
            )
            + compressed
            + b"".join(
                struct.pack("<Ii", offset, adjustment)
                for offset, adjustment in zip(relocation_offsets, adjustments)
            )
        )

    if cursor != len(data):
        raise ValueError("trailing runtime patch manifest data")

    output_data = bytearray(b"OPB2" + struct.pack("<II", count, permutation_count))
    output_data.extend(permutation_data)
    for record in records:
        output_data.extend(record)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(output_data)
    print(f"resolved {count} runtime patch destinations into {output} ({len(output_data)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build PS2 runtime asset patch recipes")
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create")
    create.add_argument("version")
    create.add_argument("elf", type=Path)
    create.add_argument("asset_table", type=Path)
    create.add_argument("extracted_dir", type=Path)
    create.add_argument("output", type=Path)

    resolve = subparsers.add_parser("resolve")
    resolve.add_argument("elf", type=Path)
    resolve.add_argument("manifest", type=Path)
    resolve.add_argument("output", type=Path)
    resolve.add_argument("--base-elf", type=Path)

    args = parser.parse_args()
    if args.command == "create":
        psp.create_manifest(args.version, args.elf, args.asset_table, args.extracted_dir, args.output)
    else:
        resolve_manifest(args.elf, args.manifest, args.output, args.base_elf)


if __name__ == "__main__":
    main()
