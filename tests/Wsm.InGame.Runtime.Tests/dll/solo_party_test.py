"""Solo-party decisions at the Native AOT C ABI; spawn records are requests, not game entities."""
import argparse
from collections import Counter
import ctypes
from itertools import count
from pathlib import Path
import unittest

from gameplay_bridge_test import Gameplay, wait_for
from run_dll_tests import ACCEPTED, INVALID_ARGUMENT, STATE_DISCONNECTED, STATE_HOSTING


def actor(name, x, alive=True):
    return dict(id=name, x=x, y=0, alive=alive)


def party(run, tick, wave=None, phase='playing', hp=(17, 29, 41), human_hp=53):
    bots = [dict(actor=actor(name, x, h > 0), hp=h, maxHp=m, weaponRange=32, maxStep=1)
            for name, x, h, m in zip(('red', 'blue', 'gold'), (0, 10, 20), hp, (31, 47, 61))]
    frame = dict(run=run, tick=tick, phase=phase, human=dict(id='host', hp=human_hp, maxHp=79), bots=bots,
                 enemies=[actor('east', 21), actor('west', 1), actor('middle', 11)])
    if wave is not None:
        frame['wave'] = dict(id=wave, enemyType='crawler')
    return frame


class SoloParty(unittest.TestCase):
    def send(self, frame):
        """Only a validation refusal (immediate or correlated) may count as a rejected frame."""
        self.assertEqual(self.api.peers(), 0)
        rid = next(self.ids)
        rc = self.api.submit(dict(kind='soloParty', requestId=rid, party=frame))
        if rc == INVALID_ARGUMENT:
            return None, f'submit returned {rc}'
        self.assertEqual(rc, ACCEPTED, 'unexpected ABI status, not a frame validation refusal')
        event = self.api.read_kind('submission')
        self.assertEqual(event['requestId'], rid)
        self.assertEqual(event['epoch'], self.epoch)
        result = event.get('partyDecision')
        if event.get('error'):
            self.assertIsNone(result, event)
            return None, event['error']
        self.assertIsInstance(result, dict, event)
        return result, None

    def check_decision(self, frame, decision, slots=(), ended=False, targets=None):
        self.assertEqual((decision['run'], decision['tick']), (frame['run'], frame['tick']))
        self.assertIs(decision['runEnded'], ended)
        for key in ('id', 'hp', 'maxHp'):
            self.assertEqual(decision['human'][key], frame['human'][key], key)
        rows = {row['actor']: row for row in decision['bots']}
        self.assertCountEqual([row['actor'] for row in decision['bots']], [b['actor']['id'] for b in frame['bots']])
        nearest = targets or {'red': 'west', 'blue': 'middle', 'gold': 'east'}
        for bot in frame['bots']:
            name = bot['actor']['id']
            row = rows[name]
            self.assertEqual((row['hp'], row['maxHp']), (bot['hp'], bot['maxHp']), name)
            if not ended and bot['hp'] and frame['phase'] == 'playing':
                self.assertEqual(row.get('attackTarget'), nearest[name], name)
                for axis in ('x', 'y'):
                    self.assertLessEqual(abs(row[axis]), bot['maxStep'], name)
            else:
                self.assertEqual((row['x'], row['y'], row.get('attackTarget')), (0, 0, None), name)
        expected = Counter((frame['run'], frame['wave']['id'], slot, frame['bots'][slot]['actor']['id'],
                            frame['wave']['enemyType']) for slot in slots)
        actual = Counter(tuple(s[k] for k in ('run', 'wave', 'slot', 'anchor', 'enemyType'))
                         for s in decision['spawns'])
        self.assertEqual(actual, expected, 'spawn requests by wave and living zero-based slot')

    def accept(self, frame, **expected):
        decision, error = self.send(frame)
        self.assertIsNone(error, f"valid soloParty run={frame['run']} tick={frame['tick']}: {error}")
        self.check_decision(frame, decision, **expected)

    def allies_on_first_tick(self, run, alias, reverse):
        frame = party(run, 1, wave=1)
        # Put a teammate closer than the real target. Reverse bot iteration as well
        # as enemy iteration: no ally may become targetable before its slot is bound.
        impostor = actor(alias, 0 if alias != 'red' else 20)
        frame['enemies'].insert(0, impostor)
        if reverse:
            frame['bots'].reverse()
            frame['enemies'].reverse()
        result, error = self.send(frame)
        if error is None:
            self.check_decision(frame, result, slots=range(3))
        else:
            frame['enemies'].remove(impostor)
            self.accept(frame, slots=range(3))  # refused first tick was not committed

    def exercise(self, death):
        self.assertEqual(self.start_host(0, 0, 3, 2, death), ACCEPTED)
        wait_for(lambda: self.api.state() == STATE_HOSTING)
        self.assertGreater(self.api.bound(), 0)
        self.epoch = self.api.info()['epoch']
        self.assertGreater(self.epoch, 0)
        self.ids = count(1)
        self.accept(party(1, 1, wave=1), slots=range(3))
        reordered = party(1, 2, wave=1)
        reordered['enemies'].reverse()
        self.accept(reordered)  # same wave on another tick: do not request it twice
        for tick in (2, 1):
            self.assertIsNotNone(self.send(party(1, tick, wave=2))[1], f'accepted stale tick {tick}')
        invalid = party(1, 3, wave=0)
        self.assertIsNotNone(self.send(invalid)[1], 'accepted nonpositive wave ID')
        self.accept(party(1, 3, wave=2, hp=(15, 0, 40)), slots=(0, 2))
        self.accept(party(1, 4, wave=3, phase='paused', hp=(15, 0, 40)))
        self.accept(party(1, 5, wave=4, phase='upgrade', hp=(15, 0, 40)))
        self.accept(party(1, 6, wave=5, hp=(0, 0, 0)))  # host is still alive

        # New observations, not fixed bot/enemy pairings. No wave was requested.
        self.accept(party(2, 1, wave=1), slots=range(3))
        moved = party(2, 2)
        moved['enemies'] = [actor('east', 1), actor('middle', 21), actor('west', 11),
                            actor('fallen', 0, alive=False)]
        self.accept(moved, targets={'red': 'east', 'blue': 'west', 'gold': 'middle'})
        self.accept(party(2, 3, wave=2, human_hp=0), ended=True)
        # Even if HP changes later, report that tick's individual values without restarting combat.
        self.accept(party(2, 4, wave=3, hp=(0, 13, 27), human_hp=7), ended=True)
        self.accept(party(3, 1, wave=1), slots=range(3))  # clears defeat, tick and wave memory
        for i, alias in enumerate(('host', 'gold', 'red')):
            for order in (0, 1):
                with self.subTest(alias=alias, reversed=bool(order)):
                    self.allies_on_first_tick(4 + 2 * i + order, alias, order)
        for i, (phase, conflict) in enumerate((('paused', 'older'), ('paused', 'type'),
                                               ('upgrade', 'older'), ('upgrade', 'type'))):
            with self.subTest(phase=phase, conflict=conflict):
                run = 10 + i
                self.accept(party(run, 1, wave=1), slots=range(3))
                self.accept(party(run, 2, wave=3, phase=phase))
                self.accept(party(run, 3, phase=phase))  # absent field is not a wave reset
                invalid = party(run, 4, wave=2 if conflict == 'older' else 3)
                if conflict == 'type':
                    invalid['wave']['enemyType'] = 'different-type'
                decision, error = self.send(invalid)
                self.assertIsNotNone(error, f'{phase} resume accepted {conflict} wave: {decision}')
                # Refusal must not consume the tick or the pending wave. Only surviving bots add orders.
                self.accept(party(run, 4, wave=3, hp=(17, 0, 41)), slots=(0, 2))
                self.accept(party(run, 5, phase=phase, wave=3))
                self.accept(party(run, 6, wave=3))  # no second batch on resume
        self.assertEqual(self.api.peers(), 0)

    def test_solo_party(self):
        api = self.api
        self.start_host = api.dll.wsm_ingame_host_rules
        self.start_host.argtypes = [ctypes.c_int] * 5
        self.start_host.restype = ctypes.c_int
        try:
            for mode, death in [('AnyDeathEndsRun', 1), ('AllDead', 0)]:
                with self.subTest(mode=mode):
                    try:
                        self.exercise(death)
                    finally:
                        self.assertEqual(api.leave(), ACCEPTED)
                        wait_for(lambda: api.state() == STATE_DISCONNECTED)
        finally:
            self.assertEqual(api.shutdown(), ACCEPTED)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True)
    SoloParty.api = Gameplay(str(Path(parser.parse_args().dll).resolve()))
    unittest.main(argv=[__file__], verbosity=2)
