# -*- coding: utf-8 -*-
"""


@author: 

Copyright Epic Games, Inc. All Rights Reserved.

"""

import unreal

if __name__ == "__main__":
    if hasattr(unreal, 'HairCardGenControllerBase'):
        from CardUEInterop import instantiate_card_gen_controller
        instantiate_card_gen_controller()
