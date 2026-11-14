"""Parse output of the actual native roster serializer, including escaped errors."""
import json
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True, check=True)
rows = [s.removeprefix('ROSTER_ERROR_JSON ') for s in result.stdout.splitlines()
        if s.startswith('ROSTER_ERROR_JSON ')]
assert len(rows) == 1, 'exactly one native serialized result required'
value = json.loads(rows[0])
assert 'errors' in value, 'native result omitted structured failure reasons'
assert value['errors'] == {
    'action': 'action "quoted"\\path\n\t',
    'roster': '\b\f\r',
    'characterConfiguration': '\x01',
    'playerObservation': 'player observation failed',
}
print('PASS actual native roster JSON error roundtrip: quotes, backslash, newline, tab, and control characters')
