# -*- coding: utf-8 -*-
"""
GLSL Shader Helper Utilities

@author:

Copyright Epic Games, Inc. All Rights Reserved.
"""

import os
import logging
from typing import Union

import moderngl

_include_keyword: str = '$include'

class ShaderContext:
    """Shader Expand Context
        This class is responsible for expnsion from a root .glsl file.
        Multiple includes are dropped and cycles are broken.
    """
    class ShaderBlob:
        """Shader Blob
            Represents an unresolved piece of shader text (eg. text or an include reference)
        """
        def expand(self, context: 'ShaderContext') -> str:
            return None
        
    class ShaderText(ShaderBlob):
        """Shader Text
            Static glsl text that needs no resolution
        """
        def __init__(self, text: str):
            self._text = text

        def expand(self, context: 'ShaderContext') -> str:
            return self._text

    class ShaderRef(ShaderBlob):
        """Shader Reference
            Lightweight type for 
        """
        def __init__(self, name: str):
            self._name = name

        def expand(self, context: 'ShaderContext') -> str:
            return context.resolve_ref(self._name)
    
    def __init__(self, loader: 'ShaderHandler'):
        self._loader = loader
        self._resolved_refs = dict[str,str]()

    def resolve_ref(self, ref_name: str) -> str:
        debug_header = f'//{_include_keyword} <{ref_name}>'
        if ref_name in self._resolved_refs:
            return debug_header + ' (multiple include)'

        shader_source = self._loader._get_cached_source(ref_name)
        if shader_source is None:
            return debug_header + ' (unparsed include file)'

        resolved_shader = shader_source.expand(self)
        self._resolved_refs[ref_name] = resolved_shader
        return debug_header + '\n' + resolved_shader


class ShaderSource:
    """Shader Source
        Holds source for a single shader file with all references unexpanded
    """
    def __init__(self, name: str):
        self._name = name
        self._includes = set[str]()
        self._source = list[ShaderContext.ShaderBlob]()
    
    def _parse_line(self, line_num: int, line: str, blob_text: str) -> Union[str,None]:
        chkline = line.strip()
        if not chkline.startswith(f'{_include_keyword} '):
            return blob_text + line
        
        # Complete blob even if there are include parse errors
        self._add_text_blob(blob_text)
        
        # Parse include directive
        matchends = ['""','<>']
        inc_line = chkline[8:].strip()
        chkends = inc_line[0]+inc_line[-1]
        if chkends not in matchends:
            logging.error(f'Invalid include syntax: {self._name}:{line_num}\n\t{chkline}')
            return None
        
        include_name = inc_line[1:-1]
        self._includes.add(include_name)
        self._source.append(ShaderContext.ShaderRef(include_name))
        return ''

    def _add_text_blob(self, text: str) -> str:
        if len(text) > 0:
            self._source.append(ShaderContext.ShaderText(text))
        return ''

    def parse_lines(self, lines: list[str]) -> bool:
        blob_text = ''
        for line_num,line in enumerate(lines):
            blob_text = self._parse_line(line_num, line, blob_text)
            if blob_text is None:
                return False

        self._add_text_blob(blob_text)
        return True

    def expand(self, context: 'ShaderContext') -> str:
        return '\n'.join([x.expand(context) for x in self._source])


class ShaderHandler:
    """Shader Utils
        Helper class to allow simple includes in glsl files
    """
    def __init__(self, ctx: moderngl.Context, shader_dir: str = '.'):
        self._glctx = ctx
        self._root_path = shader_dir
        self._source_cache = dict[str,ShaderSource]()
        self._debug_generate = False

    def _parse_shader_file(self, shader: str, shader_file: str) -> ShaderSource:
        shader_source = ShaderSource(shader)
        with open(shader_file, 'rt') as shrf:
            lines = shrf.readlines()
        
        if not shader_source.parse_lines(lines):
            return None
        
        # Add to cache here to break cyclic includes
        self._source_cache[shader] = shader_source
        # Recursively parse includes
        for inc_name in shader_source._includes:
            self._load_shader_source(inc_name)
        
        return shader_source
    
    def _get_cached_source(self, shader: str) -> ShaderSource:
        if shader in self._source_cache:
            return self._source_cache[shader]
        return None
    
    def _load_shader_source(self, shader: str) -> ShaderSource:
        cached_source = self._get_cached_source(shader)
        if cached_source is not None:
            return cached_source

        shader_path = os.path.join(self._root_path, shader)
        if not os.path.exists(shader_path):
            logging.error(f'Cannot find shader {shader} (root: {self._root_path})')
            return None
        
        return self._parse_shader_file(shader, shader_path)
    
    def set_debug_generate(self, debug_gen: bool):
        self._debug_generate = debug_gen
    
    def load_shader_text(self, shader: str = None) -> str:
        """Load (and cache) a .glsl shader file and all dependencies and return expanded shader source
        """
        if shader is None:
            return None
        
        shader = shader.replace('\\','/')
        sh_src = self._load_shader_source(shader)
        if sh_src is None:
            return None
        
        full_source = sh_src.expand(ShaderContext(self))
        if self._debug_generate and full_source is not None:
            with open(os.path.join(self._root_path, '_dbg_'+shader), 'wt') as dbgf:
                dbgf.write(full_source)

        return full_source

    def build_program(self, 
                      vertex_shader: str,
                      geometry_shader: str = None,
                      fragment_shader: str = None
                    ) -> moderngl.Program:
        """Build and return a moderngl.Program using the specified shader files (relative to shader root).
            $include directives will be recursively expanded before final source is built
        """
        vs_src = self.load_shader_text(vertex_shader)
        gs_src = self.load_shader_text(geometry_shader)
        fs_src = self.load_shader_text(fragment_shader)

        return self._glctx.program(vertex_shader = vs_src,
                                   geometry_shader = gs_src,
                                   fragment_shader = fs_src)
