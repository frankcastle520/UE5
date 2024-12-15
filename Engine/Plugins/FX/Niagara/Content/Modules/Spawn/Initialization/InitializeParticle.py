#Reference

# upgrade_context.reset_to_default("Input Name")
# can be used to reset an input to its default value

# inputvar.is_set()
# can tell whether an input is set or hidden. Also can tell whether the input is a value
# that can be copied to the clipboard or not, such as a default graph defined input value

#inputvar.is_local_value():
# Can tell if the input is not a constant value such as a dynamic input

# Lifetime
###################################################
write_lifetime = upgrade_context.get_old_input("Write Lifetime")

if write_lifetime.is_set():
    # is this set at all or is it hidden by something like a static switch
    # showing this as an example, this module doesn't have that option as the 
    # bools are driven by inline edit condition which are only constant
    
    if not write_lifetime.is_local_value():
        # is this NOT a constant value? i.e. is it a dynamic or linked input
        # this is only needed as it's a dynamic bool that controls the write
        print("Write Lifetime Bool not constant, skipping")
    else:
        if write_lifetime.as_bool():
            old_lifetime = upgrade_context.get_old_input("Lifetime")
            upgrade_context.set_enum_input("Lifetime Mode", "Direct Set")
            upgrade_context.set_new_input("Lifetime", old_lifetime)
            print("Setting Lifetime to Stack Value")
        else:
            upgrade_context.set_float_input("Lifetime", 1)
            print("Setting Lifetime to its default of 1")

else:
    print("Lifetime Cannot be transferred as Write Lifetime is Unset")
###################################################

# Position
###################################################
write_pos = upgrade_context.get_old_input("Write Position")

if not write_pos.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write Position Bool not constant, skipping")
else:
    if write_pos.as_bool():
        old_pos = upgrade_context.get_old_input("Position")
        if old_pos.is_set(): # checking is set here lets us see if the input is still its' default value
            upgrade_context.set_enum_input("Position Mode", "Direct Set")
            upgrade_context.set_new_input("Position", old_pos)
            print("Setting Position to Stack Value")
        else:
            upgrade_context.set_enum_input("Position Mode", "Simulation Position")

    else:
        upgrade_context.set_enum_input("Position Mode", "Simulation Position")
        print("Setting Position to its default of Sim Pos")
###################################################

# Mass
###################################################
write_mass = upgrade_context.get_old_input("Write Mass")

if not write_mass.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write Mass Bool not constant, skipping")
else:
    if write_mass.as_bool():
        old_mass = upgrade_context.get_old_input("Mass")
        upgrade_context.set_enum_input("Mass Mode", "Direct Set")
        upgrade_context.set_new_input("Mass", old_mass)
        print("Setting Mass to Stack Value")
    else:
        upgrade_context.set_enum_input("Mass Mode", "Unset / (Mass of 1)")
        print("Setting Mass to its default of 1")
###################################################

# Color
###################################################
write_color = upgrade_context.get_old_input("Write Color")

if not write_color.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write Color Bool not constant, skipping")
else:
    if write_color.as_bool():
        old_color = upgrade_context.get_old_input("Color")
        upgrade_context.set_enum_input("Color Mode", "Direct Set")
        upgrade_context.set_new_input("Color", old_color)
        print("Setting Color to Stack Value")
    else:
        upgrade_context.set_color_input("Color", (1, 1, 1, 1))
        print("Setting Color to its old default of 1,1,1,1")
###################################################

# Sprite Size
###################################################
write_spritesize = upgrade_context.get_old_input("Write SpriteSize")

if not write_spritesize.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write SpriteSize Bool not constant, skipping")
else:
    if write_spritesize.as_bool():
        old_spritesize = upgrade_context.get_old_input("Sprite Size")
        upgrade_context.set_enum_input("Sprite Size Mode", "Non-Uniform")
        upgrade_context.set_new_input("Sprite Size", old_spritesize)
        print("Setting Sprite Size to Stack Value")
    else:
        upgrade_context.set_enum_input("Sprite Size Mode", "Non-Uniform")
        upgrade_context.set_vec2_input("Sprite Size", [10,10])
        print("Setting Sprite Size to its old default of 10,10")
###################################################

# Sprite Rotation
###################################################
write_spriterot = upgrade_context.get_old_input("Write SpriteRotation")

if not write_spriterot.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write SpriteRotation Bool not constant, skipping")
else:
    if write_spriterot.as_bool():
        old_spriterot = upgrade_context.get_old_input("Sprite Rotation")
        upgrade_context.set_enum_input("Sprite Rotation Mode", "Direct Angle (Degrees)")
        upgrade_context.set_new_input("Sprite Rotation Angle", old_spriterot)
        print("Setting Sprite Rotation to Stack Value")
    else:
        upgrade_context.set_enum_input("Sprite Rotation Mode", "Unset")
        print("Setting Sprite Rotation to Unset / 0")
###################################################

# Mesh Scale
###################################################
write_meshscale = upgrade_context.get_old_input("Write Scale")

if not write_meshscale.is_local_value():
    # is this NOT a constant value? i.e. is it a dynamic or linked input
    print("Write Mesh Scale Bool not constant, skipping")
else:
    if write_meshscale.as_bool():
        old_meshscale = upgrade_context.get_old_input("Mesh Scale")
        upgrade_context.set_enum_input("Mesh Scale Mode", "Non-Uniform")
        upgrade_context.set_new_input("Mesh Scale", old_meshscale)
        print("Setting Mesh Scale to Stack Value")
    else:
        upgrade_context.set_enum_input("Mesh Scale Mode", "Unset")
        print("Setting Mesh Scale to Unset / 1,1,1")
###################################################