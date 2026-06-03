#!/usr/bin/env python3

if __name__ == "__main__":
    data = open('prelude.f', 'rb').read() + b'\x00'
    out = ['const char prelude[] = {']
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        out.append('  ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',')
    out.append('};')
    out.append(f'const unsigned int prelude_len = {len(data)};')
    open('prelude.h', 'w').write('\n'.join(out))
