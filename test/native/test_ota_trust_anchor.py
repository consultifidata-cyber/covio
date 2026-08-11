"""Pin the OTA trust anchor that the deployed fleet actually verifies against.

WHY THIS TEST EXISTS
--------------------
On 2026-08-11 an OTA publish was aborted seconds before arming, because
``ota_keys.h`` had at some point been changed to embed a different public key
than the one the commissioned Balaji meter runs against -- and the private half
of that replacement was lost. Nothing failed. Nothing warned. The firmware
compiled, CI went green, releases were cut, and the change was invisible until
someone derived the public half of the signing key by hand and compared.

Publishing a manifest at that point would have flashed an image whose trust
anchor no one can sign for, permanently ending remote updates for that device
with a USB site visit as the only recovery.

A signing key is not ordinary configuration: swapping it is irreversible from
the moment a device boots the new image. So it gets pinned here, and any change
to it has to be deliberate enough to also edit this file.

WHAT TO DO IF THIS TEST FAILS
-----------------------------
Do not "fix" it by pasting the new value in. Answer this first:

    Does someone hold the PRIVATE half of the key now in ota_keys.h,
    and is it stored somewhere it cannot be lost?

If the answer is not an unambiguous yes, the change is a fleet-wide lockout
waiting to happen -- revert it. If it is yes, update the constants below in the
same commit, and record the custody in
Docs/audit/coviu_balaji_v1_freeze/03_OTA_SIGNING_KEY_CEREMONY.md.
"""

import os
import re
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OTA_KEYS_H = os.path.join(REPO_ROOT, "ota_keys.h")

# The anchor the deployed fleet verifies against, and whose private half is
# confirmed to exist and be in the owner's possession.
EXPECTED_KEY_ID = "covio-prod-erp-2026-07"
EXPECTED_PUBKEY_B64 = (
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE7ZBAvBUFPk3RHkxuruueklHr7yGk"
    "N4PmjI1ZpcfWt7lUd1udOsMBSsXN5rB6xiXY7/xc4c05vppYTISWdrWLEg=="
)

# The 2026-07-26 ceremony key. Its private half was written to a laptop, never
# moved to a vault, and is gone. It must never come back without its key.
LOST_PUBKEY_PREFIX = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE3dSVWNuB"


def _read_ota_keys():
    with open(OTA_KEYS_H, encoding="utf-8") as f:
        return f.read()


class TestOtaTrustAnchor(unittest.TestCase):
    def test_key_id_matches_the_deployed_fleet(self):
        m = re.search(r'#define\s+COVIO_OTA_KEY_ID\s+"([^"]+)"', _read_ota_keys())
        self.assertIsNotNone(m, "COVIO_OTA_KEY_ID not found in ota_keys.h")
        self.assertEqual(
            m.group(1),
            EXPECTED_KEY_ID,
            "OTA key_id changed. The device rejects UNKNOWN_KEY_ID on any mismatch, "
            "so this silently disables OTA for every deployed unit. Read this "
            "file's docstring before changing it.",
        )

    def test_public_key_is_the_one_we_can_sign_for(self):
        m = re.search(r"BEGIN PUBLIC KEY-----(.*?)-----END PUBLIC KEY", _read_ota_keys(), re.S)
        self.assertIsNotNone(m, "No PUBLIC KEY block found in ota_keys.h")
        actual = re.sub(r"[^A-Za-z0-9+/=]", "", m.group(1))
        self.assertEqual(
            actual,
            EXPECTED_PUBKEY_B64,
            "The OTA public key changed. Every device that boots an image built "
            "from this tree will trust ONLY this key from then on. If the private "
            "half is not held and safely stored, this is a permanent fleet "
            "lockout recoverable only by visiting each unit with a USB cable.",
        )

    def test_the_lost_ceremony_key_has_not_returned(self):
        actual = re.sub(
            r"[^A-Za-z0-9+/=]",
            "",
            re.search(
                r"BEGIN PUBLIC KEY-----(.*?)-----END PUBLIC KEY",
                _read_ota_keys(),
                re.S,
            ).group(1),
        )
        self.assertFalse(
            actual.startswith(LOST_PUBKEY_PREFIX),
            "ota_keys.h embeds the 2026-07-26 ceremony key, whose private half is "
            "LOST. An image built this way can never be updated remotely again.",
        )

    def test_placeholder_guard_is_off_so_release_builds_compile(self):
        m = re.search(r"#define\s+COVIO_OTA_KEY_IS_PLACEHOLDER\s+(\d+)", _read_ota_keys())
        self.assertIsNotNone(m, "COVIO_OTA_KEY_IS_PLACEHOLDER not found")
        self.assertEqual(
            m.group(1),
            "0",
            "RELEASE_BUILD refuses to compile while this is 1 (ota_keys.h's #error).",
        )


if __name__ == "__main__":
    unittest.main()
