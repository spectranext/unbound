from typing import Union


class PostponedTouch(object):
    def __init__(self, time_to_remove):
        self.time_to_remove = time_to_remove

    def update(self) -> Union[int, bool]:
        # True, if the postponed touch has concluded
        # int in range 0..11 if there is progress
        return False

    def dispose(self):
        pass
