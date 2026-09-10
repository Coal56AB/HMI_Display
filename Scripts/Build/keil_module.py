"""Connect a display module by changing build composition only (Python 3.7+)."""
from pathlib import Path
import json, os, re, xml.etree.ElementTree as ET

def resolve(base, text):
    return (base / text.replace("\\", "/")).resolve()

def relative(path, base):
    try: return os.path.relpath(str(path), str(base)).replace("\\", "/")
    except ValueError: return str(path).replace("\\", "/")

def targets(project):
    return [t.findtext("TargetName") for t in ET.parse(str(project)).findall("./Targets/Target")]

def connect(module, project, target_name, output=None):
    module, project = Path(module).resolve(), Path(project).resolve()
    output = Path(output).resolve() if output else project
    spec = json.loads((module / "module.json").read_text(encoding="utf-8"))
    tree = ET.parse(str(project))
    target = next((t for t in tree.findall("./Targets/Target") if t.findtext("TargetName") == target_name), None)
    if target is None: raise ValueError("Target not found: " + target_name)
    controls = target.find("TargetOption/TargetArmAds/Cads/VariousControls")
    includes = [x for x in (controls.findtext("IncludePath") or "").split(";") if x]
    headers = [resolve(project.parent, x) / "display_api.h" for x in includes]
    header = next((h for h in headers if h.is_file()), None)
    if header is None: raise ValueError("Project does not expose display_api.h; select a compatible template")
    match = re.search(r"#define\s+DISPLAY_API_VERSION\s+(\d+)", header.read_text(encoding="utf-8"))
    if not match or int(match.group(1)) != spec["apiVersion"]: raise ValueError("Display API version mismatch")
    # Validate every module path before writing anything.
    def checked(name, directory=False):
        path = resolve(module, name)
        if os.path.commonpath([str(module), str(path)]) != str(module): raise ValueError("Module path escapes directory: " + name)
        if not (path.is_dir() if directory else path.is_file()): raise ValueError("Missing module path: " + name)
        return path
    sources = [checked(n) for n in spec["firmware"]["sources"]]
    module_includes = [checked(n, True) for n in spec["firmware"]["includes"]]
    # Rebase paths only when generating a separate build project.
    if output != project:
        for node in tree.iter("FilePath"):
            if node.text: node.text = relative(resolve(project.parent, node.text), output.parent)
        for node in tree.iter("IncludePath"):
            if node.text: node.text = ";".join(relative(resolve(project.parent,x),output.parent) for x in node.text.split(";") if x)
        for node in tree.iter("ScatterFile"):
            if node.text: node.text = relative(resolve(project.parent,node.text),output.parent)
    groups = target.find("Groups")
    for group in list(groups):
        if group.findtext("GroupName") == "DisplaySrc": groups.remove(group)
        else:
            files = group.find("Files")
            if files is not None:
                for file in list(files):
                    if file.findtext("FileName") == "display_empty.c": files.remove(file)
    group = ET.SubElement(groups,"Group"); ET.SubElement(group,"GroupName").text = "DisplaySrc"
    files = ET.SubElement(group,"Files")
    for path in sources:
        file = ET.SubElement(files,"File")
        ET.SubElement(file,"FileName").text = path.name
        ET.SubElement(file,"FileType").text = "1"
        ET.SubElement(file,"FilePath").text = relative(path,output.parent)
    # Module include paths are managed using a dedicated project-local sidecar.
    sidecar = project.with_suffix(".display.json")
    previous = json.loads(sidecar.read_text(encoding="utf-8")) if sidecar.exists() else {}
    prior = previous.get(target_name,{})
    old_includes = prior.get("includes",[])
    final = [relative(resolve(project.parent,x),output.parent) for x in includes if x not in old_includes]
    new_includes = [relative(x,output.parent) for x in module_includes]
    controls.find("IncludePath").text = ";".join(dict.fromkeys(final + new_includes))
    defs = [x for x in (controls.findtext("Define") or "").split(",") if x and x not in prior.get("defines",[])]
    new_defs = spec["firmware"].get("defines",[])
    controls.find("Define").text = ",".join(dict.fromkeys(defs+new_defs))
    output.parent.mkdir(parents=True,exist_ok=True)
    # No parent directories or C sources are edited.
    tree.write(str(output),encoding="utf-8",xml_declaration=True)
    if output == project:
        previous[target_name] = {"includes":new_includes,"defines":new_defs}
        sidecar.write_text(json.dumps(previous,indent=2),encoding="utf-8")
    return output
