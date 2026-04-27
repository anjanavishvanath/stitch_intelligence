import os
from flask import request, jsonify
from passlib.hash import bcrypt
from datetime import datetime, timedelta, timezone
from flask_jwt_extended import create_access_token, create_refresh_token, decode_token, get_jwt_identity, get_jwt
from .db_helpers import insert_user, get_user_by_email, insert_refresh_token, is_refresh_token_revoked, revoke_refresh_token

JWT_ACCESS_EXPIRES = int(os.getenv("JWT_ACCESS_EXPIRES_SEC", 900))
JWT_REFRESH_EXPIRES = int(os.getenv("JWT_REFRESH_EXPIRES_SEC", 60*60*24*7))

def hash_password(password) -> str:
    '''
    Hashes a plaintext password using bcrypt.
    guard against bcrypts 72 character limit.
    '''
    if password is None:
        raise ValueError("Password is required")
    if not isinstance(password, str):
        raise ValueError("Password must be a string")
    pw_bytes = password.encode('utf-8')
    if len(pw_bytes) > 72:
        raise ValueError("Password too long (max 72 bytes)")
    return bcrypt.hash(password)

def verify_password(plaintext, hashed) -> bool:
    return bcrypt.verify(plaintext, hashed)

def build_tokens(identity_claims: dict) -> dict:
    '''
        identity_claims must contain: {"user_id": int, "email": str, "username": str, "role": str, "organization": str or None, "user_id": int}
        Returns: {access, refresh, jti, expires_at}
    '''
    identity_str = str(identity_claims.get("user_id"))
    additional = {
        "username": identity_claims.get("username"),
        "email": identity_claims.get("email"),
        "role": identity_claims.get("role"),
        "organization": identity_claims.get("organization")
    }
    access = create_access_token(identity=identity_str, additional_claims=additional, expires_delta=timedelta(seconds=JWT_ACCESS_EXPIRES))
    refresh = create_refresh_token(identity=identity_str, additional_claims=additional, expires_delta=timedelta(seconds=JWT_REFRESH_EXPIRES))
    decode = decode_token(refresh)
    jti = decode.get("jti")
    exp = decode.get("exp")
    expires_at = datetime.fromtimestamp(exp, tz=timezone.utc)
    return {
        "access": access,
        "refresh": refresh,
        "jti": jti,
        "expires_at": expires_at
    }

def signup() -> tuple:
    data = request.get_json()
    username = data.get("username")
    email = data.get("email")
    password = data.get("password")
    role = data.get("role")
    organization = data.get("organization")
    # Basic validation
    if not username:
        return jsonify({"error": "Username is required"}), 400
    if not email:
        return jsonify({"error": "Email is required"}), 400
    if not password:
        return jsonify({"error": "Password is required"}), 400
    if not role:
        return jsonify({"error": "Role is required"}), 400
    if get_user_by_email(email):
        return jsonify({"error": "Email already exists"}), 400
    try:
        pw_hash = hash_password(password)
    except ValueError as e:
        return jsonify({"error": str(e)}), 400
    try:
        user_id = insert_user(username, email, pw_hash, role, organization)
        return jsonify({"message": "User signed up successfully"}), 201
    except Exception as e:
        return jsonify({"error": "Failed to create user"}), 500
        
def login() -> tuple:
    data = request.get_json()
    email = data.get("email")
    password = data.get("password")
    if not email:
        return jsonify({"error": "Email is required"}), 400
    if not password:
        return jsonify({"error": "Password is required"}), 400
    user = get_user_by_email(email)
    if not user:
        return jsonify({"error": "Invalid email or password"}), 401
    user_id = user.get("id")
    username = user.get("username")
    role = user.get("role")
    organization = user.get("organization")
    password_hash = user.get("password_hash")
    if not verify_password(password, password_hash):
        return jsonify({"error": "Invalid email or password"}), 401
    identity = {
        "user_id": user_id,
        "email": email,
        "username": username,
        "role": role,
        "organization": organization
    }
    tokens = build_tokens(identity)
    access = tokens["access"]
    refresh = tokens["refresh"]
    jti = tokens["jti"]
    expires_at = tokens["expires_at"]
    try:
        insert_refresh_token(jti, user_id, expires_at)
        return jsonify({"access": access, "refresh": refresh}), 200
    except Exception as e:
        return jsonify({"error": "Failed to create refresh token"}), 500

def refresh() -> tuple:
    identity = get_jwt_identity()
    claims = get_jwt()
    jti = claims.get("jti")
    # if token (jti) is revoked, reject
    if is_refresh_token_revoked(jti):
        return jsonify({"error": "Token has been revoked"}), 401
    # build new token using identity and claims
    additional = {
        "username": claims.get("username"),
        "email": claims.get("email"),
        "role": claims.get("role"),
        "organization": claims.get("organization")
    }
    try:    
        access = create_access_token(identity=identity, additional_claims=additional)
        return jsonify({"access": access}), 200
    except Exception as e:
        return jsonify({"error": "Failed to refresh token"}), 500

def logout() -> tuple:
    claims = get_jwt()
    jti = claims.get("jti")
    try:
        revoke_refresh_token(jti)
        return jsonify({"message": "Logged out successfully"}), 200
    except Exception as e:
        return jsonify({"error": "Failed to revoke token"}), 500
