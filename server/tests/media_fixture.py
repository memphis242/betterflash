"""Small, decodable media fixtures with distinct content hashes."""
import io
from PIL import Image


def png_bytes(index=0):
    output = io.BytesIO()
    Image.new("RGB", (2, 2), (index % 256, (index // 256) % 256, 42)).save(output, "PNG")
    return output.getvalue()
