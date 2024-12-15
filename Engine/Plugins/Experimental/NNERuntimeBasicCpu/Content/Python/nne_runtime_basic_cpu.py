# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

"""
This file contains functions that can be used to load and save model files in the format used
by UE::NNE::RuntimeBasic::FModel. The basic way this works is that a simple dictionary object is 
passed to `nne_runtime_basic_cpu.serialization_save_model_to_file` containing a description of the 
model structure and its weights. For example, a simple MLP might look as follows where 
`weights_layer_1` and `biases_layer_1` etc contain the layer weights as numpy float32 arrays.
    
    nne_runtime_basic_cpu.serialization_save_model_to_file(f, {
        'Type': 'Sequence',
        'Layers': [
            {
                'Type': 'Linear',
                'Weights': weights_layer_1,
                'Biases': biases_layer_1,
            },
            {
                'Type': 'ReLU',
                'Size': len(biases_layer_1),
            },
            {
                'Type': 'Linear',
                'Weights': weights_layer_2,
                'Biases': biases_layer_2,
            },
            {
                'Type': 'ReLU',
                'Size': len(biases_layer_2),
            },
            {
                'Type': 'Linear',
                'Weights': weights_layer_3,
                'Biases': biases_layer_3,
            },
        ]
    })

The function `nne_runtime_basic_cpu.serialization_load_model_from_file` does the opposite and will 
produce the same dictionary structure by reading a file. You can then use this to update the weights 
of your model.

For full description of the layers avaliable and what parameters they require consider this file
as the documentation.
"""

import struct
import numpy as np
from collections import OrderedDict

# Constants - these should match what is in  NNERuntimeBasicCpuModel.cpp

magic_number = 0x0BA51C01
version_number = 1


# Layer Ids - this should match what is in  NNERuntimeBasicCpuModel.cpp

layer_id_map = {
    "Invalid": 0,
    "Sequence": 1,
    "Normalize": 2,
    "Denormalize": 3,
    "Linear": 4,
    "CompressedLinear": 5,
    "MultiLinear": 6,
    "ReLU": 7,
    "ELU": 8,
    "TanH": 9,
    "PReLU": 10,
    "MemoryCell": 11,
    "Copy": 12,
    "Concat": 13,
    "Array": 14,
    "AggregateSet": 15,
    "AggregateOrExclusive": 16,
    "AggregateOrInclusive": 17,
    "Clamp": 18,
    "SparseMixtureOfExperts": 19,
    "GELU": 20,
    "LayerNorm": 21,
    "LipschiztLinear": 22,
}

layer_id_map_inv = {v: k for k, v in layer_id_map.items()}


# Weight Compression and Decompression for the CompressedLinear layer


def compress_weights(weights, eps=1e-8):
    maxs = weights.max(axis=-1)
    mins = weights.min(axis=-1)
    ranges = np.maximum(maxs - mins, eps)

    compressed = np.round(
        65535.0 * np.clip((weights - mins[..., None]) / ranges[..., None], 0.0, 1.0)
    ).astype(np.uint16)
    offsets = mins
    scales = ranges / 65535.0

    return compressed, offsets, scales


def decompress_weights(compressed, offsets, scales):
    return compressed.astype(np.float32) * scales[..., None] + offsets[..., None]


# Alignment


def serialization_align(offset, alignment):
    return ((offset + alignment - 1) // alignment) * alignment


# Compute the size required for serialization


def serialization_size_uint32(offset):
    return serialization_align(offset, 4) + 4


def serialization_size_float(offset):
    return serialization_align(offset, 4) + 4


def serialization_size_float_array(offset, x):
    return serialization_align(offset, 64) + (np.prod(x.shape) * 4)


def serialization_size_uint16_array(offset, x):
    return serialization_align(offset, 64) + (np.prod(x.shape) * 2)


def serialization_size_uint32_array(offset, x):
    return serialization_align(offset, 64) + (np.prod(x.shape) * 4)


def serialization_size_cstr(offset, x):
    return serialization_size_uint32(offset) + len(bytes(x)) + 1


def serialization_size_layer(offset, layer):

    layer_type = layer["Type"]
    offset = serialization_size_uint32(offset)
    
    if layer_type == "Invalid":
        raise Exception("Invalid Layer")

    elif layer_type == "Sequence":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer_list(offset, layer["Layers"])

    elif layer_type in ["Normalize", "Denormalize"]:
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Mean"])
        offset = serialization_size_float_array(offset, layer["Std"])

    elif layer_type == "Linear":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Biases"])
        offset = serialization_size_float_array(offset, layer["Weights"])

    elif layer_type == "CompressedLinear":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["WeightOffsets"])
        offset = serialization_size_float_array(offset, layer["WeightScales"])
        offset = serialization_size_float_array(offset, layer["Biases"])
        offset = serialization_size_uint16_array(offset, layer["Weights"])

    elif layer_type == "MultiLinear":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Biases"])
        offset = serialization_size_float_array(offset, layer["Weights"])

    elif layer_type in ["ReLU", "ELU", "TanH", "GELU"]:
        offset = serialization_size_uint32(offset)

    elif layer_type == "PReLU":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Alpha"])

    elif layer_type == "MemoryCell":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer(offset, layer["RememberLayer"])
        offset = serialization_size_layer(offset, layer["PassthroughLayer"])
        offset = serialization_size_layer(offset, layer["MemoryUpdateLayer"])
        offset = serialization_size_layer(offset, layer["OutputInputUpdateLayer"])
        offset = serialization_size_layer(offset, layer["OutputMemoryUpdateLayer"])

    elif layer_type == "Copy":
        offset = serialization_size_uint32(offset)
    
    elif layer_type == "Concat":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32_array(offset, layer["InputSizes"])
        offset = serialization_size_uint32_array(offset, layer["OutputSizes"])
        offset = serialization_size_layer_list(offset, layer["Layers"])

    elif layer_type == "Array":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer(offset, layer["SubLayer"])
    
    elif layer_type == "AggregateSet":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer(offset, layer["SubLayer"])
        offset = serialization_size_layer(offset, layer["QueryLayer"])
        offset = serialization_size_layer(offset, layer["KeyLayer"])
        offset = serialization_size_layer(offset, layer["ValueLayer"])

    elif layer_type == "AggregateOrExclusive":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32_array(offset, layer["SubLayerInputSizes"])
        offset = serialization_size_uint32_array(offset, layer["SubLayerOutputSizes"])
        offset = serialization_size_layer_list(offset, layer["SubLayers"])
        offset = serialization_size_layer_list(offset, layer["Encoders"])

    elif layer_type == "AggregateOrInclusive":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32_array(offset, layer["SubLayerInputSizes"])
        offset = serialization_size_uint32_array(offset, layer["SubLayerOutputSizes"])
        offset = serialization_size_layer_list(offset, layer["SubLayers"])
        offset = serialization_size_layer_list(offset, layer["QueryLayers"])
        offset = serialization_size_layer_list(offset, layer["KeyLayers"])
        offset = serialization_size_layer_list(offset, layer["ValueLayers"])
    
    elif layer_type == "Clamp":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["MinValues"])
        offset = serialization_size_float_array(offset, layer["MaxValues"])
    
    elif layer_type == "SparseMixtureOfExperts":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer(offset, layer["GatingLayer"])
        offset = serialization_size_uint32(offset)
        offset = serialization_size_layer_list(offset, layer["SubLayers"])

    elif layer_type == "LayerNorm":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Offset"])
        offset = serialization_size_float_array(offset, layer["Scale"])
        offset = serialization_size_float(offset)

    elif layer_type == "LipschiztLinear":
        offset = serialization_size_uint32(offset)
        offset = serialization_size_uint32(offset)
        offset = serialization_size_float_array(offset, layer["Biases"])
        offset = serialization_size_float_array(offset, layer["Weights"])

    else:
        raise Exception('Unknown Layer Type "%s"' % layer_type)

    return offset


def serialization_size_layer_list(offset, layers):
    for layer in layers:
        offset = serialization_size_layer(offset, layer)
    return offset
    

def serialization_size_model(offset, layer):

    assert offset % 64 == 0
    offset = serialization_size_uint32(offset)  # Magic Number
    offset = serialization_size_uint32(offset)  # Version Number
    offset = serialization_size_layer(offset, layer)

    return offset


# Saving models


def serialization_save_uint32(offset, x, buffer):
    offset = serialization_align(offset, 4)
    buffer[offset : offset + 4] = np.frombuffer(struct.pack("I", x), np.uint8)
    return offset + 4


def serialization_save_float(offset, x, buffer):
    offset = serialization_align(offset, 4)
    buffer[offset : offset + 4] = np.frombuffer(struct.pack("f", x), np.uint8)
    return offset + 4


def serialization_save_float_array(offset, x, buffer):
    offset = serialization_align(offset, 64)
    data = np.frombuffer(np.asarray(x, dtype=np.float32).tobytes(), np.uint8)
    buffer[offset : offset + len(data)] = data
    return offset + len(data)


def serialization_save_uint16_array(offset, x, buffer):
    offset = serialization_align(offset, 64)
    data = np.frombuffer(np.asarray(x, dtype=np.uint16).tobytes(), np.uint8)
    buffer[offset : offset + len(data)] = data
    return offset + len(data)


def serialization_save_uint32_array(offset, x, buffer):
    offset = serialization_align(offset, 64)
    data = np.frombuffer(np.asarray(x, dtype=np.uint32).tobytes(), np.uint8)
    buffer[offset : offset + len(data)] = data
    return offset + len(data)


def serialization_save_cstr(offset, x, buffer):
    offset = serialization_save_uint32(offset, len(bytes(x)) + 1, buffer)
    data = np.hstack([np.frombuffer(bytes(x), np.uint8), 0])
    buffer[offset : offset + len(data)] = data
    return offset + len(data)


def serialization_save_layer(offset, layer, buffer):

    layer_type = layer["Type"]
    offset = serialization_save_uint32(offset, layer_id_map[layer_type], buffer)
    
    if layer_type == "Invalid":
        raise Exception("Invalid Layer")

    elif layer_type == "Sequence":
        offset = serialization_save_uint32(offset, len(layer["Layers"]), buffer)
        offset = serialization_save_layer_list(offset, layer["Layers"], buffer)

    elif layer_type == "Normalize":
        offset = serialization_save_uint32(offset, layer["Mean"].shape[0], buffer)
        offset = serialization_save_float_array(offset, layer["Mean"], buffer)
        offset = serialization_save_float_array(offset, layer["Std"], buffer)

    elif layer_type == "Denormalize":
        offset = serialization_save_uint32(offset, layer["Mean"].shape[0], buffer)
        offset = serialization_save_float_array(offset, layer["Mean"], buffer)
        offset = serialization_save_float_array(offset, layer["Std"], buffer)

    elif layer_type == "Linear":
        offset = serialization_save_uint32(offset, layer["Weights"].shape[0], buffer)
        offset = serialization_save_uint32(offset, layer["Weights"].shape[1], buffer)
        offset = serialization_save_float_array(offset, layer["Biases"], buffer)
        offset = serialization_save_float_array(offset, layer["Weights"], buffer)

    elif layer_type == "CompressedLinear":
        offset = serialization_save_uint32(offset, layer["Weights"].shape[0], buffer)
        offset = serialization_save_uint32(offset, layer["Weights"].shape[1], buffer)
        offset = serialization_save_float_array(offset, layer["WeightOffsets"], buffer)
        offset = serialization_save_float_array(offset, layer["WeightScales"], buffer)
        offset = serialization_save_float_array(offset, layer["Biases"], buffer)
        offset = serialization_save_uint16_array(offset, layer["Weights"], buffer)

    elif layer_type == "MultiLinear":
        offset = serialization_save_uint32(offset, layer["Weights"].shape[0], buffer)
        offset = serialization_save_uint32(offset, layer["Weights"].shape[1], buffer)
        offset = serialization_save_uint32(offset, layer["Weights"].shape[2], buffer)
        offset = serialization_save_float_array(offset, layer["Biases"], buffer)
        offset = serialization_save_float_array(offset, layer["Weights"], buffer)

    elif layer_type in ["ReLU", "ELU", "TanH", "GELU"]:
        offset = serialization_save_uint32(offset, layer["Size"], buffer)

    elif layer_type == "PReLU":
        offset = serialization_save_uint32(offset, layer["Alpha"].shape[0], buffer)
        offset = serialization_save_float_array(offset, layer["Alpha"], buffer)

    elif layer_type == "MemoryCell":
        offset = serialization_save_uint32(offset, layer["InputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["OutputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["MemorySize"], buffer)
        offset = serialization_save_layer(offset, layer["RememberLayer"], buffer)
        offset = serialization_save_layer(offset, layer["PassthroughLayer"], buffer)
        offset = serialization_save_layer(offset, layer["MemoryUpdateLayer"], buffer)
        offset = serialization_save_layer(offset, layer["OutputInputUpdateLayer"], buffer)
        offset = serialization_save_layer(offset, layer["OutputMemoryUpdateLayer"], buffer)
    
    elif layer_type == "Copy":
        offset = serialization_save_uint32(offset, layer["Size"], buffer)
    
    elif layer_type == "Concat":
        offset = serialization_save_uint32(offset, len(layer["Layers"]), buffer)
        offset = serialization_save_uint32_array(offset, layer["InputSizes"], buffer)
        offset = serialization_save_uint32_array(offset, layer["OutputSizes"], buffer)
        offset = serialization_save_layer_list(offset, layer["Layers"], buffer)

    elif layer_type == "Array":
        offset = serialization_save_uint32(offset, layer["ElementNum"], buffer)
        offset = serialization_save_uint32(offset, layer["ElementInputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["ElementOutputSize"], buffer)
        offset = serialization_save_layer(offset, layer["SubLayer"], buffer)
    
    elif layer_type == "AggregateSet":
        offset = serialization_save_uint32(offset, layer["MaxElementNum"], buffer)
        offset = serialization_save_uint32(offset, layer["ElementInputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["ElementOutputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["OutputEncodingSize"], buffer)
        offset = serialization_save_uint32(offset, layer["AttentionEncodingSize"], buffer)
        offset = serialization_save_uint32(offset, layer["AttentionHeadNum"], buffer)
        offset = serialization_save_layer(offset, layer["SubLayer"], buffer)
        offset = serialization_save_layer(offset, layer["QueryLayer"], buffer)
        offset = serialization_save_layer(offset, layer["KeyLayer"], buffer)
        offset = serialization_save_layer(offset, layer["ValueLayer"], buffer)

    elif layer_type == "AggregateOrExclusive":
        offset = serialization_save_uint32(offset, len(layer["SubLayers"]), buffer)
        offset = serialization_save_uint32(offset, layer["OutputEncodingSize"], buffer)
        offset = serialization_save_uint32_array(offset, layer["SubLayerInputSizes"], buffer)
        offset = serialization_save_uint32_array(offset, layer["SubLayerOutputSizes"], buffer)
        offset = serialization_save_layer_list(offset, layer["SubLayers"], buffer)
        offset = serialization_save_layer_list(offset, layer["Encoders"], buffer)

    elif layer_type == "AggregateOrInclusive":
        offset = serialization_save_uint32(offset, len(layer["SubLayers"]), buffer)
        offset = serialization_save_uint32(offset, layer["OutputEncodingSize"], buffer)
        offset = serialization_save_uint32(offset, layer["AttentionEncodingSize"], buffer)
        offset = serialization_save_uint32(offset, layer["AttentionHeadNum"], buffer)
        offset = serialization_save_uint32_array(offset, layer["SubLayerInputSizes"], buffer)
        offset = serialization_save_uint32_array(offset, layer["SubLayerOutputSizes"], buffer)
        offset = serialization_save_layer_list(offset, layer["SubLayers"], buffer)
        offset = serialization_save_layer_list(offset, layer["QueryLayers"], buffer)
        offset = serialization_save_layer_list(offset, layer["KeyLayers"], buffer)
        offset = serialization_save_layer_list(offset, layer["ValueLayers"], buffer)
    
    elif layer_type == "Clamp":
        offset = serialization_save_uint32(offset, layer["MinValues"].shape[0], buffer)
        offset = serialization_save_float_array(offset, layer["MinValues"], buffer)
        offset = serialization_save_float_array(offset, layer["MaxValues"], buffer)

    elif layer_type == "SparseMixtureOfExperts":
        offset = serialization_save_uint32(offset, layer["InputSize"], buffer)
        offset = serialization_save_uint32(offset, layer["OutputSize"], buffer)
        offset = serialization_save_layer(offset, layer["GatingLayer"], buffer)
        offset = serialization_save_uint32(offset, len(layer["SubLayers"]), buffer)
        offset = serialization_save_layer_list(offset, layer["SubLayers"], buffer)
    
    elif layer_type == "LayerNorm":
        offset = serialization_save_uint32(offset, layer["Offset"].shape[0], buffer)
        offset = serialization_save_float_array(offset, layer["Offset"], buffer)
        offset = serialization_save_float_array(offset, layer["Scale"], buffer)
        offset = serialization_save_float(offset, layer["Epsilon"], buffer)
    
    elif layer_type == "LipschiztLinear":
        offset = serialization_save_uint32(offset, layer["Weights"].shape[0], buffer)
        offset = serialization_save_uint32(offset, layer["Weights"].shape[1], buffer)
        offset = serialization_save_float_array(offset, layer["Biases"], buffer)
        offset = serialization_save_float_array(offset, layer["Weights"], buffer)
    
    else:
        raise Exception('Unknown Layer Type "%s"' % layer_type)
    
    return offset


def serialization_save_layer_list(offset, layers, buffer):
    for layer in layers:
        offset = serialization_save_layer(offset, layer, buffer)
    return offset



def serialization_save_model(offset, layer, buffer):

    assert offset % 64 == 0
    offset = serialization_save_uint32(offset, magic_number, buffer)  # Magic Number
    offset = serialization_save_uint32(offset, version_number, buffer)  # Version Number
    offset = serialization_save_layer(offset, layer, buffer)

    return offset


def serialization_save_model_to_file(f, layer):
    buffer = np.zeros([serialization_size_model(0, layer)], dtype=np.uint8)
    offset = serialization_save_model(0, layer, buffer)
    assert offset == len(buffer)
    f.write(buffer.tobytes())


# Loading models


def serialization_load_uint32(offset, buffer):
    offset = serialization_align(offset, 4)
    data = struct.unpack("I", buffer[offset : offset + 4].tobytes())[0]
    return offset + 4, data


def serialization_load_float(offset, buffer):
    offset = serialization_align(offset, 4)
    data = struct.unpack("f", buffer[offset : offset + 4].tobytes())[0]
    return offset + 4, data


def serialization_load_float_array(offset, buffer, shape):
    offset = serialization_align(offset, 64)
    data = (
        np.frombuffer(
            buffer[offset : offset + np.prod(shape) * 4].tobytes(), dtype=np.float32
        )
        .reshape(shape)
        .copy()
    )
    return offset + np.prod(shape) * 4, data


def serialization_load_uint16_array(offset, buffer, shape):
    offset = serialization_align(offset, 64)
    data = (
        np.frombuffer(
            buffer[offset : offset + np.prod(shape) * 2].tobytes(), dtype=np.uint16
        )
        .reshape(shape)
        .copy()
    )
    return offset + np.prod(shape) * 2, data


def serialization_load_uint32_array(offset, buffer, shape):
    offset = serialization_align(offset, 64)
    data = (
        np.frombuffer(
            buffer[offset : offset + np.prod(shape) * 4].tobytes(), dtype=np.uint32
        )
        .reshape(shape)
        .copy()
    )
    return offset + np.prod(shape) * 4, data


def serialization_load_layer(offset, buffer):

    offset, layer_type_id = serialization_load_uint32(offset, buffer)

    layer_type = layer_id_map_inv[layer_type_id]
    
    if layer_type == "Invalid":
        raise Exception("Invalid Layer")

    elif layer_type == "Sequence":
        offset, layer_num = serialization_load_uint32(offset, buffer)
        layers = []
        for i in range(layer_num):
            offset, l = serialization_load_layer(offset, buffer)
            layers.append(l)
        result = {"Type": layer_type, "Layers": layers,}

    elif layer_type == "Normalize":
        offset, size = serialization_load_uint32(offset, buffer)
        offset, mean = serialization_load_float_array(offset, buffer, [size])
        offset, std = serialization_load_float_array(offset, buffer, [size])
        result = {"Type": layer_type, "Mean": mean, "Std": std,}

    elif layer_type == "Denormalize":
        offset, size = serialization_load_uint32(offset, buffer)
        offset, mean = serialization_load_float_array(offset, buffer, [size])
        offset, std = serialization_load_float_array(offset, buffer, [size])
        result = {"Type": layer_type, "Mean": mean, "Std": std,}

    elif layer_type == "Linear":
        offset, rows = serialization_load_uint32(offset, buffer)
        offset, cols = serialization_load_uint32(offset, buffer)
        offset, biases = serialization_load_float_array(offset, buffer, [cols])
        offset, weights = serialization_load_float_array(offset, buffer, [rows, cols])
        result = {"Type": layer_type, "Weights": weights, "Biases": biases,}

    elif layer_type == "CompressedLinear":
        offset, rows = serialization_load_uint32(offset, buffer)
        offset, cols = serialization_load_uint32(offset, buffer)
        offset, weight_offsets = serialization_load_float_array(offset, buffer, [rows])
        offset, weight_scales = serialization_load_float_array(offset, buffer, [rows])
        offset, biases = serialization_load_float_array(offset, buffer, [cols])
        offset, weights = serialization_load_uint16_array(offset, buffer, [rows, cols])
        result = {
            "Type": layer_type,
            "Weights": weights,
            "WeightOffsets": weight_offsets,
            "WeightScales": weight_scales,
            "Biases": biases,
        }

    elif layer_type == "MultiLinear":
        offset, blocks = serialization_load_uint32(offset, buffer)
        offset, rows = serialization_load_uint32(offset, buffer)
        offset, cols = serialization_load_uint32(offset, buffer)
        offset, biases = serialization_load_float_array(offset, buffer, [blocks, cols])
        offset, weights = serialization_load_float_array(
            offset, buffer, [blocks, rows, cols]
        )

        result = {"Type": layer_type, "Weights": weights, "Biases": biases,}

    elif layer_type in ["ReLU", "TanH", "ELU", "GELU"]:
        offset, size = serialization_load_uint32(offset, buffer)
        result = {"Type": layer_type, "Size": size}

    elif layer_type == "PReLU":
        offset, size = serialization_load_uint32(offset, buffer)
        offset, alpha = serialization_load_float_array(offset, buffer, [size])
        result = {"Type": layer_type, "Alpha": alpha,}

    elif layer_type == "MemoryCell":
        offset, input_size = serialization_load_uint32(offset, buffer)
        offset, output_size = serialization_load_uint32(offset, buffer)
        offset, memory_size = serialization_load_uint32(offset, buffer)
        offset, remember_layer = serialization_load_layer(offset, buffer)
        offset, passthrough_layer = serialization_load_layer(offset, buffer)
        offset, memory_update_layer = serialization_load_layer(offset, buffer)
        offset, output_input_layer = serialization_load_layer(offset, buffer)
        offset, output_memory_layer = serialization_load_layer(offset, buffer)
        result = {
            "Type": layer_type,
            "InputSize": input_size,
            "OutputSize": output_size,
            "MemorySize": memory_size,
            "RememberLayer": remember_layer,
            "PassthroughLayer": passthrough_layer,
            "MemoryUpdateLayer": memory_update_layer,
            "OutputInputUpdateLayer": output_input_layer,
            "OutputMemoryUpdateLayer": output_memory_layer,
        }

    elif layer_type == "Copy":
        offset, size = serialization_load_uint32(offset, buffer)
        result = {"Type": layer_type, "Size": size, }
    
    elif layer_type == "Concat":
        offset, layer_num = serialization_load_uint32(offset, buffer)
        offset, input_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, output_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, layers = serialization_load_layer_list(layer_num, offset, buffer)
        result = {"Type": layer_type, "InputSizes": input_sizes, "OutputSizes": output_sizes, "Layers": layers,}
        
    elif layer_type == "Array":
        offset, element_num = serialization_load_uint32(offset, buffer)
        offset, element_input_size = serialization_load_uint32(offset, buffer)
        offset, element_output_size = serialization_load_uint32(offset, buffer)
        offset, sublayer = serialization_load_layer(offset, buffer)
        result = {
            "Type": layer_type, 
            "ElementNum": element_num, 
            "ElementInputSize": element_input_size, 
            "ElementOutputSize": element_output_size,
            "SubLayer": sublayer, 
        }

    elif layer_type == "AggregateSet":
        offset, max_element_num = serialization_load_uint32(offset, buffer)
        offset, element_input_size = serialization_load_uint32(offset, buffer)
        offset, element_output_size = serialization_load_uint32(offset, buffer)
        offset, output_encoding_size = serialization_load_uint32(offset, buffer)
        offset, attention_encoding_size = serialization_load_uint32(offset, buffer)
        offset, attention_head_num = serialization_load_uint32(offset, buffer)
        offset, sublayer = serialization_load_layer(offset, buffer)
        offset, query_layer = serialization_load_layer(offset, buffer)
        offset, key_layer = serialization_load_layer(offset, buffer)
        offset, value_layer = serialization_load_layer(offset, buffer)
        result = {
            "Type": layer_type,
            "MaxElementNum": max_element_num,
            "ElementInputSize": element_input_size,
            "ElementOutputSize": element_output_size,
            "OutputEncodingSize": output_encoding_size,
            "AttentionEncodingSize": attention_encoding_size,
            "AttentionHeadNum": attention_head_num,
            "SubLayer": sublayer,
            "QueryLayer": query_layer,
            "KeyLayer": key_layer,
            "ValueLayer": value_layer,
        }

    elif layer_type == "AggregateOrExclusive":
        offset, layer_num = serialization_load_uint32(offset, buffer)
        offset, output_encoding_size = serialization_load_uint32(offset, buffer)
        offset, sublayer_input_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, sublayer_output_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, sub_layers = serialization_load_layer_list(layer_num, offset, buffer)
        offset, encoders = serialization_load_layer_list(layer_num, offset, buffer)
        result = {
            "Type": layer_type,
            "OutputEncodingSize": output_encoding_size,
            "SubLayerInputSizes": sublayer_input_sizes,
            "SubLayerOutputSizes": sublayer_output_sizes,
            "SubLayers": sub_layers,
            "Encoders": encoders,
        }
    
    elif layer_type == "AggregateOrInclusive":
        offset, layer_num = serialization_load_uint32(offset, buffer)
        offset, output_encoding_size = serialization_load_uint32(offset, buffer)
        offset, attention_encoding_size = serialization_load_uint32(offset, buffer)
        offset, attention_head_num = serialization_load_uint32(offset, buffer)
        offset, sublayer_input_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, sublayer_output_sizes = serialization_load_uint32_array(offset, buffer, [layer_num])
        offset, sub_layers = serialization_load_layer_list(layer_num, offset, buffer)
        offset, query_layers = serialization_load_layer_list(layer_num, offset, buffer)
        offset, key_layers = serialization_load_layer_list(layer_num, offset, buffer)
        offset, value_layers = serialization_load_layer_list(layer_num, offset, buffer)
        result = {
            "Type": layer_type,
            "OutputEncodingSize": output_encoding_size,
            "AttentionEncodingSize": attention_encoding_size,
            "AttentionHeadNum": attention_head_num,
            "SubLayerInputSizes": sublayer_input_sizes,
            "SubLayerOutputSizes": sublayer_output_sizes,
            "SubLayers": sub_layers,
            "QueryLayers": query_layers,
            "KeyLayers": key_layers,
            "ValueLayers": value_layers,
        }

    elif layer_type == "Clamp":
        offset, size = serialization_load_uint32(offset, buffer)
        offset, min_values = serialization_load_float_array(offset, buffer, [size])
        offset, max_values = serialization_load_float_array(offset, buffer, [size])
        result = {"Type": layer_type, "MinValues": min_values, "MaxValues": max_values,}

    elif layer_type == "SparseMixtureOfExperts":
        offset, input_size = serialization_load_uint32(offset, buffer)
        offset, output_size = serialization_load_uint32(offset, buffer)
        offset, gating_layer = serialization_load_layer(offset, buffer)
        offset, layer_num = serialization_load_uint32(offset, buffer)
        offset, sub_layers = serialization_load_layer_list(layer_num, offset, buffer)
        result = {
            "Type": layer_type,
            "InputSize": input_size,
            "OutputSize": output_size,
            "GatingLayer": gating_layer,
            "SubLayers": sub_layers,
        }

    elif layer_type == "LayerNorm":
        offset, size = serialization_load_uint32(offset, buffer)
        offset, off = serialization_load_float_array(offset, buffer, [size])
        offset, scale = serialization_load_float_array(offset, buffer, [size])
        offset, epsilon = serialization_load_float(offset, buffer)
        result = {"Type": layer_type, "Offset": off, "Scale": scale, "Epsilon": epsilon}

    elif layer_type == "LipschiztLinear":
        offset, rows = serialization_load_uint32(offset, buffer)
        offset, cols = serialization_load_uint32(offset, buffer)
        offset, biases = serialization_load_float_array(offset, buffer, [cols])
        offset, weights = serialization_load_float_array(offset, buffer, [rows, cols])
        result = {"Type": layer_type, "Weights": weights, "Biases": biases,}

    else:
        raise Exception('Unknown Layer Type "%s"' % layer_type)

    return offset, result

def serialization_load_layer_list(size, offset, buffer):
    layers = []
    for i in range(size):
        offset, layer = serialization_load_layer(offset, buffer)
        layers.append(layer)
    return offset, layers


def serialization_load_model(offset, buffer):

    assert offset % 64 == 0
    offset, magic = serialization_load_uint32(offset, buffer)
    assert magic == magic_number

    offset, version = serialization_load_uint32(offset, buffer)
    assert version == version_number

    return serialization_load_layer(offset, buffer)


def serialization_load_model_from_file(f):
    buffer = f.read()
    offset, layer = serialization_load_model(0, buffer)
    assert len(buffer) == offset
    return layer


# Optimization


"""
Provided here are some extremely basic functions for optimizing networks described by the 
dictionary structure. Right now this includes merging normalization and denormalization
layers into linear layers.
"""


def optimize_sequence(layer):
    """ Applies optimizations to sub-layers in a sequence """

    if layer["Type"] == "Sequence":

        print("Applying to Subsequence...")

        layers = layer["Layers"]

        for i in range(len(layers)):
            found, optimized_layer = optimize(layers[i])

            if found:
                return (
                    True,
                    {
                        "Type": "Sequence",
                        "Layers": layers[:i] + [optimized_layer] + layers[i + 1 :],
                    },
                )

    return False, layer


def optimize_merge_two_consecutive(l0, l1):
    """ Attempts to merge two consecutive layers """

    if l0["Type"] == "Normalize" and l1["Type"] == "Linear":

        return (
            True,
            {
                "Type": "Linear",
                "Weights": l1["Weights"] / l0["Std"][:, None],
                "Biases": l1["Biases"] - (l0["Mean"] / l0["Std"]).dot(l1["Weights"]),
            },
        )

    if l0["Type"] == "Denormalize" and l1["Type"] == "Linear":

        return (
            True,
            {
                "Type": "Linear",
                "Weights": l1["Weights"] * l0["Std"][:, None],
                "Biases": l1["Biases"] + l0["Mean"].dot(l1["Weights"]),
            },
        )

    if l0["Type"] == "Linear" and l1["Type"] == "Normalize":

        return (
            True,
            {
                "Type": "Linear",
                "Weights": l0["Weights"] / l1["Std"],
                "Biases": (l0["Biases"] - l1["Mean"]) / l1["Std"],
            },
        )

    if l0["Type"] == "Linear" and l1["Type"] == "Denormalize":

        return (
            True,
            {
                "Type": "Linear",
                "Weights": l0["Weights"] * l1["Std"],
                "Biases": (l0["Biases"] + l1["Mean"] / l1["Std"]) * l1["Std"],
            },
        )

    return False, (l0, l1)


def optimize_sequence_merge_consecutive(layer):
    """ Attempts to merge consecutive layers in a sequence (e.g. Normalize, Denormalize, Linear) """

    if layer["Type"] == "Sequence":

        layers = layer["Layers"]

        for i in range(1, len(layers)):

            if layers[i - 1]["Type"] in [
                "Normalize",
                "Denormalize",
                "Linear",
            ] and layers[i + 0]["Type"] in ["Normalize", "Denormalize", "Linear"]:

                found, merged = optimize_merge_two_consecutive(
                    layers[i - 1], layers[i + 0]
                )

                if found:
                    return (
                        True,
                        {
                            "Type": "Sequence",
                            "Layers": layers[: i - 1] + [merged] + layers[i + 1 :],
                        },
                    )
                else:
                    continue

    return False, layer


def optimize(layer):

    iterate = True
    any_found = False

    # Loop until no more optimizations found
    while iterate:

        print("Optimization Iteration...")

        iterate = False

        # Search through optimizations to apply
        for optimization in [optimize_sequence, optimize_sequence_merge_consecutive]:

            found, layer = optimization(layer)

            # If any found continue trying to apply optimizations
            if found:
                any_found = True
                iterate = True
                break

    # No more optimizations found
    return any_found, layer

def _softmax_plus_one_masked(z, m, axis):
    z_pos_max = np.maximum(z * m, 0.0).max(axis=axis, keepdims=True)
    return m * (np.exp(z - z_pos_max) / (np.sum(m * np.exp(z - z_pos_max), axis=axis, keepdims=True) + 1))

def _mask_to_count_offset(m):
    m_sum = m.astype(np.int64).sum(axis=-1)
    m_off = np.hstack([0, np.cumsum(m_sum[:-1])])
    return m_sum, m_off

def _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot):
    
    elem_bi = np.zeros([elem_tot], dtype=np.int64)
    elem_ei = np.zeros([elem_tot], dtype=np.int64)

    offset = 0
    for bi, en in enumerate(elem_num):
        elem_bi[offset:offset+en] = bi
        elem_ei[offset:offset+en] = np.arange(en)
        offset += en
    
    assert(offset == elem_tot)
    
    return elem_bi, elem_ei

def _mask_to_indices_exclusive(m):    
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        indices[np.where(m[i] == 1)[0][0]].append(i)
    return [np.asarray(i) for i in indices]
    
def _mask_to_indices_inclusive(m):    
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        for j in np.where(m[i] == 1)[0]:
            indices[j].append(i)
    return [np.asarray(i) for i in indices]
    
def _gating_to_indices_weights(gating):
    val_sort = gating.argsort(axis=1)
    ind0 = val_sort[:,-1]
    ind1 = val_sort[:,-2]
    ind_dim0 = np.arange(len(gating))
    val0 = gating[ind_dim0,ind0]
    val1 = gating[ind_dim0,ind1]
    
    valexp0 = np.exp(val0 - np.maximum(val0, val1))
    valexp1 = np.exp(val1 - np.maximum(val0, val1))
    
    wei0 = valexp0 / (valexp0 + valexp1)
    wei1 = valexp1 / (valexp0 + valexp1)

    indices = [[] for i in range(gating.shape[1])]
    weights = [[] for i in range(gating.shape[1])]
    for i in range(gating.shape[0]):
        indices[ind0[i]].append(i)
        indices[ind1[i]].append(i)
        weights[ind0[i]].append(wei0[i])
        weights[ind1[i]].append(wei1[i])
    indices = [np.asarray(i) for i in indices] 
    weights = [np.asarray(w) for w in weights] 
    
    return indices, weights
    
def _indices_inclusive_accumulate_offsets(ind, elem_off, elem_num):
    
    elem_accum = np.zeros_like(elem_off)
    
    acc_off = [[] for i in range(len(ind))]
    for i in range(len(ind)):
        for j in range(len(ind[i])):
            k = ind[i][j]
            acc_off[i].append(elem_off[k] + elem_accum[k])
            elem_accum[k] += 1
        acc_off[i] = np.array(acc_off[i])
    
    assert np.all(elem_accum == elem_num)

    return acc_off
    
def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x)) 
    
def evaluate(layer, x):
    """Evaluate this layer using numpy. This is useful for verifying that the results of this 
    layer match what you get via pytorch or whatever other system is being used for training
    """

    layer_type = layer["Type"]

    if layer_type == "Invalid":
        raise Exception("Invalid Layer")

    elif layer_type == "Sequence":
        for l in layer["Layers"]:
            x = evaluate(l, x)
        return x

    elif layer_type == "Normalize":
        return (x - layer["Mean"]) / layer["Std"]

    elif layer_type == "Denormalize":
        return (x * layer["Std"]) + layer["Mean"]

    elif layer_type == "Linear":
        return x.dot(layer["Weights"]) + layer["Biases"]

    elif layer_type == "CompressedLinear":
        weights = decompress_weights(
            layer["Weights"], layer["WeightOffsets"], layer["WeightScales"]
        )
        return x.dot(weights) + layer["Biases"]

    elif layer_type == "MultiLinear":
        batchnum = x.shape[0]
        blocknum = layer["Weights"].shape[0]
        inputnum = layer["Weights"].shape[1]
        outputnum = layer["Weights"].shape[2]
        x = x.reshape([batchnum, blocknum, inputnum]).transpose([1, 0, 2])
        x = (x @ layer["Weights"]) + layer["Biases"][:, None, :]
        x = x.transpose([1, 0, 2]).reshape([batchnum, blocknum * outputnum])
        return x

    elif layer_type == "ReLU":
        return np.maximum(x, 0.0)

    elif layer_type == "ELU":
        return np.where(x >= 0.0, x, np.exp(x) - 1.0)

    elif layer_type == "GELU":
        return x * _sigmoid(1.702 * x)

    elif layer_type == "TanH":
        return np.tanh(x)

    elif layer_type == "PReLU":
        return np.where(x >= 0.0, x, layer["Alpha"] * x)

    elif layer_type == "MemoryCell":
        if layer['MemorySize'] > 0:
            m = x[:,-layer['MemorySize']:]
            r = _sigmoid(evaluate(layer['RememberLayer'], x))
            m_new = (1 - r) * m + r * np.tanh(evaluate(layer['MemoryUpdateLayer'], x))
            p = _sigmoid(evaluate(layer['PassthroughLayer'], x))
            y = (1 - p) * np.tanh(evaluate(layer['OutputMemoryUpdateLayer'], m_new)) + p * np.tanh(evaluate(layer['OutputInputUpdateLayer'], x))
            return np.concatenate([y, m_new], axis=-1)
        else:
            p = _sigmoid(evaluate(layer['PassthroughLayer'], x))
            y = p * np.tanh(evaluate(layer['OutputInputUpdateLayer'], x))
            return y

    
    elif layer_type == "Copy":
        return x.copy()
         
    elif layer_type == "Concat":
        outputs = []
        input_offset = 0
        for layer, input_size in zip(layer['Layers'], layer['InputSizes']):
            outputs.append(evaluate(layer, x[:,input_offset:input_offset+input_size]))
            input_offset += input_size
        
        return np.concatenate(outputs, axis=-1)
        
    elif layer_type == "Array":
        batchsize = x.shape[0]
        return evaluate(layer['SubLayer'], x.reshape([batchsize * layer['ElementInputSize'], -1])).reshape(batchsize, -1)
        
    elif layer_type == "AggregateSet":
        
        elem_mask = x[:,layer['MaxElementNum'] * layer['ElementInputSize']:]
        assert elem_mask.shape[1] == layer['MaxElementNum']
        
        elem_num, elem_off = _mask_to_count_offset(elem_mask)
        elem_tot = elem_num.sum()
        elem_bi, elem_ei = _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot)

        xelem = x[:,:layer['MaxElementNum'] * layer['ElementInputSize']].reshape([len(x), layer['MaxElementNum'],  layer['ElementInputSize']])
        
        activ = evaluate(layer['SubLayer'], xelem[elem_bi, elem_ei])
        query = evaluate(layer['QueryLayer'], activ).reshape([elem_tot, layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
        key = evaluate(layer['KeyLayer'], activ).reshape([elem_tot, layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
        value = evaluate(layer['ValueLayer'], activ).reshape([elem_tot, layer['AttentionHeadNum'], layer['OutputEncodingSize']])
        
        attention_mask = np.zeros([len(x), layer['MaxElementNum'], layer['AttentionHeadNum']], dtype=np.float32)
        attention_mask[elem_bi, elem_ei] = 1.0

        attention_padded = np.zeros([len(x), layer['MaxElementNum'], layer['AttentionHeadNum']], dtype=np.float32)
        attention_padded[elem_bi, elem_ei] = (query * key).sum(axis=-1) / np.sqrt(layer['AttentionEncodingSize'])
        attention_padded = _softmax_plus_one_masked(attention_padded, attention_mask, axis=1)
        
        value_padded = np.zeros([len(x), layer['MaxElementNum'], layer['AttentionHeadNum'], layer['OutputEncodingSize']], dtype=np.float32)
        value_padded[elem_bi, elem_ei] = value
        
        output = np.zeros([len(x), layer['AttentionHeadNum'] * layer['OutputEncodingSize'] + 1], dtype=x.dtype)
        output[:,:layer['AttentionHeadNum'] * layer['OutputEncodingSize']] = (value_padded * attention_padded[...,None]).sum(axis=1).reshape([len(x), layer['AttentionHeadNum'] * layer['OutputEncodingSize']])
        output[:,layer['AttentionHeadNum'] * layer['OutputEncodingSize']:] = (elem_num.astype(np.float32) / float(layer['MaxElementNum']))[:,None]
        
        return output
        

    elif layer_type == "AggregateOrExclusive":
        
        sublayer_masks = x[:,np.max(layer['SubLayerInputSizes']):]
        sublayer_indices = _mask_to_indices_exclusive(sublayer_masks)

        output = np.zeros([x.shape[0], layer['OutputEncodingSize'] + len(layer['SubLayers'])])

        for i in range(len(layer['SubLayers'])):
        
            if len(sublayer_indices[i]) == 0: continue
        
            xsub = x[sublayer_indices[i],:layer['SubLayerInputSizes'][i]]
            esub = evaluate(layer['Encoders'][i], evaluate(layer['SubLayers'][i], xsub))
            output[sublayer_indices[i],:layer['OutputEncodingSize']] = esub
            
        output[:,layer['OutputEncodingSize']:] = sublayer_masks
        
        return output


    elif layer_type == "AggregateOrInclusive":
    
        sublayer_input_offsets = np.hstack([0, np.cumsum(layer['SubLayerInputSizes'][:-1])])
        sublayer_masks = x[:,np.sum(layer['SubLayerInputSizes']):]
        sublayer_indices = _mask_to_indices_inclusive(sublayer_masks)
        
        elem_num, elem_off = _mask_to_count_offset(sublayer_masks)
        elem_tot = np.sum(elem_num)
        elem_bi, elem_ei = _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot)

        sublayer_acc_off = _indices_inclusive_accumulate_offsets(sublayer_indices, elem_off, elem_num)

        output = np.zeros([x.shape[0], layer['AttentionHeadNum'] * layer['OutputEncodingSize'] + len(layer['SubLayers'])])
        query = np.zeros([elem_tot, layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
        key = np.zeros([elem_tot, layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
        value = np.zeros([elem_tot, layer['AttentionHeadNum'], layer['OutputEncodingSize']])
        
        for i in range(len(layer['SubLayers'])):
        
            if len(sublayer_indices[i]) == 0: continue
            
            sub_o, sub_n = sublayer_input_offsets[i], layer['SubLayerInputSizes'][i]
            xsub = x[sublayer_indices[i],sub_o:sub_o+sub_n]
            xact = evaluate(layer['SubLayers'][i], xsub)
            
            query[sublayer_acc_off[i]] = evaluate(layer['QueryLayers'][i], xact).reshape([len(sublayer_indices[i]), layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
            key[sublayer_acc_off[i]] = evaluate(layer['KeyLayers'][i], xact).reshape([len(sublayer_indices[i]), layer['AttentionHeadNum'], layer['AttentionEncodingSize']])
            value[sublayer_acc_off[i]] = evaluate(layer['ValueLayers'][i], xact).reshape([len(sublayer_indices[i]), layer['AttentionHeadNum'], layer['OutputEncodingSize']])
            
            
        attention_mask = np.zeros([len(x), len(layer['SubLayers']), layer['AttentionHeadNum']], dtype=np.float32)
        attention_mask[elem_bi, elem_ei] = 1.0

        attention_padded = np.zeros([len(x), len(layer['SubLayers']), layer['AttentionHeadNum']], dtype=np.float32)
        attention_padded[elem_bi, elem_ei] = (query * key).sum(axis=-1) / np.sqrt(layer['AttentionEncodingSize'])
        attention_padded = _softmax_plus_one_masked(attention_padded, attention_mask, axis=1)
        
        value_padded = np.zeros([len(x), len(layer['SubLayers']), layer['AttentionHeadNum'], layer['OutputEncodingSize']], dtype=np.float32)
        value_padded[elem_bi, elem_ei] = value
        
        output = np.zeros([len(x), layer['AttentionHeadNum'] * layer['OutputEncodingSize'] + len(layer['SubLayers'])], dtype=x.dtype)
        output[:,:layer['AttentionHeadNum'] * layer['OutputEncodingSize']] = (value_padded * attention_padded[...,None]).sum(axis=1).reshape([len(x), layer['AttentionHeadNum'] * layer['OutputEncodingSize']])
        output[:,layer['AttentionHeadNum'] * layer['OutputEncodingSize']:] = sublayer_masks
        
        return output
        
    elif layer_type == "Clamp":
        return np.clip(x, layer["MinValues"], layer["MaxValues"])
        
    elif layer_type == "SparseMixtureOfExperts":
        
        gating_values = evaluate(layer['GatingLayer'], x)
       
        sublayer_indices, sublayer_weights = _gating_to_indices_weights(gating_values)
        
        output = np.zeros([len(x), layer['OutputSize']], dtype=np.float32)

        for i, (bis, wei) in enumerate(zip(sublayer_indices, sublayer_weights)):
            
            if len(bis) == 0: continue
            
            output[bis] += wei[:,None] * evaluate(layer['SubLayers'][i], x[bis])
            
        return output
        
    elif layer_type == "LayerNorm":
        x = (x - x.mean(axis=-1)[...,None]) / np.sqrt(x.var(axis=-1)[...,None] + layer['Epsilon'])
        return x * layer['Scale'] + layer['Offset']
        
    elif layer_type == "LipschiztLinear":
        return x.dot(layer["Weights"]) + layer["Biases"]
        
    else:
        raise Exception('Unknown Layer Type "%s"' % layer_type)
