import os
import numpy as np

class OverlayPath:
    """ Base overlay directory class loads read paths from a parent reference if not local
    """
    @classmethod
    def create(cls, root_path: str, name: str, derive_from: str) -> 'OverlayPath':
        """ Create or update metadata folder under metadata root and return OverlayPath object
        """
        md = OverlayPath()
        md._internal_init(root_path, name)
        md._init_metadata_path(derive_from)
        md._parent = md._load_parent()
        return md
    
    @classmethod
    def open(cls, root_path: str, name: str) -> 'OverlayPath':
        """ Open a metadata folder that already exists and return OverlayPath object
        """
        md = OverlayPath()
        md._internal_init(root_path, name)
        md._parent = md._load_parent()
        if not os.path.exists(md._path):
            return None
        return md
    
    def read_file(self, file_name: str) -> str:
        """ Get full path to a metadata file for reading (follows derived chain)
        """
        if file_name in self._path_cache:
            return self._path_cache[file_name]
        full_path = self._full_path(file_name)
        if os.path.exists(full_path):
            return self._update_cached(file_name, full_path)
        if self._parent is not None:
            parent_path = self._parent.read_file(file_name)
            return self._update_cached(file_name, parent_path)
        return None

    def write_file(self, file_name: str) -> str:
        """ Get full path to a metadata file for writing (always current metadata folder)
        """
        full_path = self._full_path(file_name)
        return self._update_cached(file_name, full_path)
    
    def remove_file(self, file_name: str) -> str:
        """ Get full path to a metadata file for removal (always current metadata folder)
        """
        # Drop cache entry, just in case
        self._path_cache.pop(file_name, None)
        return self._full_path(file_name)
    
    def _internal_init(self, root_path: str, name: str) -> None:
        self._name = name
        self._root = root_path
        self._path = os.path.join(self._root, self._name)
        self._parent: OverlayPath = None
        self._path_cache: dict[str,str] = {}

    def _full_path(self, file_name: str) -> str:
        return os.path.join(self._path, file_name)
    
    def _update_cached(self, file_name: str, full_path: str) -> str:
        self._path_cache[file_name] = full_path
        return full_path

    def _init_metadata_path(self, derive_from: str) -> None:
        os.makedirs(self._path, exist_ok=True)
        self._set_derived(derive_from)

    def _set_derived(self, derive_from: str) -> None:
        parent_fpath = self._full_path('.parent')
        if derive_from is None and os.path.exists(parent_fpath):
            os.remove(parent_fpath)
        elif derive_from is not None:
            with open(parent_fpath, 'wt') as fp:
                fp.write(derive_from)
    
    def _load_parent(self) -> 'OverlayPath':
        parent_fpath = self._full_path('.parent')
        if not os.path.exists(parent_fpath):
            return None
        with open(parent_fpath, 'rt') as fp:
            parent_name = fp.readline().strip()
        if not parent_name:
            return None
        return OverlayPath.open(self._root, parent_name)


class Metadata:
    """ Implements a simple overlay system for pulling metadata files from derived folders.
        
        For example if GroomLOD1 derives from GroomLOD0 then metadata that doesn't  exist in
        the GroomLOD1 folder will be loaded from GroomLOD0.

        Writes always go to the current metadata folder.
    """
    def __init__(self, overlay: OverlayPath) -> None:
        self._overlay = overlay

    @classmethod
    def create(cls, metadata_root: str, name: str, derived_from: str) -> 'Metadata':
        return Metadata(OverlayPath.create(metadata_root, name, derived_from))
    
    @classmethod
    def open(cls, metadata_root: str, name: str) -> 'Metadata':
        return Metadata(OverlayPath.open(metadata_root, name))

    def load(self, file_name: str, 
             mmap_mode=None,
             allow_pickle=False,
             fix_imports=True,
             encoding='ASCII') -> np.ndarray:
        """ Wraps numpy.load() functionality
        """
        fpath = self._overlay.read_file(file_name)
        if fpath is None:
            return None
        return np.load(fpath, mmap_mode=mmap_mode,
                       allow_pickle=allow_pickle,
                       fix_imports=fix_imports,
                       encoding=encoding)

    def save(self, file_name: str,
             arr: np.ndarray,
             allow_pickle: bool = True,
             fix_imports: bool = True) -> None:
        """ Wraps numpy.save() functionality
        """
        fpath = self._overlay.write_file(file_name)
        np.save(fpath, arr, allow_pickle=allow_pickle, fix_imports=fix_imports)

    def savez(self, file_name: str, *args, **kwargs) -> None:
        """ Wraps numpy.savez() functionality
        """
        fpath = self._overlay.write_file(file_name)
        np.savez(fpath, *args, **kwargs)

    def remove(self, file_name: str):
        """ Remove a metadata file for current dir if it exists (will not remove parent metadata)
        """
        fpath = self._overlay.remove_file(file_name)
        if os.path.exists(fpath):
            os.remove(fpath)
