"""Drive the host rules and observed-bot mailbox in the built DLL, without a game or guest."""
import argparse
import ctypes
from pathlib import Path
from gameplay_bridge_test import Gameplay, wait_for


def main(path):
    api = Gameplay(str(Path(path).resolve()))
    start = api.dll.wsm_ingame_host_rules
    start.argtypes = [ctypes.c_int] * 5
    start.restype = ctypes.c_int
    try:
        assert start(0, 0, 1, 2, 0) == 0
        wait_for(lambda: api.state() == 1)
        assert api.peers() == 0
        actor = dict(id='ally', x=0, y=0, alive=True)
        enemy = dict(id='enemy', x=7, y=0, alive=True)
        f = dict(slot=0, run=1, tick=0, primary='primary', phase='playing', actor=actor,
                 weaponRange=3, maxStep=1, enemies=[enemy])
        def step():
            f['tick'] += 1
            assert api.submit(dict(kind='botFrame', requestId=f['tick'], bot=f)) == 0
            event = api.read_kind('submission')
            assert event.get('error') is None and event['requestId'] == f['tick']
            result = event['botDecision']
            assert (result['run'], result['slot'], result['tick'], result['actor']) == (1, 0, f['tick'], actor['id'])
            return result
        # The blocked actor is still at the same captured point on the next tick.
        assert step()['x'] == 1 and step()['x'] == 1
        actor['x'] = 6
        retreat = step()
        assert retreat['x'] < 0 and retreat['attackTarget'] == 'enemy'
        f.update(phase='paused')
        paused = step()
        assert paused['x'] == paused['y'] == 0 and 'attackTarget' not in paused
        f.update(phase='upgrade', offer=dict(owner='primary', level=2, choices=['blade', 'boots']))
        assert 'choice' not in step()
        f['offer']['owner'] = 'ally'
        own = step()
        assert own['choice'] in f['offer']['choices'] and own['level'] == 2 and own['x'] == 0
        del f['offer']
        f.update(phase='playing', enemies=[], personalPickup=dict(id='xp:5', x=6, y=3, alive=True))
        pickup = step()
        assert pickup['x'] == 0 and pickup['y'] == 1 and 'attackTarget' not in pickup
        actor['alive'] = False
        dead = step()
        assert dead['x'] == dead['y'] == 0 and 'choice' not in dead
        assert api.peers() == 0
        print('PASS solo host: blocked movement, retreat, pause, personal offer, personal pickup direction and death (controlled observations)')
    finally:
        assert api.shutdown() == 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', required=True)
    main(parser.parse_args().dll)
