"""Summarize AVR compiler stack reports; no hardware watermark is inferred."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
folder = root / 'build/avr'
rows = []
for path in sorted(folder.glob('*.su')):
    for line in path.read_text().splitlines():
        location, amount, qualifier = line.split('\t')
        if qualifier != 'static':
            raise RuntimeError('Non-static frame requires manual analysis: '+line)
        rows.append({'file': path.name, 'function': location.rsplit(':', 1)[-1],
                     'bytes': int(amount), 'kind': qualifier})
if not rows:
    raise RuntimeError('No .su reports; build AVR with -fstack-usage first')
report = json.loads((root/'build/report.json').read_text())
static = report['avr']['static_sram']
c_total = sum(row['bytes'] for row in rows)
# This is an intentionally loose sum of EVERY C function frame, rather than
# an assertion that all frames actually coexist. Source/assembly review must
# confirm acyclic calls and platform callbacks for the selected firmware.
library_allowance = 64
reservation = 1536
if c_total + library_allowance > reservation:
    raise RuntimeError('Reviewed stack reservation needs updating')
summary = {'compiler_frames': rows, 'sum_all_c_frames': c_total,
           'reviewed_library_allowance': library_allowance,
           'conservative_estimate': c_total+library_allowance,
           'reserved_c_stack': reservation, 'static_sram': static,
           'unreserved_sram': 16384-static-reservation,
           'status': 'STATIC_ESTIMATE_NOT_HARDWARE_MEASUREMENT',
           'assumptions': ['Stock AVR adapter callbacks only', 'Interrupts disabled',
                           'No C recursion or callback reentry',
                           'Library assembly reviewed as described in docs/VALIDATION.md']}
(folder/'stack.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps({k: v for k, v in summary.items() if k != 'compiler_frames'}, indent=2))
