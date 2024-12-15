import os
import sys
import subprocess

if __name__ == '__main__':
    utils_root = os.path.dirname(os.path.realpath(sys.argv[0]))
    utils_path = os.path.join(utils_root, 'ue-pipinstall-utils')
    reqs_path = os.path.join(utils_root, 'requirements.txt')
    wheels_path = os.path.join(utils_root, '..', 'Lib', 'wheels')
    subprocess.run(f'python -m pip download -r {reqs_path} -d {wheels_path}')
    subprocess.run(f'python -m pip wheel --no-deps {utils_path} -w {wheels_path}')
