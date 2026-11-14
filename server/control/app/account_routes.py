import logging
import re
import secrets
from fastapi import APIRouter, BackgroundTasks, Depends, HTTPException, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import select, delete
from .accounts import Accounts, User, Session, Recovery, Audit, COOKIE, PUBLIC_ORIGIN, digest, password_hash, verify
from .mailer import MailConfig, MailgunMailer, address
from .email_templates import render
from .mfa import CHALLENGE_COOKIE, TTL
from .store import ROBOTS


class Login(BaseModel):
    model_config = ConfigDict(extra='forbid')
    username: str = Field(min_length=1, max_length=64)
    password: str = Field(min_length=1, max_length=128)

class Reset(BaseModel):
    token: str = Field(min_length=20, max_length=128)
    password: str = Field(min_length=15, max_length=128)

class Forgot(BaseModel):
    email: str = Field(min_length=3, max_length=254)

class Code(BaseModel):
    model_config = ConfigDict(extra='forbid')
    code: str = Field(pattern=r'^[0-9]{8}$')

class Member(BaseModel):
    model_config = ConfigDict(extra='forbid')
    username: str = Field(pattern=r'^[a-zA-Z0-9_.-]{3,40}$')
    email: str = Field(max_length=254)
    name: str = Field(min_length=1, max_length=80)
    grants: dict[str, list[str]]

class Access(BaseModel):
    model_config = ConfigDict(extra='forbid')
    enabled: bool
    grants: dict[str, list[str]]

class Change(BaseModel):
    current_password: str = Field(max_length=128)
    password: str = Field(min_length=15, max_length=128)


def validate_grants(grants):
    if any(robot not in ROBOTS or any(p not in {'view', 'control', 'map_edit'} for p in rights)
           or len(rights) != len(set(rights)) or (rights and 'view' not in rights)
           for robot, rights in grants.items()):
        raise HTTPException(422, 'Invalid robot permissions')
    return grants


def install(app, accounts):
    router = APIRouter()
    def current(request: Request):
        user = accounts.current(request.cookies.get(COOKIE))
        if request.method not in ('GET', 'HEAD', 'OPTIONS'):
            token = request.cookies.get(COOKIE, '')
            if not secrets.compare_digest(request.headers.get('x-csrf-token', ''), digest('csrf:' + token)):
                raise HTTPException(403, 'Invalid CSRF token')
        return user

    def admin(user=Depends(current)):
        if not user['admin']:
            raise HTTPException(403, 'Administrator required')
        return user

    def send_link(email, token, invitation=False):
        try:
            message = render('invitation' if invitation else 'recovery', token=token)
            MailgunMailer(MailConfig.load()).send(email, message.subject, message.text,
                                                html=message.html, category='auth')
        except Exception:
            logging.getLogger('roocore.auth').warning('Account email could not be sent')

    def send_changed(email):
        try:
            message = render('password_changed')
            MailgunMailer(MailConfig.load()).send(email, message.subject, message.text,
                                                html=message.html, category='auth')
        except Exception:
            logging.getLogger('roocore.auth').warning('Password change notice could not be sent')

    def challenge_response(challenge):
        token, code, email = challenge
        try:
            message = render('login', code=code)
            MailgunMailer(MailConfig.load()).send(email, message.subject, message.text,
                                                html=message.html, category='auth')
        except Exception:
            accounts.mfa.delivery(token, False)
            logging.getLogger('roocore.auth').warning('Login code email could not be sent')
            raise HTTPException(503, 'Email could not be sent. Sign in again later.') from None
        accounts.mfa.delivery(token, True)
        local, domain = email.split('@', 1)
        response = JSONResponse({'mfa_required': True, 'email': local[:1] + '***@' + domain,
                                 'expires_in': TTL, 'resend_after': 60})
        response.set_cookie(CHALLENGE_COOKIE, token, max_age=TTL, secure=True, httponly=True,
                            samesite='lax', path='/')
        return response

    @router.post('/api/auth/login')
    def login(body: Login):
        return challenge_response(accounts.login(body.username, body.password))

    @router.post('/api/auth/resend')
    def resend(request: Request):
        return challenge_response(accounts.mfa.resend(request.cookies.get(CHALLENGE_COOKIE)))

    @router.get('/api/auth/challenge')
    def pending(request: Request):
        return accounts.mfa.info(request.cookies.get(CHALLENGE_COOKIE)) or {'mfa_required': False}

    @router.post('/api/auth/verify')
    def verify_code(body: Code, request: Request):
        token = accounts.mfa.complete(request.cookies.get(CHALLENGE_COOKIE), body.code)
        response = JSONResponse({'ok': True})
        response.set_cookie(COOKIE, token, max_age=28800, secure=True, httponly=True,
                            samesite='lax', path='/')
        response.delete_cookie(CHALLENGE_COOKIE, secure=True, httponly=True, samesite='lax', path='/')
        return response

    @router.get('/api/me')
    def me(request: Request, user=Depends(current)):
        return dict(user=user, csrf=digest('csrf:' + request.cookies[COOKIE]))

    @router.post('/api/auth/logout')
    def logout(request: Request, user=Depends(current)):
        with accounts.store.session.begin() as s:
            s.execute(delete(Session).where(Session.token == digest(request.cookies[COOKIE])))
        response = JSONResponse({'ok': True})
        response.delete_cookie(COOKIE, secure=True, httponly=True, samesite='lax', path='/')
        return response

    @router.post('/api/auth/forgot')
    def forgot(body: Forgot, tasks: BackgroundTasks):
        accounts.throttle('forgot-global', 5, 900)
        accounts.throttle('forgot-' + digest(body.email.strip().lower()), 1, 60)
        with accounts.store.session() as s:
            user = s.scalar(select(User).where(User.email == body.email.strip().lower(), User.enabled == True))
            if user:
                token = accounts.issue(user.id)
                tasks.add_task(send_link, user.email, token)
        return {'detail': 'If the account exists, a recovery link will be sent.'}

    @router.post('/api/auth/reset')
    def reset(body: Reset, tasks: BackgroundTasks):
        email = accounts.reset(body.token, body.password)
        tasks.add_task(send_changed, email)
        return {'ok': True}

    @router.post('/api/auth/password')
    def change(body: Change, tasks: BackgroundTasks, user=Depends(current)):
        accounts.throttle('change-' + user['id'], 5, 900)
        with accounts.store.session() as s:
            record = s.get(User, user['id'])
            if not verify(body.current_password, record.password):
                raise HTTPException(401, 'Incorrect current password')
        email = accounts.reset(accounts.issue(user['id']), body.password)
        tasks.add_task(send_changed, email)
        return {'ok': True}

    @router.get('/api/users')
    def users(user=Depends(admin)):
        with accounts.store.session() as s:
            return {'users': [accounts.describe(u) for u in s.scalars(select(User).order_by(User.username))]}

    @router.post('/api/users')
    def create(body: Member, tasks: BackgroundTasks, actor=Depends(admin)):
        try:
            email = address(body.email.strip().lower())
        except ValueError:
            raise HTTPException(422, 'Invalid email')
        validate_grants(body.grants)
        with accounts.lock, accounts.store.session.begin() as s:
            if len(list(s.scalars(select(User.id)))) >= 30:
                raise HTTPException(409, 'Account limit reached')
            if s.scalar(select(User.id).where((User.username == body.username.lower()) | (User.email == email))):
                raise HTTPException(409, 'Username or email already exists')
            uid = secrets.token_hex(16)
            s.add(User(id=uid, username=body.username.lower(), email=email, name=body.name,
                       password=None, admin=False, grants=body.grants))
            s.add(Audit(actor=actor['id'], action='user_created', target=uid))
        token = accounts.issue(uid)
        tasks.add_task(send_link, email, token, True)
        return {'id': uid, 'activation_url': PUBLIC_ORIGIN + '/reset#' + token}

    @router.patch('/api/users/{uid}')
    def update(uid: str, body: Access, actor=Depends(admin)):
        validate_grants(body.grants)
        with accounts.store.session.begin() as s:
            user = s.get(User, uid)
            if not user:
                raise HTTPException(404)
            if user.admin:
                raise HTTPException(409, 'The owner account cannot be disabled or changed here')
            user.enabled, user.grants = body.enabled, body.grants
            s.execute(delete(Session).where(Session.user_id == uid))
            s.execute(delete(Recovery).where(Recovery.user_id == uid))
            accounts.mfa.revoke(s, uid)
            s.add(Audit(actor=actor['id'], action='access_changed', target=uid))
        return {'ok': True}

    @router.post('/api/users/{uid}/invite')
    def invite(uid: str, tasks: BackgroundTasks, actor=Depends(admin)):
        accounts.throttle('invite-global', 10, 900)
        with accounts.store.session() as s:
            user = s.get(User, uid)
            if not user or not user.enabled:
                raise HTTPException(404)
            token = accounts.issue(uid)
            tasks.add_task(send_link, user.email, token, True)
        return {'activation_url': PUBLIC_ORIGIN + '/reset#' + token}

    app.include_router(router)
    return current, admin
