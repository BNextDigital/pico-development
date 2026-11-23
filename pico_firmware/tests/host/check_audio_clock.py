#!/usr/bin/env python3
"""Exercise the pinned nrfx divider search without a target I2S peripheral."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

nrf = Path(sys.argv[1]).resolve()
pico = Path(__file__).resolve().parents[2]
nrfx = nrf.parent / 'modules/hal/nordic/nrfx/drivers/src/nrfx_i2s.c'
source = nrfx.read_text()


def function(name):
    pattern = rf'^(?:static inline bool|int) {name}\(.*?^}}'
    return re.search(pattern, source, re.M | re.S).group()


# The enum encodings are irrelevant to the search. Use their ratios/divisors
# for a readable host result and select the 52840's discrete-divider branch.
definitions = []
for prefix in ('NRF_I2S_MCK_32MDIV', 'NRF_I2S_RATIO_'):
    for symbol, value in sorted(set(re.findall(rf'({prefix}(\d+)X?)\b', source))):
        definitions.append(f'#define {symbol} {value}\n')

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'clock.cpp').write_text(
        '#include <pico/audio.hpp>\n#include <cassert>\n#include <cerrno>\n'
        '#define NRFX_ASSERT assert\n#define NRF_I2S_HAS_CLKCONFIG 0\n'
        '#define NRF_I2S_HAS_SWIDTH_32BIT 0\n#define NRF_I2S_MCK_DISABLED 0\n'
        '#define NRF_I2S_MODE_MASTER 1\n#define NRF_I2S_SWIDTH_16BIT 16\n'
        '#define NRF_I2S_SWIDTH_24BIT 24\n#define NRFX_LOG_ERROR(...)\n'
        '#define NRFX_ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))\n'
        '#define NRFX_DIFF(a,b) ((a)>(b) ? (a)-(b) : (b)-(a))\n'
        'using nrf_i2s_mode_t=int; using nrf_i2s_ratio_t=int; '
        'using nrf_i2s_swidth_t=int; using nrf_i2s_mck_t=int;\n'
        'struct nrfx_i2s_clk_params_t { uint32_t base_clock_freq, transfer_rate; '
        'int swidth; bool allow_bypass; };\n'
        'struct nrfx_i2s_prescalers_t { int mck_setup, ratio; };\n'
        + ''.join(definitions)
        + function('validate_config') + '\n' + function('nrfx_i2s_prescalers_calc')
        + '''
int main()
{
    using namespace pico::audio;
    const auto verify = [](uint32_t request, int divider, int ratio) {
        const nrfx_i2s_clk_params_t config{kClockSourceHz, request, kWordBits, false};
        nrfx_i2s_prescalers_t result{};
        assert(nrfx_i2s_prescalers_calc(&config, &result) == 0);
        assert(result.mck_setup == divider && result.ratio == ratio);
    };
    verify(31250, 32, 32);
    verify(32000, 31, 32);
    verify(48000, 21, 32);
    verify(kSampleRate, kMckDivider, kMckRatio);
}
'''
    )
    binary = path / 'clock'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-I',
                    str(pico / 'lib/pico_core/include'), str(path / 'clock.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Pinned nrfx I2S prescaler regression passed')
