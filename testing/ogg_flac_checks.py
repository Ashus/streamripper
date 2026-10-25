"""Generated Ogg FLAC live-stream fixtures and structural/seek checks."""
import struct
import subprocess


def crc(data, bits, polynomial):
    value = 0
    for byte in data:
        value ^= byte << (bits - 8)
        for _ in range(8):
            value = ((value << 1) ^ (polynomial if value & (1 << (bits - 1)) else 0)) & ((1 << bits) - 1)
    return value


def page(packet, serial, sequence, flags=0, granule=0, continued=False):
    assert not continued or len(packet) % 255 == 0
    lacing = bytes([255] * (len(packet) // 255) + ([] if continued else [len(packet) % 255]))
    header = b'OggS\0' + bytes([flags]) + struct.pack('<QII', granule, serial, sequence)
    data = header + bytes(4) + bytes([len(lacing)]) + lacing + packet
    return data[:22] + struct.pack('<I', crc(data, 32, 0x04c11db7)) + data[26:]


def packets(data):
    position = 0
    partial = bytearray()
    result = []
    while position < len(data):
        assert data[position:position + 4] == b'OggS', position
        count = data[position + 26]
        size = 27 + count + sum(data[position + 27:position + 27 + count])
        current = data[position:position + size]
        assert len(current) == size, 'Incomplete Ogg page'
        assert crc(current[:22] + bytes(4) + current[26:], 32, 0x04c11db7) == struct.unpack_from('<I', current, 22)[0]
        body = 27 + count
        for lace in current[27:27 + count]:
            partial.extend(current[body:body + lace])
            body += lace
            if lace < 255:
                result.append(bytes(partial))
                partial.clear()
        position += size
    assert not partial, 'Incomplete Ogg packet'
    return result


def number(value):
    size = 1
    while value >= (1 << (7 if size == 1 else 5 * size + 1)) and size < 7:
        size += 1
    if size == 1:
        return bytes([value])
    tail = bytearray(size)
    for i in range(size - 1, 0, -1):
        tail[i] = 128 | (value & 63)
        value >>= 6
    tail[0] = ((255 << (8 - size)) & 255) | value
    return bytes(tail)


def verbatim_fixture(original):
    source = packets(original)
    output = page(source[0], 42, 0, 2) + page(source[1], 42, 1)
    for index in range(len(source) - 2):
        header = b'\xff\xf8\x70\x08' + number(index) + b'\x0f\xff'
        header += bytes([crc(header, 8, 7)])
        frame = header + b'\x02' + bytes(8192)
        frame += struct.pack('>H', crc(frame, 16, 0x8005))
        output += page(frame, 42, index + 2, 4 if index == len(source) - 3 else 0, (index + 1) * 4096)
    return output


def seektable_fixture(original, padding=False):
    source = packets(original)
    mapping = source[0][:7] + b'\0\x02' + source[0][9:]
    comments = bytes([source[1][0] & 127]) + source[1][1:]
    payload = bytes(18) if padding else struct.pack('>QQH', 999999999, 123456789, 4096)
    table = bytes([0x81 if padding else 0x83]) + b'\0\0\x12' + payload
    output = page(mapping, 42, 0, 2) + page(comments, 42, 1) + page(table, 42, 2)
    for index, packet in enumerate(source[2:]):
        output += page(packet, 42, index + 3, 4 if index == len(source) - 3 else 0, (index + 1) * 4096)
    return output


def live_fixture(original, ending='complete', serial=42, split_frames=False):
    source = packets(original)
    first = bytearray(source[0])
    first[30] &= 0xf0
    first[31:51] = bytes(20)
    output = page(first, serial, 0, 2) + page(source[1], serial, 1)
    # Radio Paradise: bootstrap headers followed by pages from an old encoder.
    origin = 875 * 3600 * 44100
    for index, packet in enumerate(source[2:]):
        header = packet[:4] + number(34000000 + index) + packet[5:7]
        header += bytes([crc(header, 8, 7)])
        frame = header + packet[8:-2]
        frame += struct.pack('>H', crc(frame, 16, 0x8005))
        if split_frames:
            output += page(frame[:4080], serial, 1000000 + index * 2,
                           granule=(1 << 64) - 1, continued=True)
            output += page(frame[4080:], serial, 1000001 + index * 2,
                           flags=1, granule=origin + (index + 1) * 4096)
        else:
            output += page(frame, serial, 1000000 + index, granule=origin + (index + 1) * 4096)
    extra = page(source[2], serial, 1000000 + len(source) - 2, granule=origin + 21 * 4096)
    if ending == 'partial-page':
        output += extra[:-5]
    elif ending == 'partial-packet':
        # A valid Ogg page containing only the first fragment of a FLAC packet.
        output += page(source[2] + bytes(255 - len(source[2])), serial,
                       1000000 + len(source) - 2, granule=(1 << 64) - 1, continued=True)
    return output


def check(recorded, expected, decoder=None, path=None):
    actual_packets = packets(recorded)
    expected_packets = packets(expected)
    audio_count = sum(packet[:1] == b'\xff' for packet in expected_packets)
    assert len(actual_packets) == len(expected_packets)
    samples = 0
    count = 0
    pos = 0
    serial = None
    last_flags = 0
    for actual, source in zip(actual_packets, expected_packets):
        if actual.startswith(b'\x7fFLAC'):
            assert actual[:30] == source[:30]
            samples = int.from_bytes(actual[30:35], 'big') & ((1 << 36) - 1)
            assert samples == audio_count * 4096
            assert actual[35:51] == bytes(16)
        else:
            # Renumbered frames retain the exact compressed audio and valid CRCs.
            assert actual == source, (actual.hex(), source.hex())
    while pos < len(recorded):
        page_size = 27 + recorded[pos + 26] + sum(recorded[pos + 27:pos + 27 + recorded[pos + 26]])
        current = recorded[pos:pos + page_size]
        page_serial, sequence = struct.unpack_from('<II', current, 14)
        if serial is None:
            serial = page_serial
        assert page_serial == serial and sequence == count, (sequence, count)
        count += 1
        granule = struct.unpack_from('<Q', current, 6)[0]
        assert granule <= samples or granule == (1 << 64) - 1
        last_flags = current[5]
        last_granule = granule
        pos += page_size
    assert last_flags & 4 and last_granule == samples, (last_flags, last_granule, samples)
    if decoder:
        # Seeking exercises both the rewritten STREAMINFO count and frame indices.
        output = subprocess.run([decoder, '-d', '-c', '--silent', '--force-raw-format',
                                 '--endian=little', '--sign=signed', '--skip=44100', '--until=66150',
                                 str(path)], check=True, capture_output=True).stdout
        assert output == bytes(22050 * 2), len(output)
