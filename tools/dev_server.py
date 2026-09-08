"""Serve Strawberry Post web assets with deterministic in-memory mock APIs."""

from __future__ import annotations

import argparse
import base64
import json
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlsplit


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WEB_ROOT = PROJECT_ROOT / "web"
ADMIN_USERNAME = "postie"
ADMIN_PASSWORD = "change-me-postie"
ADMIN_AUTHORIZATION = "Basic " + base64.b64encode(
    f"{ADMIN_USERNAME}:{ADMIN_PASSWORD}".encode("ascii")
).decode("ascii")

MAX_FORM_BYTES = 4096
MAX_FORM_FIELDS = 8
LIMITS = {
    "notice_category": 32,
    "notice_message": 512,
    "letter_recipient": 160,
    "letter_location": 96,
    "letter_message": 768,
    "letter_sender": 96,
}
NOTICE_CATEGORIES = {
    "General",
    "Missed Connection",
    "Lost & Found",
    "Event / Schedule",
    "Ride Share",
    "Help Wanted",
    "For Sale / Swap",
}
LETTER_STATUSES = {
    "Waiting",
    "Written",
    "OutForDelivery",
    "Delivered",
    "CouldNotFind",
}

PAGE_FILES = {
    "/": "index.html",
    "/letters": "letters.html",
    "/track": "track.html",
    "/postie": "postie.html",
    "/postie/diagnostics": "diagnostics.html",
}
CAPTIVE_PATHS = {
    "/generate_204",
    "/gen_204",
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/ncsi.txt",
    "/connecttest.txt",
    "/redirect",
}


def byte_length(value: str) -> int:
    return len(value.encode("utf-8"))


def valid_text(value: str, allow_newlines: bool = True) -> bool:
    for character in value:
        code_point = ord(character)
        if code_point < 0x20 or code_point == 0x7F:
            if not (allow_newlines and character in "\n\r\t"):
                return False
    return True


class MockState:
    """Thread-safe, restart-to-reset state for browser previewing."""

    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.started_at = time.monotonic()
        self.request_count = 0
        self.last_uri = "/"
        self.next_notice_id = 3
        self.next_letter_id = 4
        self.next_tracking_number = 430
        self.total_notices_submitted = 2
        self.total_letters_submitted = 3
        self.device_time_set = False
        self.notice_storage_backend = "sd"
        self.letter_storage_backend = "sd"
        self.notice_storage_readable = True
        self.letter_storage_readable = True
        self.notice_storage_writable = True
        self.letter_storage_writable = True
        self.storage_issue = "none"
        self.notices: list[dict[str, Any]] = [
            {
                "id": 1,
                "category": "General",
                "message": "The mock notice board is ready for browser testing.",
                "hidden": False,
            },
            {
                "id": 2,
                "category": "Missed Connection",
                "message": "To the person in the red hat: we traded excellent dance recommendations and I forgot your name.",
                "hidden": False,
            },
        ]
        self.letters: list[dict[str, Any]] = [
            {
                "id": 1,
                "tracking": "STRAW-0427",
                "recipient": "Alex",
                "location": "Near the main stage",
                "message": "Meet us by the red flags after the next set.",
                "sender": "Sam",
                "status": "Waiting",
            },
            {
                "id": 2,
                "tracking": "STRAW-0428",
                "recipient": "Jo",
                "location": "Camp kitchen",
                "message": "Your raincoat is at the Postie desk.",
                "sender": "",
                "status": "OutForDelivery",
            },
            {
                "id": 3,
                "tracking": "STRAW-0429",
                "recipient": "Morgan",
                "location": "Volunteer tent",
                "message": "Thanks for the strawberries!",
                "sender": "Taylor",
                "status": "Delivered",
            },
        ]

    def record_request(self, uri: str) -> None:
        with self.lock:
            self.request_count += 1
            self.last_uri = uri

    def stats(self) -> dict[str, Any]:
        with self.lock:
            status_count = lambda status: sum(
                1 for letter in self.letters if letter["status"] == status
            )
            return {
                "lettersSubmitted": self.total_letters_submitted,
                "lettersWaiting": status_count("Waiting"),
                "lettersOutForDelivery": status_count("OutForDelivery"),
                "lettersDelivered": status_count("Delivered"),
                "noticesActive": sum(1 for item in self.notices if not item["hidden"]),
                "noticesSubmitted": self.total_notices_submitted,
                "noticePostingAvailable": self.notice_storage_writable,
                "letterPostingAvailable": self.letter_storage_writable,
                "noticeHistoryComplete": self.notice_storage_readable,
                "letterHistoryComplete": self.letter_storage_readable,
                "letterLookupAvailable": self.letter_storage_readable or bool(self.letters),
                "storageInterrupted": self.storage_issue != "none",
            }

    def public_notices(self, before_id: int = 0) -> dict[str, Any]:
        with self.lock:
            visible = [
                {
                    "id": item["id"],
                    "category": item["category"],
                    "message": item["message"],
                    "createdAtEpochSeconds": item.get("createdAtEpochSeconds", 0),
                }
                for item in self.notices
                if not item["hidden"] and (not before_id or item["id"] < before_id)
            ]
            visible.sort(key=lambda item: item["id"], reverse=True)
            page = visible[:8]
            return {
                "notices": page,
                "totalSubmitted": self.total_notices_submitted,
                "nextBeforeId": page[-1]["id"] if len(visible) > len(page) else 0,
            }

    def create_notice(self, category: str, message: str) -> int:
        with self.lock:
            record_id = self.next_notice_id
            self.next_notice_id += 1
            self.total_notices_submitted += 1
            self.notices.append(
                {"id": record_id, "category": category, "message": message, "hidden": False}
            )
            return record_id

    def create_letter(
        self, recipient: str, location: str, message: str, sender: str
    ) -> str | None:
        with self.lock:
            used = {letter["tracking"] for letter in self.letters}
            for _ in range(10_000):
                tracking = f"STRAW-{self.next_tracking_number:04d}"
                self.next_tracking_number = (self.next_tracking_number + 1) % 10_000
                if tracking not in used:
                    break
            else:
                return None

            record_id = self.next_letter_id
            self.next_letter_id += 1
            self.total_letters_submitted += 1
            self.letters.append(
                {
                    "id": record_id,
                    "tracking": tracking,
                    "recipient": recipient,
                    "location": location,
                    "message": message,
                    "sender": sender,
                    "status": "Waiting",
                }
            )
            return tracking

    def tracking_status(self, tracking: str) -> dict[str, str] | None:
        with self.lock:
            for letter in self.letters:
                if letter["tracking"] == tracking:
                    return {"tracking": tracking, "status": str(letter["status"])}
        return None

    def overview(self, letter_before: int = 0, notice_before: int = 0) -> dict[str, Any]:
        with self.lock:
            letters = sorted(
                (dict(item) for item in self.letters if not letter_before or item["id"] < letter_before),
                key=lambda item: item["id"],
                reverse=True,
            )
            notices = sorted(
                (dict(item) for item in self.notices if not notice_before or item["id"] < notice_before),
                key=lambda item: item["id"],
                reverse=True,
            )
            letter_page = letters[:6]
            notice_page = notices[:8]
            return {
                "letters": letter_page,
                "notices": notice_page,
                "nextLetterBeforeId": letter_page[-1]["id"] if len(letters) > len(letter_page) else 0,
                "nextNoticeBeforeId": notice_page[-1]["id"] if len(notices) > len(notice_page) else 0,
                "deviceTimeSet": self.device_time_set,
                "deviceEpochSeconds": int(time.time()) if self.device_time_set else 0,
                "noticeStorageBackend": self.notice_storage_backend,
                "noticeStorageReadable": self.notice_storage_readable,
                "noticeStorageWritable": self.notice_storage_writable,
                "letterStorageBackend": self.letter_storage_backend,
                "letterStorageReadable": self.letter_storage_readable,
                "letterStorageWritable": self.letter_storage_writable,
                "storageIssue": self.storage_issue,
            }

    def update_letter(self, record_id: int, status: str) -> bool:
        with self.lock:
            for letter in self.letters:
                if letter["id"] == record_id:
                    letter["status"] = status
                    return True
        return False

    def delete_letter(self, record_id: int) -> bool:
        with self.lock:
            for index, letter in enumerate(self.letters):
                if letter["id"] == record_id:
                    self.letters.pop(index)
                    return True
        return False

    def moderate(self, collection_name: str, record_id: int, action: str) -> bool:
        with self.lock:
            collection: list[dict[str, Any]] = getattr(self, collection_name)
            for index, item in enumerate(collection):
                if item["id"] != record_id:
                    continue
                if action == "delete":
                    collection.pop(index)
                elif action in {"hide", "unhide"}:
                    item["hidden"] = action == "hide"
                else:
                    return False
                return True
        return False

    def diagnostics(self) -> dict[str, Any]:
        with self.lock:
            return {
                "mode": "desktop-mock",
                "uptimeMs": int((time.monotonic() - self.started_at) * 1000),
                "requestCount": self.request_count,
                "lastUri": self.last_uri,
                "connectedStations": 0,
                "maxStations": 8,
                "freeHeap": "not available on desktop",
                "storage": "in-memory; resets when server stops",
                "deviceTimeSet": self.device_time_set,
                "deviceEpochSeconds": int(time.time()) if self.device_time_set else 0,
                "noticeStorageBackend": self.notice_storage_backend,
                "noticeStorageReadable": self.notice_storage_readable,
                "noticeStorageWritable": self.notice_storage_writable,
                "letterStorageBackend": self.letter_storage_backend,
                "letterStorageReadable": self.letter_storage_readable,
                "letterStorageWritable": self.letter_storage_writable,
                "storageIssue": self.storage_issue,
                "lettersStored": len(self.letters),
                "noticesStored": len(self.notices),
            }


class PreviewServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], quiet: bool = False) -> None:
        self.state = MockState()
        self.quiet = quiet
        super().__init__(address, PreviewHandler)


class PreviewHandler(BaseHTTPRequestHandler):
    server: PreviewServer

    def log_message(self, format_string: str, *args: object) -> None:
        if not self.server.quiet:
            print(f"{self.address_string()} - {format_string % args}")

    def send_bytes(
        self,
        status: int,
        content_type: str,
        content: bytes,
        extra_headers: dict[str, str] | None = None,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("X-Content-Type-Options", "nosniff")
        if extra_headers:
            for name, value in extra_headers.items():
                self.send_header(name, value)
        self.end_headers()
        self.wfile.write(content)

    def send_json(self, status: int, value: Any) -> None:
        content = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_bytes(
            status,
            "application/json; charset=utf-8",
            content,
            {"Cache-Control": "no-store"},
        )

    def send_error_json(self, status: int, message: str) -> None:
        self.send_json(status, {"error": message})

    def authenticated(self) -> bool:
        if self.headers.get("Authorization") == ADMIN_AUTHORIZATION:
            return True
        content = b"Postie authentication required.\n"
        self.send_bytes(
            HTTPStatus.UNAUTHORIZED,
            "text/plain; charset=utf-8",
            content,
            {"WWW-Authenticate": 'Basic realm="Strawberry Post Postie"'},
        )
        return False

    def serve_web_file(self, filename: str, content_type: str) -> None:
        content = (WEB_ROOT / filename).read_bytes()
        cache = "public, max-age=3600" if filename.endswith(".css") else "no-store"
        self.send_bytes(HTTPStatus.OK, content_type, content, {"Cache-Control": cache})

    def read_form(self) -> dict[str, str] | None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid Content-Length")
            return None
        if length < 0:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid Content-Length")
            return None
        if length > MAX_FORM_BYTES:
            self.send_error_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Request is too large")
            return None
        content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
        if content_type != "application/x-www-form-urlencoded":
            self.send_error_json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, "Expected form data")
            return None
        try:
            raw = self.rfile.read(length).decode("utf-8", errors="strict")
            parsed = parse_qs(raw, keep_blank_values=True, max_num_fields=MAX_FORM_FIELDS, errors="strict")
        except (UnicodeDecodeError, ValueError):
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid form data")
            return None
        return {name: values[0] for name, values in parsed.items()}

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        path = parsed.path
        self.server.state.record_request(self.path)

        if path in {"/postie", "/postie/diagnostics"} and not self.authenticated():
            return
        if path in PAGE_FILES:
            self.serve_web_file(PAGE_FILES[path], "text/html; charset=utf-8")
            return
        if path in CAPTIVE_PATHS:
            self.serve_web_file("index.html", "text/html; charset=utf-8")
            return
        if path == "/style.css":
            self.serve_web_file("style.css", "text/css; charset=utf-8")
            return
        if path == "/logo.svg":
            self.serve_web_file("logo.svg", "image/svg+xml")
            return
        if path == "/api/stats":
            self.send_json(HTTPStatus.OK, self.server.state.stats())
            return
        if path == "/api/notices":
            try:
                before_id = int(parse_qs(parsed.query).get("before", ["0"])[0])
            except ValueError:
                before_id = 0
            self.send_json(HTTPStatus.OK, self.server.state.public_notices(before_id))
            return
        if path == "/api/letters/status":
            tracking = parse_qs(parsed.query).get("tracking", [""])[0].strip().upper()
            if not tracking or len(tracking) > 10 or not valid_text(tracking, False):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "A valid tracking code is required")
                return
            status = self.server.state.tracking_status(tracking)
            if status is None:
                self.send_error_json(HTTPStatus.NOT_FOUND, "Tracking code not found")
            else:
                self.send_json(HTTPStatus.OK, status)
            return
        if path.startswith("/api/admin/"):
            if not self.authenticated():
                return
            if path == "/api/admin/overview":
                query = parse_qs(parsed.query)
                try:
                    letter_before = int(query.get("letterBefore", ["0"])[0])
                    notice_before = int(query.get("noticeBefore", ["0"])[0])
                except ValueError:
                    letter_before = notice_before = 0
                self.send_json(
                    HTTPStatus.OK,
                    self.server.state.overview(letter_before, notice_before),
                )
                return
            if path == "/api/admin/diagnostics":
                self.send_json(HTTPStatus.OK, self.server.state.diagnostics())
                return

        self.send_response(HTTPStatus.FOUND)
        self.send_header("Location", "/")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self) -> None:
        path = urlsplit(self.path).path
        self.server.state.record_request(self.path)
        if path.startswith("/api/admin/") and not self.authenticated():
            return
        form = self.read_form()
        if form is None:
            return

        if path == "/api/notices":
            if not self.server.state.notice_storage_writable:
                self.send_error_json(
                    HTTPStatus.SERVICE_UNAVAILABLE, "Persistent storage is unavailable"
                )
                return
            category = form.get("category", "").strip()
            message = form.get("message", "").strip()
            if not category or not message:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Category and message are required")
            elif not valid_text(category, False) or not valid_text(message):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Notice contains invalid text")
            elif category not in NOTICE_CATEGORIES:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Choose a valid notice category")
            elif byte_length(category) > LIMITS["notice_category"] or byte_length(message) > LIMITS["notice_message"]:
                self.send_error_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Notice is too long")
            else:
                self.send_json(HTTPStatus.CREATED, {"id": self.server.state.create_notice(category, message)})
            return

        if path == "/api/letters":
            if not self.server.state.letter_storage_writable:
                self.send_error_json(
                    HTTPStatus.SERVICE_UNAVAILABLE, "Persistent storage is unavailable"
                )
                return
            recipient = form.get("recipient", "").strip()
            location = form.get("location", "").strip()
            message = form.get("message", "").strip()
            sender = form.get("sender", "").strip()
            if not recipient or not message:
                self.send_error_json(
                    HTTPStatus.BAD_REQUEST, "Recipient and message are required"
                )
            elif not all(
                (
                    valid_text(recipient, False),
                    valid_text(location, False),
                    valid_text(message),
                    valid_text(sender, False),
                )
            ):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Letter contains invalid text")
            elif any(
                (
                    byte_length(recipient) > LIMITS["letter_recipient"],
                    byte_length(location) > LIMITS["letter_location"],
                    byte_length(message) > LIMITS["letter_message"],
                    byte_length(sender) > LIMITS["letter_sender"],
                )
            ):
                self.send_error_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "Letter is too long")
            else:
                tracking = self.server.state.create_letter(recipient, location, message, sender)
                if tracking is None:
                    self.send_error_json(HTTPStatus.SERVICE_UNAVAILABLE, "The Postie bag is full")
                else:
                    self.send_json(
                        HTTPStatus.CREATED, {"tracking": tracking, "status": "Waiting"}
                    )
            return

        if path == "/api/admin/letters/status":
            if not self.server.state.letter_storage_writable:
                self.send_error_json(
                    HTTPStatus.INSUFFICIENT_STORAGE, "Could not save letter status"
                )
                return
            try:
                record_id = int(form.get("id", "0"))
            except ValueError:
                record_id = 0
            status = form.get("status", "")
            if record_id <= 0 or status not in LETTER_STATUSES - {"Waiting"}:
                self.send_error_json(
                    HTTPStatus.BAD_REQUEST, "Valid letter id and status are required"
                )
            elif not self.server.state.update_letter(record_id, status):
                self.send_error_json(HTTPStatus.NOT_FOUND, "Letter not found")
            else:
                self.send_json(HTTPStatus.OK, {"ok": True})
            return

        if path == "/api/admin/time":
            try:
                epoch_seconds = int(form.get("epochSeconds", "0"))
            except ValueError:
                epoch_seconds = 0
            if not 1_577_836_800 <= epoch_seconds < 4_102_444_800:
                self.send_error_json(
                    HTTPStatus.BAD_REQUEST, "A valid browser UTC time is required"
                )
            else:
                self.server.state.device_time_set = True
                self.send_json(HTTPStatus.OK, {"ok": True})
            return

        if path == "/api/admin/letters/delete":
            if not self.server.state.letter_storage_writable:
                self.send_error_json(
                    HTTPStatus.INSUFFICIENT_STORAGE, "Could not delete letter"
                )
                return
            try:
                record_id = int(form.get("id", "0"))
            except ValueError:
                record_id = 0
            if record_id <= 0:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Valid letter id is required")
            elif not self.server.state.delete_letter(record_id):
                self.send_error_json(HTTPStatus.NOT_FOUND, "Letter not found")
            else:
                self.send_json(HTTPStatus.OK, {"ok": True})
            return

        moderation = {
            "/api/admin/notices/moderate": ("notices", "Notice"),
        }
        if path in moderation:
            if not self.server.state.notice_storage_writable:
                self.send_error_json(
                    HTTPStatus.INSUFFICIENT_STORAGE,
                    "Notice not found or could not be saved",
                )
                return
            try:
                record_id = int(form.get("id", "0"))
            except ValueError:
                record_id = 0
            action = form.get("action", "")
            collection, label = moderation[path]
            if action not in {"hide", "unhide", "delete"}:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid moderation action")
            elif not self.server.state.moderate(collection, record_id, action):
                self.send_error_json(HTTPStatus.NOT_FOUND, f"{label} not found")
            else:
                self.send_json(HTTPStatus.OK, {"ok": True})
            return

        self.send_error_json(HTTPStatus.NOT_FOUND, "Route not found")


def create_server(
    host: str = "127.0.0.1", port: int = 8080, quiet: bool = False
) -> PreviewServer:
    return PreviewServer((host, port), quiet)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    arguments = parser.parse_args()
    server = create_server(arguments.host, arguments.port)
    print(f"Strawberry Post preview: http://{arguments.host}:{server.server_port}/")
    print(f"Postie login: {ADMIN_USERNAME} / {ADMIN_PASSWORD}")
    print("Mock data is held in memory and resets when this process stops.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
