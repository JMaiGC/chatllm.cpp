import os, json
import copy
import binding

def get_model_url_on_modelscope(proj: str, fn: str, user: str = 'judd2024') -> str:
    return f"https://modelscope.cn/api/v1/models/{user}/{proj}/repo?Revision=master&FilePath={fn}"

with open(os.path.join(binding.PATH_SCRIPTS, 'models.json'), encoding='utf-8') as f:
    all_models = json.load(f)

DEF_STORAGE_DIR = '../quantized'

def print_progress_bar (iteration, total, prefix = '', suffix = '', decimals = 1, length = 60, fill = '█', printEnd = "\r", auto_nl = True):
    percent = ("{0:." + str(decimals) + "f}").format(100 * (iteration / float(total)))
    filledLength = int(length * iteration // total)
    bar = fill * filledLength + '-' * (length - filledLength)
    print(f'\r{prefix} |{bar}| {percent}% {suffix}', end = printEnd)
    if (iteration == total) and auto_nl:
        print()

def download_file(url: str, fn: str, prefix: str):
    flag = False
    try:
        import requests
    except:
        print(f"`requests` is required. use `pip install requests` to install it.")
        return flag

    print(f"downloading {prefix}")
    with open(fn, 'wb') as f:
        with requests.get(url, stream=True) as r:
            r.raise_for_status()
            total = int(r.headers.get('content-length', 0))

            progress = 0

            for chunk in r.iter_content(chunk_size=8192):
                progress += len(chunk)
                f.write(chunk)
                print_progress_bar(progress, total)

            flag = progress == total
    return flag

def show():
    total = 0

    def format_number(num):
        if num >= 1_000_000_000_000:
            return f"{num / 1_000_000_000_000:.2f} T"
        elif num >= 1_000_000_000:
            return f"{num / 1_000_000_000:.2f} G"
        elif num >= 1_000_000:
            return f"{num / 1_000_000:.2f} M"
        elif num >= 1_000:
            return f"{num / 1_000:.2f} K"
        else:
            return str(num)

    def acc_file_size(variant: dict):
        nonlocal total
        for q, o in variant['quantized'].items():
            total += o['size']

    def show_variants(info, default):
        sizes = [s for s in info.keys()]
        [acc_file_size(variant) for variant in info.values()]
        variants = [m + ":" + s for s in sizes]
        all_var = ', '.join(variants)
        print(f"Available: {all_var}")
        if len(variants) > 1:
            print(f"Default  : {m + ':' + default}")

    def show_model(m):
        info = all_models[m]
        print(f"**{m}**: {info['brief']}")
        print(f"License  : {info['license']}")
        show_variants(info['variants'], info['default'])
        print()

    for m in sorted(all_models.keys()):
        show_model(m)

    print(f"\n-------\nTotal: {format_number(total)}B")

def parse_model_id(model_id: str):
    parts = model_id.split(':')
    model = all_models[parts[0]]
    variants = model['variants']
    var = variants[parts[1] if len(parts) >= 2 else model['default']]
    q = parts[2] if len(parts) >= 3 else var['default']
    r =  copy.deepcopy(var['quantized'][q])
    url = r['url'].split('/')
    r['url'] = get_model_url_on_modelscope(*url)
    r['fn'] = url[1]
    return r

def get_model(model_id, storage_dir):
    if not os.path.isdir(storage_dir):
        os.mkdir(storage_dir)
    assert os.path.isdir(storage_dir), f"{storage_dir} is invalid"

    info = parse_model_id(model_id)
    fn = os.path.join(storage_dir, info['fn'])
    if os.path.isfile(fn):
        if os.path.getsize(fn) == info['size']:
            return fn
        else:
            print(f"{fn} is incomplete, download again")

    assert download_file(info['url'], fn, model_id), f"failed to download {model_id}"
    assert os.path.getsize(fn) == info['size'], f"downloaded file size mismatch!"

    return fn

def find_index(l: list, x) -> int:
    if x in l:
        return l.index(x)
    else:
        return -1

def preprocess_args(args: list[str], storage_dir) -> list[str]:
    candidates = ['-m', '--model', '--embedding_model', '--reranker_model']
    for param in candidates:
        i = find_index(args, param)
        if i < 0: continue

        if args[i + 1].startswith(':'):
            args[i + 1] = get_model(args[i + 1][1:].lower(), storage_dir)

    return args

def enum_missing():
    import glob
    all = set()
    for m in all_models.keys():
        info = all_models[m]
        variants = info['variants']
        for s in variants.keys():
            for q in variants[s]['quantized'].keys():
                quantized = variants[s]['quantized'][q]
                all.add(quantized['url'].split('/')[1])
    f = glob.glob(os.path.join(DEF_STORAGE_DIR, '*.bin'))
    l = []
    for x in f:
        k = os.path.basename(x)
        if not k in all:
            l.append(k)
    print(f'not uploaded models: {sorted(l)}')

def check_default():
    for m in all_models.keys():
        info = all_models[m]
        if info['default'] not in info['variants']:
            print(f"{m} default missing")

if __name__ == '__main__':
    import sys
    args = sys.argv[1:]
    if len(args) == 0:
        show()
        exit()

    if args[0] == 'check':
        enum_missing()
        check_default()
        exit(0)

    print(preprocess_args(args, DEF_STORAGE_DIR))