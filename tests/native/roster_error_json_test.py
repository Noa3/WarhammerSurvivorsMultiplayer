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
items = [json.loads(s.removeprefix('ROSTER_ITEMS_JSON ')) for s in result.stdout.splitlines()
         if s.startswith('ROSTER_ITEMS_JSON ')]
assert len(items) == 1 and items[0]['action'] == 'upgradeItems'
assert items[0]['upgradeContext']['choiceSelection'] == selections[0]['upgradeContext']['choiceSelection']
assert items[0]['upgradeContext']['choiceItems'] == {
    'observed': True, 'childCount': 2,
    'items': [
        {'childReferencePresent': True, 'childUnityAlive': True,
         'componentReferencePresent': False, 'componentUnityAlive': False,
         'fieldsObserved': False, 'weaponType': 0, 'dataReferencePresent': False, 'isNew': False},
        {'childReferencePresent': True, 'childUnityAlive': True,
         'componentReferencePresent': True, 'componentUnityAlive': True,
         'fieldsObserved': True, 'weaponType': 7002, 'dataReferencePresent': False, 'isNew': True},
    ],
}
print('PASS actual native item JSON: explicit query, ordered component/data flags and typed enum values')
hints = [json.loads(s.removeprefix('ROSTER_HINT_JSON ')) for s in result.stdout.splitlines()
         if s.startswith('ROSTER_HINT_JSON ')]
assert len(hints) == 1 and hints[0]['action'] == 'upgradeHint'
assert hints[0]['upgradeContext']['choiceItems'] == items[0]['upgradeContext']['choiceItems']
assert hints[0]['upgradeContext']['fourthOptionHint'] == {
    'observed': True, 'referencePresent': True, 'unityAlive': True,
    'childComparisonComplete': True, 'childSlot': 1,
}
print('PASS actual native hint JSON: explicit query, typed reference/liveness/comparison flags and child slot')
equipment = [json.loads(s.removeprefix('ROSTER_EQUIPMENT_JSON ')) for s in result.stdout.splitlines()
             if s.startswith('ROSTER_EQUIPMENT_JSON ')]
assert len(equipment) == 1 and equipment[0]['action'] == 'equipment'
assert equipment[0]['equipment'] == {
    'state': 'captured',
    'actors': [{'slot': 1, 'inputId': 52, 'characterType': 91,
                'managerReferencePresent': True, 'collectionReferencePresent': True,
                'items': [
                    {'referencePresent': True, 'isWeapon': True, 'fieldsObserved': True,
                     'weaponType': 7002, 'ownerReferencePresent': True, 'ownerSlot': 1,
                     'runtimeType': 'Weapons.Quoted"Name'},
                    {'referencePresent': False, 'isWeapon': False, 'fieldsObserved': False,
                     'weaponType': 0, 'ownerReferencePresent': False, 'ownerSlot': -1,
                     'runtimeType': ''},
                ]}],
}
assert equipment[0]['upgradeContext'] == {'state': 'not_requested', 'actors': []}
print('PASS actual native equipment JSON: typed owner identity, runtime class escaping and absent item defaults')
profiles = [json.loads(s.removeprefix('ROSTER_INPUT_PROFILE_JSON ')) for s in result.stdout.splitlines()
            if s.startswith('ROSTER_INPUT_PROFILE_JSON ')]
assert len(profiles) == 1 and profiles[0]['action'] == 'inputProfile'
assert profiles[0]['inputProfile'] == {
    'state': 'captured', 'managerCount': 1, 'horizontalActionId': 101, 'verticalActionId': 103,
    'definitions': [{'sourceId': 7, 'axisIds': [2, 9]}, {'sourceId': 11, 'axisIds': []}],
}
assert profiles[0]['equipment'] == {'state': 'not_requested', 'actors': []}
print('PASS actual native input profile JSON: typed source/axis/action IDs, valid empty axes and opt-in boundaries')
sources = [json.loads(s.removeprefix('ROSTER_CUSTOM_SOURCES_JSON ')) for s in result.stdout.splitlines()
           if s.startswith('ROSTER_CUSTOM_SOURCES_JSON ')]
assert len(sources) == 1 and sources[0]['action'] == 'controlSources'
assert sources[0]['controlEnvironment']['customSourcesCaptured'] is True
assert sources[0]['controlEnvironment']['players'][0]['customSources'] == [
    {'controllerId': 400, 'sourceId': 7, 'axisIds': [2, 9], 'mapIds': [401]},
    {'controllerId': 402, 'sourceId': 11, 'axisIds': [], 'mapIds': []},
]
assert sources[0]['controlEnvironment']['players'][0]['maps'][0]['controllerId'] == 400
admissions = [json.loads(s.removeprefix('ROSTER_ZERO_ADMISSION_JSON ')) for s in result.stdout.splitlines()
              if s.startswith('ROSTER_ZERO_ADMISSION_JSON ')]
assert len(admissions) == 1 and admissions[0]['action'] == 'inputZeroStart'
assert admissions[0]['inputProfile']['state'] == admissions[0]['controlEnvironment']['state'] == 'captured'
assert admissions[0]['inputProfile']['definitions'] == profiles[0]['inputProfile']['definitions']
assert 'customSourcesCaptured' not in admissions[0]['controlEnvironment']
assert admissions[0]['inputZero']['stateCaptured'] is False
print('PASS actual native custom-source and zero-admission JSON: distinct IDs, mapped/unmapped sources and observed catalogs')
