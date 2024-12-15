import unreal
from collections import deque
from collections.abc import Iterable
from threading import Lock
  
class PyTick():
 
    _delegate_handle = None
    _current = None
    _lock = Lock()
    schedule = None
 
    def __init__(self):
        self.schedule = deque()
        self._delegate_handle = unreal.register_slate_post_tick_callback(self._callback)
 
    def _callback(self, _):
        # Test if the lock is acquired by another thread.
        # If it is, we skip this tick, another python is already running.
        if not self._lock.acquire(False):
            return
 
        if self._current is None:
            if self.schedule:
                self._current = self.schedule.popleft()
 
            else:
                print ('Done jobs')
                unreal.unregister_slate_post_tick_callback(self._delegate_handle)
                self._lock.release()
                return
 
        try:
            task = next(self._current)
 
            if task is not None and isinstance(task, Iterable):
                # reschedule current task, and do the new one
                self.schedule.appendleft(self._current)
                self._current = task
 
        except StopIteration:
            self._current = None
 
        except:
            self._current = None
            raise
 
        finally:
            self._lock.release()


class VCamSmooth(object):

	def __init__( self ):
		self.camera_binding = None
		self.camera_subsection = None
		self._find_camera_binding()

	def run( self, filter_width=20):
		py_tick = PyTick()
		py_tick.schedule.append( self._prep() )
		py_tick.schedule.append( self._run(filter_width) )
		
	def reset( self ):
		# get camera component binding proxy
		binding = self._get_camera_component_binding()
		
		# This should reset tracks
		self._check_transform_tracks(binding)

		# Lock the level sequence again
		unreal.LevelSequenceEditorBlueprintLibrary.set_lock_level_sequence(True)


	def _prep( self ):
		unreal.LevelSequenceEditorBlueprintLibrary.set_lock_level_sequence(False)
		
		# get camera component binding proxy
		self.binding = self._get_camera_component_binding()

		# get source transform track
		source_track = self._check_transform_tracks(self.binding)

		# duplicate the transform track
		self.target_section = self._duplicate_track(self.binding, source_track)

		yield

	def _run( self, filter_width ):
		# run gaussian filter
		self._apply_filter(self.binding, self.target_section, filter_width)
		yield

	def _find_camera_binding( self ):
		ls = unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
		tracks = ls.find_tracks_by_type(unreal.MovieSceneCameraCutTrack)
		if not tracks:
			raise RuntimeError('Camera Cut Track Not Found')
		unreal.LevelSequenceEditorBlueprintLibrary.focus_parent_sequence()
		bid = tracks[0].get_sections()[0].get_camera_binding_id()
		camera_actor = unreal.LevelSequenceEditorBlueprintLibrary.get_bound_objects(bid)[0]

		# Find camera binding in subsequences
		for track in ls.get_tracks():
			if isinstance(track, unreal.MovieSceneSubTrack):
				subsection = track.get_sections()[0]
				unreal.LevelSequenceEditorBlueprintLibrary.focus_level_sequence(subsection)
				subseq = subsection.get_sequence()
				for binding in subseq.get_bindings():
					bid = ls.get_portable_binding_id(subseq, binding)
					bound_objects = unreal.LevelSequenceEditorBlueprintLibrary.get_bound_objects(bid)
					if len(bound_objects) > 0 and bound_objects[0] == camera_actor:
						self.camera_binding = binding 
						self.camera_subsection = subsection
						break
				unreal.LevelSequenceEditorBlueprintLibrary.focus_parent_sequence()

		if self.camera_binding:
			return

		# Find camera binding in current level sequence (not typical)
		for binding in ls.get_bindings():
			bid = ls.get_portable_binding_id(ls, binding)
			bound_objects = unreal.LevelSequenceEditorBlueprintLibrary.get_bound_objects(bid)
			if len(bound_objects) > 0 and bound_objects[0] == camera_actor:
				self.camera_binding = binding 
				return


	def _lock_camera_sequence( self, locked ):
		if self.camera_subsection:
			unreal.LevelSequenceEditorBlueprintLibrary.focus_level_sequence(self.camera_subsection)
		unreal.LevelSequenceEditorBlueprintLibrary.set_lock_level_sequence( locked )
		if self.camera_subsection:
			unreal.LevelSequenceEditorBlueprintLibrary.focus_parent_sequence()

	def _get_camera_component_binding( self ):
		for child in self.camera_binding.get_child_possessables():
			if child.get_possessed_object_class().get_name() == 'CineCameraComponent':
				return child 

	def _check_transform_tracks( self, camera_component_binding ):
		active_tracks = []
		inactive_tracks = []
		for track in camera_component_binding.get_tracks():
			if isinstance(track, unreal.MovieScene3DTransformTrack):
				if track.get_sections()[0].is_active():
					active_tracks.append(track)
				else:
					inactive_tracks.append(track)

		# If there is no inactive track, that's likely the default, so return active track
		# If there is no active track, unexpected but just make it active
		# If there're both active and inactive tracks, delete active ones and re-active inactive one
		if active_tracks and not inactive_tracks:
			return active_tracks[0]
		elif not active_tracks and inactive_tracks:
			inactive_tracks[0].get_sections()[0].set_is_active(True)
			return inactive_tracks[0]
		elif active_tracks and inactive_tracks:
			for track in active_tracks:
				camera_component_binding.remove_track(track)
			inactive_tracks[0].get_sections()[0].set_is_active(True)
			return inactive_tracks[0]

	def _duplicate_track( self, binding, source_track ):
		ss = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)
		copied = ss.copy_tracks([source_track])
		target_tracks = ss.paste_tracks(copied, unreal.MovieScenePasteTracksParams(sequence=binding.sequence, bindings=[binding]))
		target_track = target_tracks[0]
		source_section = source_track.get_sections()[0]
		target_section = target_track.get_sections()[0]
		source_section.set_is_active(False)
		target_section.set_is_active(True)
		return target_section

	def _apply_filter( self, binding, target_section, filter_width=5 ):
		ses = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)
		curve_editor = ses.get_curve_editor()
		if not curve_editor.is_curve_editor_open(): curve_editor.open_curve_editor()
	
		if self.camera_subsection:
			unreal.LevelSequenceEditorBlueprintLibrary.focus_level_sequence(self.camera_subsection)

		for channel in target_section.get_all_channels():
			sc = unreal.SequencerChannelProxy()
			sc.section = target_section
			sc.channel_name = channel.channel_name

			numkeys = channel.get_num_keys()
			curve_editor.show_curve( sc, True )
			curve_editor.select_keys( sc, list(range(numkeys)))

		bake = unreal.CurveEditorBakeFilter()
		curve_editor.apply_filter(bake)
		
		gauss = unreal.CurveEditorGaussianFilter()
		gauss.gaussian_params.kernel_width = filter_width
		curve_editor.apply_filter(gauss)

		unreal.LevelSequenceEditorBlueprintLibrary.focus_parent_sequence()