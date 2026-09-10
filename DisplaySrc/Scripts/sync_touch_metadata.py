"""Update editor references without rewriting renderer code or hand-maintained UI metadata."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
project=ROOT/'editor_project.json'
data=json.loads(project.read_text(encoding='utf-8'))
ui=data.setdefault('runtimeUi',{})
ui['controller']='Src/hmi_ui.c'
ui['valueDefinitions']=[{'id':i,'label':json.loads(name),'unit':json.loads(unit)} for i,(name,unit) in enumerate(re.findall(r'\{NAME\(("(?:[^"\\]|\\.)*")\),UNIT\(("(?:[^"\\]|\\.)*")\)',(ROOT/'Src/hmi_ui.c').read_text(encoding='utf-8')))]
project.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(project)
