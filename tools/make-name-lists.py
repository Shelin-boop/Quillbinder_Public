#!/usr/bin/env python3
"""Builds the name lists of the add-on "Name Generator" from the person
providers of the Python package Faker (MIT licence), without running its code:
the module files are parsed and only literal lists are read.

    pip download faker --no-deps -d /tmp/faker && cd /tmp/faker && python3 -m zipfile -e faker-*.whl x
    tools/make-name-lists.py /tmp/faker/x addons/tool-name-generator/payload/names [--survey]
"""
import ast, json, os, re, sys, unicodedata

def literal(node, env):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Tuple):
        return tuple(literal(e, env) for e in node.elts)
    if isinstance(node, (ast.List, ast.Set)):
        return [literal(e, env) for e in node.elts]
    if isinstance(node, ast.Dict):
        return {literal(k, env): literal(v, env) for k, v in zip(node.keys, node.values)}
    if isinstance(node, ast.Name):
        if node.id in env:
            return env[node.id]
        raise ValueError(node.id)
    # tuple(map(itemgetter(i), NAME))
    if isinstance(node, ast.Call) and getattr(node.func, 'id', None) == 'map' and len(node.args) == 2 \
            and isinstance(node.args[0], ast.Call) and getattr(node.args[0].func, 'id', None) == 'itemgetter':
        idx = literal(node.args[0].args[0], env)
        return [x[idx] for x in literal(node.args[1], env)]
    if isinstance(node, ast.Call) and getattr(node.func, 'id', None) in ('OrderedDict', 'dict', 'tuple', 'list', 'set') and len(node.args) == 1:
        v = literal(node.args[0], env)
        if node.func.id in ('OrderedDict', 'dict'):
            return dict(v) if isinstance(v, (list, tuple)) else v
        return list(v)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        a, b = literal(node.left, env), literal(node.right, env)
        if isinstance(a, dict) and isinstance(b, dict):
            c = dict(a); c.update(b); return c
        if isinstance(a, dict): a = list(a)
        if isinstance(b, dict): b = list(b)
        return list(a) + list(b)  # tuples and lists alike
    if isinstance(node, (ast.ListComp, ast.GeneratorExp)) and len(node.generators) == 1:
        g = node.generators[0]
        src = literal(g.iter, env)
        if isinstance(node.elt, ast.Subscript) and isinstance(g.target, ast.Name) \
                and isinstance(node.elt.value, ast.Name) and node.elt.value.id == g.target.id:
            idx = literal(node.elt.slice, env)
            return [x[idx] for x in src]
        raise ValueError('comprehension')
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
        a, b = literal(node.left, env), literal(node.right, env)
        c = dict(a); c.update(b); return c
    raise ValueError(type(node).__name__)

def read_provider(path):
    tree = ast.parse(open(path, encoding='utf-8').read())
    out = {}
    for cls in [n for n in tree.body if isinstance(n, ast.ClassDef)]:
        env = {}
        for st in cls.body:
            if isinstance(st, ast.Assign) and len(st.targets) == 1 and isinstance(st.targets[0], ast.Name):
                try:
                    env[st.targets[0].id] = literal(st.value, env)
                except Exception:
                    pass
            elif isinstance(st, ast.AnnAssign) and isinstance(st.target, ast.Name) and st.value is not None:
                try:
                    env[st.target.id] = literal(st.value, env)
                except Exception:
                    pass
        out.update(env)
    return out

def names_of(v):
    """list of (name, weight or None)"""
    if v is None:
        return []
    if isinstance(v, dict):
        return [(str(k), float(w)) for k, w in v.items() if isinstance(w, (int, float))]
    return [(str(x), None) for x in v if isinstance(x, str)]

# (list id, English name, faker locale, keys tried in order for male / female / surnames)
ORIGINS = [
    ("american", "American (US)", "en_US"), ("english", "English (UK)", "en_GB"), ("irish", "Irish", "en_IE"),
    ("irish-gaelic", "Irish (Gaelic)", "ga_IE"), ("new-zealand", "New Zealand", "en_NZ"), ("indian", "Indian", "en_IN"),
    ("nigerian", "Nigerian", "en_NG"), ("yoruba", "Yoruba", "yo_NG"), ("igbo", "Igbo", "ig_NG"), ("hausa", "Hausa", "ha_NG"),
    ("kenyan", "Kenyan", "en_KE"), ("pakistani", "Pakistani", "en_PK"), ("ghanaian", "Ghanaian (Twi)", "tw_GH"),
    ("zulu", "Zulu", "zu_ZA"), ("swahili", "Swahili", "sw"),
    ("german", "German", "de_DE"), ("austrian", "Austrian", "de_AT"), ("swiss-german", "Swiss (German)", "de_CH"),
    ("dutch", "Dutch", "nl_NL"), ("flemish", "Flemish", "nl_BE"), ("french", "French", "fr_FR"),
    ("french-canadian", "French Canadian", "fr_CA"), ("italian", "Italian", "it_IT"), ("spanish", "Spanish", "es_ES"),
    ("mexican", "Mexican", "es_MX"), ("argentine", "Argentine", "es_AR"), ("colombian", "Colombian", "es_CO"),
    ("chilean", "Chilean", "es_CL"), ("portuguese", "Portuguese", "pt_PT"), ("brazilian", "Brazilian", "pt_BR"),
    ("danish", "Danish", "da_DK"), ("swedish", "Swedish", "sv_SE"), ("norwegian", "Norwegian", "no_NO"),
    ("finnish", "Finnish", "fi_FI"), ("icelandic", "Icelandic", "is_IS"), ("polish", "Polish", "pl_PL"), ("estonian", "Estonian", "et_EE"),
    ("czech", "Czech", "cs_CZ"), ("slovak", "Slovak", "sk_SK"), ("hungarian", "Hungarian", "hu_HU"),
    ("romanian", "Romanian", "ro_RO"), ("croatian", "Croatian", "hr_HR"), ("slovenian", "Slovenian", "sl_SI"),
("latvian", "Latvian", "lv_LV"),
    ("turkish", "Turkish", "tr_TR"), ("azerbaijani", "Azerbaijani", "az_AZ"), ("indonesian", "Indonesian", "id_ID"),
    ("vietnamese", "Vietnamese", "vi_VN"),
    ("russian", "Russian (Cyrillic)", "ru_RU"), ("ukrainian", "Ukrainian (Cyrillic)", "uk_UA"),
    ("bulgarian", "Bulgarian (Cyrillic)", "bg_BG"), ("greek", "Greek (Greek script)", "el_GR"),
    ("hebrew", "Hebrew (Hebrew script)", "he_IL"), ("arabic", "Arabic (Arabic script)", "ar_AA"),
    ("persian", "Persian (Arabic script)", "fa_IR"), ("armenian", "Armenian", "hy_AM"), ("georgian", "Georgian", "ka_GE"),
    ("japanese", "Japanese", "ja_JP"), ("chinese", "Chinese", "zh_CN"), ("korean", "Korean (Hangul)", "ko_KR"),
    ("thai", "Thai (Thai script)", "th_TH"), ("hindi", "Hindi (Devanagari)", "hi_IN"),
]

MALE = ["first_names_male", "first_names_male_romanized", "first_romanized_names_male", "romanized_first_names_male"]
FEMALE = ["first_names_female", "first_names_female_romanized", "first_romanized_names_female", "romanized_first_names_female"]
LAST = ["last_names", "last_names_male", "last_romanized_names", "romanized_last_names", "last_names_romanized"]

def pick(env, keys, prefer_roman):
    order = sorted(keys, key=lambda k: ('roman' not in k)) if prefer_roman else keys
    for k in order:
        if env.get(k):
            return k, names_of(env[k])
    return None, []

def main():
    src, out = sys.argv[1], sys.argv[2]
    survey = '--survey' in sys.argv
    base = os.path.join(src, 'faker', 'providers', 'person')
    version = 'unknown'
    for d in os.listdir(src):
        if d.startswith('faker-') and d.endswith('.dist-info'):
            version = d[len('faker-'):-len('.dist-info')]
    os.makedirs(out, exist_ok=True)
    written = []
    for lid, label, loc in ORIGINS:
        path = os.path.join(base, loc, '__init__.py')
        if not os.path.exists(path):
            print('missing', loc); continue
        env = read_provider(path)
        # Japanese and Chinese: the romanized lists (Latin letters) where Faker has them
        roman = loc in ('ja_JP', 'zh_CN')
        mk, male = pick(env, MALE, roman)
        fk, female = pick(env, FEMALE, roman)
        lk, last = pick(env, LAST, roman)
        lastf = []
        unisex = False
        if loc == 'et_EE':  # Estonian names (the provider also has Russian ones)
            male, female = names_of(env.get('first_names_male_est')), names_of(env.get('first_names_female_est'))
            last = names_of(env.get('last_names_est'))
        if loc == 'zh_CN':  # romanized given names are not split by gender
            male = female = names_of(env.get('first_romanized_names'))
            unisex = True
        if not male and not female and env.get('first_names'):
            male = female = names_of(env['first_names'])
            unisex = True
        # surnames with a form of their own for women
        if env.get('last_names_male') and env.get('last_names_female'):
            last, lastf = names_of(env['last_names_male']), names_of(env['last_names_female'])
        if loc == 'pl_PL':
            def fem(n):
                for a, b in (('ski', 'ska'), ('cki', 'cka'), ('dzki', 'dzka')):
                    if n.endswith(a):
                        return n[:-len(a)] + b
                return n
            uni = names_of(env.get('unisex_last_names'))
            m = names_of(env.get('male_last_names'))
            last = m + uni
            lastf = [(fem(n), w) for n, w in m] + uni
        if loc == 'is_IS':  # patronymics, formed from the fathers' names as Faker does it (a simplification)
            stems = []
            for n, w in names_of(env.get('first_names_male')):
                if n.endswith('ur'):
                    n = n[:-2]
                if not n.endswith('s'):
                    n = n + 's'
                stems.append((n, w))
            last = [(n + 'son', w) for n, w in stems]
            lastf = [(n + 'dóttir', w) for n, w in stems]
        if survey:
            print(f"{loc:6} {lid:14} m={len(male):5} f={len(female):5} l={len(last):5} lf={len(lastf):4} {'unisex' if unisex else ''} e.g. {[n for n, _ in male[:2]]} {[n for n, _ in last[:2]]} {[n for n, _ in lastf[:2]]}")
        if len(male) < 10 or len(female) < 10 or len(last) < 10:
            print('skipped (too few names)', loc); continue
        # entries that are no plain names: abbreviations ("Hans D."), numbering ("Koch II"),
        # notes in brackets, digits, a vowel sign without its letter
        odd = re.compile(r"\s(I|II|III|IV|V|VI)$|\d|[()\[\]]|\.$")
        def pack(lst):
            seen, names, weights = set(), [], []
            for n, w in lst:
                n = n.strip()
                if not n or n in seen: continue
                if odd.search(n) or unicodedata.category(n[0]).startswith('M'): continue
                seen.add(n); names.append(n); weights.append(w)
            if all(w is not None for w in weights):
                order = sorted(range(len(names)), key=lambda i: -weights[i])
                return {"names": [names[i] for i in order], "weights": [round(weights[i], 6) for i in order]}
            return {"names": names}
        data = {"id": lid, "name": label, "source": f"Faker {version} (MIT licence), locale {loc}",
                "male": pack(male), "female": pack(female), "surnames": pack(last)}
        if lastf:
            data["surnamesFemale"] = pack(lastf)
        if unisex:
            data["unisex"] = True
        if loc in ('ja_JP', 'zh_CN', 'ko_KR', 'hu_HU', 'vi_VN'):
            data["surnameFirst"] = True  # family name first
        with open(os.path.join(out, lid + '.json'), 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, separators=(',', ':'))
        written.append(lid)
    print(len(written), 'lists written')

main()
