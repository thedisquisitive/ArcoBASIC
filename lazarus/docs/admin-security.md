# Administration Security

Lazarus protects bench configuration, branding, storage assignment, and OS
installation behind service-enforced administrator authentication.

## Initial Setup

The first Administration entry currently accepts any non-empty password up to
256 characters. The service then generates a unique appliance recovery key and
shows it once. Record that key outside the appliance before continuing.

There is no universal Lazarus recovery password. A shared hidden password
would grant the same access to every appliance if it were ever disclosed.

## Stored Credentials

Lazarus stores salted PBKDF2-HMAC-SHA256 hashes for the password and recovery
key. Plaintext credentials are never written to disk. The credential file is
created with mode `0600`.

Installed appliances store credentials at:

```text
/var/lib/arcology-lazarus/admin.auth
```

Live systems mirror the credential file to persistent image storage and
restore it during boot when that storage is available.

## Sessions

Successful authentication creates a random service-side session token that
expires after 15 minutes. Returning to Home logs out and invalidates the token.
Privileged profile changes and OS installation are rejected by the service
without a valid token, even if a client bypasses the GTK interface.

Repeated failed logins are rate limited.

## Recovery

The appliance recovery key unlocks the same Administration area as the normal
password. From **Password and Recovery**, an administrator can set a new
password or rotate the recovery key. Rotation immediately invalidates the old
key and displays the replacement once.

If both credentials are lost, recovery requires trusted offline service of the
appliance state. Lazarus does not contain a vendor backdoor.
