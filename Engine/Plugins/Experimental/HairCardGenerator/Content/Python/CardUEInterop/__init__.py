
from .ue_card_gen_controller import HairCardGenController

def instantiate_card_gen_controller():
    global hair_card_controller_inst
    hair_card_controller_inst = ue_card_gen_controller.HairCardGenController()
