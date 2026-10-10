"""Parse and check Jelli Art request bodies, so every bad request gets a clear 4xx.

The studio has no login (tailnet membership is the boundary, ADR-011), so a
write must declare its body as application/json. A browser only sends that
cross-site after a CORS preflight, which the studio never grants, so another
web page an artist visits cannot post saves (a plain form post is refused 415).
"""
import json
from http import HTTPStatus


class RequestError(ValueError):
    """A request the studio refuses before acting on it; carries its HTTP status."""

    def __init__(self, message, status=HTTPStatus.BAD_REQUEST):
        super().__init__(message)
        self.status = status


def read_json(headers, stream, limit):
    """The JSON object a POST carries; RequestError with 400/411/413/415 otherwise."""
    kind = headers.get("Content-Type", "").split(";")[0].strip().lower()
    if kind != "application/json":
        raise RequestError("Send the body as Content-Type: application/json", HTTPStatus.UNSUPPORTED_MEDIA_TYPE)
    raw_length = headers.get("Content-Length")
    if raw_length is None:
        raise RequestError("Content-Length is required", HTTPStatus.LENGTH_REQUIRED)
    try:
        length = int(raw_length)
    except ValueError:
        raise RequestError("Content-Length must be a number") from None
    if length < 0:
        raise RequestError("Content-Length must not be negative")
    if length > limit:
        if length <= 16 * limit:  # drain a modest overshoot so the client reads the 413 instead of a reset
            remaining = length
            while remaining > 0 and (chunk := stream.read(min(remaining, 1 << 16))):
                remaining -= len(chunk)
        raise RequestError(f"Request body is {length} bytes; the limit is {limit}", HTTPStatus.REQUEST_ENTITY_TOO_LARGE)
    data = stream.read(length)
    if len(data) != length:
        raise RequestError("Request body ended early")
    try:
        body = json.loads(data.decode("utf-8")) if data else {}
    except (UnicodeDecodeError, ValueError) as error:
        raise RequestError(f"Request body is not valid JSON: {error}") from None
    if not isinstance(body, dict):
        raise RequestError("Request body must be a JSON object")
    return body


_TYPES = {str: "a string", int: "an integer", list: "a list", dict: "an object", bool: "true or false"}


def field(body, name, kind, required=True, default=None):
    """body[name] checked against kind (a type or tuple of types); bool is never accepted as int."""
    if name not in body or body[name] is None:
        if required:
            raise RequestError(f"Missing field '{name}'")
        return default
    value = body[name]
    kinds = kind if isinstance(kind, tuple) else (kind,)
    if isinstance(value, bool) and bool not in kinds or not isinstance(value, kinds):
        raise RequestError(f"Field '{name}' must be {' or '.join(_TYPES.get(k, k.__name__) for k in kinds)}")
    return value


def artist(body):
    value = body.get("artist", "")
    return value if isinstance(value, str) else ""
