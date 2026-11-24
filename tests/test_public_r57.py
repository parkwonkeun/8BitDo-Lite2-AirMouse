"""Run the dual-mode checks against the exact public release image."""
import hashlib
import test_ble_gamepad as gamepad
from test_ble_controls import ROOT, decrypt
from release_inputs import R57_SHA


class PublicR57(gamepad.DualBle):
    image_name = 'Lite2AirMouse-r57-home-power-public.dat'

    def test_release_image(self):
        package = (ROOT / 'build' / self.image_name).read_bytes()
        self.assertEqual(hashlib.sha256(package).hexdigest(), R57_SHA)
        image = decrypt(package)
        self.assertEqual(image[0x15714:0x1571c], bytes([128]) * 8)
        self.assertEqual(image[-8:], bytes([255]) * 8)
        self.assertEqual(int.from_bytes(image[0x7130:0x7134], 'little'), 2000)
