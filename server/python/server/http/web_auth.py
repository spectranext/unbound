import base64
import hashlib
import hmac
import json
import os
import time


TOKEN_LIFETIME_SECONDS = 30 * 24 * 60 * 60
_JWT_SECRET = os.environ.get("WEB_JWT_SECRET")
if _JWT_SECRET is None:
    _JWT_SECRET = os.urandom(32).hex()


def base64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def base64url_decode(data: str) -> bytes:
    padding = "=" * (-len(data) % 4)
    return base64.urlsafe_b64decode(data + padding)


def jwt_secret_bytes() -> bytes:
    return _JWT_SECRET.encode("utf-8")


def jwt_sign(message: bytes) -> str:
    return base64url_encode(hmac.new(jwt_secret_bytes(), message, hashlib.sha256).digest())


def jwt_encode(payload: dict) -> str:
    header = {
        "alg": "HS256",
        "typ": "JWT",
    }
    encoded_header = base64url_encode(json.dumps(header, separators=(",", ":"), sort_keys=True).encode("utf-8"))
    encoded_payload = base64url_encode(json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8"))
    signing_input = f"{encoded_header}.{encoded_payload}".encode("ascii")
    signature = jwt_sign(signing_input)
    return f"{encoded_header}.{encoded_payload}.{signature}"


def jwt_decode(token: str) -> dict:
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("Malformed JWT")

    encoded_header, encoded_payload, encoded_signature = parts
    signing_input = f"{encoded_header}.{encoded_payload}".encode("ascii")
    expected_signature = jwt_sign(signing_input)
    if not hmac.compare_digest(encoded_signature, expected_signature):
        raise ValueError("Invalid JWT signature")

    header = json.loads(base64url_decode(encoded_header).decode("utf-8"))
    if header.get("alg") != "HS256" or header.get("typ") != "JWT":
        raise ValueError("Unsupported JWT header")

    return json.loads(base64url_decode(encoded_payload).decode("utf-8"))


def generate_web_token(
    server_hash: str,
    action: str,
    name: str,
    lifetime_seconds: int = TOKEN_LIFETIME_SECONDS,
    context: dict | None = None,
) -> str:
    now = int(time.time())
    payload = {
        "sub": "server-web",
        "server_hash": server_hash,
        "action": action,
        "name": name,
        "iat": now,
        "exp": now + lifetime_seconds,
    }
    if context:
        payload.update(context)
    return jwt_encode(payload)


def verify_web_token(token: str, server_hash: str, action: str) -> dict:
    payload = jwt_decode(token)
    now = int(time.time())

    exp = payload.get("exp")
    if not isinstance(exp, int) or exp < now:
        raise ValueError("Expired JWT")

    if payload.get("sub") != "server-web":
        raise ValueError("Invalid JWT subject")
    if payload.get("server_hash") != server_hash:
        raise ValueError("Invalid JWT server hash")
    if payload.get("action") != action:
        raise ValueError("Invalid JWT action")
    name = payload.get("name")
    if not isinstance(name, str) or not name:
        raise ValueError("Invalid JWT name")

    return payload
