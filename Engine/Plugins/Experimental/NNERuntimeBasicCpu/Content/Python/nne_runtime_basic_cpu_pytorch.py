# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

"""
This file contains a basic way to construct PyTorch networks from the binary format used by this 
runtimeusing by calling the functions in `nne_runtime_basic_cpu` and converting the resulting JSON 
to and from PyTorch Modules. To use this just construct an object of type `NeuralNetwork` defined 
in this file:

    network = NeuralNetwork()

This object is a PyTroch module with functions `get_filedata_size`, `save_to_filedata`, and 
`load_from_filedata` which can be used to load or save the binary file format used by 
`nne_runtime_basic_cpu`:

    network.load_from_filedata(file_data)
"""

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import nne_runtime_basic_cpu

# Custom Modules


class ReLU(nn.Module):

    def __init__(self, size):
        super(ReLU, self).__init__()
        self.size = size 
        
    def forward(self, x):
        return torch.relu(x)


class ELU(nn.Module):

    def __init__(self, size):
        super(ELU, self).__init__()
        self.size = size 

    def forward(self, x):
        return F.elu(x)

class GELU(nn.Module):

    def __init__(self, size):
        super(GELU, self).__init__()
        self.size = size 

    def forward(self, x):
        return x * F.sigmoid(1.702 * x)


class TanH(nn.Module):

    def __init__(self, size):
        super(TanH, self).__init__()
        self.size = size 
    
    def forward(self, x):
        return torch.tanh(x)


class Normalize(nn.Module):

    def __init__(self, size, device):
        super(Normalize, self).__init__()
        self.size = int(size)
        self.mean = nn.Parameter(torch.zeros([self.size], device=device))
        self.std = nn.Parameter(torch.ones([self.size], device=device))
    
    def forward(self, x):
        return (x - self.mean) / self.std


class Denormalize(nn.Module):

    def __init__(self, size, device):
        super(Denormalize, self).__init__()
        self.size = int(size)
        self.mean = nn.Parameter(torch.zeros([self.size], device=device))
        self.std = nn.Parameter(torch.ones([self.size], device=device))
    
    def forward(self, x):
        return x * self.std + self.mean


class Linear(nn.Module):

    def __init__(self, rows, cols, device):
        super(Linear, self).__init__()
        self.weight = nn.Parameter(torch.zeros([rows, cols], device=device))
        self.bias = nn.Parameter(torch.zeros([cols], device=device))

    def forward(self, x):
        return torch.matmul(x, self.weight) + self.bias


class CompressedLinear(nn.Module):

    def __init__(self, rows, cols, device):
        super(CompressedLinear, self).__init__()
        self.weight = nn.Parameter(torch.zeros([rows, cols], device=device))
        self.bias = nn.Parameter(torch.zeros([cols], device=device))

    def forward(self, x):
        return torch.matmul(x, self.weight) + self.bias


class MemoryCell(nn.Module):
    """This module is a memory cell similar to an LSTM or GRU but adapted to the design
    of LearningAgents. More specifically, it only has one recurrent connection (the memory 
    state) and allows the output (usually called the hidden state in GRU or LSTM) to be 
    a different size to the memory state.
    
    More specifically, there are two gates - the remember gate the and passthrough gate.
    
    The remember gate controls how the memory state is updated, either from the input or the 
    previous memory state.
    
    The passthrough gate controls how the output is produced, either from the input or the 
    new memory state.
    
    Having the passthrough gate allows the network to produce outputs without memorizing 
    what is required to produce them. This should allow it to only keep in its memory
    useful information that it may need in the future. This produces a few more parameters
    than using a GRU but hopefully allows the memory state to encode only things that need
    to be remembered rather than all previous history plus the current input.
    """
    
    def __init__(self, 
        input_size,
        output_size,
        memory_size,
        remember_layer, 
        passthrough_layer,
        memory_update_layer,
        output_input_update_layer,
        output_memory_update_layer):
        
        super(MemoryCell, self).__init__()
        
        self.input_size = input_size
        self.output_size = output_size
        self.memory_size = memory_size
        self.Wr = remember_layer
        self.Wp = passthrough_layer
        self.Wn = memory_update_layer
        self.Wz = output_input_update_layer
        self.Wy = output_memory_update_layer
        
    def forward(self, x):
        
        # Memory State
        m = x[:,self.input_size:]
    
        # Remember Gate
        r = torch.sigmoid(self.Wr(x))
    
        # New Memory State
        m_new = (1 - r) * m + r * torch.tanh(self.Wn(x))
    
        # Passthrough Gate
        p = torch.sigmoid(self.Wp(x))
    
        # Output
        y = (1 - p) * torch.tanh(self.Wy(m_new)) + p * torch.tanh(self.Wz(x))
    
        return torch.cat([y, m_new], dim=-1)
        
        
class Copy(nn.Module):

    def __init__(self, size):
        super(Copy, self).__init__()
        self.size = size 
        
    def forward(self, x):
        return x


class Concat(nn.Module):
    
    def __init__(self, input_sizes, output_sizes, layers):
        super(Concat, self).__init__()
        self.input_sizes = input_sizes
        self.output_sizes = output_sizes
        self.layers = nn.ModuleList(layers)
        self.input_offsets = np.hstack([0, np.cumsum(self.input_sizes[:-1], dtype=np.int64)])
    
    def forward(self, x):
        return torch.cat([
            layer(x[:,in_off:in_off+in_size]) for
            (in_off, in_size, layer) in 
            zip(self.input_offsets, self.input_sizes, self.layers)
        ], dim=-1)
    
    
class Array(nn.Module):
    
    def __init__(self, element_num, element_input_size, element_output_size, sublayer):
        super(Array, self).__init__()
        self.element_num = element_num
        self.element_input_size = element_input_size
        self.element_output_size = element_output_size
        self.sublayer = sublayer
    
    def forward(self, x):
        batchsize = x.shape[0]
        return self.sublayer(x.reshape([batchsize * self.element_num, -1])).reshape([batchsize, -1])
   

def _softmax_plus_one_masked(z, m, dim):
    z_pos_max = F.relu(z * m).max(dim=dim, keepdim=True).values
    return m * (torch.exp(z - z_pos_max) / (torch.sum(m * torch.exp(z - z_pos_max), dim=dim, keepdim=True) + 1))

def _mask_to_count_offset(m):
    m_sum = m.astype(np.int64).sum(axis=-1)
    m_off = np.hstack([0, np.cumsum(m_sum[:-1], dtype=np.int64)])
    return m_sum, m_off

def _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot, device):
    
    elem_bi = np.zeros([elem_tot], dtype=np.int64)
    elem_ei = np.zeros([elem_tot], dtype=np.int64)

    offset = 0
    for bi, en in enumerate(elem_num):
        elem_bi[offset:offset+en] = bi
        elem_ei[offset:offset+en] = np.arange(en)
        offset += en
    
    assert(offset == elem_tot)
    
    return (
        torch.as_tensor(elem_bi, device=device), 
        torch.as_tensor(elem_ei, device=device))

def _mask_to_indices_exclusive(m, device):
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        indices[np.where(m[i] == 1)[0][0]].append(i)
    return [torch.as_tensor(i, dtype=torch.long, device=device) for i in indices]
   
def _mask_to_indices_inclusive(m, device):
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        for j in np.where(m[i] == 1)[0]:
            indices[j].append(i)
    return [torch.as_tensor(i, dtype=torch.long, device=device) for i in indices] 
    
    
def _gating_to_indices_weights(gating, device):
    val_sort = gating.argsort(dim=1)
    ind0 = val_sort[:,-1]
    ind1 = val_sort[:,-2]
    ind_dim0 = torch.arange(len(gating), device=device)
    val0 = gating[ind_dim0,ind0]
    val1 = gating[ind_dim0,ind1]
    
    valexp0 = torch.exp(val0 - torch.maximum(val0, val1))
    valexp1 = torch.exp(val1 - torch.maximum(val0, val1))
    
    wei0 = valexp0 / (valexp0 + valexp1)
    wei1 = valexp1 / (valexp0 + valexp1)
    
    with torch.no_grad():
        ind0_np = ind0.cpu().numpy()
        ind1_np = ind1.cpu().numpy()
        wei0_np = wei0.cpu().numpy()
        wei1_np = wei1.cpu().numpy()
        
        indices = [[] for i in range(gating.shape[1])]
        weights = [[] for i in range(gating.shape[1])]
        for i in range(gating.shape[0]):
            indices[ind0_np[i]].append(i)
            indices[ind1_np[i]].append(i)
            weights[ind0_np[i]].append(wei0_np[i])
            weights[ind1_np[i]].append(wei1_np[i])
        indices = [torch.as_tensor(i, dtype=torch.long, device=device) for i in indices] 
        weights = [torch.as_tensor(w, dtype=torch.float32, device=device) for w in weights] 
    
    return indices, weights
    
    
def _indices_inclusive_accumulate_offsets(ind, elem_off, elem_num, device):
    
    elem_accum = np.zeros_like(elem_off)
    
    acc_off = [[] for i in range(len(ind))]
    for i in range(len(ind)):
        for j in range(len(ind[i])):
            k = ind[i][j]
            acc_off[i].append(elem_off[k] + elem_accum[k])
            elem_accum[k] += 1
        acc_off[i] = torch.as_tensor(acc_off[i], dtype=torch.long, device=device)
        
    assert np.all(elem_accum == elem_num)
    
    return acc_off
   
   
class AggregateSet(nn.Module):
    
    def __init__(self, 
        max_element_num, 
        element_input_size, 
        element_output_size,
        output_encoding_size,
        attention_encoding_size,
        attention_head_num,
        sublayer,
        query_layer,
        key_layer,
        value_layer,
        device):
        super(AggregateSet, self).__init__()
        
        self.max_element_num = max_element_num
        self.element_input_size = element_input_size
        self.element_output_size = element_output_size
        self.output_encoding_size = output_encoding_size
        self.attention_encoding_size = attention_encoding_size
        self.attention_head_num = attention_head_num
        self.sublayer = sublayer
        self.query_layer = query_layer
        self.key_layer = key_layer
        self.value_layer = value_layer
        
    def forward(self, x):
        
        elem_mask = x[:,self.max_element_num * self.element_input_size:]
        
        with torch.no_grad():
            elem_mask_np = elem_mask.cpu().numpy()
            assert elem_mask_np.shape[1] == self.max_element_num
            elem_num, elem_off = _mask_to_count_offset(elem_mask_np)
            elem_tot = elem_num.sum()
            elem_bi, elem_ei = _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot, x.device)
            
        xelem = x[:,:self.max_element_num * self.element_input_size].reshape([len(x), self.max_element_num, self.element_input_size])
        
        activ = self.sublayer(xelem[elem_bi, elem_ei])
        query = self.query_layer(activ).reshape([elem_tot, self.attention_head_num, self.attention_encoding_size])
        key = self.key_layer(activ).reshape([elem_tot, self.attention_head_num, self.attention_encoding_size])
        value = self.value_layer(activ).reshape([elem_tot, self.attention_head_num, self.output_encoding_size])

        attention_mask = torch.zeros([len(x), self.max_element_num, self.attention_head_num], dtype=torch.float32, device=x.device)
        attention_mask[elem_bi, elem_ei] = 1.0

        attention_padded = torch.zeros([len(x), self.max_element_num, self.attention_head_num], dtype=torch.float32, device=x.device)
        attention_padded[elem_bi, elem_ei] = (query * key).sum(dim=-1) / np.sqrt(self.attention_encoding_size)
        attention_padded = _softmax_plus_one_masked(attention_padded, attention_mask, dim=1)
        
        value_padded = torch.zeros([len(x), self.max_element_num, self.attention_head_num, self.output_encoding_size], dtype=torch.float32, device=x.device)
        value_padded[elem_bi, elem_ei] = value
        
        output = torch.zeros([len(x), self.attention_head_num * self.output_encoding_size + 1], dtype=x.dtype, device=x.device)
        output[:,:self.attention_head_num * self.output_encoding_size] = (value_padded * attention_padded[...,None]).sum(dim=1).reshape([len(x), self.attention_head_num * self.output_encoding_size])
        output[:,self.attention_head_num * self.output_encoding_size:] = (torch.as_tensor(elem_num.astype(np.float32) / float(self.max_element_num), dtype=torch.float32, device=x.device))[:,None]
        
        return output
        
        
class AggregateOrExclusive(nn.Module):
    
    def __init__(self, 
        output_encoding_size,
        sublayer_input_sizes,
        sublayer_output_sizes,
        sublayers,
        encoders):
        super(AggregateOrExclusive, self).__init__()

        self.output_encoding_size = output_encoding_size
        self.sublayer_input_sizes = sublayer_input_sizes
        self.sublayer_output_sizes = sublayer_output_sizes
        self.sublayers = nn.ModuleList(sublayers)
        self.encoders = nn.ModuleList(encoders)
        self.max_sublayer_input_size = np.max(sublayer_input_sizes)
        
    def forward(self, x):
        
        sublayer_mask = x[:,self.max_sublayer_input_size:]
        
        with torch.no_grad():
            sublayer_mask_np = sublayer_mask.cpu().numpy()
            sublayer_indices = _mask_to_indices_exclusive(sublayer_mask_np, device=x.device)

        output = torch.empty([x.shape[0], self.output_encoding_size + len(self.sublayers)], device=x.device)

        for i in range(len(self.sublayers)):
        
            if len(sublayer_indices[i]) == 0: continue
        
            xsub = x[sublayer_indices[i],:self.sublayer_input_sizes[i]]
            output[sublayer_indices[i],:self.output_encoding_size] = self.encoders[i](self.sublayers[i](xsub))
            
        output[:,self.output_encoding_size:] = sublayer_mask
        
        return output
    
    
class AggregateOrInclusive(nn.Module):
    
    def __init__(self, 
        output_encoding_size,
        attention_encoding_size,
        attention_head_num,
        sublayer_input_sizes,
        sublayer_output_sizes,
        sublayers,
        query_layers, 
        key_layers,
        value_layers, 
        device):
        super(AggregateOrInclusive, self).__init__()

        self.output_encoding_size = output_encoding_size
        self.attention_encoding_size = attention_encoding_size
        self.attention_head_num = attention_head_num
        self.sublayer_input_sizes = sublayer_input_sizes
        self.sublayer_output_sizes = sublayer_output_sizes
        self.sublayers = nn.ModuleList(sublayers)
        self.query_layers = nn.ModuleList(query_layers)
        self.key_layers = nn.ModuleList(key_layers)
        self.value_layers = nn.ModuleList(value_layers)
        self.sublayer_input_offsets = np.hstack([0, np.cumsum(sublayer_input_sizes[:-1], dtype=np.int64)])
        self.total_sublayer_input_size = np.sum(self.sublayer_input_sizes)
    
    
    def forward(self, x):
        
        sublayer_mask = x[:,self.total_sublayer_input_size:]
        
        with torch.no_grad():
            sublayer_mask_np = sublayer_mask.cpu().numpy()
            sublayer_indices = _mask_to_indices_inclusive(sublayer_mask_np, device=x.device)
            sublayer_indices_np = [i.cpu().numpy() for i in sublayer_indices]
            elem_num, elem_off = _mask_to_count_offset(sublayer_mask_np)
            elem_tot = np.sum(elem_num)
            elem_accum = np.zeros_like(elem_num)
            elem_bi, elem_ei = _elem_count_offset_to_indices_set(elem_off, elem_num, elem_tot, x.device)

            sublayer_acc_off = _indices_inclusive_accumulate_offsets(sublayer_indices_np, elem_off, elem_num, x.device)
            
        query = torch.zeros([elem_tot, self.attention_head_num, self.attention_encoding_size], dtype=torch.float32, device=x.device)
        key = torch.zeros([elem_tot, self.attention_head_num, self.attention_encoding_size], dtype=torch.float32, device=x.device)
        value = torch.zeros([elem_tot, self.attention_head_num, self.output_encoding_size], dtype=torch.float32, device=x.device)
        
        for i, bis in enumerate(sublayer_indices):
            
            if len(bis) == 0: continue
            
            sub_o, sub_n = self.sublayer_input_offsets[i], self.sublayer_input_sizes[i]
            xact = self.sublayers[i](x[bis,sub_o:sub_o+sub_n])
            
            query[sublayer_acc_off[i]] = self.query_layers[i](xact).reshape([len(bis), self.attention_head_num, self.attention_encoding_size])
            key[sublayer_acc_off[i]] = self.key_layers[i](xact).reshape([len(bis), self.attention_head_num, self.attention_encoding_size])
            value[sublayer_acc_off[i]] = self.value_layers[i](xact).reshape([len(bis), self.attention_head_num, self.output_encoding_size])
        
        attention_mask = torch.zeros([len(x), len(self.sublayers), self.attention_head_num], dtype=torch.float32, device=x.device)
        attention_mask[elem_bi, elem_ei] = 1.0

        attention_padded = torch.zeros([len(x), len(self.sublayers), self.attention_head_num], dtype=torch.float32, device=x.device)
        attention_padded[elem_bi, elem_ei] = (query * key).sum(dim=-1) / np.sqrt(self.attention_encoding_size)
        attention_padded = _softmax_plus_one_masked(attention_padded, attention_mask, dim=1)
        
        value_padded = torch.zeros([len(x), len(self.sublayers), self.attention_head_num, self.output_encoding_size], dtype=torch.float32, device=x.device)
        value_padded[elem_bi, elem_ei] = value
        
        output = torch.zeros([len(x), self.attention_head_num * self.output_encoding_size + len(self.sublayers)], dtype=torch.float32, device=x.device)
        output[:,:self.attention_head_num * self.output_encoding_size] = (value_padded * attention_padded[...,None]).sum(dim=1).reshape([len(x), self.attention_head_num * self.output_encoding_size])
        output[:,self.attention_head_num * self.output_encoding_size:] = sublayer_mask

        return output


class Clamp(nn.Module):

    def __init__(self, size, device):
        super(Clamp, self).__init__()
        self.size = int(size)
        self.min_values = nn.Parameter(torch.zeros([self.size], device=device), requires_grad=False)
        self.max_values = nn.Parameter(torch.zeros([self.size], device=device), requires_grad=False)
    
    def forward(self, x):
        return torch.clamp(x, self.min_values, self.max_values)


class SparseMixtureOfExperts(nn.Module):
    
    def __init__(self, 
        input_size,
        output_size,
        gating_layer,
        sublayers,
        device):
        super(SparseMixtureOfExperts, self).__init__()

        self.input_size = input_size
        self.output_size = output_size
        self.gating_layer = gating_layer
        self.sublayers = nn.ModuleList(sublayers)
    
    def forward(self, x):
    
        gating_values = self.gating_layer(x)
    
        sublayer_indices, sublayer_weights = _gating_to_indices_weights(gating_values, device=x.device)
        
        output = torch.zeros([len(x), self.output_size], dtype=torch.float32, device=x.device)

        for i, (bis, wei) in enumerate(zip(sublayer_indices, sublayer_weights)):
            
            if len(bis) == 0: continue
            
            output[bis] += wei[:,None] * self.sublayers[i](x[bis])
            
        avg_prob = F.softmax(gating_values, dim=-1).mean(dim=0)
        
        self.loss = (avg_prob * torch.log(avg_prob * gating_values.shape[1] + 1e-8)).sum(dim=-1)
        
        return output


class LayerNorm(nn.Module):

    def __init__(self, size, device):
        super(LayerNorm, self).__init__()
        self.size = int(size)
        self.offset = nn.Parameter(torch.zeros([self.size], device=device))
        self.scale = nn.Parameter(torch.ones([self.size], device=device))
        self.epsilon = 1e-5
    
    def forward(self, x):
        x = (x - x.mean(dim=-1)[...,None]) / torch.sqrt(x.var(dim=-1)[...,None] + self.epsilon)
        return x * self.scale + self.offset


def _softplus(x):
    return np.logaddexp(0, x)

def _lipschizt_normalize(c, W):
    scale = np.minimum(_softplus(c) / np.maximum(np.sum(abs(W), axis=0), 1e-7), 1.0)
    return scale[None] * W

def _lipschizt_init(W):
    return np.max(np.sum(np.abs(W), axis=0))

class LipschiztLinear(nn.Module):
    
    def __init__(self, rows, cols, device):
        super(LipschiztLinear, self).__init__()
        self.weight = nn.Parameter(torch.zeros([rows, cols], device=device))
        self.bias = nn.Parameter(torch.zeros([cols], device=device))
        self.c = nn.Parameter(torch.zeros([1], device=device))

    def forward(self, x):
        scale = torch.clamp(F.softplus(self.c) / torch.clamp(torch.sum(abs(self.weight), dim=0), min=1e-7), max=1.0)
        return torch.matmul(x, scale[None] * self.weight) + self.bias


# Functions to convert from NNE and back

def update_pytorch_module_from_nne_data(module, nne_data):
    
    with torch.no_grad():
        
        if nne_data['Type'] == 'Linear':
            assert isinstance(module, Linear)
            module.weight.data[:] = torch.as_tensor(nne_data['Weights'])
            module.bias.data[:] = torch.as_tensor(nne_data['Biases'])
            
        elif nne_data['Type'] == 'CompressedLinear':
            assert isinstance(module, CompressedLinear)
            
            weights = nne_runtime_basic_cpu.decompress_weights(
                nne_data['Weights'], 
                nne_data["WeightOffsets"], 
                nne_data["WeightScales"])
            
            module.weight.data[:] = torch.as_tensor(weights)
            module.bias.data[:] = torch.as_tensor(nne_data['Biases'])
            
        elif nne_data['Type'] in ['ReLU', 'ELU', 'TanH', 'Copy', 'GELU']:
            pass
            
        elif nne_data['Type'] in ['Normalize', 'Denormalize']:
            assert isinstance(module, (Normalize, Denormalize))
            module.mean.data[:] = torch.as_tensor(nne_data['Mean'])
            module.std.data[:] = torch.as_tensor(nne_data['Std'])

        elif nne_data['Type'] == 'Sequence':
            assert isinstance(module, nn.Sequential)
            for submodule, nne_sub in zip(module, nne_data['Layers']):
                update_pytorch_module_from_nne_data(submodule, nne_sub)
            
        elif nne_data['Type'] == 'MemoryCell':
            assert isinstance(module, MemoryCell)
            update_pytorch_module_from_nne_data(module.Wr, nne_data['RememberLayer'])
            update_pytorch_module_from_nne_data(module.Wp, nne_data['PassthroughLayer'])
            update_pytorch_module_from_nne_data(module.Wn, nne_data['MemoryUpdateLayer'])
            update_pytorch_module_from_nne_data(module.Wz, nne_data['OutputInputUpdateLayer'])
            update_pytorch_module_from_nne_data(module.Wy, nne_data['OutputMemoryUpdateLayer'])
        
        elif nne_data['Type'] == 'Concat':
            assert isinstance(module, Concat)
            for mod_sub, nne_sub in zip(module.layers, nne_data['Layers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
        
        elif nne_data['Type'] == 'Array':
            assert isinstance(module, Array)
            update_pytorch_module_from_nne_data(module.sublayer, nne_data['SubLayer'])

        elif nne_data['Type'] == 'AggregateSet':
            assert isinstance(module, AggregateSet)
            update_pytorch_module_from_nne_data(module.sublayer, nne_data['SubLayer'])
            update_pytorch_module_from_nne_data(module.query_layer, nne_data['QueryLayer'])
            update_pytorch_module_from_nne_data(module.key_layer, nne_data['KeyLayer'])
            update_pytorch_module_from_nne_data(module.value_layer, nne_data['ValueLayer'])
        
        elif nne_data['Type'] == 'AggregateOrExclusive':
            assert isinstance(module, AggregateOrExclusive)
            for mod_sub, nne_sub in zip(module.sublayers, nne_data['SubLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
            for mod_sub, nne_sub in zip(module.encoders, nne_data['Encoders']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
        
        elif nne_data['Type'] == 'AggregateOrInclusive':
            assert isinstance(module, AggregateOrInclusive)
            for mod_sub, nne_sub in zip(module.sublayers, nne_data['SubLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
            for mod_sub, nne_sub in zip(module.query_layers, nne_data['QueryLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
            for mod_sub, nne_sub in zip(module.value_layers, nne_data['KeyLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)
            for mod_sub, nne_sub in zip(module.value_layers, nne_data['ValueLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)

        elif nne_data['Type'] == 'Clamp':
            assert isinstance(module, Clamp)
            module.min_values.data[:] = torch.as_tensor(nne_data['MinValues'])
            module.max_values.data[:] = torch.as_tensor(nne_data['MaxValues'])

        elif nne_data['Type'] == 'SparseMixtureOfExperts':
            assert isinstance(module, SparseMixtureOfExperts)
            update_pytorch_module_from_nne_data(module.gating_layer, nne_data['GatingLayer'])
            for mod_sub, nne_sub in zip(module.sublayers, nne_data['SubLayers']):
                update_pytorch_module_from_nne_data(mod_sub, nne_sub)

        elif nne_data['Type'] == 'LayerNorm':
            assert isinstance(module, LayerNorm)
            module.offset.data[:] = torch.as_tensor(nne_data['Offset'])
            module.scale.data[:] = torch.as_tensor(nne_data['Scale'])
            module.epsilon = nne_data['Epsilon']

        elif nne_data['Type'] == 'LipschiztLinear':
            assert isinstance(module, LipschiztLinear)
            module.weight.data[:] = torch.as_tensor(nne_data['Weights'])
            module.bias.data[:] = torch.as_tensor(nne_data['Biases'])
            module.c.data[:] = torch.as_tensor(_lipschizt_init(nne_data['Weights']))

        else:
            raise Exception('Unknown Layer Type "%s"' % nne_data['Type'])
    
    
def create_pytorch_module_from_nne_data(nne_data, device):
    
    if nne_data['Type'] == 'Linear':
        module = Linear(nne_data['Weights'].shape[0], nne_data['Weights'].shape[1], device=device)
        
    elif nne_data['Type'] == 'CompressedLinear':
        module = CompressedLinear(nne_data['Weights'].shape[0], nne_data['Weights'].shape[1], device=device)
        
    elif nne_data['Type'] == 'ReLU': module = ReLU(nne_data['Size'])
    elif nne_data['Type'] == 'ELU': module = ELU(nne_data['Size'])
    elif nne_data['Type'] == 'TanH': module = TanH(nne_data['Size'])
    elif nne_data['Type'] == 'Copy': module = Copy(nne_data['Size'])
    elif nne_data['Type'] == 'GELU': module = GELU(nne_data['Size'])
    elif nne_data['Type'] == 'Normalize': module = Normalize(len(nne_data['Mean']), device=device)
    elif nne_data['Type'] == 'Denormalize': module = Denormalize(len(nne_data['Mean']), device=device)
        
    elif nne_data['Type'] == 'Sequence':
        module = nn.Sequential(*[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['Layers']])
        
    elif nne_data['Type'] == 'MemoryCell':
        module = MemoryCell(
            input_size=nne_data['InputSize'],
            output_size=nne_data['OutputSize'],
            memory_size=nne_data['MemorySize'],
            remember_layer=create_pytorch_module_from_nne_data(nne_data['RememberLayer'], device=device), 
            passthrough_layer=create_pytorch_module_from_nne_data(nne_data['PassthroughLayer'], device=device), 
            memory_update_layer=create_pytorch_module_from_nne_data(nne_data['MemoryUpdateLayer'], device=device),
            output_input_update_layer=create_pytorch_module_from_nne_data(nne_data['OutputInputUpdateLayer'], device=device),
            output_memory_update_layer=create_pytorch_module_from_nne_data(nne_data['OutputMemoryUpdateLayer'], device=device))
            
    elif nne_data['Type'] == 'Concat':
        module = Concat(
            input_sizes=nne_data['InputSizes'],
            output_sizes=nne_data['OutputSizes'],
            layers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['Layers']])
            
    elif nne_data['Type'] == 'Array':
        module = Array(
            element_num=nne_data['ElementNum'],
            element_input_size=nne_data['ElementInputSize'],
            element_output_size=nne_data['ElementOutputSize'],
            sublayer=create_pytorch_module_from_nne_data(nne_data['SubLayer'], device=device))
             
    elif nne_data['Type'] == 'AggregateSet':
        module = AggregateSet(
            max_element_num=nne_data['MaxElementNum'],
            element_input_size=nne_data['ElementInputSize'],
            element_output_size=nne_data['ElementOutputSize'],
            output_encoding_size=nne_data['OutputEncodingSize'],
            attention_encoding_size=nne_data['AttentionEncodingSize'],
            attention_head_num=nne_data['AttentionHeadNum'],
            sublayer=create_pytorch_module_from_nne_data(nne_data['SubLayer'], device=device),
            query_layer=create_pytorch_module_from_nne_data(nne_data['QueryLayer'], device=device),
            key_layer=create_pytorch_module_from_nne_data(nne_data['KeyLayer'], device=device),
            value_layer=create_pytorch_module_from_nne_data(nne_data['ValueLayer'], device=device),
            device=device)
            
    elif nne_data['Type'] == 'AggregateOrExclusive':
        module = AggregateOrExclusive(
            output_encoding_size=nne_data['OutputEncodingSize'],
            sublayer_input_sizes=nne_data['SubLayerInputSizes'],
            sublayer_output_sizes=nne_data['SubLayerOutputSizes'],
            sublayers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['SubLayers']],
            encoders=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['Encoders']])
            
    elif nne_data['Type'] == 'AggregateOrInclusive':
        module = AggregateOrInclusive(
            output_encoding_size=nne_data['OutputEncodingSize'],
            attention_encoding_size=nne_data['AttentionEncodingSize'],
            attention_head_num=nne_data['AttentionHeadNum'],
            sublayer_input_sizes=nne_data['SubLayerInputSizes'],
            sublayer_output_sizes=nne_data['SubLayerOutputSizes'],
            sublayers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['SubLayers']],
            query_layers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['QueryLayers']],
            key_layers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['KeyLayers']],
            value_layers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['ValueLayers']],
            device=device)
           
    elif nne_data['Type'] == 'Clamp': module = Clamp(len(nne_data['MinValues']), device=device)

    elif nne_data['Type'] == 'SparseMixtureOfExperts':
        module = SparseMixtureOfExperts(
            input_size=nne_data['InputSize'],
            output_size=nne_data['OutputSize'],
            gating_layer=create_pytorch_module_from_nne_data(nne_data['GatingLayer'], device=device),
            sublayers=[create_pytorch_module_from_nne_data(l, device=device) for l in nne_data['SubLayers']],
            device=device)

    elif nne_data['Type'] == 'LayerNorm': module = LayerNorm(len(nne_data['Offset']), device=device)

    elif nne_data['Type'] == 'LipschiztLinear':
        module = LipschiztLinear(nne_data['Weights'].shape[0], nne_data['Weights'].shape[1], device=device)

    else:
        raise Exception('Unknown Layer Type "%s"' % nne_data['Type'])

    update_pytorch_module_from_nne_data(module, nne_data)
    
    return module
    

def get_nne_data_from_pytorch_module(module):
    
    with torch.no_grad():
        
        if isinstance(module, ReLU): return { 'Type': 'ReLU', 'Size': module.size }
        elif isinstance(module, ELU): return { 'Type': 'ELU', 'Size': module.size }
        elif isinstance(module, TanH): return { 'Type': 'TanH', 'Size': module.size }
        elif isinstance(module, Copy): return { 'Type': 'Copy', 'Size': module.size }
        elif isinstance(module, GELU): return { 'Type': 'GELU', 'Size': module.size }
        
        elif isinstance(module, Normalize):
            return {
                'Type': 'Normalize',
                'Mean': module.mean.cpu().detach().numpy().astype(np.float32),
                'Std': module.std.cpu().detach().numpy().astype(np.float32)
            }
        
        elif isinstance(module, Denormalize):
            return {
                'Type': 'Denormalize',
                'Mean': module.mean.cpu().detach().numpy().astype(np.float32),
                'Std': module.std.cpu().detach().numpy().astype(np.float32)
            }
        
        elif isinstance(module, Linear):
            return {
                'Type': 'Linear',
                'Weights': module.weight.cpu().detach().numpy().astype(np.float32),
                'Biases': module.bias.cpu().detach().numpy().astype(np.float32)
            }
        
        elif isinstance(module, CompressedLinear):
            
            weights = module.weight.cpu().detach().numpy().astype(np.float32)
            biases = module.bias.cpu().detach().numpy().astype(np.float32)
            
            compressed, offsets, scales = nne_runtime_basic_cpu.compress_weights(weights)
            
            return {
                'Type': 'CompressedLinear',
                'WeightOffsets': offsets,
                'WeightScales': scales,
                'Biases': biases,
                'Weights':compressed
            }
        
        elif isinstance(module, nn.Sequential):
            return {
                'Type': 'Sequence', 
                'Layers': [get_nne_data_from_pytorch_module(l) for l in module]
            }
        
        elif isinstance(module, MemoryCell):
            return {
                'Type': 'MemoryCell',
                'InputSize': module.input_size,
                'OutputSize': module.output_size,
                'MemorySize': module.memory_size,
                'RememberLayer': get_nne_data_from_pytorch_module(module.Wr),
                'PassthroughLayer': get_nne_data_from_pytorch_module(module.Wp),
                'MemoryUpdateLayer': get_nne_data_from_pytorch_module(module.Wn),
                'OutputInputUpdateLayer': get_nne_data_from_pytorch_module(module.Wz),
                'OutputMemoryUpdateLayer': get_nne_data_from_pytorch_module(module.Wy),
            }
        
        elif isinstance(module, Concat):
            return {
                'Type': 'Concat',
                'InputSizes': module.input_sizes,
                'OutputSizes': module.output_sizes,
                'Layers': [get_nne_data_from_pytorch_module(l) for l in module.layers],
            }
        
        elif isinstance(module, Array):
            return {
                'Type': 'Array',
                'ElementNum': module.element_num,
                'ElementInputSize': module.element_input_size,
                'ElementOutputSize': module.element_output_size,
                'SubLayer': get_nne_data_from_pytorch_module(module.sublayer),
            }
        
        elif isinstance(module, AggregateSet):
            return {
                'Type': 'AggregateSet',
                'MaxElementNum': module.max_element_num,
                'ElementInputSize': module.element_input_size,
                'ElementOutputSize': module.element_output_size,
                'OutputEncodingSize': module.output_encoding_size,
                'AttentionEncodingSize': module.attention_encoding_size,
                'AttentionHeadNum': module.attention_head_num,
                'SubLayer': get_nne_data_from_pytorch_module(module.sublayer),
                'QueryLayer': get_nne_data_from_pytorch_module(module.query_layer),
                'KeyLayer': get_nne_data_from_pytorch_module(module.key_layer),
                'ValueLayer': get_nne_data_from_pytorch_module(module.value_layer),
            }
        
        elif isinstance(module, AggregateOrExclusive):
            return {
                'Type': 'AggregateOrExclusive',
                'OutputEncodingSize': module.output_encoding_size,
                'SubLayerInputSizes': module.sublayer_input_sizes,
                'SubLayerOutputSizes': module.sublayer_output_sizes,
                'SubLayers': [get_nne_data_from_pytorch_module(l) for l in module.sublayers],
                'Encoders': [get_nne_data_from_pytorch_module(l) for l in module.encoders],
            }
        
        elif isinstance(module, AggregateOrInclusive):
            return {
                'Type': 'AggregateOrInclusive',
                'OutputEncodingSize': module.output_encoding_size,
                'AttentionEncodingSize': module.attention_encoding_size,
                'AttentionHeadNum': module.attention_head_num,
                'SubLayerInputSizes': module.sublayer_input_sizes,
                'SubLayerOutputSizes': module.sublayer_output_sizes,
                'SubLayers': [get_nne_data_from_pytorch_module(l) for l in module.sublayers],
                'QueryLayers': [get_nne_data_from_pytorch_module(l) for l in module.query_layers],
                'KeyLayers': [get_nne_data_from_pytorch_module(l) for l in module.key_layers],
                'ValueLayers': [get_nne_data_from_pytorch_module(l) for l in module.value_layers],
            }
        
        elif isinstance(module, Clamp):
            return {
                'Type': 'Clamp',
                'MinValues': module.min_values.cpu().detach().numpy().astype(np.float32),
                'MaxValues': module.max_values.cpu().detach().numpy().astype(np.float32)
            }
        
        elif isinstance(module, SparseMixtureOfExperts):
            return {
                'Type': 'SparseMixtureOfExperts',
                'InputSize': module.input_size,
                'OutputSize': module.output_size,
                'GatingLayer': get_nne_data_from_pytorch_module(module.gating_layer),
                'SubLayers': [get_nne_data_from_pytorch_module(l) for l in module.sublayers]
            }
        
        elif isinstance(module, LayerNorm):
            return {
                'Type': 'LayerNorm',
                'Offset': module.offset.cpu().detach().numpy().astype(np.float32),
                'Scale': module.scale.cpu().detach().numpy().astype(np.float32),
                'Epsilon': module.epsilon,
            }
        
        elif isinstance(module, LipschiztLinear):
            return {
                'Type': 'LipschiztLinear',
                'Weights': _lipschizt_normalize(module.c.cpu().detach().numpy().astype(np.float32), module.weight.cpu().detach().numpy().astype(np.float32)),
                'Biases': module.bias.cpu().detach().numpy().astype(np.float32)
            }
        
        else:
            raise Exception('Unknown Module Type "%s"' % module.__class__.__name__)
    

# Network Class

class NeuralNetwork(nn.Module):

    def __init__(self, device):
        super(NeuralNetwork, self).__init__()
        self.model = None
        self.device = device

    def forward(self, x):
        return self.model(x)
        
    def get_filedata_size(self):
        return nne_runtime_basic_cpu.serialization_size_model(0, get_nne_data_from_pytorch_module(self.model))
        
    def save_to_filedata(self, data):
        nne_layers = get_nne_data_from_pytorch_module(self.model)
        assert len(data) == nne_runtime_basic_cpu.serialization_size_model(0, nne_layers)
        offset = nne_runtime_basic_cpu.serialization_save_model(0, nne_layers, data)
        assert offset == len(data)

    def load_from_filedata(self, data):
        offset, nne_layers = nne_runtime_basic_cpu.serialization_load_model(0, data)
        assert len(data) == offset
        assert len(data) == nne_runtime_basic_cpu.serialization_size_model(0, nne_layers)
        if self.model is None:
            self.model = create_pytorch_module_from_nne_data(nne_layers, device=self.device)
        update_pytorch_module_from_nne_data(self.model, nne_layers)
