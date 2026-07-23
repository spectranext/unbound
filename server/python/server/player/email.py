from typing import List, Dict, Tuple, Optional, TYPE_CHECKING, Callable

from .. api.query import QueryResponse, OPT, ACT
from .. import loc, icons


if TYPE_CHECKING:
    from . client import Client


class Email(object):
    def __init__(self, email_id: int, subject: str, bodies: List[str], image: bytes = None,
                 action_name: bytes = None, action: Callable[[], Optional[QueryResponse]] = None):
        self.email_id = email_id
        self.subject = subject
        self.bodies = bodies
        self.read = False
        self.image = image
        self.action_name: Optional[bytes] = action_name
        self.action = action

    def set_read(self):
        self.read = True

    def is_read(self) -> bool:
        return self.read

    def has_action(self) -> bool:
        return self.action_name is not None and self.action is not None

    def trigger_action(self) -> Optional[QueryResponse]:
        if not self.has_action():
            return None
        cb = self.action
        self.action = None
        self.action_name = None
        return cb()


class EmailInbox(object):
    def __init__(self):
        self.next_id = 1
        self.emails: List[Email] = []
        self.index: Dict[int, Email] = {}

    def fetch_list(self) -> List[Email]:
        return self.emails

    def fetch_email(self, email_id: int) -> Optional[Email]:
        return self.index.get(email_id, None)

    def count_total(self) -> int:
        return len(self.emails)

    def count_unread(self) -> int:
        return sum(1 for x in self.emails if not x.read)

    def add_email(self, subject: str, bodies: List[str], image: bytes = None,
                  action_name: bytes = None, action: Callable[[], Optional[QueryResponse]] = None) -> Email:
        e = Email(self.next_id, subject, bodies, image, action_name, action)
        self.next_id += 1
        self.emails.append(e)
        self.index[e.email_id] = e
        return e

    def remove_email(self, email_id: int):
        e = self.index.get(email_id)
        if not e:
            return
        del self.index[email_id]
        self.emails.remove(e)

    def fetch_top_email(self) -> Optional[Email]:
        if self.emails:
            return self.emails[-1]
        return None


class EmailSession(QueryResponse):
    def __init__(self, client: 'Client', selected_email: int = 0, page: int = 0):

        if selected_email:
            email: Email = client.email_inbox.fetch_email(selected_email)
        else:
            email: Email = client.email_inbox.fetch_top_email()

        email.set_read()

        super().__init__(b"", loc.STATUS_EMAIL_SUMMARY.format(
            str(client.email_inbox.count_total()),
            str(client.email_inbox.count_unread()),
        ).encode())

        self.page = page
        self.email = email
        self.client = client
        if email.image:
            self.image = email.image

        self.description = email.bodies[page].encode()
        if len(email.bodies) > 1:
            self.description += loc.EMAIL_PAGE.format(page + 1, len(email.bodies)).encode()
        self.flags |= QueryResponse.FLAG_MESSAGE_TO_SIDE

        emails = list(reversed(self.client.email_inbox.fetch_list()))

        self.options = [
            OPT(email.subject, ACT(self.select_email, email.email_id),
                icon=icons.ICON_EMAIL_READ if email.is_read() else icons.ICON_EMAIL)
            for email in emails
        ]

        self.current = emails.index(email)
        if email and email.has_action():
            self.actions = [email.action_name]
        else:
            self.actions = [loc.OK.encode()]

    def select_email(self, action: bytes, email_id: int) -> QueryResponse:
        if email_id == self.email.email_id:
            if self.email.has_action() and action == self.email.action_name:
                return self.email.trigger_action()
            page = self.page + 1
            if page >= len(self.email.bodies):
                page = 0
        else:
            page = 0
        return EmailSession(self.client, email_id, page=page)
