"""Real Native AOT C ABI gameplay delivery through separate loopback processes.

Uses original controlled test payloads, not game observations. Never starts the
actual game, fabricates network responses, or requires the Unity SDK.
"""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import queue
import threading
import time
import uuid
from run_dll_tests import Runtime


class Gameplay(Runtime):
    def __init__(self, path):
        super().__init__(path)
        for name in ('version', 'epoch'):
            getattr(self.dll, 'wsm_ingame_gameplay_' + name).restype = ctypes.c_int
        for name in ('info', 'poll'):
            method = getattr(self.dll, 'wsm_ingame_gameplay_' + name)
            method.restype = ctypes.c_int
            method.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.dll.wsm_ingame_gameplay_submit.restype = ctypes.c_int
        self.dll.wsm_ingame_gameplay_submit.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]

    def info(self):
        length = self.dll.wsm_ingame_gameplay_info(None, 0)
        assert length > 0
        buffer = ctypes.create_string_buffer(length + 1)
        assert self.dll.wsm_ingame_gameplay_info(buffer, len(buffer)) == length
        return json.loads(buffer.raw[:length])

    def submit(self, request, epoch=None):
        data = json.dumps(request, separators=(',', ':')).encode()
        buffer = ctypes.create_string_buffer(data)
        return self.dll.wsm_ingame_gameplay_submit(self.info()['epoch'] if epoch is None else epoch, buffer, len(data))

    def poll(self):
        for _ in range(5):
            length = self.dll.wsm_ingame_gameplay_poll(None, 0)
            assert length >= 0, length
            if length == 0:
                return None
            buffer = ctypes.create_string_buffer(length + 64)
            result = self.dll.wsm_ingame_gameplay_poll(buffer, len(buffer))
            if result == -5:
                continue
            assert 0 < result < len(buffer) and buffer.raw[result] == 0, result
            return json.loads(buffer.raw[:result])
        raise AssertionError('Repeated event buffer growth')

    def read_kind(self, kind, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            item = self.poll()
            if item and item['kind'] == kind:
                return item
            time.sleep(.005)
        raise AssertionError('Event not received: ' + kind)


def wait_for(predicate):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError('Bounded state wait expired')


def read_owned_line(child):
    result = queue.Queue(maxsize=1)
    threading.Thread(target=lambda: result.put(child.stdout.readline()), daemon=True).start()
    try:
        line = result.get(timeout=20)
    except queue.Empty:
        child.kill()
        raise AssertionError('Owned guest IPC deadline expired')
    assert line, 'Owned guest ended before its test result'
    return line


def guest_worker(dll, port):
    api = Gameplay(dll)
    assert api.join('127.0.0.1', port) == 0
    wait_for(lambda: api.state() == 3)
    print(json.dumps(api.info()), flush=True)
    try:
        for line in sys.stdin:
            command = json.loads(line)
            op = command['op']
            if op == 'submit':
                result = api.submit(command['request'], command.get('epoch'))
            elif op == 'read':
                result = api.read_kind(command['kind'])
            elif op == 'quiet':
                deadline = time.monotonic() + .25
                found = []
                while time.monotonic() < deadline:
                    item = api.poll()
                    if item:
                        found.append(item['kind'])
                    time.sleep(.005)
                result = found
            elif op == 'rejoin':
                assert api.leave() == 0
                wait_for(lambda: api.state() == 0)
                assert api.join('127.0.0.1', port) == 0
                wait_for(lambda: api.state() == 3)
                result = api.info()
            elif op == 'quit':
                assert api.shutdown() == 0
                print(json.dumps({'shutdown': True}), flush=True)
                return
            else:
                raise AssertionError('Unknown test command')
            print(json.dumps(result), flush=True)
    finally:
        api.shutdown()


def main(dll, exports_only):
    api = Gameplay(dll)
    assert api.dll.wsm_ingame_version() == 1 and api.dll.wsm_ingame_gameplay_version() == 1
    assert api.info()['epoch'] == 0 and not api.info()['active']
    if exports_only:
        print('PASS optional gameplay exports, original ABI unchanged')
        return
    children = []
    def rpc(child, request):
        child.stdin.write(json.dumps(request) + '\n')
        child.stdin.flush()
        # Child-side operations have their own bounded deadlines. Parent waits
        # only on the verified child it started, never on an arbitrary process.
        line = read_owned_line(child)
        return json.loads(line)
    try:
        assert api.host(0, 0) == 0
        wait_for(lambda: api.state() == 1)
        port = api.bound()
        ids = []
        for _ in range(2):
            child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--dll', dll, '--guest-worker', str(port)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            children.append(child)
            identity = json.loads(read_owned_line(child))
            assert identity['active'] and identity['role'] == 'guest' and identity['epoch'] > 0
            ids.append(identity)
        wait_for(lambda: api.peers() == 2)
        print('PASS two real Native AOT guest processes joined', flush=True)
        assert rpc(children[0], {'op': 'submit', 'request': {'kind': 'input', 'requestId': 1, 'x': .25, 'y': -.5}}) == 0
        frame = api.read_kind('input')
        assert frame['playerId'] == ids[0]['playerId'] and frame['input']['x'] == .25 and frame['input']['y'] == -.5
        assert frame['epoch'] == api.info()['epoch'] and frame['ageMilliseconds'] >= 0
        print('PASS authenticated native input crosses TCP and is polled', flush=True)
        snapshot = {'tick': 1, 'entities': [{'id': 'native-abi-fixture', 'x': 12.5, 'y': -4.25, 'alive': True}], 'enemies': []}
        assert api.submit({'kind': 'snapshot', 'requestId': 1, 'snapshot': snapshot}) == 0
        worlds = [rpc(c, {'op': 'read', 'kind': 'snapshot'})['snapshot'] for c in children]
        assert worlds[0] == worlds[1] == snapshot
        print('PASS same snapshot delivered to both actual guest processes', flush=True)
        receipt = {'receiptId': str(uuid.uuid4()), 'recipientId': ids[0]['playerId'], 'kind': 0, 'amount': 9}
        assert api.submit({'kind': 'reward', 'requestId': 2, 'reward': receipt}) == 0
        reward = rpc(children[0], {'op': 'read', 'kind': 'reward'})['reward']
        assert reward['receiptId'] == receipt['receiptId'] and reward['amount'] == 9
        assert 'reward' not in rpc(children[1], {'op': 'quiet'})
        print('PASS recipient-private XP receipt is not sent to the other guest', flush=True)
        assert rpc(children[1], {'op': 'submit', 'request': {'kind': 'upgrade', 'requestId': 1, 'level': 2, 'choice': 'native-abi-personal-choice'}}) == 0
        choice = api.read_kind('upgrade')
        assert choice['playerId'] == ids[1]['playerId'] and choice['upgrade']['choice'] == 'native-abi-personal-choice'
        print('PASS personal upgrade choice crosses the real native ABI and TCP', flush=True)
        old = ids[0]['epoch']
        renewed = rpc(children[0], {'op': 'rejoin'})
        assert renewed['epoch'] > old and renewed['playerId'] != ids[0]['playerId']
        assert rpc(children[0], {'op': 'submit', 'epoch': old, 'request': {'kind': 'input', 'requestId': 2, 'x': 0, 'y': 0}}) == -2
        print('PASS stale native epoch cannot submit into rejoined session', flush=True)
        assert api.dll.wsm_ingame_gameplay_poll(None, 1) == -1
        assert api.dll.wsm_ingame_gameplay_submit(api.info()['epoch'], None, 1) == -1
        assert api.submit({'kind': 'input', 'requestId': 5, 'x': 0, 'y': 0}) == -1
        print('PASS null buffers and unauthorized host command refused', flush=True)
        for child in children:
            assert rpc(child, {'op': 'quit'}) == {'shutdown': True}
            assert child.wait(timeout=15) == 0, child.stderr.read()
        assert api.shutdown() == 0
        print('PASS native gameplay transport suite; no engine/world gameplay claim', flush=True)
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=15)
        api.shutdown()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', required=True)
    parser.add_argument('--guest-worker', type=int)
    parser.add_argument('--check-exports', action='store_true')
    args = parser.parse_args()
    if args.guest_worker:
        guest_worker(args.dll, args.guest_worker)
    else:
        main(args.dll, args.check_exports)
