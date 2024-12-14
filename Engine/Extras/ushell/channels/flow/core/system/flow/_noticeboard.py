import os
import marshal
from typing import Any
from pathlib import Path

#-------------------------------------------------------------------------------
class _Journaler(object):
    _dir        : Path
    _name       : str
    _uniqueness : int   = os.getpid()

    def __init__(self, path:Path) -> None:
        self._dir = path.parent
        self._name = path.name

    @classmethod
    def _get_uniqueness(self) -> int:
        _Journaler._uniqueness += 1
        return _Journaler._uniqueness

    def _get_serial_range(self) -> tuple[int, int]:
        min_index = 1 << 31
        max_index = -1
        for item in self._dir.glob(self._name + ".*"):
            try:
                candidate = int(item.suffix[1:])
                min_index = min(min_index, candidate)
                max_index = max(max_index, candidate)
            except ValueError:
                pass
        return min(min_index, max_index), max_index

    def read(self) -> dict:
        _, serial = self._get_serial_range()
        if serial < 0:
            return {}

        try:
            path = self._dir / (self._name + f".{serial}")
            for i in range(5):
                try:
                    with path.open("rb") as inp:
                        if data := marshal.load(inp):
                            return data
                    return {}
                except PermissionError as e:
                    continue
        except OSError:
            pass

        return {}

    def flush(self, edits:dict) -> None:
        self._dir.mkdir(parents=True, exist_ok=True)
        make_path = lambda x: self._dir / (self._name + "." + str(x))
        for retry in range(50):
            min_serial, serial = self._get_serial_range()
            if serial >= 0:
                try:
                    with make_path(serial).open("rb") as inp:
                        data = marshal.load(inp)
                except IOError as e:
                    import time
                    time.sleep(0.02)
                    continue
            else:
                data = {}

            data.update(edits)

            # Write edited board to a temporary file
            temp_name = f"wr_{_Journaler._get_uniqueness()}"
            temp_path = make_path(temp_name)
            try:
                with temp_path.open("wb") as out:
                    marshal.dump(data, out)
            except IOError as e:
                return

            # Try and swap edited temp file into place
            serial += 1
            dest_path = make_path(serial)
            try:
                temp_path.rename(dest_path)
                break
            except (FileExistsError, PermissionError):
                temp_path.unlink()
                continue
            except Exception as e:
                temp_path.unlink()
                continue

        for index in range(min_serial, serial - 8):
            try: make_path(index).unlink(missing_ok=True)
            except: break

#-------------------------------------------------------------------------------
class Noticeboard(object):
    _edits          : dict
    _context        : int
    _journaler      : _Journaler

    def __init__(self, board_path:Path|str, context:int|str=-1) -> None:
        self._edits = {}
        self._context = context
        self._journaler = _Journaler(Path(board_path))

    def __del__(self) -> None:
        self.flush()

    def __setitem__(self, key:str, value:Any) -> None:
        sub_edit = self._edits.setdefault(self._context, {})
        sub_edit[key] = value

    def __getitem__(self, name:str) -> Any:
        data = self._journaler.read()
        if sub_data := data.get(self._context):
            return sub_data.get(name)

    def get(self, name:str, default:Any) -> Any:
        ret = self[name]
        return default if ret is None else ret

    def flush(self) -> None:
        if self._edits:
            self._journaler.flush(self._edits)
            self._edits = {}

    def debug(self, print_fn) -> None:
        data = self._journaler.read()
        print_fn(data)
