# Czyta plik .env z katalogu projektu i generuje naglowek env.h z #define.
# Naglowek zamiast -D w linii polecen: hasla ze spacjami, $, &, " itp.
# nie psuja sie przez cytowanie w powloce Windows.
Import("env")
import os
import re

project_dir = env.subst("$PROJECT_DIR")
env_path = os.path.join(project_dir, ".env")
gen_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
header_path = os.path.join(gen_dir, "env.h")


def c_string(value):
    out = []
    for ch in value:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 32:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


lines = ["// Wygenerowane z .env przez scripts/load_env.py - NIE EDYTOWAC", "#pragma once"]
if os.path.isfile(env_path):
    with open(env_path, encoding="utf-8-sig") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if not re.match(r"^[A-Z_][A-Z0-9_]*$", key):
                print("load_env.py: pomijam niepoprawna nazwe '%s'" % key)
                continue
            if value == "":
                continue  # pusta wartosc = domyslna z include/config.h
            lines.append("#define %s %s" % (key, c_string(value)))
            if key == "OTA_PASSWORD" and env.GetProjectOption("upload_protocol", "") == "espota":
                env.Append(UPLOAD_FLAGS=["--auth=" + value])
else:
    print("UWAGA: brak pliku .env - skopiuj .env.example do .env i uzupelnij dane")

content = "\n".join(lines) + "\n"
os.makedirs(gen_dir, exist_ok=True)
old = None
if os.path.isfile(header_path):
    with open(header_path, encoding="utf-8") as f:
        old = f.read()
if old != content:  # przepisz tylko przy zmianie, zeby nie wymuszac pelnej rekompilacji
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(content)

env.Append(CPPPATH=[gen_dir])
