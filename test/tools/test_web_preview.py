"""Desktop integration tests for generated assets and mock HTTP workflows."""

from __future__ import annotations

import base64
import html
import http.client
import json
import sys
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


PROJECT_ROOT = Path(__file__).resolve().parents[2]
TOOLS_ROOT = PROJECT_ROOT / "tools"
sys.path.insert(0, str(TOOLS_ROOT))

import dev_server  # noqa: E402
import generate_web_assets  # noqa: E402


class WebPreviewTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = dev_server.create_server(port=0, quiet=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"
        token = base64.b64encode(b"postie:change-me-postie").decode("ascii")
        self.admin_headers = {"Authorization": f"Basic {token}"}

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(
        self,
        path: str,
        *,
        method: str = "GET",
        form: dict[str, object] | None = None,
        headers: dict[str, str] | None = None,
    ) -> tuple[int, bytes, dict[str, str]]:
        request_headers = dict(headers or {})
        data = None
        if form is not None:
            data = urlencode(form).encode("utf-8")
            request_headers["Content-Type"] = "application/x-www-form-urlencoded"
        request = Request(
            self.base_url + path,
            data=data,
            headers=request_headers,
            method=method,
        )
        try:
            with urlopen(request, timeout=3) as response:
                return response.status, response.read(), dict(response.headers)
        except HTTPError as error:
            return error.code, error.read(), dict(error.headers)

    def request_json(self, path: str, **kwargs: object) -> tuple[int, object]:
        status, body, _ = self.request(path, **kwargs)
        return status, json.loads(body.decode("utf-8"))

    def test_generated_assets_match_canonical_web_files(self) -> None:
        for path, expected in generate_web_assets.generated_outputs().items():
            self.assertEqual(expected, path.read_text(encoding="utf-8"), path.name)

        for path in (PROJECT_ROOT / "web").iterdir():
            if path.suffix not in {".html", ".css"}:
                continue
            content = path.read_text(encoding="utf-8").lower()
            self.assertNotIn("http://", content, path.name)
            self.assertNotIn("https://", content, path.name)
            self.assertNotIn(chr(0x2014), content, path.name)
            self.assertNotIn("&" + "mdash;", content, path.name)

        index = (PROJECT_ROOT / "web" / "index.html").read_text(encoding="utf-8")
        for category in dev_server.NOTICE_CATEGORIES:
            self.assertIn(f"<option>{html.escape(category)}</option>", index)

    def test_pages_styles_and_postie_authentication_are_served(self) -> None:
        for path in ("/", "/letters", "/track", "/style.css", "/logo.svg"):
            status, body, headers = self.request(path)
            self.assertEqual(200, status, path)
            self.assertGreater(len(body), 100, path)
            self.assertIn("Content-Type", headers)

        status, body, headers = self.request("/logo.svg")
        self.assertEqual("image/svg+xml", headers["Content-Type"])
        self.assertIn(b"Strawberry Post logo", body)

        status, _, headers = self.request("/postie")
        self.assertEqual(401, status)
        self.assertIn("Basic", headers["WWW-Authenticate"])
        status, body, _ = self.request("/postie", headers=self.admin_headers)
        self.assertEqual(200, status)
        self.assertIn(b"Sorting Room", body)

        status, body, _ = self.request("/")
        self.assertEqual(200, status)
        self.assertIn(b"Notice Board", body)
        self.assertIn(b"Missed Connection", body)
        self.assertNotIn(b"A tiny rural postal service", body)

        status, body, _ = self.request("/letters")
        self.assertEqual(200, status)
        self.assertIn(b"tracking number", body.lower())
        self.assertIn(b"first stranger they meet", body)
        self.assertIn(b"not for a specific person", body)
        self.assertNotIn(b"Claim ticket", body)

        status, tracking_body, _ = self.request("/track")
        self.assertEqual(200, status)
        self.assertIn(b"Track my letter", tracking_body)
        self.assertIn(b"STRAW-", tracking_body)
        self.assertIn(b'maxlength="4"', tracking_body)
        self.assertNotIn(b'id="send"', tracking_body)
        self.assertNotIn(b'id="track"', body)

    def test_notice_board_supports_missed_connection_category(self) -> None:
        status, created = self.request_json(
            "/api/notices",
            method="POST",
            form={"category": "Missed Connection", "message": "Desktop notice 🍓"},
        )
        self.assertEqual(201, status)
        self.assertGreater(created["id"], 0)
        status, notices = self.request_json("/api/notices")
        self.assertEqual(200, status)
        self.assertTrue(any(item["message"] == "Desktop notice 🍓" for item in notices["notices"]))

        created_notice = next(
            item for item in notices["notices"] if item["message"] == "Desktop notice 🍓"
        )
        self.assertEqual("Missed Connection", created_notice["category"])

    def test_letter_tracking_is_public_but_contents_require_authentication(self) -> None:
        private_message = "A private desktop test message"
        status, created = self.request_json(
            "/api/letters",
            method="POST",
            form={
                "recipient": "Casey",
                "location": "Postbox",
                "message": private_message,
                "sender": "Riley",
            },
        )
        self.assertEqual(201, status)
        tracking = created["tracking"]

        status, public_status = self.request_json(
            f"/api/letters/status?tracking={tracking}"
        )
        self.assertEqual(200, status)
        self.assertEqual({"tracking": tracking, "status": "Waiting"}, public_status)
        self.assertNotIn(private_message, json.dumps(public_status))

        status, _, _ = self.request("/api/admin/overview")
        self.assertEqual(401, status)
        status, overview = self.request_json(
            "/api/admin/overview", headers=self.admin_headers
        )
        self.assertEqual(200, status)
        self.assertTrue(any(item["message"] == private_message for item in overview["letters"]))

    def test_admin_status_and_moderation_update_public_results(self) -> None:
        status, _ = self.request_json(
            "/api/admin/letters/status",
            method="POST",
            form={"id": 1, "status": "Delivered"},
            headers=self.admin_headers,
        )
        self.assertEqual(200, status)
        status, tracked = self.request_json("/api/letters/status?tracking=STRAW-0427")
        self.assertEqual(200, status)
        self.assertEqual("Delivered", tracked["status"])

        status, _ = self.request_json(
            "/api/admin/notices/moderate",
            method="POST",
            form={"id": 1, "action": "hide"},
            headers=self.admin_headers,
        )
        self.assertEqual(200, status)
        _, notices = self.request_json("/api/notices")
        self.assertFalse(any(item["id"] == 1 for item in notices["notices"]))

    def test_postie_can_delete_a_letter(self) -> None:
        status, overview = self.request_json(
            "/api/admin/overview", headers=self.admin_headers
        )
        self.assertEqual(200, status)
        letter = overview["letters"][0]

        status, _ = self.request_json(
            "/api/admin/letters/delete",
            method="POST",
            form={"id": letter["id"]},
            headers=self.admin_headers,
        )
        self.assertEqual(200, status)

        status, overview = self.request_json(
            "/api/admin/overview", headers=self.admin_headers
        )
        self.assertEqual(200, status)
        self.assertFalse(any(item["id"] == letter["id"] for item in overview["letters"]))

        status, error = self.request_json(
            f"/api/letters/status?tracking={letter['tracking']}"
        )
        self.assertEqual(404, status)
        self.assertIn("error", error)

    def test_malformed_form_encoding_and_negative_length_are_rejected(self) -> None:
        for body in (b"", b"category=General&message=%FF"):
            connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
            try:
                connection.request("POST", "/api/notices", body=body, headers={
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Content-Length": str(len(body)) if body else "-1",
                })
                response = connection.getresponse()
                self.assertEqual(400, response.status)
                self.assertIn("error", json.loads(response.read()))
            finally:
                connection.close()

    def test_form_limits_and_errors_match_firmware_shape(self) -> None:
        status, error = self.request_json(
            "/api/notices",
            method="POST",
            form={"category": "", "message": "missing category"},
        )
        self.assertEqual(400, status)
        self.assertIn("error", error)

        status, error = self.request_json(
            "/api/notices",
            method="POST",
            form={"category": "Not a category", "message": "unknown category"},
        )
        self.assertEqual(400, status)
        self.assertIn("valid notice category", error["error"])

        status, error = self.request_json(
            "/api/notices",
            method="POST",
            form={"category": "General", "message": "x" * 513},
        )
        self.assertEqual(413, status)
        self.assertIn("error", error)


if __name__ == "__main__":
    unittest.main()
