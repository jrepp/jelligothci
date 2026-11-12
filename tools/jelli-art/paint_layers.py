"""Bounded authoring layers carried in the PNG; ordinary readers see its composite."""
import base64
import binascii
import json

KEY = "jelli.paint"
LIMIT = 8


def validate(document, image):
    if len(json.dumps(document)) > 512 * 1024:
        raise ValueError("Paint layer document is too large (512 KiB limit)")
    if not isinstance(document, dict) or document.get("version") != 1:
        raise ValueError("Unsupported paint layer document")
    layers = document.get("layers")
    if not isinstance(layers, list) or not 1 <= len(layers) <= LIMIT:
        raise ValueError(f"Paint needs 1–{LIMIT} layers")
    active = document.get("active")
    if type(active) is not int or not 0 <= active < len(layers):
        raise ValueError("Select a valid paint layer")
    composite = bytearray(image.width * image.height * 4)
    clean = []
    for layer in layers:
        if not isinstance(layer, dict):
            raise ValueError("Invalid paint layer")
        name = layer.get("name")
        if not isinstance(name, str) or not name.strip() or len(name) > 48:
            raise ValueError("Layer names need 1–48 characters")
        if type(layer.get("visible")) is not bool or type(layer.get("locked")) is not bool:
            raise ValueError("Layer visibility and lock must be booleans")
        try:
            encoded = layer.get("data")
            if not isinstance(encoded, str) or len(encoded) != ((len(composite) + 2) // 3) * 4:
                raise ValueError("Wrong layer dimensions")
            data = base64.b64decode(encoded, validate=True)
        except (binascii.Error, TypeError) as error:
            raise ValueError("Invalid layer pixels") from error
        if len(data) != len(composite) or any(a not in (0, 255) for a in data[3::4]):
            raise ValueError("Layers need matching dimensions and binary alpha")
        if layer["visible"]:
            for i in range(0, len(data), 4):
                if data[i + 3]:
                    composite[i:i + 4] = data[i:i + 4]
        clean.append({"name": name.strip(), "visible": layer["visible"], "locked": layer["locked"], "data": encoded})
    expected = image.tobytes()
    if any(composite[i:i + 4] != expected[i:i + 4] for i in range(0, len(expected), 4) if composite[i + 3] or expected[i + 3]):
        raise ValueError("Paint layers do not match the submitted image")
    return {"version": 1, "layers": clean, "active": active}


def read(image):
    raw = image.info.get(KEY)
    if raw is None:
        return None
    return validate(json.loads(raw), image.convert("RGBA"))


def remap(image, old, new):
    """Palette edits include hidden layers, without flattening the document."""
    doc = read(image)
    if not doc:
        return False
    changed = False
    for layer in doc["layers"]:
        data = bytearray(base64.b64decode(layer["data"]))
        for i in range(0, len(data), 4):
            if data[i + 3] and tuple(data[i:i + 3]) == old:
                data[i:i + 3] = bytes(new)
                changed = True
        layer["data"] = base64.b64encode(data).decode("ascii")
    image.info[KEY] = json.dumps(doc, separators=(",", ":"))
    return changed


def resize(image, size):
    """Creature duplication scales every layer with the same nearest-neighbour rule."""
    from PIL import Image
    doc = read(image)
    resized = image.resize(size, Image.Resampling.NEAREST)
    if doc:
        for layer in doc["layers"]:
            pixels = Image.frombytes("RGBA", image.size, base64.b64decode(layer["data"]))
            layer["data"] = base64.b64encode(pixels.resize(size, Image.Resampling.NEAREST).tobytes()).decode("ascii")
        resized.info[KEY] = json.dumps(validate(doc, resized), separators=(",", ":"))
    return resized
