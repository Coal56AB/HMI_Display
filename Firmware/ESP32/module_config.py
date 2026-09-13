"""Select an optional application's partition table; template stays standalone."""
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
module = project.parents[1] / "DisplaySrc" / "esp32"
if env["PIOENV"] != "template" and (module / "CMakeLists.txt").is_file():
    partitions = module / "partitions.csv"
    if partitions.is_file():
        env.BoardConfig().update("build.partitions", str(partitions))
