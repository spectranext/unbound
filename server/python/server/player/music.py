from typing import Optional
from .. api.query import QueryResponse, QueryResponseOption, OPT, NOACT
from .. api.map import MapAPI
from . import PlayerObject
from .. import loc
from .. qr import qr_image


class QRLinkQueryResponse(QueryResponse):
    def __init__(self, link: str):
        super().__init__(b"", b"QR Link")
        self.image = qr_image(link).bake()
        self.actions = [b"OK"]


class MusicQueryResponse(QueryResponse):
    def __init__(self, p: PlayerObject):
        super().__init__(b"", b"Music")
        self.description = p.client.get_current_music_name()
        self.link = p.client.get_current_music_link()
        self.p = p
        self.options = [
            OPT(loc.MUSIC_PLAY_NEXT, self.play_next),
            OPT(loc.MUSIC_QR_LINK, self.qr_link),
            OPT(loc.MUSIC_SHUFFLE, self.shuffle),
            OPT(loc.MUSIC_STOP, self.stop),
        ]
        self.actions = [b"OK"]

    def qr_link(self, action: bytes) -> Optional[QueryResponse]:
        return QRLinkQueryResponse(self.link)

    def play_next(self, action: bytes) -> Optional[QueryResponse]:
        def delay():
            self.p.client.music_enabled = True
            self.p.client.play_playlist()
        MapAPI.instance.schedule_callback(delay, 100)
        return None

    def stop(self, action: bytes) -> Optional[QueryResponse]:
        def delay():
            self.p.client.music_enabled = False
            self.p.client.stop_music()
        MapAPI.instance.schedule_callback(delay, 100)
        return None

    def shuffle(self, action: bytes) -> Optional[QueryResponse]:
        def delay():
            self.p.client.clear_playlist()
            self.p.client.play_playlist()
        MapAPI.instance.schedule_callback(delay, 100)
        return None
