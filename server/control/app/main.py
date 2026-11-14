from contextlib import asynccontextmanager
import platform
from pathlib import Path
from starlette.concurrency import run_in_threadpool

from fastapi import FastAPI, Depends, HTTPException, Query, Request
from fastapi.responses import FileResponse, JSONResponse, RedirectResponse
from .accounts import Accounts, COOKIE, PUBLIC_ORIGIN, permitted
from .account_routes import install as install_accounts
from .maps import install as install_maps
from .device_stream import install as install_device_stream, DEVICE_PATH, DeviceStream
import time

from .config import Settings
from .mqtt import Receiver
from .store import Store, ROBOTS, utcnow

WEB = Path(__file__).parent / "static"


def create_app(settings=None, receiver_factory=Receiver):
    settings = settings or Settings.from_env()
    store = Store(settings.database_url)
    receiver = receiver_factory(settings, store)
    accounts = Accounts(store, settings)
    started_at = utcnow().isoformat()
    try:
        os_name = platform.freedesktop_os_release().get("PRETTY_NAME", platform.system())
    except OSError:
        os_name = platform.system()

    @asynccontextmanager
    async def lifespan(app):
        store.start()
        accounts.start()
        store.prune(settings.retention_days)
        receiver.start()
        yield
        receiver.stop()
        store.engine.dispose()

    app = FastAPI(title="RooCore Server", lifespan=lifespan, docs_url=None,
                  redoc_url=None, openapi_url=None)
    app.state.store, app.state.receiver = store, receiver
    app.state.accounts = accounts
    authenticate, administrator = install_accounts(app, accounts)
    install_maps(app, store, authenticate)
    install_device_stream(app, store, authenticate)

    @app.middleware("http")
    async def headers(request, call_next):
        if request.headers.get('x-forwarded-proto') == 'http':
            return RedirectResponse(PUBLIC_ORIGIN + request.url.path, status_code=308)
        if request.method not in ('GET', 'HEAD', 'OPTIONS'):
            if request.url.path != DEVICE_PATH and request.headers.get('origin') != PUBLIC_ORIGIN:
                return JSONResponse({'detail': 'Invalid origin'}, status_code=403)
            public_auth = {'/api/auth/login','/api/auth/resend','/api/auth/verify','/api/auth/forgot','/api/auth/reset'}
            try:
                if request.url.path == DEVICE_PATH:
                    await run_in_threadpool(app.state.authenticate_device, request)
                elif request.url.path not in public_auth:
                    await run_in_threadpool(authenticate, request)
            except HTTPException as error:
                return JSONResponse({'detail': error.detail}, status_code=error.status_code,
                                    headers={**(error.headers or {}), 'Cache-Control':'no-store'})
            limit = 16384 if request.url.path in public_auth else 1024 * 1024
            body = bytearray()
            async for chunk in request.stream():
                if len(body) + len(chunk) > limit:
                    return JSONResponse({'detail': 'Request too large'}, status_code=413)
                body.extend(chunk)
            request._body = bytes(body)
        response = await call_next(request)
        response.headers.update({
            "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff",
            "X-Frame-Options": "DENY", "Referrer-Policy": "no-referrer",
            "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; "
                                       "img-src 'self'; connect-src 'self'; frame-ancestors 'none'; "
                                       "base-uri 'none'; form-action 'self'",
        })
        return response

    @app.exception_handler(Exception)
    async def unavailable(request, exc):
        return JSONResponse({"detail": "Service unavailable"}, status_code=503)

    @app.get("/health/live")
    def live():
        return {"status": "up"}

    @app.get("/health/ready")
    def ready():
        try:
            store.healthy()
            if not receiver.connected:
                raise RuntimeError("mqtt unavailable")
        except Exception:
            return JSONResponse({"status": "not_ready"}, status_code=503)
        return {"status": "ready"}

    @app.get("/")
    def index(request: Request):
        try:
            accounts.current(request.cookies.get(COOKIE))
        except HTTPException:
            return RedirectResponse('/login', status_code=303)
        return FileResponse(WEB / 'index.html')

    @app.get('/login')
    @app.get('/reset')
    def login_page():
        return FileResponse(WEB / 'login.html')

    @app.get("/assets/{filename}")
    def asset(filename: str):
        if filename not in ("app.js", "style.css", "account.js", "workspace.js"):
            raise HTTPException(404)
        return FileResponse(WEB / filename)

    @app.get("/api/status", dependencies=[Depends(authenticate)])
    def status(user=Depends(authenticate)):
        store.healthy()
        rows = store.robots(receiver.connected, receiver.fresh_after, settings.stale_seconds)
        with store.session() as session:
            for row in rows:
                stream = session.get(DeviceStream, row['id'])
                if stream:
                    age = max(0, time.time() - stream.received)
                    state = stream.snapshot['status']
                    states = ['idle','cleaning','returning','docking','charging','resuming','settling','recovering','held','fault','complete','edging','touchup','yielding']
                    row.update(online=stream.received >= app.state.device_fresh_after and age < 5, age_seconds=round(age,1),
                               last_seen=__import__('datetime').datetime.fromtimestamp(stream.received,__import__('datetime').timezone.utc).isoformat(),
                               telemetry=dict(battery_percent=state.get('battery') if state.get('battery_valid') else None,
                                              battery_mv=None,state=states[state['state']],
                                              firmware=state.get('firmware_version','unknown'),local_safety='unverified'),
                               transport='https_device', cloud_map_id=stream.cloud_map_id)
        return dict(mode="telemetry_only", database="ready", mqtt_connected=receiver.connected,
                    hardware_commands_enabled=False, sci_verified=False,
                    firmware_mqtt_integrated=False, firmware_https_integrated=True, physical_safety_verified=False,
                    stale_after_seconds=settings.stale_seconds,
                    rejected_messages=receiver.rejected,
                    runtime={"os": os_name, "python": platform.python_version(), "started_at": started_at},
                    robots=[r for r in rows if permitted(user, r["id"])])

    @app.get("/assets/fonts/{filename}")
    def font(filename: str):
        if filename != "heebo-variable.ttf":
            raise HTTPException(404)
        return FileResponse(WEB / "fonts" / filename, media_type="font/ttf")

    @app.get("/api/events", dependencies=[Depends(authenticate)])
    def events(limit: int = Query(50, ge=1, le=200), user=Depends(authenticate)):
        return {"events": [e for e in store.history(limit) if permitted(user, e["robot_id"])]}

    @app.post("/api/robots/{robot_id}/commands", dependencies=[Depends(authenticate)])
    def blocked_command(robot_id: str, user=Depends(authenticate)):
        if not permitted(user, robot_id, "control"):
            raise HTTPException(403, "Robot control permission required")
        if robot_id not in ROBOTS:
            raise HTTPException(404, "Unknown robot")
        # Intentionally unconditional: no env flag or MQTT message can enable this.
        raise HTTPException(423, "Hardware commands are unavailable pending SCI/OI verification "
                            "and separately reviewed controller integration")

    return app


# Uvicorn factory keeps importing library modules independent of secrets.
def app_factory():
    return create_app()
