"""Download public audio fixtures into the ignored build directory and verify hashes."""
from pathlib import Path
import hashlib
import urllib.request

dest = Path(__file__).resolve().parents[1] / 'build'
dest.mkdir(exist_ok=True)
samples = {
    'sample-5s.mp3': ('https://samplelib.com/mp3/sample-5s.mp3', 'b3cb356d6ab822252c7199ab2bb692620af7eef07c3ff22618e0a352d7d77d9f'),
    'sample-3s.wav': ('https://samplelib.com/wav/sample-3s.wav', 'b3726eac5c9612ea20e245314812575bf9df5fb6b8024b80c7cfe9033452bb2b'),
}
for name, (url, digest) in samples.items():
    path = dest / name
    data = path.read_bytes() if path.is_file() else urllib.request.urlopen(url, timeout=30).read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError(f'Unexpected fixture content: {name}')
    path.write_bytes(data)
    print('Verified:', name)
