"""Ziel "pio run -t uploadconfig": schreibt config/config.json per USB in die Partition "config".

Warum
-----
Die Konfiguration liegt in einem eigenen Dateisystem (Partition "config", siehe partitions.csv),
damit "pio run -t uploadfs" sie nicht ueberschreibt. Dieses Ziel ist der Weg dorthin: Es
baut aus dem Ordner config/ ein LittleFS-Abbild und schreibt es mit esptool an den Anfang der
Partition. Offset und Groesse stehen nur in partitions.csv und werden von dort gelesen.

Achtung: Die ganze Partition wird ersetzt. Was die Weboberflaeche seither gespeichert hat,
ist danach weg; es gilt der Stand der Datei config/config.json.
Die Datei enthaelt Passwoerter und steht deshalb in der .gitignore.
"""

import csv
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821

PARTITION = "config"
SOURCE_DIR = os.path.join(env.subst("$PROJECT_DIR"), "config")  # noqa: F821
PAGE_SIZE = 256
BLOCK_SIZE = 4096


def read_partition():
    table = os.path.join(env.subst("$PROJECT_DIR"), env.GetProjectOption("board_build.partitions"))  # noqa: F821
    with open(table, newline="") as handle:
        lines = [line for line in handle if line.strip() and not line.lstrip().startswith("#")]
    for row in csv.reader(lines, skipinitialspace=True):
        if row[0].strip() == PARTITION:
            return int(row[3], 0), int(row[4], 0)
    raise SystemExit("uploadconfig: Partition '%s' fehlt in %s" % (PARTITION, table))


def upload_config(target, source, env):  # noqa: F811
    offset, size = read_partition()

    if not os.path.isfile(os.path.join(SOURCE_DIR, "config.json")):
        raise SystemExit("uploadconfig: %s nicht gefunden" % os.path.join(SOURCE_DIR, "config.json"))

    # Nur config.json ins Abbild, auch wenn im Ordner mehr liegt (Sicherungen und dergleichen).
    staging = os.path.join(env.subst("$BUILD_DIR"), "configfs")
    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging)
    shutil.copy(os.path.join(SOURCE_DIR, "config.json"), staging)

    image = os.path.join(env.subst("$BUILD_DIR"), "config.bin")
    mkfs = os.path.join(env.PioPlatform().get_package_dir("tool-mklittlefs"), "mklittlefs")
    subprocess.check_call([mkfs, "-c", staging, "-s", str(size), "-p", str(PAGE_SIZE),
                           "-b", str(BLOCK_SIZE), image])
    if os.path.getsize(image) != size:
        raise SystemExit("uploadconfig: Abbild hat %d statt %d Bytes" % (os.path.getsize(image), size))

    if not env.subst("$UPLOAD_PORT"):
        env.AutodetectUploadPort(env)
    port = env.subst("$UPLOAD_PORT")
    if not port:
        raise SystemExit("uploadconfig: kein Geraet gefunden (upload_port in platformio.ini setzen)")

    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    print("uploadconfig: schreibe %s nach 0x%X (%d Bytes) auf %s" % (image, offset, size, port))
    subprocess.check_call([
        env.subst("$PYTHONEXE"), esptool,
        "--chip", "esp32", "--port", port, "--baud", env.subst("$UPLOAD_SPEED"),
        "--before", "default_reset", "--after", "hard_reset",
        "write_flash", "-z", "0x%X" % offset, image,
    ])


env.AddCustomTarget(  # noqa: F821
    name="uploadconfig",
    dependencies=None,
    actions=[upload_config],
    title="Upload Config",
    description="Schreibt config/config.json in die Partition 'config' (ersetzt sie ganz)",
)
