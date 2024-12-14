# Copyright Epic Games, Inc. All Rights Reserved.

import os
from peafour import P4
import subprocess as sp
from pathlib import Path

from .syncer import Syncer

#-------------------------------------------------------------------------------
p4_set_result = None
def get_p4_set(prop_name):
    if ret := os.getenv(prop_name):
        return ret

    global p4_set_result
    if not p4_set_result:
        p4_set_result = {}
        try: proc = sp.Popen(("p4", "set", "-q"), stdout=sp.PIPE)
        except: return

        for line in iter(proc.stdout.readline, b""):
            try: key, value = line.split(b"=", 1)
            except ValueError: continue
            p4_set_result[key.decode()] = value.strip().decode()

        proc.wait()
        proc.stdout.close()

    return p4_set_result.get(prop_name, None)

#-------------------------------------------------------------------------------
def login():
    try:
        detail = P4.login(s=True).run()
    except P4.Error:
        raise EnvironmentError("No valid Perforce session found. Run 'p4 login' to authenticate.")
    return getattr(detail, "User", None)

#-------------------------------------------------------------------------------
def get_p4config_name():
    return os.path.basename(get_p4_set("P4CONFIG") or ".p4config.txt")

#-------------------------------------------------------------------------------
def has_p4config(start_dir):
    from pathlib import Path
    p4config = get_p4config_name()
    for dir in (Path(start_dir) / "x").parents:
        candidate = dir / p4config
        if os.path.isfile(candidate):
            return True, candidate
    return False, p4config

#-------------------------------------------------------------------------------
def create_p4config(p4config_path, client, username, port=None):
    with open(p4config_path, "wt") as out:
        print_args = { "sep" : "", "file" : out }
        print("P4CLIENT=", client, **print_args)
        print("P4USER=", username, **print_args)
        if port:
            print("P4PORT", port, **print_args)

#-------------------------------------------------------------------------------
def ensure_p4config(start_dir=None):
    start_dir = start_dir or os.getcwd()
    found, p4config_name = has_p4config(start_dir)
    if found:
        return p4config_name, False

    username = login()

    # Get the client for 'start_dir'
    client = get_client_from_dir(start_dir, username)
    if not client:
        return
    client, root_dir = client

    # Now we know where to locate a p4config file
    p4config_path = f"{root_dir}/{p4config_name}"
    create_p4config(p4config_path, client, username)
    return p4config_path, True

#-------------------------------------------------------------------------------
def get_client_from_dir(root_dir, username):
    import socket
    host_name = socket.gethostname().lower()

    root_dir = Path(root_dir)

    clients = (x for x in P4.clients(u=username) if x.Host.lower() == host_name)
    for client in clients:
        if root_dir.is_relative_to(client.Root):
            return client.client, client.Root

#-------------------------------------------------------------------------------
def get_branch_root(depot_path):
    depot_path = str(depot_path)

    def fstat_paths():
        limit = 5 # ...two of which are always required
        query_path = "//"
        for piece in depot_path[2:].split("/")[:limit]:
            query_path += piece + "/"
            yield query_path + "GenerateProjectFiles.bat"

    print("Probing for well-known file:")
    for x in fstat_paths():
        print(" ", x)

    def on_error(message):
        if data := getattr(message, "data", None):
            if "has been unloaded" in data:
                raise EnvironmentError("Attempting to use an unloaded client")

    fstat = P4.fstat(fstat_paths(), T="depotFile")
    root_path = fstat.run(on_error=on_error)
    if root_path:
        return "/".join(root_path.depotFile.split("/")[:-1]) + "/"

    raise ValueError("Unable to establish branch root")

#-------------------------------------------------------------------------------
def run_p4vc(*args):
    try:
        proc = sp.run(("p4vc", *args))
    except FileNotFoundError:
        proc = sp.run(("p4v", "-p4vc", *args))
    return proc.returncode



#-------------------------------------------------------------------------------
class TempBranchSpec(object):
    def __init__(self, use, username, from_path, to_path, ignore_streams=False):
        import hashlib
        id = hashlib.md5()
        id.update(from_path.encode())
        id.update(to_path.encode())
        id = id.hexdigest()[:6]
        self._name = f"{username}-ushell.{use}-{id}"

        # To map between streams we need to extract the internal branchspec that
        # Perforce builds. If from/to aren't related streams it will fail so we
        # fallback to a conventional trivial branchspec.
        try:
            if ignore_streams:
                raise P4.Error("")

            branch = P4.branch(self._name, o=True, S=from_path[:-1], P=to_path[:-1])
            result = branch.run()
            spec = result.as_dict()
        except P4.Error:
            spec = {
                "Branch" : self._name,
                "View0"  : f"{from_path}... {to_path}...",
            }

        P4.branch(i=True).run(input_data=spec, on_error=False)

    def __del__(self):
        P4.branch(self._name, d=True).run()

    def __str__(self):
        return self._name
