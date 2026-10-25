"""Whole-stream HLS/FLAC regression tests; no FFmpeg or external fixtures.
Usage: python testing/stream_capture.py executable openssl [--flac decoder]
"""
import argparse
import collections
import hashlib
import http.server
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
from pathlib import Path


def crc(data, bits, polynomial):
    value = 0
    for byte in data:
        value ^= byte << (bits - 8)
        for _ in range(8):
            value = (value << 1) ^ (polynomial if value & (1 << (bits - 1)) else 0)
            value &= (1 << bits) - 1
    return value


def flac_fixture():
    frames = []
    for number in range(20):
        header = b'\xff\xf8\x70\x08' + bytes([number]) + struct.pack('>H', 4095)
        header += bytes([crc(header, 8, 7)])
        frame = header + b'\0\0\0'  # Constant mono 16-bit subframe.
        frames.append(frame + struct.pack('>H', crc(frame, 16, 0x8005)))
    info = struct.pack('>HH', 4096, 4096) + bytes(6)
    info += ((44100 << 44) | (15 << 36) | (4096 * 20)).to_bytes(8, 'big')
    info += hashlib.md5(bytes(4096 * 20 * 2)).digest()
    native = b'fLaC\x80\0\0\x22' + info

    def page(packet, sequence, flags, granule=0):
        lacing = bytes([255] * (len(packet) // 255) + [len(packet) % 255])
        header = b'OggS\0' + bytes([flags]) + struct.pack('<QII', granule, 42, sequence)
        header += bytes(4) + bytes([len(lacing)]) + lacing
        data = header + packet
        checksum = crc(data, 32, 0x04c11db7)
        return data[:22] + struct.pack('<I', checksum) + data[26:]

    mapping = b'\x7fFLAC\x01\0\0\x01fLaC\0\0\0\x22' + info
    comments = b'\0\0\0\0\0\0\0\0'
    ogg = page(mapping, 0, 2)
    ogg += page(b'\x84' + len(comments).to_bytes(3, 'big') + comments, 1, 0)
    for i, frame in enumerate(frames):
        ogg += page(frame, i + 2, 4 if i == len(frames) - 1 else 0, (i + 1) * 4096)
    return native + b''.join(frames), ogg


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('openssl')
    parser.add_argument('--flac')
    args = parser.parse_args()
    exe = str(args.executable.resolve())
    native, ogg = flac_fixture()
    # Structurally valid transport packets test exact concatenation, not demuxing.
    packets = [b'\x47\x1f\xff\x10' + bytes([i]) * 184 for i in range(1, 5)]
    key = bytes(range(16))
    counts = collections.Counter()
    requests = []
    resources = {}
    with tempfile.TemporaryDirectory(prefix='streamripper-capture-') as tmp:
        root = Path(tmp)
        certificate, private_key = root / 'cert.pem', root / 'key.pem'
        subprocess.run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(private_key), '-out', str(certificate), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, capture_output=True)

        def encrypted(data, iv):
            return subprocess.run([args.openssl, 'enc', '-aes-128-cbc', '-K', key.hex(),
                                   '-iv', iv.hex()], input=data, capture_output=True, check=True).stdout

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, *_args):
                pass

            def do_GET(self):
                path = self.path.split('?')[0]
                counts[path] += 1
                requests.append((path, time.monotonic()))
                if path == '/redirect':
                    self.send_response(302)
                    self.send_header('Location', '/vod/list.m3u8?token=1')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                if path == '/live.m3u8':
                    if counts[path] == 1:
                        body = playlist(['s1.ts', 's2.ts'], end=False, sequence=1)
                    else:
                        body = playlist(['s2.ts', 's3.ts'], sequence=2)
                    mime = 'application/vnd.apple.mpegurl'
                elif path == '/endless.m3u8':
                    body, mime = playlist(['s1.ts'], end=False), 'application/vnd.apple.mpegurl'
                elif path == '/gap.m3u8':
                    body = playlist(['s1.ts', 's2.ts'], end=False, sequence=1) if counts[path] == 1 else playlist(['s4.ts'], sequence=4)
                    mime = 'application/vnd.apple.mpegurl'
                elif path == '/retry.ts' and counts[path] == 1:
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(packets[0])))
                    self.end_headers()
                    self.wfile.write(packets[0][:80])
                    self.wfile.flush()
                    self.close_connection = True
                    return
                elif path in resources:
                    body, mime = resources[path]
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header('Content-Type', mime)
                self.send_header('icy-name', 'Capture Test')
                if path == '/icy.flac':
                    self.send_header('icy-metaint', '64')
                chunked = path in ('/chunked.flac', '/chunked.ts')
                if chunked:
                    self.send_header('Transfer-Encoding', 'chunked')
                elif path != '/live.flac':
                    self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                try:
                    if chunked:
                        for i in range(0, len(body), 37):
                            part = body[i:i + 37]
                            self.wfile.write(f'{len(part):x}\r\n'.encode() + part + b'\r\n')
                        self.wfile.write(b'0\r\n\r\n')
                    else:
                        self.wfile.write(body)
                        self.wfile.flush()
                        if path == '/live.flac':
                            time.sleep(4)
                except (BrokenPipeError, ConnectionResetError):
                    pass

        def playlist(names, end=True, sequence=0, prefix=''):
            text = f'#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:{sequence}\n{prefix}'
            text += ''.join(f'#EXTINF:2,\n{name}\n' for name in names)
            return (text + ('#EXT-X-ENDLIST\n' if end else '')).encode()

        for index, packet in enumerate(packets, 1):
            resources[f'/s{index}.ts'] = packet, 'video/mp2t'
        resources['/retry.ts'] = packets[0], 'video/mp2t'
        resources['/chunked.ts'] = packets[1], 'video/mp2t'
        resources['/vod/list.m3u8'] = playlist(['../s1.ts', '../s2.ts']), 'application/octet-stream'
        resources['/master.m3u8'] = (
            b'#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000\nlow.m3u8\n'
            b'#EXT-X-STREAM-INF:BANDWIDTH=256000\nhigh.m3u8\n', 'application/vnd.apple.mpegurl')
        resources['/low.m3u8'] = playlist(['s1.ts']), 'application/vnd.apple.mpegurl'
        resources['/high.m3u8'] = playlist(['s2.ts']), 'application/vnd.apple.mpegurl'
        resources['/retry.m3u8'] = playlist(['retry.ts', 'chunked.ts']), 'application/vnd.apple.mpegurl'
        resources['/key'] = key, 'application/octet-stream'
        resources['/badkey'] = key + b'!', 'application/octet-stream'
        resources['/badkey.m3u8'] = playlist(['s1.ts'], prefix='#EXT-X-KEY:METHOD=AES-128,URI="badkey"\n'), 'application/vnd.apple.mpegurl'
        resources['/badpadding.ts'] = bytes(16), 'video/mp2t'
        resources['/badpadding.m3u8'] = playlist(['badpadding.ts'], prefix='#EXT-X-KEY:METHOD=AES-128,URI="key"\n'), 'application/vnd.apple.mpegurl'
        resources['/badrange.m3u8'] = playlist(['s1.ts'], prefix='#EXT-X-BYTERANGE:188@0\n'), 'application/vnd.apple.mpegurl'
        resources['/encrypted.ts'] = encrypted(packets[2], (42).to_bytes(16, 'big')), 'video/mp2t'
        resources['/explicit.ts'] = encrypted(packets[3], bytes([0xaa]) * 16), 'video/mp2t'
        resources['/shortiv.ts'] = encrypted(packets[0], (1).to_bytes(16, 'big')), 'video/mp2t'
        resources['/shortiv.m3u8'] = playlist(['shortiv.ts'], prefix='#EXT-X-KEY:METHOD=AES-128,URI="key",IV=0x1\n'), 'application/vnd.apple.mpegurl'
        aes = '#EXT-X-KEY:METHOD=AES-128,URI="key"\n#EXTINF:2,\nencrypted.ts\n'
        aes += '#EXT-X-KEY:METHOD=AES-128,URI="key",IV=0x' + 'aa' * 16 + '\n#EXTINF:2,\nexplicit.ts\n#EXT-X-ENDLIST\n'
        resources['/aes.m3u8'] = (f'#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:42\n{aes}'.encode(),
                                  'application/vnd.apple.mpegurl')
        resources['/badcrypt.m3u8'] = playlist(['s1.ts'], prefix='#EXT-X-KEY:METHOD=SAMPLE-AES,URI="key"\n'), 'application/vnd.apple.mpegurl'
        resources['/badmap.m3u8'] = playlist(['s1.ts'], prefix='#EXT-X-MAP:URI="init.mp4"\n'), 'application/vnd.apple.mpegurl'
        resources['/badbody.m3u8'] = playlist(['native.flac']), 'application/vnd.apple.mpegurl'
        resources['/native.flac'] = native, 'audio/flac'
        resources['/upper'] = native, 'Audio/FLAC'
        resources['/empty.flac'] = b'', 'audio/flac'
        resources['/chunked.flac'] = native, 'audio/x-flac'
        resources['/live.flac'] = native, 'audio/flac'
        resources['/ogg'] = ogg, 'application/ogg'
        icy = bytearray()
        for i in range(0, len(native), 64):
            block = native[i:i + 64]
            icy += block
            if len(block) == 64:
                title = f"StreamTitle='Artist - Song {i}';".encode()
                title += bytes(-len(title) % 16)
                icy += bytes([len(title) // 16]) + title
        resources['/icy.flac'] = bytes(icy), 'audio/flac'
        adts = bytes([255, 241, 80, 128, 13, 127, 252]) + bytes(100)
        packed = b'ID3\x04\0\0\0\0\0\0' + adts * 8
        resources['/packed.aac'] = packed, 'audio/aac'
        resources['/aac.m3u8'] = playlist(['packed.aac', 'packed.aac']), 'application/vnd.apple.mpegurl'

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        server.daemon_threads = True
        tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        tls.daemon_threads = True
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, private_key)
        tls.socket = context.wrap_socket(tls.socket, server_side=True)
        for instance in (server, tls):
            threading.Thread(target=instance.serve_forever, daemon=True).start()
        resources['/stations.m3u'] = (f'#EXTM3U\nhttp://127.0.0.1:{server.server_port}/native.flac\n'.encode(), 'audio/x-mpegurl')

        def record(label, path, expected, extension, options=(), secure=False, diagnostic=None, incomplete=False):
            output = root / ('Žluťoučký ' + label)
            url = f'{"https://localhost" if secure else "http://127.0.0.1"}:{tls.server_port if secure else server.server_port}{path}'
            command = [exe, url, '--output-directory=' + str(output), '--keep-incomplete',
                       '--tls-ca-file=' + str(certificate), *options]
            if label != 'retry':
                command.append('-c')
            result = subprocess.run(command, capture_output=True, timeout=25)
            log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
            assert result.returncode == 0, (label, log)
            if diagnostic:
                assert diagnostic in log, (label, log)
            files = list(output.rglob('*' + extension))
            if expected is None:
                assert not files, (label, files, log)
            else:
                assert len(files) == 1, (label, files, log)
                assert files[0].read_bytes() == expected, (label, files[0], log)
                stat = files[0].stat()
                if hasattr(stat, 'st_blocks'):
                    assert stat.st_blocks * 512 < len(expected) + 65536, 'Unused allocation remains'
                assert (files[0].parent.name == 'incomplete') == incomplete, (label, files, log)
                if args.flac and extension in ('.flac', '.oga'):
                    subprocess.run([args.flac, '-t', '--silent', str(files[0])], check=True)
            assert not list(output.rglob('.lock')), (label, log)
            return log

        record('redirect', '/redirect', packets[0] + packets[1], '.ts')
        record('best', '/master.m3u8', packets[1], '.ts')
        record('worst', '/master.m3u8', packets[0], '.ts', ['--hls-variant=worst'])
        record('target', '/master.m3u8', packets[0], '.ts', ['--hls-variant=70000'])
        record('live', '/live.m3u8', b''.join(packets[:3]), '.ts')
        record('live gap', '/gap.m3u8', packets[0] + packets[1], '.ts', diagnostic='window advanced', incomplete=True)
        record('aes', '/aes.m3u8', packets[2] + packets[3], '.ts', secure=True)
        record('packed AAC', '/aac.m3u8', packed * 2, '.aac')
        record('short IV', '/shortiv.m3u8', packets[0], '.ts')
        # Reconnect retries the same partial segment without appending its prefix.
        with socket.socket() as occupied:
            occupied.bind(('127.0.0.1', 0))
            occupied.listen()
            relay_options = [f'--relay=127.0.0.1:{occupied.getsockname()[1]}', '--no-port-scan',
                             '--max-relay-clients=10', '--metadata-log', '--user-agent=WinampMPEG/5.0',
                             '--incomplete-min-duration=15m',
                             '--relay-playlist=' + str(root / 'disabled.pls')]
            log = record('ignored HLS relay', '/high.m3u8', packets[1], '.ts', relay_options,
                         diagnostic='relay is unavailable for HLS')
            assert 'relay port:' not in log and '\nrelay: ' not in log, log
            log = record('ignored FLAC relay', '/native.flac', native, '.flac', relay_options,
                         diagnostic='relay is unavailable for FLAC')
            assert 'relay port:' not in log and '\nrelay: ' not in log, log
            log = record('retry', '/retry.m3u8', packets[0] + packets[1], '.ts', relay_options)
            assert log.count('relay is unavailable for HLS') == 1, log
            assert not (root / 'disabled.pls').exists()
        assert counts['/retry.ts'] == 2
        attempts = [stamp for path, stamp in requests if path == '/retry.ts']
        assert attempts[1] - attempts[0] >= 4.8
        record('unsupported crypto', '/badcrypt.m3u8', None, '.ts', diagnostic='only HLS METHOD')
        record('unsupported map', '/badmap.m3u8', None, '.ts', diagnostic='fMP4')
        record('unsupported range', '/badrange.m3u8', None, '.ts', diagnostic='byte ranges')
        record('bad key', '/badkey.m3u8', None, '.ts', diagnostic='recording limit')
        record('bad padding', '/badpadding.m3u8', None, '.ts', diagnostic='decryption failed')
        record('bad container', '/badbody.m3u8', None, '.ts', diagnostic='expected MPEG-TS')
        record('native FLAC', '/native.flac', native, '.flac', secure=True)
        record('case-insensitive MIME', '/upper', native, '.flac')
        record('traditional M3U', '/stations.m3u', native, '.flac')
        record('empty FLAC', '/empty.flac', None, '.flac', diagnostic='missing FLAC stream header')
        record('chunked FLAC', '/chunked.flac', native, '.flac')
        record('continuous FLAC', '/native.flac', native, '.flac', ['-A', '-a', 'continuous'])
        record('continuous HLS', '/high.m3u8', packets[1], '.ts', ['-A', '-a', 'continuous'])
        record('Ogg FLAC', '/ogg', ogg, '.oga')
        record('ICY FLAC', '/icy.flac', native, '.flac', ['--metadata-log'])
        record('interrupted FLAC', '/live.flac', native, '.flac', ['-l', '1', '--incomplete-min-duration=15m'], incomplete=True)
        record('short HLS', '/endless.m3u8', None, '.ts', ['-l', '1', '--incomplete-min-duration=15m'], diagnostic='Discarded incomplete recording')
        record('rules rejected', '/native.flac', None, '.flac', ['-w', str(root / 'rules.txt')],
               diagnostic='--metadata-rules is unavailable')
        record('external metadata rejected', '/high.m3u8', None, '.ts', ['--external-metadata=must-not-run'],
               diagnostic='--external-metadata is unavailable')
        for instance in (server, tls):
            instance.shutdown()
            instance.server_close()
    print('PASS: HLS variants/live/AES/retry/TLS, byte-exact native/Ogg/ICY FLAC, output cleanup')


if __name__ == '__main__':
    main()
