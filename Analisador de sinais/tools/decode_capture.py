"""Decodifica um CSV do analisador usando o protocolo descrito em PROTOCOLO.md.

Uso: python tools/decode_capture.py Resultado/captura.csv
"""
import csv
import sys
from functools import reduce

REQUEST_FLAG = 0x8000  # desligado nas confirmações
START = 0xC6
HEADER_LEN = 5        # C6, tamanho, tipo, sequência, AD
ADDR_LEN = 3

PROPERTIES = {
    (0x10, 0x00): 'modo',
    (0x10, 0x02): 'ventilador',
    (0x10, 0x05): 'oscilar',
    (0x10, 0x07): 'aleta',
    (0x04, 0x01): 'estado',
    (0x04, 0x03): 'ligar',
    (0x04, 0x13): 'timer',
    (0x04, 0x25): 'silencioso',
    (0x04, 0x28): 'dormir',
    (0x04, 0x29): 'display',
    (0x04, 0x38): 'auto sense',
    (0x10, 0x01): 'ar indireto',
    (0x04, 0x32): 'setpoint',
    (0x00, 0x20): 'heartbeat',
}


def checksum_ok(raw):
    # XOR de tudo depois do C6, incluindo o próprio checksum, dá zero
    return reduce(lambda a, b: a ^ b, raw[1:]) == 0 and raw[1] == len(raw) - 3


def split_frames(raw):
    """Quadros com pausa menor que a do analisador chegam colados: separa pelo tamanho."""
    frames = []
    while len(raw) >= 3 and raw[0] == START and raw[1] + 3 <= len(raw):
        size = raw[1] + 3
        frames.append(raw[:size])
        raw = raw[size:]
    return frames if frames and not raw else [raw]


def parse(raw):
    if raw[0] != START or not checksum_ok(raw):
        return None
    body = raw[HEADER_LEN:-1]
    return {
        'counter': raw[2] << 8 | raw[3],
        'dst': body[:ADDR_LEN].decode('ascii', 'replace'),
        'src': body[ADDR_LEN:ADDR_LEN * 2].decode('ascii', 'replace'),
        'prop': (body[6], body[7]),
        'kind': body[8],
        'value': body[9:],
    }


def describe(f):
    name = PROPERTIES.get(f['prop'], '%02X.%02X' % f['prop'])
    value = f['value']
    if f['prop'] == (0x04, 0x13):
        text = '%d s' % int.from_bytes(value, 'big')
    elif len(value) == 4 and f['prop'][0] == 0x04:
        text = '%d C (%s)' % (value[2], value.hex())
    elif f['prop'] == (0x00, 0x20) and len(value) >= 17:
        text = 'MAC %s IP %s' % (value[6:12].hex(':'), '.'.join(str(b) for b in value[13:17]))
    else:
        text = value.hex()
    return name, text


def main(path):
    rows = list(csv.DictReader(open(path, encoding='utf8')))
    frames, bad = [], 0
    for r in rows:
        if r['tipo'] == 'marcador':
            frames.append({'t': int(r['tempo_us']) / 1e6, 'mark': r['marcador']})
        elif r['tipo'] == 'quadro':
            for raw in split_frames(bytes.fromhex(r['hex'])):
                f = parse(raw)
                if f is None:
                    bad += 1
                    print('INVÁLIDO', raw.hex().upper())
                    continue
                f.update(t=int(r['tempo_us']) / 1e6, line=r['linha'], raw=raw)
                frames.append(f)
    good = sum('mark' not in f for f in frames)
    print('%d quadros válidos, %d inválidos' % (good, bad))

    for f in frames:
        if 'mark' in f:
            print('%9.3f >>> %s' % (f['t'], f['mark']))
        elif not f['counter'] & REQUEST_FLAG:  # confirmações só repetem a propriedade
            continue
        else:
            name, text = describe(f)
            print('%9.3f %s %04X op %02X %s->%s %-11s %s'
                  % (f['t'], f['line'], f['counter'], f['kind'], f['src'], f['dst'], name, text))


if __name__ == '__main__':
    main(sys.argv[1])
