"""
Iterator wrapper for logging progress for lengthy loops.

Copyright Epic Games, Inc. All Rights Reserved.
"""

try:
    import unreal
except ImportError:
    WITH_UE = False
else:
    WITH_UE = True

if not WITH_UE:
    try:
        from tqdm import tqdm
    except ImportError:
        WITH_TQDM = False
    else: 
        WITH_TQDM = True
else:
    WITH_TQDM = False

"""
"""
class CancelProgress(Exception):
    pass    

"""
"""
class ScopedProgressTracker():
    def __init__(self, step_total:int=0, task_desc=""):
        self._task_desc = task_desc
        self._step_total = step_total
        self._next_count = 0

    def __enter__(self):
        if WITH_TQDM:
            self._progress_bar = tqdm(total=self._step_total, desc=self._task_desc)
        elif WITH_UE:
            self._slow_task = unreal.ScopedSlowTask(self._step_total, self._task_desc)
            self._slow_task.__enter__()
            if self._step_total > 0:
                self._slow_task.enter_progress_frame(1)
        return self

    def __iter__(self):
        return self
        
    def __next__(self):
        self._next_count += 1
        if self._next_count >= self._step_total:
            raise StopIteration

        if WITH_TQDM:
            self._progress_bar.update(1)
        elif WITH_UE:
            self._check_slowtask_cancel(self._slow_task, self._task_desc)
            self._slow_task.enter_progress_frame(1)

    def __exit__(self, exception_type, exception_value, exception_traceback):
        if WITH_TQDM:
            if self._next_count+1 == self._step_total:
                self._progress_bar.update(1)
            else:
                self._progress_bar.close()
        elif WITH_UE:
            self._slow_task.__exit__(exception_type, exception_value, exception_traceback)

        return exception_type is None

    @staticmethod
    def _check_slowtask_cancel(slow_task, task_name):
        if WITH_UE:
            if slow_task.should_cancel():
                cancel_message = 'Task canceled by Unreal Engine.'
                if task_name:
                    cancel_message = "'{}' task canceled by Unreal Engine.".format(task_name)
                raise CancelProgress(cancel_message)

"""
"""
def log_progress(iterable, task_desc="", num_iters: int = 0):
    iterable_len = num_iters
    if hasattr(iterable, '__len__'):
        iterable_len = len(iterable)

    with ScopedProgressTracker(step_total=iterable_len, task_desc=task_desc) as progress_tracker:
        i = 0
        for item in iterable:
            yield item
            i += 1
            if i < iterable_len:
                next(progress_tracker)