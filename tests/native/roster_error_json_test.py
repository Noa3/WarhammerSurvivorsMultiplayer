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
    'upgradeContext': 'upgrade "context"\\reference\n',
}
contexts = [json.loads(s.removeprefix('ROSTER_UPGRADE_JSON ')) for s in result.stdout.splitlines()
            if s.startswith('ROSTER_UPGRADE_JSON ')]
assert len(contexts) == 1
assert contexts[0]['action'] == 'upgradeContext'
context = contexts[0]['upgradeContext']
assert context == {
    'state': 'captured', 'levelUpPageCount': 1, 'gameSessionReferencePresent': True,
    'activeCharacterReferencePresent': True, 'activeCharacterSlot': 1, 'levelUpFactoryReferencePresent': True,
    'choiceContainerGroupReferencePresent': True, 'choiceContainerItemsReferencePresent': True,
    'choiceContainerItemReferences': [False, True],
    'actors': [
        {'slot': 0, 'inputId': 41, 'characterType': 37, 'weaponsManagerReferencePresent': True,
         'accessoriesManagerReferencePresent': False, 'accessoriesCollectionReferencePresent': False},
        {'slot': 1, 'inputId': 52, 'characterType': 91, 'weaponsManagerReferencePresent': False,
         'accessoriesManagerReferencePresent': True, 'accessoriesCollectionReferencePresent': True},
    ],
}
errors = [json.loads(s.removeprefix('ROSTER_UPGRADE_ERROR_JSON ')) for s in result.stdout.splitlines()
          if s.startswith('ROSTER_UPGRADE_ERROR_JSON ')]
assert len(errors) == 1 and errors[0]['upgradeContext'] == {'state': 'error', 'actors': []}
print('PASS actual native roster JSON error roundtrip: quotes, backslash, newline, tab, and control characters')
print('PASS actual native upgrade-context JSON: typed booleans, actor identity, null presence, stale-error suppression')
selections = [json.loads(s.removeprefix('ROSTER_SELECTION_JSON ')) for s in result.stdout.splitlines()
              if s.startswith('ROSTER_SELECTION_JSON ')]
assert len(selections) == 1 and selections[0]['action'] == 'upgradeSelection'
assert selections[0]['upgradeContext']['choiceSelection'] == {
    'observed': True, 'selectedContainerSlot': 1,
    'unityAlive': [False, True], 'activeInHierarchy': [False, True],
}
print('PASS actual native explicit selection JSON: opt-in action, ordered Unity-reported flags and selected slot')
