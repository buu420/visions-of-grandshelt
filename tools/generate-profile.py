"""Generate exact-capture property descriptors. No instance addresses are emitted."""
import json,sys,hashlib
from pathlib import Path
capture=Path(sys.argv[1]); destination=Path(sys.argv[2]); d=json.loads(capture.read_text(encoding='utf-8'))
quote=lambda s: 'L'+json.dumps(s,ensure_ascii=True)
lines=['// Generated from exact-build JMAP. Do not edit.','namespace resonance::profile {',
       'struct Property { const wchar_t* owner; const wchar_t* name; const wchar_t* type; int offset; int size; unsigned mask; const wchar_t* struct_type; const wchar_t* inner_type; int inner_size; };',
       'inline constexpr Property properties[] = {']
n=0
blueprints='--blueprints' in sys.argv[3:]
for name,o in d['objects'].items():
    if o['type'] not in ('Class','ScriptStruct') or (not blueprints and not name.startswith('/Script/')): continue
    for p in o.get('properties',[]):
        lines.append('{'+','.join([quote(name),quote(p['name']),quote(p['type']),str(p['offset']),str(p['size']),str(p.get('field_mask',255)),quote(p.get('struct') or ''),quote(p.get('inner',{}).get('type','')),str(p.get('inner',{}).get('size',0))])+'},'); n+=1
lines+=['};', 'struct Function { const wchar_t* path; int size; int result; };', 'inline constexpr Function functions[] = {']
for name,o in d['objects'].items():
    if o['type']!='Function' or (not blueprints and not name.startswith('/Script/')): continue
    result=next((p['offset'] for p in o.get('properties',[]) if 'CPF_ReturnParm' in p.get('flags','')), -1)
    lines.append('{'+','.join([quote(name),str(o.get('properties_size',0)),str(result)])+'},')
lines+=['};','}']
destination.parent.mkdir(parents=True,exist_ok=True); destination.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Generated',n,'native'+(' and loaded Blueprint' if blueprints else '')+' property descriptors; JMAP SHA256',hashlib.sha256(capture.read_bytes()).hexdigest())
