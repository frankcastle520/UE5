# Copyright Epic Games, Inc. All Rights Reserved.
import unreal

# This example showcases the creation of a UClass that overrides the Pre/Post Shot
# Job Callbacks which is necessary for the Execute Script Node. Running 
# register_callback_class() will display Object named "CallbackOverrides" in the graph's 
# Execute Script Node details. This object replaced the available available Job/Shot 
# Callbacks with basic statements

# USAGE:
#
#   import MovieGraphScriptNodeExample
#   MovieGraphScriptNodeExample.register_callback_class()
#
#   After running this function, you will be able to select this UObject
#   within a Movie Graph Config's Execute Script Node.
#
#   You can add the above snippet to any of the /Content/Python/__init__ py file 
#   to make this UObject permanently available at engine startup.

def register_callback_class():
    """This helper function creates a sample UClass called CallbackOverrides
    that overrides UMovieGraphScriptBase functions, demonstrating how individual
    callbacks run before/after jobs and shots within a Movie Graphs Execute 
    Script Node.

    Call this function to register your CallbackOverrides class so it becomes 
    available for selection in the Execute Script Node.
    """
    @unreal.uclass()
    class CallbackOverrides(unreal.MovieGraphScriptBase):
        @unreal.ufunction(override=True)
        def on_job_start(self, in_job_copy):
            super().on_job_start(in_job_copy)
            unreal.log("This is run before the render starts")


        @unreal.ufunction(override=True)
        def on_job_finished(self, in_job_copy:unreal.MoviePipelineExecutorJob, 
                            in_output_data:unreal.MoviePipelineOutputData):
            super().on_job_finished(in_job_copy, in_output_data)
            unreal.log("This is run after the render jobs are all finished")
            for shot in in_output_data.graph_data:
                for layerIdentifier in shot.render_layer_data:
                        unreal.log("render layer: " + layerIdentifier.layer_name)
                        for file in shot.render_layer_data[layerIdentifier].file_paths:
                            unreal.log("file: " + file)


        @unreal.ufunction(override=True)
        def on_shot_start(self, in_job_copy:unreal.MoviePipelineExecutorJob, 
                          in_shot_copy: unreal.MoviePipelineExecutorShot):
            super().on_shot_start(in_job_copy, in_shot_copy)
            unreal.log("  This is run before every shot rendered")


        @unreal.ufunction(override=True)
        def on_shot_finished(self, in_job_copy:unreal.MoviePipelineExecutorJob, 
                             in_shot_copy:unreal.MoviePipelineExecutorShot, 
                             in_output_data:unreal.MoviePipelineOutputData):
            super().on_shot_finished(in_job_copy, in_shot_copy, in_output_data)
            unreal.log("  This is called after every shot is finished rendering")


        @unreal.ufunction(override=True)
        def is_per_shot_callback_needed(self):
            """
            Overriding this function and returning true enables per-shot disk 
            flushes, which has the same affect as turning on Flush Disk Writes 
            Per Shot on the Global Output Settings Node.
            """
            return True     

