from typing import Optional, Set, Callable
from datetime import datetime, timedelta


class Deadlines(object):
    def __init__(self, pause_check: Callable[[], bool]):
        self.deadlines: Set['Deadline'] = set()
        self.pause_check = pause_check
        self.paused = self.should_be_paused()

    def should_be_paused(self) -> bool:
        return self.pause_check()

    def pause(self):
        from server import MapAPI

        MapAPI.instance.print("Pausing deadlines")

        for d in self.deadlines:
            d.pause()

    def resume(self):
        from server import MapAPI

        MapAPI.instance.print("Resuming deadlines")

        for d in self.deadlines:
            d.resume()

    def update(self):
        sh = self.should_be_paused()
        if self.paused != sh:
            self.paused = sh
            if self.paused:
                self.pause()
            else:
                self.resume()

    def register(self, deadline: 'Deadline'):
        self.deadlines.add(deadline)

    def unregister(self, deadline: 'Deadline'):
        self.deadlines.remove(deadline)


class Deadline(object):
    def __init__(self, deadline: int, deadlines: Deadlines):
        self.deadlines = deadlines
        if deadlines:
            if deadlines.paused:
                self.deadline: Optional[datetime] = None
                self.paused = deadline
            else:
                self.deadline: Optional[datetime] = datetime.now() + timedelta(seconds=deadline)
                self.paused: Optional[int] = None
            self.deadlines.register(self)

    def destroy(self):
        self.deadlines.unregister(self)

    def due(self) -> bool:
        if self.paused:
            return False
        return datetime.now() > self.deadline

    def remaining(self) -> int:
        if self.paused:
            return 0
        return int((self.deadline - datetime.now()).total_seconds())

    def pause(self):
        self.paused = self.remaining()
        self.deadline = None

    def resume(self):
        self.deadline = datetime.now() + timedelta(seconds=self.paused)
        self.paused = None