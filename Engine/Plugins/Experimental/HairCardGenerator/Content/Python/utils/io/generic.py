# -*- coding: utf-8 -*-
"""
IO Utils

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

__version__ = "0.2.3"
__all__ = [
    'safe_dir', 'get_filename_from_path', 'file_exists', 'write_json',
    'write_numpy_to_bin_file', 'read_from_json', 'abs_path', 'get_dir', 'remove_files',
    'make_relative', 'get_files_from_path', 'ROOT_DIR'
]

import re
import os
from os.path import dirname, abspath, join, exists
import json
from struct import pack

_CURR_DIR = dirname(abspath(__file__))
ROOT_DIR = join(_CURR_DIR, '../../')


def safe_dir(path: str) -> str:
    """Make sure directory exists, otherwise create it.

    Args:
        path (str): path to the directory

    Returns:
        str: path to the directory
    """

    if not exists(path):
        os.makedirs(path)

    return path


def get_filename_from_path(path):
    """Get name of a file given the absolute or
    relative path

    Arguments:
        path {str} -- path to the file

    Returns:
        str -- file name without format
    """

    assert isinstance(path, str)
    file_with_format = os.path.split(path)[-1]
    file_format = re.findall(r'\.[a-zA-Z]+', file_with_format)[-1]
    file_name = file_with_format.replace(file_format, '')

    return file_name, file_format


def file_exists(path):
    """Check if file exists

    Arguments:
        path {str} -- path to the file

    Returns:
        bool -- True if file exists
    """

    assert isinstance(path, str)
    return exists(path)


def write_json(path, data):
    """Save data into a json file

    Arguments:
        path {str} -- path where to save the file
        data {serializable} -- data to be stored
    """

    assert isinstance(path, str)
    with open(path, 'w') as out_file:
        json.dump(data, out_file, indent=2)


def read_from_json(path):
    """Read data from json file

    Arguments:
        path {str} -- path to json file

    Raises:
        IOError -- File not found

    Returns:
        dict -- dictionary containing data
    """

    assert isinstance(path, str)
    if '.json' not in path:
        raise IOError('Path does not point to a json file')

    with open(path, 'r') as in_file:
        data = json.load(in_file)

    return data


def write_numpy_to_bin_file(path, data, dtype='i', mode='wb'):
    """"Write out numpy array of floats to binary file.

    Arguments:
        path {str}      --  path to binary file
        data {ndarray}  --  flattened numpy array
        dtype {str}     --  data type: float:'f', double: 'd', int: 'i', long: 'l'.
        mode {str}      --  write mode: 'wb' or 'ab'.
    """
    assert isinstance(path, str)
    with open(path, mode) as out_file:
        length = len(data)
        out_file.write(pack('i', length) + pack(dtype * length, *data))


def abs_path(path):
    """Get absolute path of a relative one

    Arguments:
        path {str} -- relative path

    Raises:
        NameError -- String is empty

    Returns:
        str -- absolute path
    """

    assert isinstance(path, str)
    if path:
        return os.path.expanduser(path)

    raise NameError('Path is empty...')


def get_dir(path):
    """Get directory name from absolute or
    relative path

    Arguments:
        path {str} -- path to directory

    Returns:
        str -- directory name
    """

    assert isinstance(path, str)

    try:
        name = path.split('/')[-1]
    except IndexError:
        return path

    return name


def remove_files(paths):
    """Delete files

    Arguments:
        paths {list} -- list of paths
    """

    assert isinstance(paths, list)
    for path in paths:
        os.remove(path)


def make_relative(path, root_path):
    """Make path relative with respect to a
    root directory

    Arguments:
        path {str} -- current path
        root_path {str} -- root directory path

    Returns:
        str -- relative path
    """

    r_path = path.replace(root_path, '')
    if r_path:
        if r_path[0] == '/':
            r_path = r_path[1:]

    return r_path


def get_files_from_path(path: str, extension: str = None) -> list:
    """Get files from path with extension

    Args:
        path (str): directory path containing files
        extension (str, optional): extension. Defaults to None.

    Raises:
        RuntimeError: Folder does not exist

    Returns:
        list: list of file paths
    """

    if not os.path.exists(path):
        raise RuntimeError('{} folder does not exist'.format(path))

    assert os.path.isdir(path), "Not a folder"

    files = os.listdir(path)
    if not extension:
        return files

    filtered = []
    for f in files:
        if f.endswith('.{}'.format(extension)):
            filtered.append(os.path.join(path, f))

    return filtered
