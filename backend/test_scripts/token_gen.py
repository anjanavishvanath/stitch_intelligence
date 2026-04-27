import secrets

short_hex = secrets.token_hex(6)
long_hex = secrets.token_hex(32)
print(short_hex)
print(long_hex)