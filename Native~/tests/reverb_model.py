#!/usr/bin/env python3
"""The console's reverb modelled apart from the C, and the fingerprints the C must match.

The model keeps the console's 512 KB of sound memory and addresses it as the hardware does: relative to a current
address that advances through the work area and wraps back to its start. The C keeps a ring of the work area alone
and works its taps out ahead of time. They share only what both are written from — PSX-SPX's formula and tables,
rounded as Mednafen rounds them — and this file has its own copy of the tables, so a mistake made in the C's copy
cannot be repeated here and hidden.

    python3 reverb_model.py                    rewrite model_fingerprints.h beside this file
    python3 reverb_model.py --print CASE N     print the model's first N samples of a case
    python3 reverb_model.py --compare CASE RAW find where the C's output for a case, saved by the test, parts from it
"""

import os
import re
import struct
import sys

# PSX-SPX's reverb examples as they appear there — four rows of eight registers, dAPF1 first — in the console
# library's mode order. "Half Echo" is the library's pipe; "Chaos Echo" its echo.
PRESETS = [
    ('Reverb off', 0x10, '''
        0000h,0000h,0000h,0000h,0000h,0000h,0000h,0000h
        0000h,0000h,0001h,0001h,0001h,0001h,0001h,0001h
        0000h,0000h,0001h,0001h,0001h,0001h,0001h,0001h
        0000h,0000h,0001h,0001h,0001h,0001h,0000h,0000h'''),
    ('Room', 0x26C0, '''
        007Dh,005Bh,6D80h,54B8h,BED0h,0000h,0000h,BA80h
        5800h,5300h,04D6h,0333h,03F0h,0227h,0374h,01EFh
        0334h,01B5h,0000h,0000h,0000h,0000h,0000h,0000h
        0000h,0000h,01B4h,0136h,00B8h,005Ch,8000h,8000h'''),
    ('Studio Small', 0x1F40, '''
        0033h,0025h,70F0h,4FA8h,BCE0h,4410h,C0F0h,9C00h
        5280h,4EC0h,03E4h,031Bh,03A4h,02AFh,0372h,0266h
        031Ch,025Dh,025Ch,018Eh,022Fh,0135h,01D2h,00B7h
        018Fh,00B5h,00B4h,0080h,004Ch,0026h,8000h,8000h'''),
    ('Studio Medium', 0x4840, '''
        00B1h,007Fh,70F0h,4FA8h,BCE0h,4510h,BEF0h,B4C0h
        5280h,4EC0h,0904h,076Bh,0824h,065Fh,07A2h,0616h
        076Ch,05EDh,05ECh,042Eh,050Fh,0305h,0462h,02B7h
        042Fh,0265h,0264h,01B2h,0100h,0080h,8000h,8000h'''),
    ('Studio Large', 0x6FE0, '''
        00E3h,00A9h,6F60h,4FA8h,BCE0h,4510h,BEF0h,A680h
        5680h,52C0h,0DFBh,0B58h,0D09h,0A3Ch,0BD9h,0973h
        0B59h,08DAh,08D9h,05E9h,07ECh,04B0h,06EFh,03D2h
        05EAh,031Dh,031Ch,0238h,0154h,00AAh,8000h,8000h'''),
    ('Hall', 0xADE0, '''
        01A5h,0139h,6000h,5000h,4C00h,B800h,BC00h,C000h
        6000h,5C00h,15BAh,11BBh,14C2h,10BDh,11BCh,0DC1h
        11C0h,0DC3h,0DC0h,09C1h,0BC4h,07C1h,0A00h,06CDh
        09C2h,05C1h,05C0h,041Ah,0274h,013Ah,8000h,8000h'''),
    ('Space Echo', 0xF6C0, '''
        033Dh,0231h,7E00h,5000h,B400h,B000h,4C00h,B000h
        6000h,5400h,1ED6h,1A31h,1D14h,183Bh,1BC2h,16B2h
        1A32h,15EFh,15EEh,1055h,1334h,0F2Dh,11F6h,0C5Dh
        1056h,0AE1h,0AE0h,07A2h,0464h,0232h,8000h,8000h'''),
    ('Chaos Echo', 0x18040, '''
        0001h,0001h,7FFFh,7FFFh,0000h,0000h,0000h,8100h
        0000h,0000h,1FFFh,0FFFh,1005h,0005h,0000h,0000h
        1005h,0005h,0000h,0000h,0000h,0000h,0000h,0000h
        0000h,0000h,1004h,1002h,0004h,0002h,8000h,8000h'''),
    ('Delay', 0x18040, '''
        0001h,0001h,7FFFh,7FFFh,0000h,0000h,0000h,0000h
        0000h,0000h,1FFFh,0FFFh,1005h,0005h,0000h,0000h
        1005h,0005h,0000h,0000h,0000h,0000h,0000h,0000h
        0000h,0000h,1004h,1002h,0004h,0002h,8000h,8000h'''),
    ('Half Echo', 0x3C00, '''
        0017h,0013h,70F0h,4FA8h,BCE0h,4510h,BEF0h,8500h
        5F80h,54C0h,0371h,02AFh,02E5h,01DFh,02B0h,01D7h
        0358h,026Ah,01D6h,011Eh,012Dh,00B1h,011Fh,0059h
        01A0h,00E3h,0058h,0040h,0028h,0014h,8000h,8000h'''),
]

FIELDS = ('dAPF1 dAPF2 vIIR vCOMB1 vCOMB2 vCOMB3 vCOMB4 vWALL vAPF1 vAPF2 mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 '
          'mRCOMB2 dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 '
          'mRAPF2 vLIN vRIN').split()
OFF, ROOM, STUDIO_A, STUDIO_B, STUDIO_C, HALL, SPACE, ECHO, DELAY, PIPE = range(10)

TICKS = 55125  # 2.5 seconds, long enough to wrap even echo's ring

# mode, delay, feedback, how far the noise is shifted down, and a mode and a delay set halfway through (-1 for none).
CASES = [(mode, -1, -1, shift, -1, -1) for mode in range(10) for shift in (0, 3)] + [
    (ECHO, 64, 100, 3, -1, -1),
    (ECHO, 1, 127, 3, -1, -1),
    (ECHO, 127, 126, 3, -1, -1),
    (ECHO, 40, 30, 0, -1, -1),
    (DELAY, 30, -1, 3, -1, -1),
    (HALL, -1, -1, 3, ROOM, -1),
    (ECHO, -1, -1, 3, -1, 40),
    (DELAY, -1, -1, 0, ECHO, -1),
]


def saturate(value):
    return -32768 if value < -32768 else 32767 if value > 32767 else value


def signed(raw):
    return raw - 0x10000 if raw & 0x8000 else raw


def negate(volume):
    return 0x7FFF if volume == -32768 else -volume


class Console:
    def __init__(self):
        self.ram = [0] * 0x40000  # in 16-bit halfwords

    # As the library sets a mode with its clear flag: the table, the mode's default delay and feedback, the work area
    # zeroed and the current address back at its start. Memory outside the work area keeps what it held.
    def set_mode(self, mode):
        _, size, rows = PRESETS[mode]
        self.mode = mode
        self.registers = dict(zip(FIELDS, (int(v, 16) for v in re.findall(r'([0-9A-F]{4})h', rows))))
        self.base = (0x80000 - size) // 2
        self.ram[self.base:] = [0] * (0x40000 - self.base)
        self.current = self.base

    # The library's arithmetic, on the table's own values.
    def set_delay(self, delay):
        assert self.mode in (ECHO, DELAY) and 1 <= delay <= 127
        table = dict(zip(FIELDS, (int(v, 16) for v in re.findall(r'([0-9A-F]{4})h', PRESETS[self.mode][2]))))
        whole, half = (delay << 13) // 127, (delay << 12) // 127
        self.registers.update(mLSAME=(whole - table['dAPF1']) & 0xFFFF, mRSAME=(half - table['dAPF2']) & 0xFFFF,
                              mLCOMB1=half + table['mRCOMB1'], dLSAME=half + table['dRSAME'],
                              mLAPF1=half + table['mLAPF2'], mRAPF1=half + table['mRAPF2'])

    def set_feedback(self, feedback):
        assert self.mode in (ECHO, DELAY) and 0 <= feedback <= 127
        self.registers['vWALL'] = feedback * 0x8100 // 127

    # Addresses count halfwords ahead of the current one; past the end of memory they continue from the work area's
    # start, as the hardware wraps them.
    def address(self, halfwords):
        address = self.current + (halfwords & 0x3FFFF)
        if address & 0x40000:
            address += self.base
        return address & 0x3FFFF

    def read(self, register, back=0):
        return self.ram[self.address((register << 2) - back)]

    def write(self, register, sample):
        self.ram[self.address(register << 2)] = sample

    def process(self, inputs):
        g = self.registers
        v = {name: signed(g[name]) for name in FIELDS if name[0] == 'v'}
        sides = (
            (g['mLSAME'], g['dLSAME'], g['mLDIFF'], g['dRDIFF'],
             (g['mLCOMB1'], g['mLCOMB2'], g['mLCOMB3'], g['mLCOMB4']), g['mLAPF1'], g['mLAPF2'], v['vLIN']),
            (g['mRSAME'], g['dRSAME'], g['mRDIFF'], g['dLDIFF'],
             (g['mRCOMB1'], g['mRCOMB2'], g['mRCOMB3'], g['mRCOMB4']), g['mRAPF1'], g['mRAPF2'], v['vRIN']))
        comb_volumes = (v['vCOMB1'], v['vCOMB2'], v['vCOMB3'], v['vCOMB4'])
        outputs = []
        for (same, same_from, diff, diff_from, combs, apf1, apf2, volume_in), sample in zip(sides, inputs):
            scaled = (sample * volume_in) >> 14
            same_in = saturate((((self.read(same_from) * v['vWALL']) >> 14) + scaled) >> 1)
            diff_in = saturate((((self.read(diff_from) * v['vWALL']) >> 14) + scaled) >> 1)
            hold = 0x8000 - v['vIIR']
            same_out = saturate((((same_in * v['vIIR']) >> 14) + ((self.read(same, 1) * hold) >> 14)) >> 1)
            diff_out = saturate((((diff_in * v['vIIR']) >> 14) + ((self.read(diff, 1) * hold) >> 14)) >> 1)
            self.write(same, same_out)
            self.write(diff, diff_out)
            comb = sum((self.read(tap) * volume) >> 14 for tap, volume in zip(combs, comb_volumes))
            feedback_a = self.read((apf1 - g['dAPF1']) & 0xFFFF)
            feedback_b = self.read((apf2 - g['dAPF2']) & 0xFFFF)
            mix_a = saturate((comb + ((feedback_a * negate(v['vAPF1'])) >> 14)) >> 1)
            mix_b = saturate(feedback_a + ((((mix_a * v['vAPF1']) >> 14) +
                                            ((feedback_b * negate(v['vAPF2'])) >> 14)) >> 1))
            outputs.append(saturate(feedback_b + ((mix_b * v['vAPF2']) >> 15)))
            self.write(apf1, mix_a)
            self.write(apf2, mix_b)
        self.current = (self.current + 1) & 0x3FFFF
        if self.current == 0:
            self.current = self.base
        return outputs


# The test's noise, generated identically on both sides.
def noise(shift):
    seed = 1
    while True:
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        yield (((seed >> 16) - 0x8000) >> shift, (((seed >> 8) & 0xFFFF) - 0x8000) >> shift)


def run(case):
    mode, delay, feedback, shift, later_mode, later_delay = case
    console = Console()
    console.set_mode(mode)
    if delay >= 0:
        console.set_delay(delay)
    if feedback >= 0:
        console.set_feedback(feedback)
    samples = []
    for tick, inputs in zip(range(TICKS), noise(shift)):
        if tick == TICKS // 2:
            if later_mode >= 0:
                console.set_mode(later_mode)
            if later_delay >= 0:
                console.set_delay(later_delay)
        samples.extend(console.process(inputs))
    return samples


def fingerprint(samples):
    value = 0xCBF29CE484222325
    for byte in struct.pack(f'<{len(samples)}h', *samples):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def write_fingerprints():
    lines = ['// Generated by reverb_model.py; do not edit. What the reverb must produce for each case, from a model',
             '// of the console written apart from the C.',
             '#ifndef MODEL_FINGERPRINTS_H', '#define MODEL_FINGERPRINTS_H', '', '#include <stdint.h>', '',
             f'enum {{ MODEL_TICKS = {TICKS} }};', '',
             'typedef struct model_case', '{',
             '    int mode, delay, feedback, shift, later_mode, later_delay;',
             '    uint64_t fingerprint;', '} model_case;', '',
             'static const model_case model_cases[] = {']
    for index, case in enumerate(CASES):
        print(f'case {index}: {case}', file=sys.stderr)
        lines.append('    {' + ', '.join(str(field) for field in case) + f', 0x{fingerprint(run(case)):016X}ull}},')
    lines += ['};', '', '#endif', '']
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'model_fingerprints.h')
    with open(path, 'w') as header:
        header.write('\n'.join(lines))
    print(f'wrote {path}', file=sys.stderr)


def main(arguments):
    if not arguments:
        write_fingerprints()
    elif arguments[0] == '--print' and len(arguments) == 3:
        samples = run(CASES[int(arguments[1])])
        for tick in range(int(arguments[2])):
            print(tick, samples[2 * tick], samples[2 * tick + 1])
    elif arguments[0] == '--compare' and len(arguments) == 3:
        model = run(CASES[int(arguments[1])])
        with open(arguments[2], 'rb') as raw:
            c = struct.unpack(f'<{len(model)}h', raw.read())
        first = next((i for i in range(len(model)) if model[i] != c[i]), None)
        if first is None:
            print('the C matches the model')
        else:
            print(f'first difference at tick {first // 2}, {("left", "right")[first % 2]}: '
                  f'model {model[first]}, C {c[first]}')
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
