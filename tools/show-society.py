import json
d = json.load(open('tools/society-dump.json', encoding='utf-8'))
print('stories:', len(d['stories']), ' wildTasks:', len(d['wildTasks']))
for s in d['stories']:
    loc = s['location'] or '(无)'
    print(f"  {s['id']:<12} {s['name']:<8} loc={loc:<10} "
          f"days={s['days']} hidden={s['hidden']} opts={len(s['options'])}")
    print(f"      requires: {s['requiresSource']}")
    for o in s['options']:
        print(f"      - {o['name']}: {o['effect']}")
print()
for t in d['wildTasks']:
    print(f"  {t['id']:<8} {t['name']:<12} days={t['days']} fee={t['fee']} style={t['style']}")
