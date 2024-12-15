# Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
import unreal

import os
import shutil

import numpy as np
import operator

from Modules.Geometry.cluster import ClumpGenerator
from Modules.Geometry.optim import GeometryOptimizer
from Modules.Texture import TextureGenerator
from Modules.Texture import TextureCompressionOptimizer
from Modules.Quantization import Quantization

from Modules.Export.mesh_generator import MeshGenerator
from Modules.Export.variable_txt_atlas import VariableTxtAtlas

from utils import math as umath
from utils import Metadata
from utils.io.generic import safe_dir
from utils.data import Groom

from logger.progress_iterator import log_progress, CancelProgress, ScopedProgressTracker

from .settings import Settings, GroupSettings

from logger.console_logger import ConsoleLogger


@unreal.uclass()
class HairCardGenController(unreal.HairCardGenControllerBase):
    """ HairCardGenController - UClass handles Unreal/python interop for running HairCardGenerator
    """

    def __init__(self) -> None:
        super().__init__()
        self._loaded_groom = None
        self._logger = ConsoleLogger(self.__class__.__name__)
        self.num_points_per_curve = 128

    @unreal.ufunction(override=True)
    def load_settings(self, generator_settings):
        """ load_settings(self, generator_settings)
            Load user parameters
        """

        self._settings = Settings()
        self._settings.random_seed = generator_settings.random_seed
        # TODO: Derive random seed from primary seed in a better way?
        np.random.seed(self._settings.random_seed)
        random_seeds = np.random.randint(low=32000000, size=len(generator_settings.filter_group_generation_settings))

        self._settings.atlas_size = 2 ** generator_settings.atlas_size.value
        self._settings.depth_min_range = generator_settings.depth_minimum
        self._settings.depth_max_range = generator_settings.depth_maximum
        self._settings.generate_for_groom_group = generator_settings.generate_for_groom_group
        self._settings.derive_from_previous_lod = generator_settings.reduce_cards_from_previous_lod
        self._settings.use_prev_reserve_tx = generator_settings.use_reserved_space_from_previous_lod
        self._settings.reserve_tx_pct = generator_settings.reserve_texture_space_lod / 100.0
        self._settings.texture_name = generator_settings.derived_texture_settings_name
        self._settings.channel_layout = generator_settings.channel_layout.value
        self._settings.hair_widths = np.array(generator_settings.hair_widths)
        self._settings.root_scales = np.array(generator_settings.root_scales)
        self._settings.tip_scales = np.array(generator_settings.tip_scales)

        # Handle generating all groups on first groom group id
        if generator_settings.generate_geometry_for_all_groups:
            self._settings.generate_for_groom_group = -1

        # Generation root paths (intermediate/output)
        tmp_root = generator_settings.intermediate_path
        self._output_name = generator_settings.base_filename
        self._texture_root = safe_dir(os.path.join(tmp_root, 'Output'))
        self._metadata_path = safe_dir(os.path.join(tmp_root, 'Metadata'))
        self._cards_path = safe_dir(os.path.join(tmp_root, 'Card'))
        # Debug-only paths
        self._debug_texture_path = os.path.join(tmp_root, 'Textures')

        # HACK: Generate special parent metadata for g0 to pull texture layouts
        # Reserved texture is only used in non-derived case, but we want to derive the texture layout from parent
        g0_settings = generator_settings.filter_group_generation_settings[0]
        self._settings.g0_parent = None
        if g0_settings.parent_name:
            self._settings.g0_parent = Metadata.open(self._metadata_path, g0_settings.parent_name)

        # Per-generation group settings
        self._settings.group = []
        for i, group_settings in enumerate(generator_settings.filter_group_generation_settings):
            self._settings.group.append(GroupSettings())
            # Per-group output name
            self._settings.group[i].output_name = group_settings.generate_filename

            # Create a metadata overlay object per-group
            parent_name = group_settings.parent_name if self._settings.derive_from_previous_lod else None
            group_metadata = Metadata.create(self._metadata_path, group_settings.generate_filename, parent_name)
            self._settings.group[i].metadata = group_metadata

            # Geometry settings
            self._settings.group[i].num_clumps = group_settings.target_number_of_cards

            # Texture settings
            self._settings.group[i].num_quant_textures = group_settings.number_of_textures_in_atlas
            self._settings.group[i].use_multi_card_clumps = group_settings.use_multi_card_clumps
            self._settings.group[i].max_number_of_flyaways = group_settings.max_number_of_flyaways
            self._settings.group[i].width_scale = group_settings.strand_width_scaling_factor
            self._settings.group[i].compression_factor = -1.0 if group_settings.use_optimized_compression_factor else 1.0

            # Write a derived random seed to file
            group_metadata.save('random_seed.npy', [random_seeds[i]])

        # HACK: Remove old files that won't be regenerated in a reduce from prv LOD case
        #   Should eventually be handled by more robust metadata/LOD system
        if self._settings.derive_from_previous_lod:
            remove_list = ['clumps_strand_labels.npy',
                           'max_main_clump.npy',
                           'quantization_mapping.npy',
                           'texture_layout.npz',
                           'texture_binpack.npy']
            for gs in self._settings.group:
                for rmf in remove_list:
                    gs.metadata.remove(rmf)

        if (not hasattr(self, '_loaded_groom') or self._loaded_groom is None):
            return False

        if (not self._loaded_groom.reset_curve_filter_groups(generator_settings.strand_filter_group_index_map)):
            return False

        return True

    @unreal.ufunction(override=True)
    def get_points_per_curve(self):
        """ get_points_per_curve(self)
            Get points per curve
        """

        return self.num_points_per_curve

    @unreal.ufunction(override=True)
    def load_groom_data(self, groom_data, name, cached_grooms_path, save_cached = False):
        """ load_groom_data(self, groom_data, name, cached_grooms_path, save_cached = False)
            Load groom data from unreal uasset
        """

        cached_file = os.path.join(safe_dir(cached_grooms_path), name + '.cached')

        if os.path.exists(cached_file):
            self._loaded_groom = Groom.load(cached_file)
        else:
            self._loaded_groom = Groom(num_points_per_curve = self.num_points_per_curve)
            self._loaded_groom.unmarshal_groom_data(groom_data)

            if save_cached:
                self._loaded_groom.dump(cached_file)

        return True

    @unreal.ufunction(override=True)
    def generate_clumps(self, settings_group_index: int):
        """ generate_clumps(self, settings_group_index)
            Cluster strands for settings group
        """
        
        safe_dir(self._cards_path)

        with ScopedProgressTracker(task_desc = "Clustering strands into cards"):
            try:
                group_settings = self._settings.group[settings_group_index]
                clump_gen = ClumpGenerator(self._loaded_groom,
                                            metadata=group_settings.metadata,
                                            group_index=settings_group_index,
                                            name=group_settings.output_name)
                clump_gen.cluster_strands(target_num_clumps=group_settings.num_clumps,
                                          max_flyaways=group_settings.max_number_of_flyaways,
                                          use_multi_card_clumps=group_settings.use_multi_card_clumps)
            except CancelProgress:
                return False
        return True

    @unreal.ufunction(override=True)
    def set_optimizations(self, settings_group_index: int):
        """ set_optimizations(self, settings_group_index)
            Generate card optimizations for settings group
        """
    
        safe_dir(self._cards_path)

        res = unreal.Array(int)

        with ScopedProgressTracker():
            try:
                group_settings = self._settings.group[settings_group_index]
                self.geometry_opt = GeometryOptimizer(self._loaded_groom,
                                                 metadata=group_settings.metadata,
                                                 obj_path=self._cards_path,
                                                 group_index=settings_group_index,
                                                 use_multicard_clumps=group_settings.use_multi_card_clumps,
                                                 name=group_settings.output_name)
                self.geometry_opt.generate_optimizations(max_flyaways=group_settings.max_number_of_flyaways)

            except CancelProgress:
                return res

            for opt in self.geometry_opt.optimizations:
                res.append(len(opt.curve_ids))
            return res

    @unreal.ufunction(override=True)
    def set_interpolated_avg_curve(self, id: int, cid: int, points):
        """ set_interpolated_avg_curve(self, id: int, cid: int, points)
            Set the subdivision points
        """

        points = np.array(points).reshape(-1, 3)
        self.geometry_opt.optimizations[id]._avg_curve_points[cid] = points

    @unreal.ufunction(override=True)
    def get_average_curve(self, id: int, cid: int):
        """ get_average_curve(self, id: int, cid: int)
            Get the points of the average curve
        """

        res = unreal.Array(float)

        if id < len(self.geometry_opt.optimizations):
            points = self.geometry_opt.optimizations[id].avg_curves[cid].flatten()

            for point in points:
                res.append(point)

        return res

    @unreal.ufunction(override=True)
    def generate_cards_geometry(self):
        """ generate_cards_geometry(self)
            Generate cards geometry
        """ 

        with ScopedProgressTracker():
            try:
                self.geometry_opt.run()
            except CancelProgress:
                return False

            return True

    @unreal.ufunction(override=True)
    def cluster_textures(self, settings_group_index: int):
        """ cluster_textures(self)
            Cluster textures
        """

        with ScopedProgressTracker():
            try:
                model_path = os.path.abspath(os.path.join(os.path.dirname(
                    os.path.abspath(__file__)), os.pardir, 'torch_models', 'texture_features.pt'))
                group_settings = self._settings.group[settings_group_index]
                quantize = Quantization(name=group_settings.output_name,
                                    metadata=group_settings.metadata,
                                    obj_path=self._cards_path,
                                    num_points_per_curve=self._loaded_groom.num_points_per_curve,
                                    num_workers=0,
                                    groom=self._loaded_groom)
                quantize.run(group_settings.num_quant_textures, model_path)
            except CancelProgress:
                return False

            return True

    @unreal.ufunction(override=True)
    def check_cluster_textures(self, settings_group_index: int):
        """ check_cluster_textures(self)
            Render all textures of all clusters
        """

        safe_dir(self._intermediate_texture_path)

        with ScopedProgressTracker():
            try:
                batch_size = 25
                group_settings = self._settings.group[settings_group_index]
                config_cards = group_settings.metadata.load('config_cards_info.npy', allow_pickle=True)
                quantization = group_settings.metadata.load('quantization_mapping.npy', allow_pickle=True).item()

                for cluster_idx in log_progress(range(len(quantization)),
                                                'Rasterizing texture clusters'):
                    cards = quantization[cluster_idx]['cards']
                    card_sizes = np.array([[c.length, c.width] for c in config_cards[cards]])

                    quant_atlas_manager = VariableTxtAtlas(self._settings.atlas_size // 4)
                    quant_atlas_manager.generate_coordinates(card_sizes)

                    group_data = {'name': group_settings.output_name,
                                  'dataset_card_filter': cards,
                                  'strand_width_scale': 1}

                    quant_texture_generator = TextureGenerator(
                            name=group_settings.output_name,
                            groom=self._loaded_groom,
                            obj_path=self._cards_path,
                            metadata=group_settings.metadata,
                            output_path=self._debug_texture_path,
                            num_points_per_curve=self._loaded_groom.num_points_per_curve,
                            atlas_manager=quant_atlas_manager,
                            group_data=[group_data],
                            atlas_size=self._settings.atlas_size // 4,
                            batch_size=batch_size,
                            depth_min_range=group_settings.depth_min_range,
                            depth_max_range=group_settings.depth_max_range,
                            # NOTE: subprocesses currently unsupported so num_workers must be 0
                            num_workers=0)

                    quant_texture_generator.generate()
                    quant_texture_generator.save_atlas(cluster_idx)

            except CancelProgress:
                return False

            return True
        
    @unreal.ufunction(override=True)
    def generate_texture_layout(self):
        """ generate_texture_layout(self)
            Generate the layout (uv coordinates) that will be used to pack card textures in atlas
        """
        with ScopedProgressTracker(2) as progress_tracker:
            try:
                card_sizes = np.empty((0, 2))
                compression_factors = np.empty((0))
                gcidslist: list[np.ndarray] = []
                for gid, group_settings in enumerate(self._settings.group):
                    quantization_filename = 'quantization_mapping.npy'
                    quantization = group_settings.metadata.load(quantization_filename, allow_pickle=True).item()
                    id_centers = [k['center'] for k in quantization.values()]

                    cardsmeta_filename = 'config_cards_info.npy'
                    config_cards = group_settings.metadata.load(cardsmeta_filename, allow_pickle=True)

                    group_card_sizes = np.array([[c.length, c.width] for c in config_cards[id_centers]])
                    card_sizes = np.append(card_sizes, group_card_sizes, axis=0)

                    # Build per-card compression factors (if compression_factor < 0)
                    if group_settings.compression_factor > 0:
                        group_compression_factors = np.full((len(group_card_sizes)), group_settings.compression_factor)
                    else:
                        compressor = TextureCompressionOptimizer(asset_name=group_settings.output_name,
                                                                groom=self._loaded_groom,
                                                                id_centers=id_centers,
                                                                metadata=group_settings.metadata,
                                                                obj_path=self._cards_path)
                        group_compression_factors = compressor.get_compression_factors()

                    compression_factors = np.append(compression_factors, group_compression_factors)
                    gcids = np.stack((np.full(len(id_centers), gid), np.array(id_centers)), axis=-1)
                    gcidslist.append(gcids)

                next(progress_tracker)
                g0_metadata = self._settings.group[0].metadata
                card_gcids = np.concatenate(gcidslist, axis=0)
                quant_atlas_manager = VariableTxtAtlas(metadata=g0_metadata,
                                                       squared_atlas_size=self._settings.atlas_size,
                                                       reserve_pct=self._settings.reserve_tx_pct)
                
                # Set the parent layout if we're using reserved space from previous LOD
                if self._settings.use_prev_reserve_tx:
                    quant_atlas_manager.set_parent_layout(self._settings.g0_parent)

                quant_atlas_manager.generate_coordinates(card_gcids, card_sizes, compression_factors)

                layout_filename = 'texture_layout.npz'
                # HACK: Place the file in settings group 0 folder to keep the metadata together
                quant_atlas_manager.save_to_file(layout_filename)
            except CancelProgress:
                return False
        return True
    
    @unreal.ufunction(override=True)
    def generate_texture_atlases(self, width_scale = -1):
        """ generate_texture_atlases(self)
            Render all strands to card textures and place in each atlas at appropriate uv coords
        """
        with ScopedProgressTracker():
            try:
                group_data = []
                for group_settings in self._settings.group:
                    group_data.append({})
                    group_data[-1]['name'] = group_settings.output_name
                    if width_scale > 0:
                        group_data[-1]['strand_width_scale'] = width_scale
                    else:
                        group_data[-1]['strand_width_scale'] = group_settings.width_scale

                    quantization_filename = 'quantization_mapping.npy'
                    quantization = group_settings.metadata.load(quantization_filename, allow_pickle=True).item()
                    group_data[-1]['dataset_card_filter'] = [k['center'] for k in quantization.values()]
                    group_data[-1]['metadata'] = group_settings.metadata

                layout_filename = 'texture_layout.npz'
                g0_metadata = self._settings.group[0].metadata
                quant_atlas_manager = VariableTxtAtlas(metadata=g0_metadata)
                quant_atlas_manager.load_from_file(layout_filename)

                # Use derived texture name (may be a previous texture)
                texture_name = self._settings.texture_name
                texture_path = os.path.join(self._texture_root, texture_name)

                # Remove textures if exporting at this LOD (does not run if filling reserved space)
                if (self._output_name == texture_name) and os.path.isdir(texture_path):
                    shutil.rmtree(texture_path)

                self._loaded_groom.update_width_multipliers(self._settings.hair_widths,
                                                            self._settings.root_scales,
                                                            self._settings.tip_scales)

                batch_size = 25
                quant_texture_generator = TextureGenerator(
                        name=texture_name,
                        groom=self._loaded_groom,
                        obj_path=self._cards_path,
                        output_path=texture_path,
                        num_points_per_curve=self._loaded_groom.num_points_per_curve,
                        atlas_manager=quant_atlas_manager,
                        group_data=group_data,
                        channel_layout=self._settings.channel_layout,
                        atlas_size=self._settings.atlas_size,
                        batch_size=batch_size,
                        random_seed=self._settings.random_seed,
                        depth_min_range=self._settings.depth_min_range,
                        depth_max_range=self._settings.depth_max_range,
                        # NOTE: subprocesses currently unsupported so num_workers must be 0
                        num_workers=0)

                quant_texture_generator.init_atlas()
                quant_texture_generator.generate()
                quant_texture_generator.dilate_atlas(dilation=16)
                quant_texture_generator.save_atlas()
            except CancelProgress:
                return False
        return True

    @unreal.ufunction(override=True)
    def generate_mesh(self, output_mesh: unreal.StaticMesh):
        """ generate_mesh(self)
            Create unified mesh from all cards
        """
        if ( not output_mesh ):
            self._logger.error("Invalid mesh asset target!")
            return False

        with ScopedProgressTracker():
            try:
                group_data = []
                for group_settings in self._settings.group:
                    group_data.append({})
                    group_data[-1]['name'] = group_settings.output_name
                    quantization_filename = 'quantization_mapping.npy'
                    quantization = group_settings.metadata.load(quantization_filename, allow_pickle=True).item()
                    group_data[-1]['dataset_card_filter'] = [k['center'] for k in quantization.values()]
                    group_data[-1]['metadata'] = group_settings.metadata
                
                layout_filename = 'texture_layout.npz'
                g0_metadata = self._settings.group[0].metadata
                quant_atlas_manager = VariableTxtAtlas(metadata=g0_metadata)
                quant_atlas_manager.load_from_file(layout_filename)

                mesh = MeshGenerator(name=self._output_name,
                                    obj_path=self._cards_path,
                                    group_data=group_data,
                                    phys_group=self._settings.generate_for_groom_group,
                                    atlas_size=self._settings.atlas_size,
                                    atlas_manager=quant_atlas_manager,
                                    from_opt_objs=False)

                mesh._output_name = self._output_name

                R = umath.identity_matrix()[:3,:3]
                self.ue_create_mesh(output_mesh, mesh.extract_data_from_cards(R))

            except CancelProgress:
                return False

        return True
 
    def ue_create_mesh(self, mesh: unreal.StaticMesh, mesh_info):
        verts, tris, normals, uvs, groups = mesh_info
        ue_verts = unreal.Array(float)
        ue_tris = unreal.Array(int)
        ue_normals = unreal.Array(float)
        ue_uvs = unreal.Array(float)
        ue_groups = unreal.Array(int)

        # Unreal uses left-handed coordinate space (inverted normals)
        lh_normals = -normals

        for vert_c in verts.flatten():
            ue_verts.append(vert_c.item())
        
        for tri_c in tris.flatten():
            ue_tris.append(tri_c.item())

        for normal_c in lh_normals.flatten():
            ue_normals.append(normal_c.item())

        for uv_c in uvs.flatten():
            ue_uvs.append(uv_c.item())
        
        for group in groups:
            ue_groups.append(group.item())

        self.create_cards_static_mesh(mesh, ue_verts, ue_tris, ue_normals, ue_uvs, ue_groups)


if __name__ == "__main__":
	# s = unreal.PyTestStruct()
    pass
