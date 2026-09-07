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
| GET | `/api/admin/diagnostics` | Runtime diagnostic JSON |

All mutation endpoints accept URL-encoded form data. Limits are configured in `src/app_config.h`.
