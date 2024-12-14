# Copyright Epic Games, Inc. All Rights Reserved.
# use python to run this and generate the .ico, requires pip install pillow
from PIL import Image

def MakeIcon(folder, name, output):
    sizes = [256, 128, 48, 32, 24, 16]
    images = []
    for size in sizes:
        print(f"Loading {folder}/icon_{size}x{size}.png")
        image = Image.open(f"{folder}/icon_{size}x{size}.png")
        images.append(image)
    print(f"Creating {output}")
    images[0].save(f"{output}", append_images=images)

MakeIcon("SubmitTool", "SubmitTool", "SubmitTool.ico")
