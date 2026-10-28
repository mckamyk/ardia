import json
import sys

import dbus

SERVICE = "org.freedesktop.secrets"
SERVICE_PATH = "/org/freedesktop/secrets"
LOGIN = "/org/freedesktop/secrets/collection/login"
INTERNAL = "org.gnome.keyring.InternalUnsupportedGuiltRiddenInterface"


def text(value):
    return bytes(bytearray(value)).decode("utf-8", "replace")


def session(service):
    _, path = service.OpenSession("plain", dbus.String("", variant_level=1), dbus_interface="org.freedesktop.Secret.Service")
    return path


def items(bus, attributes, with_secrets):
    service = bus.get_object(SERVICE, SERVICE_PATH)
    unlocked, locked = service.SearchItems(dbus.Dictionary(attributes, signature="ss"), dbus_interface="org.freedesktop.Secret.Service")
    out = []
    secrets = {}
    if with_secrets and unlocked:
        secrets = service.GetSecrets(unlocked, session(service), dbus_interface="org.freedesktop.Secret.Service")
    for path, is_locked in [(p, False) for p in unlocked] + [(p, True) for p in locked]:
        item = bus.get_object(SERVICE, path)
        props = item.GetAll("org.freedesktop.Secret.Item", dbus_interface="org.freedesktop.DBus.Properties")
        entry = {"path": str(path), "locked": is_locked, "label": str(props["Label"]), "attributes": {str(k): str(v) for k, v in props["Attributes"].items()}}
        if path in secrets:
            entry["secret"] = text(secrets[path][2])
        out.append(entry)
    return out


def create(bus, label, attributes, secret, content_type):
    service = bus.get_object(SERVICE, SERVICE_PATH)
    collection = bus.get_object(SERVICE, LOGIN)
    props = {
        "org.freedesktop.Secret.Item.Label": dbus.String(label),
        "org.freedesktop.Secret.Item.Attributes": dbus.Dictionary(attributes, signature="ss"),
    }
    value = dbus.Struct((session(service), dbus.ByteArray(b""), dbus.ByteArray(secret.encode()), dbus.String(content_type)), signature="oayays")
    item, _ = collection.CreateItem(dbus.Dictionary(props, signature="sv"), value, True, dbus_interface="org.freedesktop.Secret.Collection")
    return str(item)


def unlock(bus, password):
    service = bus.get_object(SERVICE, SERVICE_PATH)
    value = dbus.Struct((session(service), dbus.ByteArray(b""), dbus.ByteArray(password.encode()), dbus.String("text/plain")), signature="oayays")
    service.UnlockWithMasterPassword(dbus.ObjectPath(LOGIN), value, dbus_interface=INTERNAL)
    return True


def main():
    address, op, payload = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
    bus = dbus.bus.BusConnection(address)
    if op == "items":
        result = items(bus, payload.get("attributes", {}), payload.get("secrets", False))
    elif op == "create":
        result = create(bus, payload["label"], payload["attributes"], payload["secret"], payload.get("contentType", "text/plain"))
    elif op == "unlock":
        result = unlock(bus, payload["password"])
    else:
        raise SystemExit(f"unknown op {op}")
    print(json.dumps(result))


main()
