# Configuration files

Each asset has an associated configuration file containing the specific parameters to be used during the hair-card generation, for each LOD, as well as generic parameters.

## Structure

- args: contains hyper-parameters
- meta: versioning, used to keep track of the changes in the config giles

### Args

- input_path: path to the alembic / data file. The data loader is able to automatically load the file based on it's format.
- output_path: folder where the output information is going to be generated
- name: name of the asset to be saved
- margin: percentage of **geometry** margin to be used when generating the card
- uv_margin: percentage of **uv** margin to be used when generating the card uv coordinates
- card_id: which card to be optimized. -1 means optimize all cards
- min_num_cluster_root_pts: minimum number of root vertices that can be clustered together, defining a card.
- device: Target device for computation, either cpu or cuda:X. TODO: Remove parameter and guess from available resources.
- grid_type: how the plane fitting should happen. UNIFORM means that the BB is split uniformly, FIT means that it's split based on the content.
- card: which card to run. If -1, run all.
- debug: activate debug mode
- atlas
  - image_format: format of the atlas
  - overestimation_factor: it controls the over-estimation assumption where we assume the individual card textures are scaled based on the relative weight as a function of this factor. E.g. 8 means that we over-estimate that when the rescaled textures are computed, this will be rescaled proportionally, such as they can fill in a grid of size sqrt(num_textures)/8 x sqrt(num_textures)/8
  - dilation_size: size of texture dilation. Use 1 as default value. If 0 is provided, no dilation is performed.
  - dilation_iterations: number of iterations the dilation is performed. Use 10 as default value. If 0 is provided, no dilation is performed.
- quantization
  - method: vgg or ae
  - ae
    - learning_rate: step in the ae training phase
    - n_feat: dimensionality of the latent space
    - delta_threshold: stop when delta error is below the threshold
    - max_iter: stop if num iter is bigger than this number
  - vgg
    - vgg_version: VGG network architecture (from modelzoo): 11, 13, 16, 19.
    - num_layers: number of layers used for generating features. Number should be at least half the total number of layers.
    - norm_size: target image size used for data normalization. Images of this size (squared size assumed) will be sent to the network.
- geometry -> for texture generation
  - init: initial texture id we want to render. Usefull when we need to resume texture generation for any reason whatsoever.
  - fov: field of view. It is used to compute a the camera location and have a rought initial estimate of the strand width.
  - znear: minimum plane to render hairstrands.
  - zfar: maximum plane to render hairstrands.
  - radius: scale applied to minimum allowed strand radius in uv space. Values should be greater than 1; otherwise it will be clamped to 1.
  - gamma: gamma correction applied to textures to increase contrast. TODO: Move parameter to atlas.
- LODs -> LODs specific information
  - run_LOD: which LOD to optimize. if -1, optimize for all
  - LOD*N*: LOD N specific information
    - num_cards: total number of cards
    - v_subdivision: vertical sub-division
    - h_subdivision: horizontal sub-division
    - image_size: maximum allowed size when generating card textures. 
    - atlas_size: atlas size
    - num_textures: defined number of quantized textures included in the atlas
