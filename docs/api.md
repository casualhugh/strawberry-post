# HTTP API

## Public and administrative routes

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/` | Notice Board landing page, posting form, and derived summary |
| GET | `/style.css` | Flash-resident public stylesheet |
| GET | `/logo.svg` | Flash-resident Strawberry Post logo |
| GET/POST | `/api/notices` | List/create notices |
| GET | `/letters` | Private letter-writing postcard page |
| GET | `/track` | Public tracking-number lookup page |
| POST | `/api/letters` | Create a private letter |
| GET | `/api/letters/status?tracking=...` | Public status-only tracking |
| GET | `/api/stats` | Derived public statistics |
| GET | `/postie` | Authenticated sorting room |
| GET | `/postie/diagnostics` | Authenticated diagnostics page |
| GET | `/api/admin/overview` | Private letters and moderation data |
| POST | `/api/admin/letters/status` | Change letter status |
| POST | `/api/admin/letters/delete` | Permanently delete a letter |
| POST | `/api/admin/notices/moderate` | Hide/unhide/delete a notice |
| POST | `/api/admin/time` | Set device UTC time from the Postie browser |
| GET | `/api/admin/diagnostics` | Runtime diagnostic JSON |

All mutation endpoints accept URL-encoded form data. Limits are configured in `src/app_config.h`.

`GET /api/notices` returns up to eight public notices in descending ID order.
Pass the returned non-zero `nextBeforeId` as `?before=...` to request the next
page. `GET /api/admin/overview` similarly accepts `letterBefore` and
`noticeBefore`, returning up to six records of each type plus continuation IDs.
`POST /api/admin/time` accepts `epochSeconds` and requires Postie Basic Auth.

`GET /api/stats` also provides non-technical UI capability flags:
`noticePostingAvailable`, `letterPostingAvailable`, `noticeHistoryComplete`,
`letterHistoryComplete`, `letterLookupAvailable`, and `storageInterrupted`.
The authenticated overview adds each store's backend plus readable/writable
flags and a `storageIssue` code. These values let an already-open page disable
the appropriate controls if storage fails while it is in use.

The authenticated diagnostics JSON reports separate `noticeStorageBackend` and
`letterStorageBackend` values (`sd`, `littlefs`, or `none`) plus writable flags
for each selected record store, and reports whether device time is set.

Wildcard DNS answers use a one-second TTL. Browser entry requests with unknown
hostnames receive a non-cacheable `302` redirect to `http://post.local/`;
canonical-host and direct-IP API/asset requests are not redirected.
