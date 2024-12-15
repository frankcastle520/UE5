# -*- coding: utf-8 -*-
"""
Run GUI

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import open3d.visualization.gui as gui
from CardGUI import CardSolverGUI


def main():
    """Main"""
    gui.Application.instance.initialize()
    CardSolverGUI()
    gui.Application.instance.run()


if __name__ == "__main__":
    main()
