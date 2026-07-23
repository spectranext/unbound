from typing import Any, Dict, Optional, Union
import os
import pickle
import secrets

from . import loc


AUTH_TOKENS = os.environ.get("AUTH_TOKENS", "/tmp/unbound-auth-tokens")
AUTH_TOKEN_BYTES = 32


class ClientAPIAuthResult(object):
    def __init__(self, user_id: bytes, user_name: bytes, token: bytes):
        self.user_id = user_id
        self.user_name = user_name
        self.token = token


auth_db: Optional[Dict[str, Any]] = None


def _empty_auth_db() -> Dict[str, Any]:
    return {
        "next_user_id": 0,
        "tokens": {},
        "users": {},
    }


def _to_str(value: Union[bytes, str, None]) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _to_bytes(value: Union[bytes, str]) -> bytes:
    if isinstance(value, bytes):
        return value
    return value.encode()


def _normalize_auth_db(db: Any) -> Dict[str, Any]:
    if not isinstance(db, dict):
        return _empty_auth_db()

    tokens = db.get("tokens")
    users = db.get("users")
    next_user_id = db.get("next_user_id", 0)

    if not isinstance(tokens, dict):
        tokens = {}
    if not isinstance(users, dict):
        users = {}
    if not isinstance(next_user_id, int):
        next_user_id = 0

    normalized = _empty_auth_db()
    normalized["next_user_id"] = next_user_id
    normalized["tokens"] = {
        _to_str(token): _to_str(user_id)
        for token, user_id in tokens.items()
    }

    for user_id, user in users.items():
        user_id_str = _to_str(user_id)
        if isinstance(user, dict):
            token = _to_str(user.get("token"))
            name = _to_str(user.get("name", user_id_str))
        else:
            token = ""
            name = user_id_str
        normalized["users"][user_id_str] = {
            "name": name or user_id_str,
            "token": token,
        }
        if token:
            normalized["tokens"][token] = user_id_str

    return normalized


def _load_auth_db() -> Dict[str, Any]:
    try:
        with open(AUTH_TOKENS, "rb") as f:
            return _normalize_auth_db(pickle.load(f))
    except FileNotFoundError:
        return _empty_auth_db()


def _get_auth_db() -> Dict[str, Any]:
    global auth_db
    if auth_db is None:
        auth_db = _load_auth_db()
    return auth_db


def _save_auth_db() -> None:
    if auth_db is None:
        return

    auth_dir = os.path.dirname(AUTH_TOKENS)
    if auth_dir:
        os.makedirs(auth_dir, exist_ok=True)

    tmp_path = "{0}.tmp".format(AUTH_TOKENS)
    with open(tmp_path, "wb") as f:
        pickle.dump(auth_db, f)
    os.replace(tmp_path, AUTH_TOKENS)


def _generate_token(db: Dict[str, Any]) -> str:
    while True:
        token = secrets.token_hex(AUTH_TOKEN_BYTES)
        if token not in db["tokens"]:
            return token


def _allocate_user_id(db: Dict[str, Any]) -> str:
    next_user_id = db.get("next_user_id", 0)
    while True:
        next_user_id += 1
        user_id = "user{0}".format(next_user_id)
        if user_id not in db["users"]:
            db["next_user_id"] = next_user_id
            return user_id


def auth_client(token: bytes) -> Union[ClientAPIAuthResult, bytes]:
    try:
        db = _get_auth_db()
        token_str = _to_str(token)
        user_id = db["tokens"].get(token_str)

        if user_id is not None and user_id in db["users"]:
            user = db["users"][user_id]
            return ClientAPIAuthResult(
                _to_bytes(user_id),
                _to_bytes(user.get("name", user_id)),
                _to_bytes(token_str),
            )

        if user_id is not None:
            db["tokens"].pop(token_str, None)

        new_token = _generate_token(db)
        new_user_id = _allocate_user_id(db)
        db["tokens"][new_token] = new_user_id
        db["users"][new_user_id] = {
            "name": new_user_id,
            "token": new_token,
        }
        _save_auth_db()

        return ClientAPIAuthResult(
            _to_bytes(new_user_id),
            _to_bytes(new_user_id),
            _to_bytes(new_token),
        )
    except Exception:
        return loc.INTERNAL_ERROR.encode()


def auth_set_name(user_id: Union[bytes, str], name: Union[bytes, str]) -> bool:
    db = _get_auth_db()
    user_id_str = _to_str(user_id)
    if user_id_str not in db["users"]:
        return False

    db["users"][user_id_str]["name"] = _to_str(name)
    _save_auth_db()
    return True
