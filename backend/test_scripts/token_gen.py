import secrets

short_hex = secrets.token_hex(6)
long_hex = secrets.token_hex(32)
adminPwd  = secrets.token_urlsafe(32)
print(adminPwd )


# adminPwd = Dihs9iu4AUCcxRjl6JL8nLBqtwFYev2kNcl37td9VV0